// Офлайн-тест Talk2Me: разбор текста, рендер нот во всех режимах (не тишина, не NaN, ≤ 0 dBFS), слоги и удержание,
// состояние, пресеты, скорость, скриншоты интерфейса в обеих темах, About.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace t2m;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } } while (0)

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Note { double at, len; int note; float vel = 0.8f; };

void setParam (Talk2MeProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// рендер последовательности нот; editor — чтобы интерфейс видел звук
juce::AudioBuffer<float> play (Talk2MeProcessor& p, const std::vector<Note>& notes, double seconds, Talk2MeEditor* ed = nullptr,
                               std::function<void (double)> onBlock = {})
{
    const int total = (int) (seconds * kRate);
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> buf (2, kBlock);
    int pos = 0;
    while (pos < total)
    {
        const int n = juce::jmin (kBlock, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (auto& nt : notes)
        {
            const int on = (int) (nt.at * kRate), off = (int) ((nt.at + nt.len) * kRate);
            if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, nt.vel), on - pos);
            if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        pos += n;
        if (onBlock) onBlock ((double) pos / kRate);
        if (ed != nullptr && (pos / kBlock) % 6 == 0) ed->pumpForTest();
    }
    return out;
}

struct Stats { float peak = 0, rms = 0; bool nan = false; };
Stats stats (const juce::AudioBuffer<float>& b)
{
    Stats s;
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (c, i);
            if (! std::isfinite (v)) { s.nan = true; continue; }
            s.peak = juce::jmax (s.peak, std::abs (v));
            sum += (double) v * v;
        }
    s.rms = (float) std::sqrt (sum / juce::jmax (1, b.getNumChannels() * b.getNumSamples()));
    return s;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    std::cout << "  wrote " << f.getFileName() << "\n";
}

void checkAudio (const juce::AudioBuffer<float>& b, const juce::String& what)
{
    const auto s = stats (b);
    CHECK (! s.nan, what << ": NaN");
    CHECK (s.peak <= 1.0f, what << ": peak above 0 dBFS (" << s.peak << ")");
    CHECK (s.rms > 0.003f, what << ": silent (rms " << s.rms << ")");
}

