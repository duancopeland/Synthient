// Exposes 4OSC settings that Tracktion keeps outside its automatable parameters
// (waveforms, unison, filter type, switches...) and its modulation matrix, so they
// can be listed and set like ordinary parameters.
#include "SynthSettings.h"

namespace
{
    struct Setting
    {
        const char* id;
        const char* name;
        double min, max;
        const char* options;   // comma-separated labels for integer settings, or nullptr
    };

    // Oscillators and LFOs number their shapes differently (tracktion Oscillator::Waves vs SimpleLFO::WaveShape).
    const char* oscWaveOptions = "off,sine,square,saw,triangle,noise";
    const char* lfoWaveOptions = "off,sine,triangle,saw,saw down,square,random";

    std::vector<Setting> settingList()
    {
        std::vector<Setting> list;
        static const char* oscIds[]   = { "wave1", "wave2", "wave3", "wave4" };
        static const char* oscNames[] = { "Osc 1 waveform", "Osc 2 waveform", "Osc 3 waveform", "Osc 4 waveform" };
        static const char* uniIds[]   = { "unison1", "unison2", "unison3", "unison4" };
        static const char* uniNames[] = { "Osc 1 unison voices", "Osc 2 unison voices", "Osc 3 unison voices", "Osc 4 unison voices" };

        for (int i = 0; i < 4; ++i)
        {
            list.push_back ({ oscIds[i], oscNames[i], 0, 5, oscWaveOptions });
            list.push_back ({ uniIds[i], uniNames[i], 1, 8, nullptr });
        }

        list.push_back ({ "filterType", "Filter type", 0, 4, "off,lowpass,highpass,bandpass,notch" });
        list.push_back ({ "filterSlope", "Filter slope (dB/oct)", 12, 24, nullptr });
        list.push_back ({ "lfoWave1", "LFO 1 waveform", 0, 6, lfoWaveOptions });
        list.push_back ({ "lfoWave2", "LFO 2 waveform", 0, 6, lfoWaveOptions });
        list.push_back ({ "lfoSync1", "LFO 1 tempo sync", 0, 1, "off,on" });
        list.push_back ({ "lfoSync2", "LFO 2 tempo sync", 0, 1, "off,on" });
        list.push_back ({ "lfoBeat1", "LFO 1 synced length (beats)", 0.0625, 16, nullptr });
        list.push_back ({ "lfoBeat2", "LFO 2 synced length (beats)", 0.0625, 16, nullptr });
        list.push_back ({ "voiceMode", "Voice mode", 0, 2, "mono,legato,poly" });
        list.push_back ({ "ampAnalog", "Analog-style amp envelope", 0, 1, "off,on" });
        list.push_back ({ "distortionOn", "Distortion on", 0, 1, "off,on" });
        list.push_back ({ "chorusOn", "Chorus on", 0, 1, "off,on" });
        list.push_back ({ "delayOn", "Delay on", 0, 1, "off,on" });
        list.push_back ({ "delayTime", "Delay time (beats)", 0.0625, 4, nullptr });
        list.push_back ({ "reverbOn", "Reverb on", 0, 1, "off,on" });
        return list;
    }

    double getValue (te::FourOscPlugin& p, const juce::String& id)
    {
        auto osc = [&] (int i) -> te::FourOscPlugin::OscParams& { return *p.oscParams[i]; };
        auto lfo = [&] (int i) -> te::FourOscPlugin::LFOParams& { return *p.lfoParams[i]; };

        if (id.startsWith ("wave"))     return osc (id.getTrailingIntValue() - 1).waveShapeValue.get();
        if (id.startsWith ("unison"))   return osc (id.getTrailingIntValue() - 1).voicesValue.get();
        if (id == "filterType")         return p.filterTypeValue.get();
        if (id == "filterSlope")        return p.filterSlopeValue.get();
        if (id.startsWith ("lfoWave"))  return lfo (id.getTrailingIntValue() - 1).waveShapeValue.get();
        if (id.startsWith ("lfoSync"))  return lfo (id.getTrailingIntValue() - 1).syncValue.get() ? 1 : 0;
        if (id.startsWith ("lfoBeat"))  return lfo (id.getTrailingIntValue() - 1).beatValue.get();
        if (id == "voiceMode")          return p.voiceModeValue.get();
        if (id == "ampAnalog")          return p.ampAnalogValue.get() ? 1 : 0;
        if (id == "distortionOn")       return p.distortionOnValue.get() ? 1 : 0;
        if (id == "chorusOn")           return p.chorusOnValue.get() ? 1 : 0;
        if (id == "delayOn")            return p.delayOnValue.get() ? 1 : 0;
        if (id == "delayTime")          return p.delayValue.get();
        if (id == "reverbOn")           return p.reverbOnValue.get() ? 1 : 0;
        return 0;
    }

