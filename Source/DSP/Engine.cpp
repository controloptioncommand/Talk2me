#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace t2m
{
namespace
{
constexpr float kChipGain[3] = { 3.8f, 3.8f, 11.0f };   // TMS5220 тише из-за формы импульса
constexpr float kTalkGain = 2.0f;
constexpr float kFrameSec = 0.025f;

inline float midiToHz (float semis) { return 440.0f * std::pow (2.0f, (semis - 69.0f) / 12.0f); }

inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}
} // namespace

void Engine::prepare (double rate)
{
    hostRate = rate;
    phase = 0;
    for (auto& h : hist) h = 0;
    lpfRate = 0;
    for (auto& v : voices)
    {
        v.active = false;
        v.fs.prepare (kTalkRate);
    }
    held.clear();
}

void Engine::setBundle (std::shared_ptr<const Bundle> b)
{
    if (b == bundle) return;
    const bool chipChanged = bundle == nullptr || b == nullptr || b->chip != bundle->chip;
    const bool textChanged = bundle == nullptr || b == nullptr || b->textSerial != bundle->textSerial;
    bundle = std::move (b);
    if (textChanged)
    {
        // новая фраза: звучащие голоса доигрывают коротким затуханием
        lastStep = -1;
        for (auto& v : voices)
            if (v.active) { v.releasing = true; v.fastRelease = true; }
    }
    if (chipChanged)
        for (auto& v : voices) v.chip.setChip (bundle != nullptr ? bundle->chip : 0);
}

void Engine::allNotesOff()
{
    held.clear();
    for (auto& v : voices)
        if (v.active) { v.gate = false; v.releasing = true; }
}

int Engine::nextStep (int count)
{
    if (count <= 0) return 0;
    // аккорд (ноты в пределах 30 мс) поёт один и тот же слог
    if (lastStep >= 0 && now - lastStepTime < (uint64_t) (hostRate * 0.03)) return lastStep % count;
    lastStep = (lastStep + 1) % count;
    lastStepTime = now;
    return lastStep;
}

bool Engine::regionFor (int note, const EngineParams& p, Region& r)
{
    if (bundle == nullptr || bundle->script.empty()) return false;
    const auto& sc = bundle->script;
    auto wordRegion = [&] (int w)
    {
        const auto& wd = sc.words[(size_t) w];
        // держится главный ударный слог слова
        float hold = sc.syls[(size_t) wd.firstSyl].hold;
        for (int s = wd.firstSyl; s < wd.firstSyl + wd.numSyl; ++s)
            if (sc.syls[(size_t) s].stress == 1) { hold = sc.syls[(size_t) s].hold; break; }
        r = { wd.start, wd.end + 0.01f, hold, false };
    };
    switch ((PlayModeIndex) p.play)
    {
        case PlayModeIndex::Phrase:
            r = { 0.0f, sc.duration, -1.0f, true };
            return true;
        case PlayModeIndex::Syllables:
        {
            const auto& s = sc.syls[(size_t) nextStep ((int) sc.syls.size())];
            r = { s.start, s.end + 0.01f, s.hold, false };
            return true;
        }
        case PlayModeIndex::Words:
            wordRegion (nextStep ((int) sc.words.size()));
            return true;
        case PlayModeIndex::Keys:
        {
            const int n = (int) sc.words.size();
            wordRegion (((note - kKeysBaseNote) % n + n) % n);
            return true;
        }
    }
    return false;
}

void Engine::start (Voice& v, int note, float vel, const Region& r, const EngineParams& p, bool fresh)
{
    const float target = p.play == (int) PlayModeIndex::Keys ? (float) kKeysPitchNote : (float) note;
    if (fresh)
    {
        v.fs.reset();
        v.chip.reset();
        v.chip.setChip (bundle != nullptr ? bundle->chip : 0);
        v.env = 0;
        v.age = 0;
        v.vibPhase = 0;
        // в моно без аккордов glide идёт от предыдущей ноты
        if (! (! p.poly && p.glideMs > 0.5f && v.order > 0)) v.semis = target;
    }
    v.active = true;
    v.gate = true;
    v.releasing = false;
    v.fastRelease = false;
    v.note = note;
    v.vel = 0.35f + 0.65f * vel;
    v.t = r.start;
    v.startT = r.start;
    v.endT = r.end;
    v.holdT = r.hold;
    v.loopable = r.loopable;
    v.target = target;
    v.order = ++counter;
    v.rng = (uint32_t) (counter * 2654435761u) | 1u;
}

