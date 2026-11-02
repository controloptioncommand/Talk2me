#pragma once
#include "../Speech/Script.h"
#include <cmath>
#include <cstdint>

namespace t2m
{
// Цифровой резонатор Клатта: y = A·x + B·y1 + C·y2
struct Resonator
{
    float a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
    void set (float f, float bw, float sr, bool peakNorm = false);
    void reset() { y1 = y2 = 0; }
    inline float process (float x)
    {
        const float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// Антирезонатор (нуль) — обратный резонатору
struct AntiResonator
{
    float a = 1, b = 0, c = 0, x1 = 0, x2 = 0;
    void set (float f, float bw, float sr);
    void reset() { x1 = x2 = 0; }
    inline float process (float x)
    {
        const float y = a * x + b * x1 + c * x2;
        x2 = x1;
        x1 = x;
        return y;
    }
};

// Характер голоса
struct VoiceShape
{
    float formantScale = 1.0f;   // 1 — мужской, ~1.17 — женский
    float breath = 0.0f;         // шум в открытой фазе
    float buzz = 0.0f;           // 0 — мягкий импульс, 1 — острый щелчок (робот)
    float tilt = 0.35f;          // наклон спектра источника (0…0.9)
    float openQuotient = 0.55f;
    float whisper = 0.0f;        // 1 — шёпот без голоса
};

// Формантный синтезатор в духе Klatt (1980): каскадная ветвь для голоса и придыхания,
// параллельная — для шума. Работает на внутренней частоте (по умолчанию 11025 Гц).
class FormantSynth
{
public:
    void prepare (float sampleRate);
    void reset();
    float process (const CtlFrame& p, float f0);   // один отсчёт
    VoiceShape shape;
    float getSampleRate() const { return sr; }
    // фаза импульсов: 0…1, для синхронизации
    float getPhase() const { return phase; }

private:
    void update (const CtlFrame& p);
    inline float noise()
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }

    float sr = 11025.0f;
    Resonator r1, r2, r3, r4, r5, np;
    AntiResonator nz;
    Resonator p2, p3, p4, p5, p6;
    float phase = 0, tiltY = 0, prevSrc = 0, dcX = 0, dcY = 0;
    float pa2 = 0, pa3 = 0, pa4 = 0, pa5 = 0, pa6 = 0, pab = 0;
    uint32_t rng = 22222u;
    int counter = 0;
};
} // namespace t2m
