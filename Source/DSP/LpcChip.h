#pragma once
#include "ChipTables.h"
#include <cstdint>

namespace t2m
{
// Кадр LPC в индексах таблиц чипа (как в ПЗУ Speak & Spell)
struct ChipFrame
{
    uint8_t energy = 0;   // 0 — тишина
    uint8_t pitch = 0;    // 0 — глухой кадр (K5…K10 обнуляются)
    uint8_t k[10] {};
};

// Эмуляция синтезатора TMS51xx/52xx: интерполяция параметров по 8 подкадрам со сдвигами,
// возбуждение (chirp-импульс или шум LFSR), 10-звенный решётчатый фильтр в целых числах,
// 14-битное переполнение и 8-битный ЦАП с ограничением. Порт логики из MAME (tms5110.cpp, BSD-3-Clause).
// Кадр — 200 отсчётов (25 мс при 8 кГц).
class LpcChip
{
public:
    void setChip (int index);
    int getChip() const { return chip; }
    void reset();

    bool needsFrame() const { return ip == 0 && pc == 12 && subcycle == 1; }
    void loadFrame (const ChipFrame& f);

    // Высота тона снаружи (MIDI): период в отсчётах чипа, дробный. 0 — по таблице кадра.
    void setPitchPeriod (float samples, bool integerOnly) { pitchOverride = samples; integerPitch = integerOnly; }

    float step();   // один отсчёт −1…1

    const ChipTables& tables() const { return kChips[chip]; }
    bool isVoiced() const { return ! oldUnvoiced; }

private:
    int32_t lattice();

    int chip = 0;
    // текущие и целевые значения
    int32_t curEnergy = 0, curPitch = 0, curK[10] {};
    int newE = 0, newP = 0, newK[10] {};
    int32_t prevEnergy = 0;
    int32_t u[11] {}, x[10] {};
    bool oldSilence = true, oldUnvoiced = true, inhibit = true, uvZero = false;
    int ip = 0, pc = 12, subcycle = 1;
    bool pitchZero = false;
    int pitchCount = 0;
    float pitchPos = 0;
    float pitchOverride = 0;
    bool integerPitch = false;
    uint16_t rng = 0x1fff;
    int32_t excitation = 0;
};
} // namespace t2m
