#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LexiconData.h"

namespace t2m
{
namespace
{
float raw (const juce::AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }

// Голос: базовый характер + ручки Formant, Breath, Buzz
VoiceShape shapeFor (int voice, float formantPct, float breathPct, float buzzPct)
{
    VoiceShape v;
    float baseBreath = 0, baseBuzz = 0;
    switch ((VoicePreset) voice)
    {
        case VoicePreset::Male:    v.formantScale = 1.0f;  v.openQuotient = 0.55f; v.tilt = 0.35f; break;
        case VoicePreset::Female:  v.formantScale = 1.17f; v.openQuotient = 0.65f; v.tilt = 0.5f; baseBreath = 0.15f; break;
        case VoicePreset::Child:   v.formantScale = 1.32f; v.openQuotient = 0.6f;  v.tilt = 0.4f; baseBreath = 0.05f; break;
        case VoicePreset::Robot:   v.formantScale = 1.0f;  v.openQuotient = 0.4f;  v.tilt = 0.1f; baseBuzz = 0.7f; break;
        case VoicePreset::Whisper: v.formantScale = 1.05f; v.whisper = 1.0f; break;
    }
    v.formantScale *= std::pow (2.0f, formantPct / 100.0f);
    v.breath = juce::jlimit (0.0f, 1.0f, baseBreath + breathPct / 100.0f);
    v.buzz = juce::jlimit (0.0f, 1.0f, baseBuzz + (1.0f - baseBuzz) * buzzPct / 100.0f);
    return v;
}
} // namespace

Talk2MeProcessor::Talk2MeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Talk2Me", createLayout()),
      presets (apvts, [this] (const juce::String& t) { setText (t); })
{
    Lexicon::setSource ([]
    {
        juce::MemoryInputStream in (T2MLexicon::cmudict_dict_gz, (size_t) T2MLexicon::cmudict_dict_gzSize, false);
        juce::GZIPDecompressorInputStream gz (&in, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryOutputStream mo;
        mo.writeFromInputStream (gz, -1);
        return std::string ((const char*) mo.getData(), mo.getDataSize());
    });
    presets.loadFactory (0);
    rebuildNow();
    startTimerHz (15);
}

Talk2MeProcessor::~Talk2MeProcessor() { stopTimer(); }

bool Talk2MeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Talk2MeProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    if (pending == nullptr) pending = latest;
}

// ---------------------------------------------------------------------------
void Talk2MeProcessor::setText (const juce::String& t)
{
    apvts.state.setProperty ("text", t.substring (0, 2000), nullptr);
}

juce::String Talk2MeProcessor::rebuildKey() const
{
    return getText() + "|" + juce::String ((int) raw (apvts, ParamId::voice)) + "|" + juce::String (raw (apvts, ParamId::formant), 0)
         + "|" + juce::String ((int) raw (apvts, ParamId::chip));
}

void Talk2MeProcessor::rebuildNow()
{
    const auto text = getText();
    auto b = std::make_shared<Bundle>();
    b->script = buildScript (text.toStdString());
    b->chip = (int) raw (apvts, ParamId::chip);
    const auto shape = shapeFor ((int) raw (apvts, ParamId::voice), raw (apvts, ParamId::formant), 0.0f, 0.0f);
    b->chipFrames = LpcEncoder::encode (b->script, shape, b->chip);
    if (text != builtText) ++textSerial;
    b->textSerial = textSerial;
    builtText = text;
    builtKey = rebuildKey();

    latest = b;
    keepAlive.push_back (b);
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    pending = b;
}

void Talk2MeProcessor::timerCallback()
{
    if (rebuildKey() != builtKey) rebuildNow();
    // освобождаем фразы, которые больше никто не держит (аудиопоток отпустил)
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const std::shared_ptr<const Bundle>& p) { return p.use_count() == 1; }),
                     keepAlive.end());
}

EngineParams Talk2MeProcessor::snapshotParams() const
{
    EngineParams p;
    p.engine = (int) raw (apvts, ParamId::engine);
    p.play = (int) raw (apvts, ParamId::play);
    p.poly = raw (apvts, ParamId::poly) > 0.5f;
    p.loop = raw (apvts, ParamId::loop) > 0.5f;
    p.chipPitch = raw (apvts, ParamId::chipPitch) > 0.5f;
    p.rate = raw (apvts, ParamId::rate) / 100.0f;
    p.intonation = raw (apvts, ParamId::intonation) / 100.0f;
    p.glideMs = raw (apvts, ParamId::glide);
    p.vibrato = raw (apvts, ParamId::vibrato) / 100.0f;
    p.releaseMs = raw (apvts, ParamId::release);
    p.transpose = raw (apvts, ParamId::transpose);
    p.outGain = juce::Decibels::decibelsToGain (raw (apvts, ParamId::output));
    p.shape = shapeFor ((int) raw (apvts, ParamId::voice), raw (apvts, ParamId::formant), raw (apvts, ParamId::breath), raw (apvts, ParamId::buzz));
    p.bend = raw (apvts, ParamId::bend) / 100.0f;
    p.drive = (int) raw (apvts, ParamId::drive);
    p.glitch = raw (apvts, ParamId::glitch) / 100.0f;
    p.lofi = raw (apvts, ParamId::lofi) / 100.0f;
    return p;
}

// ---------------------------------------------------------------------------
void Talk2MeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked() && pending != nullptr) engine.setBundle (std::move (pending));
    }

    const auto p = snapshotParams();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying) engine.resetSteps();   // старт транспорта — слоги с начала
            wasPlaying = playing;
        }

    const int n = buffer.getNumSamples();
    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float scratchR[512];   // моно-выход: правый канал никуда не пишется

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int len = juce::jmin (end - pos, 512);
            engine.render (L + pos, R != nullptr ? R + pos : scratchR, len, p);
            pos += len;
        }
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber(), p);
        else if (m.isPitchWheel()) engine.pitchWheel (m.getPitchWheelValue());
        else if (m.isController() && m.getControllerNumber() == 1) engine.modWheel ((float) m.getControllerValue() / 127.0f);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    renderTo (n);

    // в интерфейс: волна и пик
    float pk = 0;
    for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (L[i]));
    outPeak.store (juce::jmax (pk, outPeak.load() * 0.85f));
    if (scopeFifo.getFreeSpace() >= n)
    {
        const auto w = scopeFifo.write (n);
        if (w.blockSize1 > 0) std::copy (L, L + w.blockSize1, scopeBuf.begin() + w.startIndex1);
        if (w.blockSize2 > 0) std::copy (L + w.blockSize1, L + w.blockSize1 + w.blockSize2, scopeBuf.begin() + w.startIndex2);
    }

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / juce::jmax (1e-6, n / sr));
    cpuLoad.store (cpuLoad.load() * 0.9f + load * 0.1f);
}

// ---------------------------------------------------------------------------
void Talk2MeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void Talk2MeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            rebuildNow();
        }
}

juce::AudioProcessorEditor* Talk2MeProcessor::createEditor() { return new Talk2MeEditor (*this); }
} // namespace t2m

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new t2m::Talk2MeProcessor(); }