void Engine::noteOn (int note, float vel, const EngineParams& p)
{
    if (bundle == nullptr || bundle->script.empty()) return;
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    held.push_back (note);

    if (! p.poly)
    {
        auto& v = voices[0];
        const bool sounding = v.active && ! v.releasing;
        // Phrase в моно: перекрывающиеся ноты меняют только высоту (legato)
        if (sounding && v.gate && p.play == (int) PlayModeIndex::Phrase)
        {
            v.target = (float) note;
            v.note = note;
            return;
        }
        Region r;
        if (! regionFor (note, p, r)) return;
        start (v, note, vel, r, p, ! sounding);
        return;
    }

    Region r;
    if (! regionFor (note, p, r)) return;
    Voice* best = nullptr;
    for (auto& v : voices) if (! v.active) { best = &v; break; }
    if (best == nullptr)
    {
        // свободных нет — забираем самый старый (сначала из отпущенных)
        for (auto& v : voices)
            if (best == nullptr || (v.releasing && ! best->releasing) || (v.releasing == best->releasing && v.order < best->order))
                best = &v;
    }
    start (*best, note, vel, r, p, true);
}

void Engine::noteOff (int note, const EngineParams& p)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    if (! p.poly)
    {
        auto& v = voices[0];
        if (! v.active || v.note != note) return;
        if (! held.empty())
        {
            // вернуться к предыдущей удерживаемой ноте
            v.target = p.play == (int) PlayModeIndex::Keys ? (float) kKeysPitchNote : (float) held.back();
            v.note = held.back();
            return;
        }
        v.gate = false;
        if (p.play == (int) PlayModeIndex::Phrase) v.releasing = true;
        return;
    }
    for (auto& v : voices)
        if (v.active && v.gate && v.note == note)
        {
            v.gate = false;
            if (p.play == (int) PlayModeIndex::Phrase) v.releasing = true;
        }
}

void Engine::updateFilters (float inRate)
{
    if (std::abs (inRate - lpfRate) < 1.0f) return;
    lpfRate = inRate;
    // ФНЧ Баттерворта 4-го порядка на 0.45 внутренней частоты — убирает зеркальные копии
    const float fc = std::min (0.45f * inRate, 0.45f * (float) hostRate);
    const float w = 2.0f * 3.14159265f * fc / (float) hostRate;
    const float qs[2] = { 0.5412f, 1.3066f };
    Biquad* bs[2] = { &lp1, &lp2 };
    for (int i = 0; i < 2; ++i)
    {
        const float alpha = std::sin (w) / (2.0f * qs[i]), cw = std::cos (w), a0 = 1.0f + alpha;
        bs[i]->b0 = (1.0f - cw) * 0.5f / a0;
        bs[i]->b1 = (1.0f - cw) / a0;
        bs[i]->b2 = bs[i]->b0;
        bs[i]->a1 = -2.0f * cw / a0;
        bs[i]->a2 = (1.0f - alpha) / a0;
    }
}

