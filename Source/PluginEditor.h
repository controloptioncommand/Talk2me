#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Views.h"
#include "UI/About.h"

namespace t2m
{
class Talk2MeEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::DarkModeSettingListener
{
public:
    explicit Talk2MeEditor (Talk2MeProcessor&);
    ~Talk2MeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1120, kBaseH = 760;

    // для тестов
    void setThemeForTest (ThemeId id) { setThemeMode (id == ThemeId::Rubber ? ThemeMode::Rubber : ThemeMode::Oxygen); }
    AboutPage& getAboutForTest() { return about; }
    void showAboutForTest (bool show, bool game) { showAbout (show); if (show) about.showGame (game); }
    void setViewForTest (int mode) { scriptView.setMode ((ScriptView::Mode) mode); }
    void pumpForTest() { timerCallback(); }
    PhraseView& getPhraseForTest() { return phrase; }

private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void showAbout (bool show);
    void showPresetMenu();
    void showSettingsMenu();
    void savePreset();
    void setThemeMode (ThemeMode m);
    void applyThemeMode();
    void updateEngineControls();

    Talk2MeProcessor& proc;
    T2MLookAndFeel lnf;
    ThemeMode themeMode = ThemeMode::System;
    juce::Component content;   // базовый размер, масштабируется трансформом

    // тулбар
    LogoButton logo;
    IconButton prevPreset { IconButton::Prev, "Previous preset" }, nextPreset { IconButton::Next, "Next preset" },
               settingsButton { IconButton::Settings, "Settings" };
    TextLinkButton presetName;
    Segmented engineSwitch;
    MiniSpectrogram mini;
    TransposeControl transpose;
    OutMeter outMeter;
    juce::Rectangle<int> toolbarArea;

    PhraseView phrase;
    ScriptView scriptView;

    // панель
    juce::Rectangle<int> panelArea, leftHead, rightHead, footArea;
    int dividerX = 0;
    Segmented playSwitch;
    ParamKnob rate { "Rate" }, intonation { "Intonation" }, glide { "Glide" }, vibrato { "Vibrato" }, release { "Release" };
    PillChoice voiceChoice, chipChoice;
    ParamKnob formant { "Formant" }, breath { "Breath" }, buzz { "Buzz" }, lofi { "Lo-fi" };
    ParamKnob bend { "Bend" }, drive { "Drive" }, glitch { "Glitch" }, lofi2 { "Lo-fi" };
    ParamToggle poly { "Poly" }, loop { "Loop" }, chipPitch { "Chip pitch" };
    juce::Label status;
    int shownEngine = -1, shownPlay = -1;
    std::unique_ptr<juce::ParameterAttachment> engineAtt, playAtt;
    std::shared_ptr<const Bundle> shownBundle;

    AboutPage about;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Talk2MeEditor)
};
} // namespace t2m
