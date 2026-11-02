#pragma once
#include "Formant.h"
#include "LpcEncoder.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace t2m
{
enum class PlayModeIndex { Phrase = 0, Syllables = 1, Words = 2, Keys = 3 };

// Готовая фраза: сценарий + кадры чипа. Собирается в потоке сообщений, в аудиопотоке только читается.
struct Bundle
{
    Script script;
    std::vector<ChipFrame> chipFrames;
    int chip = 0;
    uint32_t textSerial = 0;   // меняется только при смене текста — тогда звучащие ноты обрываются
};

struct EngineParams
{
    int engine = 0;          // 0 Talk 96, 1 Spell 80
    int play = 0;            // PlayMode
    bool poly = false, loop = false, chipPitch = false;
    float rate = 1.0f;       // 1 = 100%
    float intonation = 0.4f;
    float glideMs = 30.0f, vibrato = 0.0f, releaseMs = 60.0f;
    float transpose = 0.0f;
    float outGain = 0.5f;
    VoiceShape shape;
    float bend = 1.0f;       // такт чипа
    int drive = 0;
    float glitch = 0.0f;
    float lofi = 0.35f;
};

// Голоса, исполнение фразы по нотам и пересчёт внутренней частоты (11 025 Гц / 8 кГц · Bend) в частоту хоста
class Engine
{
public:
    static constexpr int kVoices = 8;
    static constexpr float kTalkRate = 11025.0f;
    static constexpr int kKeysBaseNote = 60;   // Keys: C3 (как в Ableton) — первое слово
    static constexpr int kKeysPitchNote = 57;  // Keys: высота голоса — A2 (110 Гц) + Pitch

    void prepare (double hostRate);
    void setBundle (std::shared_ptr<const Bundle> b);   // только из аудиопотока
    const Bundle* getBundle() const { return bundle.get(); }

    void noteOn (int note, float velocity, const EngineParams& p);
    void noteOff (int note, const EngineParams& p);
    void pitchWheel (int value14) { wheel = ((float) value14 - 8192.0f) / 8192.0f * 2.0f; }
    void modWheel (float v) { mod = v; }
    void allNotesOff();
    void resetSteps() { lastStep = -1; }

    void render (float* outL, float* outR, int n, const EngineParams& p);

    // для интерфейса
    struct VoiceInfo { std::atomic<bool> active { false }; std::atomic<float> t { 0.0f }; };
    std::array<VoiceInfo, kVoices> info;
    std::atomic<int> activeVoices { 0 };

private:
    struct Voice
    {
        bool active = false, gate = false, releasing = false, fastRelease = false;
        int note = 60;
        float vel = 1.0f;
        float t = 0, startT = 0, endT = 0, holdT = -1;
        bool loopable = false;
        float semis = 60, target = 60;
        float env = 0;
        float vibPhase = 0, age = 0;
        uint64_t order = 0;
        uint32_t rng = 1;
        FormantSynth fs;
        LpcChip chip;
    };
    struct Region { float start, end, hold; bool loopable; };

    bool regionFor (int note, const EngineParams& p, Region& r);
    int nextStep (int count);
    void start (Voice& v, int note, float vel, const Region& r, const EngineParams& p, bool fresh);
    float renderVoice (Voice& v, const EngineParams& p, float inRate);
    void updateFilters (float inRate);

    std::shared_ptr<const Bundle> bundle;
    std::array<Voice, kVoices> voices;
    std::vector<int> held;
    double hostRate = 44100.0;
    float wheel = 0, mod = 0;
    uint64_t counter = 0;
    int lastStep = -1;
    uint64_t lastStepTime = 0, now = 0;
    int engineType = 0;
    int chipIndex = 0;

    // пересчёт частоты
    double phase = 0;
    float hist[4] {};
    float lpfRate = 0;
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Biquad lp1, lp2;
};
} // namespace t2m
