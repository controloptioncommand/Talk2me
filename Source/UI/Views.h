#pragma once
#include "Widgets.h"
#include "../DSP/Engine.h"
#include <juce_dsp/juce_dsp.h>
#include <deque>

namespace t2m
{
class Talk2MeProcessor;

// Мини-спектрограмма в тулбаре: пиксельная лента в духе Eido, просто чтобы было на что посмотреть
class MiniSpectrogram : public juce::Component
{
public:
    MiniSpectrogram();
    void push (const float* x, int n);
    void tick();
    void paint (juce::Graphics& g) override;

    static constexpr int kCell = 3, kRows = 8;

private:
    std::vector<float> ring = std::vector<float> (2048, 0.0f);
    int64_t written = 0, lastColumnAt = 0;
    std::deque<std::array<int8_t, kRows>> cols;   // корзина цвета 0…9, −1 — пусто
    juce::dsp::FFT fft { 9 };
    std::vector<float> work = std::vector<float> (1024, 0.0f);
    double sampleRate = 44100.0;

public:
    void setSampleRate (double r) { sampleRate = r; }
};

// Фраза: крупный текст, звучащий слог — сигнальным цветом; клик — редактирование на месте
class PhraseView : public juce::Component
{
public:
    explicit PhraseView (Talk2MeProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void setBundle (std::shared_ptr<const Bundle> b);
    void setActive (const std::vector<float>& times);   // время фразы у звучащих голосов
    bool isEditing() const { return editor.isVisible(); }
    void startEditing();
    void stopEditing (bool commit);

private:

    Talk2MeProcessor& proc;
    std::shared_ptr<const Bundle> bundle;
    std::vector<std::pair<int, int>> hiSyl;   // подсвеченные символы
    std::vector<int> hiWords;
    juce::TextEditor editor;
    juce::String before;
    juce::Rectangle<int> textArea, phonArea, headArea;
};

// Главный вид: «партитура» фразы (огибающая спектра по кадрам, слова, курсоры нот) или осциллограмма выхода
class ScriptView : public juce::Component
{
public:
    enum Mode { Script = 0, Scope = 1 };
    ScriptView();
    void setBundle (std::shared_ptr<const Bundle> b, int engine);
    void setPlayheads (const std::vector<float>& times);
    void push (const float* x, int n);
    void tick();
    void setMode (Mode m);
    Mode getMode() const { return mode; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void lookAndFeelChanged() override { image = {}; repaint(); }

private:
    void buildImage (juce::Rectangle<int> area);
    void paintScript (juce::Graphics& g, juce::Rectangle<float> area);
    void paintScope (juce::Graphics& g, juce::Rectangle<float> area);
    void paintOverlay (juce::Graphics& g);
    juce::Colour background() const;

    Mode mode = Script;
    std::shared_ptr<const Bundle> bundle;
    int engine = 0;
    std::vector<float> heads;
    juce::Image image;
    juce::Rectangle<int> imageArea;
    juce::Rectangle<float> tabsArea, legendArea;

    // осциллограмма
    std::vector<float> ring = std::vector<float> (1 << 15, 0.0f);
    int64_t written = 0, scopeRead = 0;
    struct Col { float mn = 1, mx = -1, db = -100; };
    std::deque<Col> scopeCols;
};
} // namespace t2m
