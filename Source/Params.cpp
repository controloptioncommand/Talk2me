#include "Params.h"

namespace t2m
{
juce::StringArray voiceNames() { return { "Male", "Female", "Child", "Robot", "Whisper" }; }
juce::StringArray chipNames() { return { "TMS5100 '79", "TMC0281D '80", "TMS5220" }; }
juce::StringArray playModeNames() { return { "Phrase", "Syllables", "Words", "Keys" }; }

namespace
{
juce::String minusSign() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

juce::String signedText (float v, const juce::String& unit, int decimals = 0)
{
    const auto r = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    if (v > 0.0001f) return "+" + r + unit;
    if (v < -0.0001f) return minusSign() + r.substring (1) + unit;
    return (decimals == 0 ? juce::String ("0") : juce::String (0.0f, decimals)) + unit;
}

std::unique_ptr<juce::AudioParameterFloat> pct (const char* id, const char* name, float lo, float hi, float def, bool bipolar = false)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 1.0f), def,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([bipolar] (float v, int)
        {
            return bipolar ? signedText (v, "%") : juce::String (juce::roundToInt (v)) + "%";
        }));
}

std::unique_ptr<juce::AudioParameterFloat> ms (const char* id, const char* name, float lo, float hi, float def, float skewCentre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (skewCentre);
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
        }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::engine, 1 }, "Engine", juce::StringArray { "Talk 96", "Spell 80" }, 0));
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::play, 1 }, "Play mode", playModeNames(), 0));
    l.add (std::make_unique<B> (juce::ParameterID { ParamId::poly, 1 }, "Poly", false));
    l.add (std::make_unique<B> (juce::ParameterID { ParamId::loop, 1 }, "Loop", false));

    {
        juce::NormalisableRange<float> r (25.0f, 300.0f, 1.0f);
        r.setSkewForCentre (100.0f);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamId::rate, 1 }, "Rate", r, 100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
    }
    l.add (pct (ParamId::intonation, "Intonation", 0.0f, 100.0f, 40.0f));
    l.add (ms (ParamId::glide, "Glide", 0.0f, 1000.0f, 30.0f, 120.0f));
    l.add (pct (ParamId::vibrato, "Vibrato", 0.0f, 100.0f, 0.0f));
    l.add (ms (ParamId::release, "Release", 5.0f, 2000.0f, 60.0f, 200.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamId::transpose, 1 }, "Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedText (v, " st"); })));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamId::output, 1 }, "Output",
        juce::NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedText (v, " dB", 1); })));

    l.add (std::make_unique<C> (juce::ParameterID { ParamId::voice, 1 }, "Voice", voiceNames(), 0));
    l.add (pct (ParamId::formant, "Formant", -50.0f, 50.0f, 0.0f, true));
    l.add (pct (ParamId::breath, "Breath", 0.0f, 100.0f, 10.0f));
    l.add (pct (ParamId::buzz, "Buzz", 0.0f, 100.0f, 25.0f));
    l.add (pct (ParamId::lofi, "Lo-fi", 0.0f, 100.0f, 35.0f));

    l.add (std::make_unique<C> (juce::ParameterID { ParamId::chip, 1 }, "Chip", chipNames(), 0));
    {
        juce::NormalisableRange<float> r (50.0f, 200.0f, 1.0f);
        r.setSkewForCentre (100.0f);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamId::bend, 1 }, "Bend", r, 100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
    }
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ParamId::drive, 1 }, "Drive", -4, 4, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return signedText ((float) v, ""); })));
    l.add (pct (ParamId::glitch, "Glitch", 0.0f, 100.0f, 0.0f));
    l.add (std::make_unique<B> (juce::ParameterID { ParamId::chipPitch, 1 }, "Chip pitch", false));
    return l;
}
} // namespace t2m
