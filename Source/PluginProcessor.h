#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "DSP/Engine.h"

namespace t2m
{
class Talk2MeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    Talk2MeProcessor();
    ~Talk2MeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Talk2Me"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Фраза (хранится в проекте)
    void setText (const juce::String& t);
    juce::String getText() const { return apvts.state.getProperty ("text").toString(); }
    std::shared_ptr<const Bundle> getLatestBundle() const { return latest; }   // поток сообщений
    void rebuildNow();

    EngineParams snapshotParams() const;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    Engine engine;

    // для интерфейса: выход в кольцо (моно), пик, нагрузка
    static constexpr int kScopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo { kScopeSize };
    std::vector<float> scopeBuf = std::vector<float> (kScopeSize, 0.0f);
    std::atomic<float> outPeak { 0.0f }, cpuLoad { 0.0f };

private:
    void timerCallback() override;
    juce::String rebuildKey() const;

    std::shared_ptr<const Bundle> latest;                 // последняя собранная (поток сообщений)
    std::vector<std::shared_ptr<const Bundle>> keepAlive; // держим, чтобы аудиопоток не освобождал память
    std::shared_ptr<const Bundle> pending;                // передача в аудиопоток
    juce::SpinLock pendingLock;
    juce::String builtKey, builtText;
    uint32_t textSerial = 0;
    bool wasPlaying = false;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Talk2MeProcessor)
};
} // namespace t2m
