#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace t2m
{
// Фабричные пресеты: фраза + настройки (остальные параметры — по умолчанию)
struct FactoryPreset
{
    const char* name;
    const char* text;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();

// Пресеты: фабричные и пользовательские (~/Music/Talk2Me/Presets/*.t2m)
class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& state, std::function<void (const juce::String&)> setText);

    int getNumFactory() const { return (int) factoryPresets().size(); }
    void loadFactory (int index);
    void step (int dir);                        // ‹ › по фабричным и пользовательским
    juce::String getCurrentName() const;
    void setCurrentName (const juce::String& n);

    static juce::File userFolder();
    juce::Array<juce::File> userPresets() const;
    bool loadUser (const juce::File& f);
    bool save (const juce::String& name, const juce::String& text);

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::function<void (const juce::String&)> setText;
};
} // namespace t2m