float Engine::renderVoice (Voice& v, const EngineParams& p, float inRate)
{
    const auto& sc = bundle->script;
    // высота: glide, интонация фразы, колесо, вибрато
    if (p.glideMs < 0.5f) v.semis = v.target;
    else v.semis += (v.target - v.semis) * (1.0f - std::exp (-1.0f / (p.glideMs * 0.001f * inRate)));
    const auto fr = sc.at (v.t);
    float semis = v.semis + p.transpose + wheel + p.intonation * fr.pitch;
    const float vibDepth = std::min (1.0f, p.vibrato + mod) * 0.6f * std::clamp ((v.age - 0.25f) / 0.3f, 0.0f, 1.0f);
    if (vibDepth > 0)
    {
        v.vibPhase += 5.5f / inRate;
        if (v.vibPhase >= 1.0f) v.vibPhase -= 1.0f;
        semis += vibDepth * std::sin (v.vibPhase * 6.2831853f);
    }
    const float f0 = std::clamp (midiToHz (semis), 30.0f, 1600.0f);
    const float loud = std::sqrt (std::clamp (110.0f / f0, 0.2f, 2.5f));

    float s;
    if (p.engine == 0)
    {
        v.fs.shape = p.shape;
        s = v.fs.process (fr, f0) * kTalkGain;
    }
    else
    {
        if (v.chip.needsFrame())
        {
            int idx = (int) std::floor (v.t / kFrameSec);
            auto rnd = [&v] { v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5; return (float) (v.rng & 0xffffff) / 16777216.0f; };
            // «закоротка адресных линий»: прыжок по ПЗУ
            if (p.glitch > 0 && rnd() < p.glitch * 0.08f) idx += (int) (rnd() * 13.0f) - 6;
            const auto& frames = bundle->chipFrames;
            ChipFrame f = idx >= 0 && idx < (int) frames.size() ? frames[(size_t) idx] : ChipFrame {};
            if (p.glitch > 0 && f.energy > 0 && rnd() < p.glitch * 0.35f)
            {
                // переворот случайного бита в случайном поле кадра
                const int field = (int) (rnd() * 12.0f);
                if (field == 0) f.energy = (uint8_t) std::clamp ((int) (f.energy ^ (1u << (int) (rnd() * 4))), 1, 14);
                else if (field == 1) { if (f.pitch > 0) f.pitch = (uint8_t) std::max (1, (int) (f.pitch ^ (1u << (int) (rnd() * 5)))); }
                else { const int j = field - 2; f.k[j] = (uint8_t) (f.k[j] ^ (1u << (int) (rnd() * (float) kChipKBits[j]))); }
            }
            if (f.energy > 0) f.energy = (uint8_t) std::clamp ((int) f.energy + p.drive, 1, 14);
            v.chip.loadFrame (f);
        }
        v.chip.setPitchPeriod (inRate / f0, p.chipPitch);
        s = v.chip.step() * kChipGain[std::clamp (bundle->chip, 0, 2)];
    }

    // время фразы: удержание гласной, пока нажата клавиша
    const float dt = p.rate / (p.engine == 0 ? inRate : 8000.0f);
    if (v.gate && v.holdT >= 0 && v.t + dt >= v.holdT) v.t = std::max (v.t, v.holdT);
    else v.t += dt;
    v.age += 1.0f / inRate;
    if (v.t >= v.endT)
    {
        if (v.loopable && p.loop && v.gate) v.t = v.startT;
        else if (! v.releasing) { v.releasing = true; v.fastRelease = true; }
    }

    // огибающая: атака 3 мс, отпускание — Release (или 10 мс в конце фразы)
    if (v.releasing)
    {
        const float relMs = v.fastRelease ? 10.0f : std::max (5.0f, p.releaseMs);
        v.env -= 1000.0f / (relMs * inRate);
        if (v.env <= 0) { v.env = 0; v.active = false; }
    }
    else
        v.env = std::min (1.0f, v.env + 1000.0f / (3.0f * inRate));

    return s * v.env * v.vel * loud;
}

void Engine::render (float* outL, float* outR, int n, const EngineParams& p)
{
    if (p.engine != engineType)
    {
        engineType = p.engine;
        for (auto& v : voices) if (v.active) { v.releasing = true; v.fastRelease = true; }
    }
    const float inRate = p.engine == 0 ? kTalkRate : 8000.0f * p.bend;
    updateFilters (inRate);
    const double step = (double) inRate / hostRate;
    const bool talk = p.engine == 0;
    const float zoh = talk ? p.lofi * 0.8f : p.lofi;
    const float levels = talk ? std::pow (2.0f, 15.0f - p.lofi * 8.0f) : 0.0f;   // 16 → 8 бит

    for (int i = 0; i < n; ++i)
    {
        phase += step;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            float s = 0;
            if (bundle != nullptr)
                for (auto& v : voices)
                    if (v.active) s += renderVoice (v, p, inRate);
            if (talk && p.lofi > 0.01f) s = std::round (s * levels) / levels;
            hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3]; hist[3] = s;
        }
        // Эрмит между hist[1] и hist[2]
        const float x = (float) phase;
        const float c0 = hist[1];
        const float c1 = 0.5f * (hist[2] - hist[0]);
        const float c2 = hist[0] - 2.5f * hist[1] + 2.0f * hist[2] - 0.5f * hist[3];
        const float c3 = 0.5f * (hist[3] - hist[0]) + 1.5f * (hist[1] - hist[2]);
        float smooth = ((c3 * x + c2) * x + c1) * x + c0;
        smooth = lp2.process (lp1.process (smooth));
        const float y = softClip ((smooth * (1.0f - zoh) + hist[1] * zoh) * p.outGain);
        outL[i] = y;
        outR[i] = y;
        ++now;
    }

    int count = 0;
    for (int k = 0; k < kVoices; ++k)
    {
        info[(size_t) k].active.store (voices[(size_t) k].active, std::memory_order_relaxed);
        info[(size_t) k].t.store (voices[(size_t) k].t, std::memory_order_relaxed);
        count += voices[(size_t) k].active ? 1 : 0;
    }
    activeVoices.store (count, std::memory_order_relaxed);
}
} // namespace t2m