std::vector<Note> melody (double start = 0.1)
{
    // небольшая мелодия: восемь нот
    const int ns[] = { 57, 60, 64, 62, 60, 57, 55, 57 };
    std::vector<Note> v;
    for (int i = 0; i < 8; ++i) v.push_back ({ start + i * 0.42, 0.36, ns[i] });
    return v;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("render");
    outDir.createDirectory();

    // ---------------------------------------------------------------- текст
    std::cout << "text\n";
    {
        Talk2MeProcessor init;   // загружает словарь
        auto sc = buildScript ("Talk to me");
        CHECK (sc.words.size() == 3 && sc.syls.size() == 3, "talk to me: words/syllables " << (int) sc.words.size() << "/" << (int) sc.syls.size());
        CHECK (sc.wordPhonemes.size() == 3 && sc.wordPhonemes[0] == "T AO1 K", "talk: phonemes " << sc.wordPhonemes[0]);
        sc = buildScript ("1996");
        CHECK (sc.words.size() == 3, "1996 → nineteen ninety six, words " << (int) sc.words.size());
        sc = buildScript ("{HH AH0 L OW1} there");
        CHECK (sc.words.size() == 2 && sc.wordPhonemes[0] == "HH AH0 L OW1", "phonemes in braces");
        sc = buildScript ("S P E L L");
        CHECK (sc.words.size() == 5 && sc.wordPhonemes[0] == "EH1 S", "letter names");
        sc = buildScript ("glorpification zzyzx");
        CHECK (sc.words.size() == 2 && ! sc.syls.empty(), "unknown words via rules");
        sc = buildScript ("Hello, world? Yes.");
        CHECK (sc.words.size() == 3 && sc.segs.size() > 8, "punctuation");
        sc = buildScript ("");
        CHECK (sc.empty(), "empty text");
        std::cout << "  lexicon entries: " << (int) Lexicon::get().size() << "\n";
        CHECK (Lexicon::get().size() > 120000, "lexicon not loaded");
    }

    // ---------------------------------------------------------------- звук
    std::cout << "render\n";
    {
        Talk2MeProcessor p;
        p.prepareToPlay (kRate, kBlock);
        p.setText ("Talk to me. You are correct.");
        p.rebuildNow();
        for (int eng = 0; eng < 2; ++eng)
            for (int chip = 0; chip < (eng == 0 ? 1 : 3); ++chip)
                for (int mode = 0; mode < 4; ++mode)
                {
                    setParam (p, ParamId::engine, (float) eng);
                    setParam (p, ParamId::chip, (float) chip);
                    setParam (p, ParamId::play, (float) mode);
                    p.rebuildNow();
                    auto b = play (p, melody(), 4.0);
                    checkAudio (b, juce::String ("engine ") + juce::String (eng) + " chip " + juce::String (chip) + " mode " + juce::String (mode));
                }
        // голоса и крайние ручки
        setParam (p, ParamId::engine, 0);
        setParam (p, ParamId::play, 0);
        for (int v = 0; v < 5; ++v)
        {
            setParam (p, ParamId::voice, (float) v);
            for (float f : { -50.0f, 50.0f })
            {
                setParam (p, ParamId::formant, f);
                setParam (p, ParamId::buzz, 100);
                setParam (p, ParamId::breath, 100);
                setParam (p, ParamId::lofi, 100);
                auto b = play (p, { { 0.05, 1.5, 84 }, { 0.05, 1.5, 36 } }, 2.0);
                checkAudio (b, juce::String ("voice ") + juce::String (v) + " formant " + juce::String (f));
            }
        }
        setParam (p, ParamId::engine, 1);
        for (float bend : { 50.0f, 200.0f })
            for (int drive : { -4, 4 })
            {
                setParam (p, ParamId::bend, bend);
                setParam (p, ParamId::drive, (float) drive);
                setParam (p, ParamId::glitch, 100);
                setParam (p, ParamId::chipPitch, 1);
                auto b = play (p, { { 0.05, 2.0, 96 }, { 0.6, 1.0, 30 } }, 2.5);
                checkAudio (b, juce::String ("spell bend ") + juce::String (bend) + " drive " + juce::String (drive));
            }
    }

    // ---------------------------------------------------------------- слоги и удержание
    std::cout << "syllables\n";
    {
        Talk2MeProcessor p;
        p.prepareToPlay (kRate, kBlock);
        p.setText ("hello there friend");
        p.rebuildNow();
        setParam (p, ParamId::play, 1);
        const auto& sc = p.getLatestBundle()->script;
        CHECK (sc.syls.size() == 4, "hello there friend: 4 syllables, got " << (int) sc.syls.size());
        // три короткие ноты подряд — три слога по порядку
        std::vector<int> seen;
        play (p, { { 0.05, 0.15, 60 }, { 0.35, 0.15, 62 }, { 0.65, 0.15, 64 } }, 0.9, nullptr, [&] (double t)
        {
            for (double probe : { 0.12, 0.42, 0.72 })
                if (std::abs (t - probe) < 0.003)
                    for (auto& v : p.engine.info)
                        if (v.active.load())
                        {
                            const float vt = v.t.load();
                            for (int s = 0; s < (int) sc.syls.size(); ++s)
                                if (vt >= sc.syls[(size_t) s].start && vt < sc.syls[(size_t) s].end + 0.02f) { seen.push_back (s); break; }
                        }
        });
        seen.erase (std::unique (seen.begin(), seen.end()), seen.end());
        { juce::String sstr; for (int x : seen) sstr << x << " "; std::cout << "  syllables seen: " << sstr << "\n"; }
        CHECK (seen.size() >= 3 && seen[0] == 0 && seen[1] == 1 && seen[2] == 2, "syllable stepping");
        // долгая нота держит гласную
        p.engine.resetSteps();
        float heldT = -1;
        bool active = false;
        play (p, { { 0.05, 2.0, 60 } }, 1.8, nullptr, [&] (double) { heldT = p.engine.info[0].t.load(); active = p.engine.info[0].active.load(); });
        CHECK (active && std::abs (heldT - sc.syls[0].hold) < 0.01f, "hold vowel while key is down (t " << heldT << ", hold " << sc.syls[0].hold << ")");
        // аккорд в пределах 30 мс поёт один слог
        p.engine.allNotesOff();
        play (p, {}, 0.5);
        p.engine.resetSteps();
        setParam (p, ParamId::poly, 1);
        std::vector<float> chordT;
        play (p, { { 0.05, 0.3, 60 }, { 0.06, 0.3, 64 }, { 0.07, 0.3, 67 } }, 0.2, nullptr, [&] (double t)
        {
            if (std::abs (t - 0.15) < 0.003)
                for (auto& v : p.engine.info) if (v.active.load()) chordT.push_back (v.t.load());
        });
        { juce::String cs; for (float x : chordT) cs << x << " "; std::cout << "  chord t: " << cs << " syl0 " << sc.syls[0].start << ".." << sc.syls[0].end << "\n"; }
        CHECK (chordT.size() == 3 && chordT[0] < sc.syls[0].end && chordT[2] < sc.syls[0].end, "chord sings one syllable");
    }

    // ---------------------------------------------------------------- состояние и пресеты
    std::cout << "state & presets\n";
    {
        Talk2MeProcessor p;
        p.setText ("state test phrase");
        setParam (p, ParamId::engine, 1);
        setParam (p, ParamId::chip, 2);
        setParam (p, ParamId::rate, 150);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        Talk2MeProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.getText() == "state test phrase", "state: text");
        CHECK ((int) q.apvts.getRawParameterValue (ParamId::chip)->load() == 2, "state: chip");
        CHECK (std::abs (q.apvts.getRawParameterValue (ParamId::rate)->load() - 150.0f) < 0.5f, "state: rate");
        CHECK (q.getLatestBundle() != nullptr && q.getLatestBundle()->script.words.size() == 3, "state: bundle rebuilt");

        q.prepareToPlay (kRate, kBlock);
        for (int i = 0; i < q.presets.getNumFactory(); ++i)
        {
            q.presets.loadFactory (i);
            q.rebuildNow();
            auto b = play (q, melody (0.05), 3.6);
            checkAudio (b, juce::String ("preset ") + factoryPresets()[(size_t) i].name);
        }
    }

    // ---------------------------------------------------------------- скорость
    std::cout << "speed\n";
    {
        Talk2MeProcessor p;
        p.prepareToPlay (kRate, kBlock);
        p.setText ("we are the voices of the machine, talking all night long");
        setParam (p, ParamId::poly, 1);
        setParam (p, ParamId::loop, 1);
        for (int eng = 0; eng < 2; ++eng)
        {
            setParam (p, ParamId::engine, (float) eng);
            p.rebuildNow();
            std::vector<Note> chord;
            for (int k = 0; k < 8; ++k) chord.push_back ({ 0.01, 9.9, 48 + k * 3 });
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto b = play (p, chord, 10.0);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << "  engine " << eng << ": 8 voices, 10 s in " << juce::String (ms, 0) << " ms (" << juce::String (10000.0 / ms, 1) << "x realtime)\n";
            CHECK (ms < 2500.0, "too slow");
            checkAudio (b, "speed");
        }
        // время сборки фразы
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p.setText ("This is a fairly long phrase with many words, to measure how quickly the script and the chip frames are built.");
        p.rebuildNow();
        std::cout << "  rebuild: " << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << " ms\n";
    }

    // ---------------------------------------------------------------- демо WAV
    std::cout << "demo\n";
    {
        Talk2MeProcessor p;
        p.prepareToPlay (kRate, kBlock);
        const char* names[] = { "Talk To Me", "You Are Correct", "Bent Toy", "Syllable Singer", "Arcade" };
        for (auto* nm : names)
            for (int i = 0; i < p.presets.getNumFactory(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == nm)
                {
                    p.presets.loadFactory (i);
                    p.rebuildNow();
                    const bool phraseMode = (int) p.apvts.getRawParameterValue (ParamId::play)->load() == 0;
                    std::vector<Note> notes = melody();
                    if (phraseMode)
                    {
                        // легато: следующая нота начинается до конца предыдущей
                        const int ns[] = { 57, 60, 64, 62, 60, 57, 55, 57 };
                        notes.clear();
                        for (int k = 0; k < 8; ++k) notes.push_back ({ 0.1 + k * 0.45, k == 7 ? 0.8 : 0.5, ns[k] });
                    }
                    auto b = play (p, notes, 4.4);
                    writeWav (outDir.getChildFile ("demo_" + juce::String (nm).replace (" ", "_").toLowerCase() + ".wav"), b);
                }
    }

    // ---------------------------------------------------------------- интерфейс
    std::cout << "ui\n";
    {
        Talk2MeProcessor p;
        p.prepareToPlay (kRate, kBlock);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<Talk2MeEditor*> (base.get());
        CHECK (ed != nullptr, "editor");
        ed->setSize (Talk2MeEditor::kBaseW, Talk2MeEditor::kBaseH);

        auto shoot = [&] (const juce::String& name)
        {
            ed->pumpForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "  wrote " << f.getFileName() << "\n";
        };

        for (auto theme : { ThemeId::Oxygen, ThemeId::Rubber })
        {
            const juce::String tn = theme == ThemeId::Oxygen ? "oxygen" : "rubber";
            ed->setThemeForTest (theme);

            p.presets.loadFactory (0);
            p.rebuildNow();
            ed->setViewForTest (0);
            play (p, { { 0.05, 3.0, 57 } }, 0.75, ed);   // нота держится — на скриншоте курсор и подсвеченное слово
            shoot ("ui_" + tn + "_talk");

            p.presets.loadFactory (2);   // You Are Correct — Spell 80
            p.rebuildNow();
            play (p, { { 0.05, 3.0, 60 } }, 0.95, ed);
            shoot ("ui_" + tn + "_spell");
            ed->setViewForTest (1);
            play (p, { { 0.05, 3.0, 60 } }, 1.2, ed);
            shoot ("ui_" + tn + "_scope");
            ed->setViewForTest (0);
            play (p, {}, 0.6, ed);

            ed->getPhraseForTest().startEditing();
            shoot ("ui_" + tn + "_edit");
            ed->getPhraseForTest().stopEditing (false);

            ed->showAboutForTest (true, false);
            shoot ("about_" + tn);
            {
                auto& fog = ed->getAboutForTest().getFogForTest();
                std::cout << "  fog particles: " << fog.getNumParticles() << "\n";
                CHECK (fog.getNumParticles() > 150, "fog: too few particles");
                const auto c = fog.getLocalBounds().getCentre().toFloat();
                fog.blowForTest ({ c.x - 160.0f, c.y }, { c.x + 40.0f, c.y + 10.0f }, 12);
                CHECK (fog.isAnimating(), "fog: did not start");
                fog.stepForTest (90);
                CHECK (! fog.isAnimating(), "fog: did not settle");
            }
            ed->showAboutForTest (true, true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            shoot ("game_" + tn);
            auto& about = ed->getAboutForTest();
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (about.isVisible(), "escape in game closes only the game");
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (! about.isVisible(), "escape closes About");
        }
        ed->setThemeForTest (ThemeId::Oxygen);
        base.reset();
    }

    std::cout << (failures == 0 ? "ALL OK\n" : juce::String (failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
