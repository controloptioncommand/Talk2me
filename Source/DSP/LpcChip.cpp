// Логика синтеза портирована из MAME (src/devices/sound/tms5110.cpp).
// license:BSD-3-Clause, copyright-holders: Frank Palazzolo, Jarek Burczynski, Aaron Giles, Jonathan Gevaryahu, Couriersud
#include "LpcChip.h"
#include <cmath>

namespace t2m
{
namespace
{
inline int32_t mul (int32_t a, int32_t b)
{
    while (a > 511) a -= 1024;
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;
    while (b < -16384) b += 32768;
    return (a * b) >> 9;
}
} // namespace

void LpcChip::setChip (int index)
{
    chip = index < 0 ? 0 : (index > 2 ? 2 : index);
}

void LpcChip::reset()
{
    curEnergy = curPitch = prevEnergy = 0;
    for (int i = 0; i < 10; ++i) { curK[i] = 0; newK[i] = 0; x[i] = 0; }
    for (auto& v : u) v = 0;
    newE = newP = 0;
    oldSilence = oldUnvoiced = inhibit = true;
    uvZero = false;
    ip = 0;
    pc = 12;
    subcycle = 1;
    pitchZero = false;
    pitchCount = 0;
    pitchPos = 0;
    rng = 0x1fff;
    excitation = 0;
}

void LpcChip::loadFrame (const ChipFrame& f)
{
    newE = f.energy & 15;
    if (newE == 15) newE = 14;   // «стоп-кадр» не используем
    newP = f.pitch & ((1 << tables().pitchBits) - 1);
    for (int i = 0; i < 10; ++i) newK[i] = f.k[i] & ((1 << kChipKBits[i]) - 1);
    uvZero = newP == 0;
    const bool newUnvoiced = newP == 0, newSilence = newE == 0;
    inhibit = (! oldUnvoiced && newUnvoiced) || (oldUnvoiced && ! newUnvoiced) || (oldSilence && ! newSilence);
}

int32_t LpcChip::lattice()
{
    u[10] = mul (prevEnergy, excitation * 64);
    for (int i = 9; i >= 0; --i) u[i] = u[i + 1] - mul (curK[i], x[i]);
    for (int i = 9; i >= 1; --i) x[i] = x[i - 1] + mul (curK[i - 1], u[i - 1]);
    x[0] = u[0];
    prevEnergy = curEnergy;
    return u[0];
}

float LpcChip::step()
{
    const auto& T = tables();
    const bool loading = needsFrame();   // в этот отсчёт чип разбирает кадр, интерполяции нет
    if (! loading && subcycle == 2)
    {
        const int sh = kChipInterpShift[ip];
        const int on = (inhibit && ip != 0) ? 0 : 1;
        if (pc == 0)
        {
            if (ip == 0) pitchZero = false;
            curEnergy = curEnergy + (((int32_t) T.energy[newE] - curEnergy) * on >> sh);
        }
        else if (pc == 1)
            curPitch = curPitch + (((int32_t) T.pitch[newP] - curPitch) * on >> sh);
        else if (pc >= 2 && pc <= 11)
        {
            const int j = pc - 2;
            curK[j] = curK[j] + (((int32_t) T.k[j][newK[j]] - curK[j]) * on >> sh);
            if (j >= 4 && uvZero) curK[j] = 0;
        }
    }

    // возбуждение
    if (oldUnvoiced)
        excitation = (rng & 1) ? -64 : 64;
    else
        excitation = T.chirp[pitchCount >= 51 ? 51 : pitchCount];

    for (int i = 0; i < 20; ++i)
    {
        const int bit = ((rng >> 12) ^ (rng >> 3) ^ (rng >> 2) ^ rng) & 1;
        rng = (uint16_t) (((rng << 1) | bit) & 0x1fff);
    }

    int32_t s = lattice();
    while (s > 16383) s -= 32768;
    while (s < -16384) s += 32768;
    // аналоговый выход: 8 значащих бит с ограничением
    if (s > 2047) s = 2047;
    else if (s < -2048) s = -2048;
    s &= ~0xF;
    const float out = (float) s / 2048.0f;

    // счётчики
    ++subcycle;
    if (subcycle == 2 && pc == 12)
    {
        if (ip == 7 && inhibit) pitchZero = true;
        if (ip == 7)
        {
            oldSilence = newE == 0;
            oldUnvoiced = newP == 0;
        }
        subcycle = 1;
        pc = 0;
        ip = (ip + 1) & 7;
    }
    else if (subcycle == 3)
    {
        subcycle = 1;
        ++pc;
    }

    if (pitchOverride > 0 && ! oldUnvoiced)
    {
        float period = integerPitch ? std::round (pitchOverride) : pitchOverride;
        if (period < 2.0f) period = 2.0f;
        pitchPos += 1.0f;
        if (pitchZero) pitchPos = 0;
        else if (pitchPos >= period) pitchPos -= period;
        if (pitchPos >= period) pitchPos = 0;
        pitchCount = (int) pitchPos;
    }
    else
    {
        ++pitchCount;
        if (pitchCount >= curPitch || pitchZero) pitchCount = 0;
        pitchCount &= 0x1ff;
        pitchPos = (float) pitchCount;
    }
    return out;
}
} // namespace t2m
