#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace t2m
{
namespace ParamId
{
    inline constexpr const char* engine = "engine";       // Talk 96 / Spell 80
    inline constexpr const char* play = "play";           // Phrase / Syllables / Words / Keys
    inline constexpr const char* poly = "poly";
    inline constexpr const char* loop = "loop";
    inline constexpr const char* rate = "rate";
    inline constexpr const char* intonation = "intonation";
    inline constexpr const char* glide = "glide";
    inline constexpr const char* vibrato = "vibrato";
    inline constexpr const char* release = "release";
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* output = "output";
    // Talk 96
    inline constexpr const char* voice = "voice";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* breath = "breath";
    inline constexpr const char* buzz = "buzz";
    inline constexpr const char* lofi = "lofi";
    // Spell 80
    inline constexpr const char* chip = "chip";
    inline constexpr const char* bend = "bend";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* glitch = "glitch";
    inline constexpr const char* chipPitch = "chipPitch";
}

enum class EngineType { Talk = 0, Spell = 1 };
enum class PlayMode { Phrase = 0, Syllables = 1, Words = 2, Keys = 3 };
enum class VoicePreset { Male = 0, Female, Child, Robot, Whisper };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
juce::StringArray voiceNames();
juce::StringArray chipNames();
juce::StringArray playModeNames();
} // namespace t2m
