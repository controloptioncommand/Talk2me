#include "LpcEncoder.h"
#include <algorithm>
#include <cmath>

namespace t2m
{
namespace
{
int nearestK (const ChipTables& T, int j, float v)
{
    const int n = 1 << kChipKBits[j];
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; ++i)
    {
        const float d = std::abs ((float) T.k[j][i] - v);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

float chirpRms (const ChipTables& T, int period)
{
    double s = 0;
    for (int n = 0; n < period; ++n)
    {
        const double c = T.chirp[n >= 51 ? 51 : n];
        s += c * c;
    }
    return (float) std::sqrt (s / std::max (1, period));
}
} // namespace

std::vector<ChipFrame> LpcEncoder::encode (const Script& sc, const VoiceShape& shapeIn, int chipIndex)
{
    std::vector<ChipFrame> frames;
    if (sc.frames.empty()) return frames;
    const auto& T = kChips[std::clamp (chipIndex, 0, 2)];
    const int numFrames = (int) std::ceil (sc.duration * kChipRate / (float) kFrameSamples) + 1;
    const int total = numFrames * kFrameSamples;

    // 1) речь на 8 кГц ровным тоном 100 Гц
    FormantSynth fs;
    fs.prepare (kChipRate);
    fs.shape = shapeIn;
    fs.shape.buzz = 0;
    fs.shape.breath = 0;
    std::vector<float> x ((size_t) total, 0.0f);
    float peak = 1e-6f;
    for (int i = 0; i < total; ++i)
    {
        const float t = (float) i / kChipRate;
        x[(size_t) i] = t < sc.duration ? fs.process (sc.at (t), 100.0f) : 0.0f;
        peak = std::max (peak, std::abs (x[(size_t) i]));
    }
    // 2) нормировка и предыскажение (как у кодировщиков TI — отсюда «жестяной» тембр)
    float prev = 0;
    for (auto& v : x)
    {
        const float s = v / peak;
        v = s - 0.9373f * prev;
        prev = s;
    }

    // окно Хэмминга 50 мс вокруг центра кадра
    constexpr int W = 400;
    std::vector<float> win (W);
    double wsum = 0;
    for (int i = 0; i < W; ++i)
    {
        win[(size_t) i] = 0.54f - 0.46f * std::cos (2.0f * 3.14159265f * (float) i / (float) (W - 1));
        wsum += win[(size_t) i] * win[(size_t) i];
    }

    const float excV = chirpRms (T, 80), excU = 64.0f;
    constexpr float kScale = 1100.0f;   // нормированная речь → амплитуда в единицах чипа

    int voicedPitchIdx = 1;
    {
        float bd = 1e9f;
        for (int i = 1; i < (1 << T.pitchBits); ++i)
            if (std::abs ((float) T.pitch[i] - 80.0f) < bd) { bd = std::abs ((float) T.pitch[i] - 80.0f); voicedPitchIdx = i; }
    }

    std::vector<float> seg (W);
    for (int f = 0; f < numFrames; ++f)
    {
        ChipFrame cf;
        const int c = f * kFrameSamples + kFrameSamples / 2;
        for (int i = 0; i < W; ++i)
        {
            const int n = c - W / 2 + i;
            seg[(size_t) i] = (n >= 0 && n < total ? x[(size_t) n] : 0.0f) * win[(size_t) i];
        }
        double r[11];
        for (int lag = 0; lag <= 10; ++lag)
        {
            double s = 0;
            for (int i = lag; i < W; ++i) s += (double) seg[(size_t) i] * seg[(size_t) i - lag];
            // окно запаздываний: чуть расширяет полосы, чтобы фильтр не «звенел»
            const double g = 2.0 * 3.14159265 * 40.0 * lag / kChipRate;
            r[lag] = s * std::exp (-0.5 * g * g);
        }
        r[0] *= 1.0001;
        r[0] += 1e-12;

        // голос/шум по сценарию
        const auto cp = sc.at ((float) c / kChipRate);
        const bool silent = cp.av < 0.02f && cp.af < 0.01f && cp.ah < 0.01f;
        const bool voiced = cp.av > 0.25f;

        // Левинсон — Дарбин: k с тем же знаком, что в решётке чипа (A(z) = 1 + Σ a_i z^-i)
        double a[11] = { 1 }, tmp[11], k[11] = {}, err = r[0];
        for (int m = 1; m <= 10; ++m)
        {
            double acc = r[m];
            for (int i = 1; i < m; ++i) acc += a[i] * r[m - i];
            const double km = std::clamp (-acc / err, -0.995, 0.995);
            k[m] = km;
            for (int i = 0; i <= m; ++i) tmp[i] = a[i];
            for (int i = 1; i < m; ++i) a[i] = tmp[i] + km * tmp[m - i];
            a[m] = km;
            err *= (1.0 - km * km);
        }
        const float resRms = (float) std::sqrt (std::max (0.0, err) / wsum);

        if (! silent && resRms > 1e-4f)
        {
            const float target = 8.0f * kScale * resRms / (voiced ? excV : excU);
            int bestE = 0;
            float bd = 1e9f;
            for (int i = 1; i < 15; ++i)
            {
                if (T.energy[i] == 0) continue;
                const float d = std::abs (std::log ((float) T.energy[i]) - std::log (target));
                if (d < bd) { bd = d; bestE = i; }
            }
            // слишком тихо — тишина
            int firstNonZero = 1;
            while (firstNonZero < 15 && T.energy[firstNonZero] == 0) ++firstNonZero;
            if (target < (float) T.energy[firstNonZero] * 0.5f) bestE = 0;
            cf.energy = (uint8_t) bestE;
            cf.pitch = voiced ? (uint8_t) voicedPitchIdx : 0;
            for (int j = 0; j < 10; ++j) cf.k[j] = (uint8_t) nearestK (T, j, (float) (k[j + 1] * 512.0));
        }
        frames.push_back (cf);
    }
    return frames;
}

void LpcEncoder::envelope (const ChipFrame& f, int chipIndex, float* outDb, int bins)
{
    const auto& T = kChips[std::clamp (chipIndex, 0, 2)];
    double a[11] = { 1 }, tmp[11];
    const int order = f.pitch == 0 ? 4 : 10;
    for (int m = 1; m <= order; ++m)
    {
        const double km = (double) T.k[m - 1][f.k[m - 1] & ((1 << kChipKBits[m - 1]) - 1)] / 512.0;
        for (int i = 0; i <= m; ++i) tmp[i] = a[i];
        for (int i = 1; i < m; ++i) a[i] = tmp[i] + km * tmp[m - i];
        a[m] = km;
    }
    const double e = T.energy[f.energy & 15];
    for (int b = 0; b < bins; ++b)
    {
        const double w = 3.14159265 * (b + 0.5) / bins;
        double re = 0, im = 0;
        for (int i = 0; i <= order; ++i) { re += a[i] * std::cos (w * i); im -= a[i] * std::sin (w * i); }
        const double mag = std::sqrt (re * re + im * im) + 1e-9;
        outDb[b] = e <= 0 ? -120.0f : (float) (20.0 * std::log10 (e / mag));
    }
}
} // namespace t2m