    void setValue (te::FourOscPlugin& p, const juce::String& id, double v)
    {
        auto osc = [&] (int i) -> te::FourOscPlugin::OscParams& { return *p.oscParams[i]; };
        auto lfo = [&] (int i) -> te::FourOscPlugin::LFOParams& { return *p.lfoParams[i]; };
        const int iv = juce::roundToInt (v);

        if (id.startsWith ("wave"))          osc (id.getTrailingIntValue() - 1).waveShapeValue = iv;
        else if (id.startsWith ("unison"))   osc (id.getTrailingIntValue() - 1).voicesValue = iv;
        else if (id == "filterType")         p.filterTypeValue = iv;
        else if (id == "filterSlope")        p.filterSlopeValue = iv >= 18 ? 24 : 12;
        else if (id.startsWith ("lfoWave"))  lfo (id.getTrailingIntValue() - 1).waveShapeValue = iv;
        else if (id.startsWith ("lfoSync"))  lfo (id.getTrailingIntValue() - 1).syncValue = iv != 0;
        else if (id.startsWith ("lfoBeat"))  lfo (id.getTrailingIntValue() - 1).beatValue = (float) v;
        else if (id == "voiceMode")          p.voiceModeValue = iv;
        else if (id == "ampAnalog")          p.ampAnalogValue = iv != 0;
        else if (id == "distortionOn")       p.distortionOnValue = iv != 0;
        else if (id == "chorusOn")           p.chorusOnValue = iv != 0;
        else if (id == "delayOn")            p.delayOnValue = iv != 0;
        else if (id == "delayTime")          p.delayValue = (float) v;
        else if (id == "reverbOn")           p.reverbOnValue = iv != 0;
    }

    juce::String display (const Setting& s, double v)
    {
        if (s.options != nullptr)
            return juce::StringArray::fromTokens (s.options, ",", {})[juce::roundToInt (v - s.min)];

        return juce::String (v, v == std::floor (v) ? 0 : 3);
    }

    te::FourOscPlugin::ModSource modSourceFromName (const juce::String& name)
    {
        if (name == "lfo1")       return te::FourOscPlugin::lfo1;
        if (name == "lfo2")       return te::FourOscPlugin::lfo2;
        if (name == "env1")       return te::FourOscPlugin::env1;
        if (name == "env2")       return te::FourOscPlugin::env2;
        if (name == "velocity")   return te::FourOscPlugin::midiVelocity;
        if (name == "note")       return te::FourOscPlugin::midiNoteNum;
        if (name == "modwheel")   return (te::FourOscPlugin::ModSource) (te::FourOscPlugin::ccBankSelect + 1);
        return te::FourOscPlugin::none;
    }
}

juce::Array<juce::var> SynthSettings::describe (te::FourOscPlugin& p, const juce::String& filter)
{
    juce::Array<juce::var> out;

    for (auto& s : settingList())
    {
        const juce::String id (s.id), name (s.name);
        if (filter.isNotEmpty() && ! id.containsIgnoreCase (filter) && ! name.containsIgnoreCase (filter))
            continue;

        const double v = getValue (p, id);
        auto obj = new juce::DynamicObject();
        obj->setProperty ("id", id);
        obj->setProperty ("name", name);
        obj->setProperty ("min", s.min);
        obj->setProperty ("max", s.max);
        obj->setProperty ("value", v);
        obj->setProperty ("display", display (s, v));
        if (s.options != nullptr)
            obj->setProperty ("options", juce::String (s.options));
        out.add (juce::var (obj));
    }

    // Active modulation routings, as "mod:<source>:<param id>".
    for (auto param : p.getAutomatableParameters())
        for (auto src : p.getModulationSources (param))
        {
            auto obj = new juce::DynamicObject();
            obj->setProperty ("id", "mod:" + p.modulationSourceToID (src) + ":" + param->paramID);
            obj->setProperty ("name", "Modulation " + p.modulationSourceToName (src) + " > " + param->getParameterName());
            obj->setProperty ("min", -1.0);
            obj->setProperty ("max", 1.0);
            obj->setProperty ("value", p.getModulationDepth (src, param));
            obj->setProperty ("display", juce::String (p.getModulationDepth (src, param), 2));
            out.add (juce::var (obj));
        }

    return out;
}

bool SynthSettings::apply (te::FourOscPlugin& p, const juce::String& key, double value, juce::String& applied)
{
    // Modulation matrix: "mod:<lfo1|lfo2|env1|env2|velocity|note|modwheel>:<param id>" = depth -1..1 (0 removes).
    if (key.startsWithIgnoreCase ("mod:"))
    {
        auto rest = key.fromFirstOccurrenceOf (":", false, false);
        auto source = modSourceFromName (rest.upToFirstOccurrenceOf (":", false, false).toLowerCase());
        auto paramId = rest.fromFirstOccurrenceOf (":", false, false);
        auto param = p.getAutomatableParameterByID (paramId);

        if (source == te::FourOscPlugin::none || param == nullptr)
            return false;

        if (std::abs (value) < 1.0e-6)
            p.clearModulation (source, param);
        else
            p.setModulationDepth (source, param, (float) juce::jlimit (-1.0, 1.0, value));

        applied = key + " = " + juce::String (value, 2);
        return true;
    }

    for (auto& s : settingList())
    {
        if (! key.equalsIgnoreCase (s.id) && ! key.equalsIgnoreCase (s.name))
            continue;

        const double v = juce::jlimit (s.min, s.max, value);
        setValue (p, s.id, v);
        applied = juce::String (s.id) + " = " + display (s, v);
        return true;
    }

    return false;
}
