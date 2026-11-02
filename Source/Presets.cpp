#include "Presets.h"
#include "Params.h"

namespace t2m
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ParamId;
    static const std::vector<FactoryPreset> list = {
        { "Talk To Me", "Talk to me. Talk to me now.", { { engine, 0 }, { voice, 0 } } },
        { "Untrust", "untrust us", { { engine, 0 }, { voice, 1 }, { rate, 125 }, { intonation, 15 }, { play, 1 }, { buzz, 40 }, { lofi, 50 } } },
        { "You Are Correct", "That is correct. Now spell.", { { engine, 1 }, { chip, 0 }, { intonation, 60 } } },
        { "Spell It", "Spell S P E L L", { { engine, 1 }, { chip, 1 }, { play, 2 }, { intonation, 30 } } },
        { "Counting", "one two three four five six seven eight", { { engine, 1 }, { chip, 0 }, { play, 2 }, { intonation, 20 } } },
        { "Bent Toy", "Say it. Wrong, try again.", { { engine, 1 }, { chip, 0 }, { bend, 72 }, { glitch, 35 }, { drive, 2 }, { loop, 1 } } },
        { "Arcade", "Game over. Insert coin.", { { engine, 1 }, { chip, 2 }, { intonation, 30 }, { lofi, 60 } } },
        { "Robot Choir", "we are machines", { { engine, 0 }, { voice, 3 }, { poly, 1 }, { intonation, 0 }, { loop, 1 }, { release, 400 }, { buzz, 60 } } },
        { "Whisper", "Can you hear me?", { { engine, 0 }, { voice, 4 }, { intonation, 60 }, { release, 300 }, { lofi, 20 } } },
        { "Child Voice", "Hello computer. Play with me.", { { engine, 0 }, { voice, 2 }, { intonation, 70 } } },
        { "Syllable Singer", "hello there, how are you tonight", { { engine, 0 }, { voice, 1 }, { play, 1 }, { intonation, 0 }, { glide, 60 }, { vibrato, 25 }, { release, 150 } } },
        { "Numbers Station", "Seven three nine. One one four. Attention.", { { engine, 1 }, { chip, 1 }, { rate, 80 }, { intonation, 20 }, { lofi, 70 } } },
    };
    return list;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, std::function<void (const juce::String&)> st)
    : apvts (s), setText (std::move (st)) {}

juce::String PresetManager::getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
void PresetManager::setCurrentName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

void PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float v = rp->getDefaultValue();
            for (auto& kv : pr.values)
                if (rp->paramID == kv.first) v = rp->convertTo0to1 (kv.second);
            rp->setValueNotifyingHost (v);
        }
    setText (pr.text);
    setCurrentName (pr.name);
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Talk2Me").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.t2m");
    files.sort();
    return files;
}

bool PresetManager::loadUser (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto text = tree.getProperty ("text").toString();
    apvts.replaceState (tree);
    setText (text);
    setCurrentName (f.getFileNameWithoutExtension());
    return true;
}

bool PresetManager::save (const juce::String& name, const juce::String& text)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    auto state = apvts.copyState();
    state.setProperty ("text", text, nullptr);
    state.setProperty ("presetName", clean, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + ".t2m"))) return false;
    setCurrentName (clean);
    return true;
}

void PresetManager::step (int dir)
{
    // общий список: фабричные, затем пользовательские
    const auto users = userPresets();
    const int nf = getNumFactory(), total = nf + users.size();
    if (total == 0) return;
    const auto cur = getCurrentName();
    int idx = -1;
    for (int i = 0; i < nf; ++i) if (cur == factoryPresets()[(size_t) i].name) idx = i;
    for (int i = 0; i < users.size(); ++i) if (cur == users[i].getFileNameWithoutExtension()) idx = nf + i;
    idx = idx < 0 ? (dir > 0 ? 0 : total - 1) : (idx + dir + total) % total;
    if (idx < nf) loadFactory (idx);
    else loadUser (users[idx - nf]);
}
} // namespace t2m
