#include "Formant.h"
#include <algorithm>

namespace t2m
{
namespace
{
constexpr float kPi = 3.14159265358979f;
}

void Resonator::set (float f, float bw, float sr, bool peakNorm)
{
    const float T = 1.0f / sr;
    c = -std::exp (-2.0f * kPi * bw * T);
    b = 2.0f * std::exp (-kPi * bw * T) * std::cos (2.0f * kPi * f * T);
    if (! peakNorm)
    {
        a = 1.0f - b - c;   // единичное усиление на нуле частот
        return;
    }
    // единичное усиление на частоте резонанса
    const float w = 2.0f * kPi * f * T;
    const float re = 1.0f - b * std::cos (w) - c * std::cos (2.0f * w);
    const float im = b * std::sin (w) + c * std::sin (2.0f * w);
    a = std::sqrt (re * re + im * im);
}

void AntiResonator::set (float f, float bw, float sr)
{
    Resonator r;
    r.set (f, bw, sr);
    a = 1.0f / r.a;
    b = -r.b / r.a;
    c = -r.c / r.a;
}

void FormantSynth::prepare (float sampleRate)
{
    sr = sampleRate;
    reset();
}

void FormantSynth::reset()
{
    for (auto* r : { &r1, &r2, &r3, &r4, &r5, &np, &p2, &p3, &p4, &p5, &p6 }) r->reset();
    nz.reset();
    phase = tiltY = prevSrc = dcX = dcY = 0;
    counter = 0;
}

void FormantSynth::update (const CtlFrame& p)
{
    const float s = shape.formantScale;
    const float top = sr * 0.46f;
    auto F = [&] (float f) { return std::min (f * s, top); };
    auto BW = [&] (float bw) { return bw * (0.7f + 0.3f * s); };
    r1.set (F (p.f1), BW (p.b1), sr);
    r2.set (F (p.f2), BW (p.b2), sr);
    r3.set (F (p.f3), BW (p.b3), sr);
    r4.set (F (3300.0f), 250.0f, sr);
    r5.set (F (3850.0f), 300.0f, sr);
    np.set (F (270.0f), 100.0f, sr);
    nz.set (F (p.fnz), 100.0f, sr);
    p2.set (F (p.f2), 200.0f, sr, true);
    p3.set (F (p.f3), 300.0f, sr, true);
    p4.set (F (3300.0f), 400.0f, sr, true);
    p5.set (F (3850.0f), 500.0f, sr, true);
    p6.set (F (4900.0f), 700.0f, sr, true);
}

float FormantSynth::process (const CtlFrame& p, float f0)
{
    if (counter-- <= 0)
    {
        update (p);
        counter = 7;
    }

    // ---- источник: производная глоттального потока (KLGLOTT88) или острый импульс
    float src = 0;
    const float oq = shape.openQuotient;
    const float dph = std::clamp (f0, 20.0f, sr * 0.45f) / sr;
    phase += dph;
    bool wrapped = false;
    if (phase >= 1.0f) { phase -= 1.0f; wrapped = true; }
    const bool open = phase < oq;
    if (open)
    {
        const float x = phase / oq;
        src = 2.0f * x - 3.0f * x * x;
    }
    if (shape.buzz > 0)
        src = src * (1.0f - shape.buzz) + (wrapped ? -2.5f * shape.buzz : 0.0f);
    tiltY = (1.0f - shape.tilt) * src + shape.tilt * tiltY;
    src = tiltY * (1.0f + shape.tilt * 1.5f);

    const float n = noise();
    float av = p.av * (1.0f - shape.whisper);
    float ah = p.ah + p.av * shape.whisper * 0.45f;
    // придыхание в открытой фазе — «воздух» в голосе
    const float breathN = (open ? 1.0f : 0.35f) * shape.breath * 0.03f * p.av * n;

    // ---- каскад: голос + придыхание
    float x = av * src * 0.18f + (ah * 0.045f) * n + breathN;
    x = nz.process (np.process (x));
    x = r5.process (x);
    x = r4.process (x);
    x = r3.process (x);
    x = r2.process (x);
    x = r1.process (x);

    // ---- параллельная ветвь: шум через полосы, знаки чередуются (как у Клатта)
    float y = 0;
    if (p.af > 0.0001f)
    {
        const float fn = n * p.af;
        y = p2.process (fn) * p.a2 - p3.process (fn) * p.a3 + p4.process (fn) * p.a4 - p5.process (fn) * p.a5
          + p6.process (fn) * p.a6 + fn * p.ab * 0.6f;
    }
    else
    {
        // резонаторы затухают сами — без щелчков при следующем включении
        p2.process (0); p3.process (0); p4.process (0); p5.process (0); p6.process (0);
    }

    float out = x + y * 0.55f;
    // убрать постоянную составляющую
    const float o = out - dcX + 0.995f * dcY;
    dcX = out;
    dcY = o;
    return o;
}
} // namespace t2m
