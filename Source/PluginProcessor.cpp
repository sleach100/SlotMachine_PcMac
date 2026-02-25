#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <memory>

#if __has_include("BinaryData.h")
#include "BinaryData.h"
#else
namespace BinaryData
{
    inline const void* getNamedResource(const char*, int& size)
    {
        size = 0;
        return nullptr;
    }
}
#endif

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// 
namespace {
#if JUCE_DEBUG
static int gDebugBlockCounter = 0;

static void logProcessBlockState(const SlotMachineAudioProcessor& proc,
    int numSamples,
    bool transportRunning,
    bool blockStartIsDownbeat,
    double samplesUntilNextDownbeat,
    double masterBeatsAccum,
    double currentCycleBeats,
    double currentCyclePhase01,
    int currentPatternIndex)
{
    DBG("PB[" << gDebugBlockCounter << "] ns=" << numSamples
        << " run=" << (int)transportRunning
        << " downbeat=" << (int)blockStartIsDownbeat
        << " sampToNext=" << samplesUntilNextDownbeat
        << " BPM=" << proc.getBpm()
        << " beatsAccum=" << masterBeatsAccum
        << " cycleBeats=" << currentCycleBeats
        << " phase01=" << currentCyclePhase01
        << " pat=" << currentPatternIndex);
}
#endif

static std::unique_ptr<juce::AudioFormatReader> makeReaderFromMemory(juce::AudioFormatManager& fm,
    const void* data, int sizeBytes)
{
    if (data == nullptr || sizeBytes <= 0)
        return {};

#if JUCE_MAJOR_VERSION >= 7
    auto stream = std::make_unique<juce::MemoryInputStream>(data, (size_t)sizeBytes, false);
    return std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(std::move(stream)));
#else
    auto* stream = new juce::MemoryInputStream(data, (size_t)sizeBytes, false);
    return std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(stream));
#endif
}

static int igcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a < 0 ? -a : a; }
static int ilcm(int a, int b) { return (a == 0 || b == 0) ? 0 : (a / igcd(a, b)) * b; }

static void accumulateCycleLength(int spacingNumerator, int spacingDenominator,
    int& cycleNumerator, int& cycleDenominator, bool& hasCycle)
{
    if (spacingNumerator <= 0 || spacingDenominator <= 0)
        return;

    const int spacingGcd = igcd(spacingNumerator, spacingDenominator);
    spacingNumerator /= spacingGcd;
    spacingDenominator /= spacingGcd;

    if (!hasCycle)
    {
        cycleNumerator = spacingNumerator;
        cycleDenominator = spacingDenominator;
        hasCycle = true;
        return;
    }

    cycleNumerator = ilcm(cycleNumerator, spacingNumerator);
    cycleDenominator = igcd(cycleDenominator, spacingDenominator);

    const int reduce = igcd(cycleNumerator, cycleDenominator);
    if (reduce != 0)
    {
        cycleNumerator /= reduce;
        cycleDenominator /= reduce;
    }
}

static bool decodeReaderToStereoBuffer(juce::AudioFormatReader& reader,
    double targetSampleRate,
    juce::AudioBuffer<float>& output)
{
    const int numChannels = juce::jlimit<int>(1, 2, (int)reader.numChannels);
    const double sourceRate = reader.sampleRate;
    const int maxLength = juce::jlimit<int>(1,
        (int)reader.lengthInSamples,
        (int)std::ceil(8.0 * 60.0 * sourceRate));

    if (maxLength <= 0)
        return false;

    juce::AudioBuffer<float> buffer(numChannels, maxLength);
    reader.read(&buffer, 0, maxLength, 0, true, true);

    const double effectiveTarget = (targetSampleRate > 0.0) ? targetSampleRate : sourceRate;
    if (sourceRate > 0.0 && effectiveTarget > 0.0
        && std::abs(effectiveTarget - sourceRate) > 1.0e-6)
    {
        const double speedRatio = sourceRate / effectiveTarget;
        const int resampledLength = juce::jmax(1,
            (int)std::ceil((double)buffer.getNumSamples() * (effectiveTarget / sourceRate)));
        juce::AudioBuffer<float> resampled(numChannels, resampledLength);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process(speedRatio,
                buffer.getReadPointer(ch),
                resampled.getWritePointer(ch),
                resampledLength);
        }

        buffer = std::move(resampled);
    }

    if (numChannels == 1)
    {
        output.setSize(2, buffer.getNumSamples());
        output.clear();
        output.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
        output.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }
    else
    {
        output.makeCopyOf(buffer);
    }

    return output.getNumSamples() > 0;
}

static constexpr float kDecayUiMin = 1.0f;
static constexpr float kDecayUiMax = 100.0f;
static constexpr float kDecayUiStep = 0.1f;
static constexpr float kDecayUiSkew = 0.4f;

static constexpr float kDecayMsMin = 10.0f;
static constexpr float kDecayMsMax = 4000.0f;

static const juce::Identifier kStateVersionProperty("slotMachineStateVersion");
static const juce::Identifier kAutoInitialiseProperty("slotMachineAutoInitialise");
static const juce::Identifier kPatternsNodeId("patterns");
static const juce::Identifier kPatternNodeType("pattern");
static const juce::Identifier kPatternNameProperty("name");
static const juce::Identifier kPatternMasterBpmProperty("masterBPM");
static const juce::Identifier kPatternTimingModeProperty("timingMode");
static const juce::Identifier kPatternRepeatProperty("repeat");
static const juce::Identifier kCurrentPatternIndexProperty("currentPatternIndex");
static constexpr int kCurrentStateVersion = 4;

static bool varToFloat(const juce::var& value, float& out)
{
    if (value.isDouble() || value.isInt())
    {
        out = (float)value;
        return true;
    }

    if (value.isString())
    {
        const auto text = value.toString();
        if (text.isNotEmpty())
        {
            out = text.getFloatValue();
            return true;
        }
    }

    return false;
}

static float decayUiToMilliseconds(float uiValue)
{
    static const juce::NormalisableRange<float> uiRange(kDecayUiMin, kDecayUiMax, kDecayUiStep, kDecayUiSkew);
    static const juce::NormalisableRange<float> msRange(kDecayMsMin, kDecayMsMax, 1.0f, kDecayUiSkew);

    const float clamped = juce::jlimit(kDecayUiMin, kDecayUiMax, uiValue);
    const float normalised = uiRange.convertTo0to1(clamped);
    return msRange.convertFrom0to1(normalised);
}

static float legacyDecayMsToUi(float msValue)
{
    static const juce::NormalisableRange<float> uiRange(kDecayUiMin, kDecayUiMax, kDecayUiStep, kDecayUiSkew);
    static const juce::NormalisableRange<float> msRange(kDecayMsMin, kDecayMsMax, 1.0f, kDecayUiSkew);

    const float clamped = juce::jlimit(kDecayMsMin, kDecayMsMax, msValue);
    const float normalised = msRange.convertTo0to1(clamped);
    return uiRange.convertFrom0to1(normalised);
}

static void approximateRational(double x, int maxDen, int& num, int& den)
{
    int a0 = (int)std::floor(x);
    if (a0 > maxDen) { num = a0; den = 1; return; }
    int n0 = 1, d0 = 0, n1 = a0, d1 = 1;
    double frac = x - (double)a0;
    while (frac > 1e-12 && d1 <= maxDen) {
        double inv = 1.0 / frac;
        int ai = (int)std::floor(inv);
        int n2 = n0 + ai * n1, d2 = d0 + ai * d1;
        if (d2 > maxDen) break;
        n0 = n1; d0 = d1; n1 = n2; d1 = d2;
        frac = inv - (double)ai;
    }
    num = n1; den = d1;
}

static const juce::StringArray kSlotParamSuffixes{ "Mute", "Solo", "Rate", "Count", "Gain", "Pan", "Decay", "MidiChannel" };

static juce::String slotParamId(int slotIndex, const juce::String& suffix)
{
    return "slot" + juce::String(slotIndex + 1) + "_" + suffix;
}

static int getSlotCountValue(APVTS& apvts, int slotIndex)
{
    const juce::String countParamId = slotParamId(slotIndex, "Count");

    if (auto* raw = apvts.getRawParameterValue(countParamId))
        return juce::jlimit(1, 64, juce::roundToInt(raw->load()));

    if (auto* param = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(countParamId)))
        return juce::jlimit(1, 64, param->get());

    return 4;
}

static constexpr uint64_t kDefaultCountMask = std::numeric_limits<uint64_t>::max();

static uint64_t parseCountMaskVar(const juce::var& value)
{
    if (value.isVoid())
        return kDefaultCountMask;

    if (value.isString())
    {
        auto text = value.toString().trim();
        if (text.isEmpty())
            return kDefaultCountMask;

        if (text.startsWithIgnoreCase("0x"))
            text = text.substring(2);

        const juce::int64 parsed = text.getHexValue64();
        return (uint64_t)(juce::uint64)parsed;
    }

    if (value.isDouble() || value.isInt())
    {
        const double numeric = (double)value;
        if (!std::isfinite(numeric) || numeric < 0.0)
            return kDefaultCountMask;

        if (numeric >= (double)std::numeric_limits<uint64_t>::max())
            return std::numeric_limits<uint64_t>::max();

        return (uint64_t)numeric;
    }

    return kDefaultCountMask;
}

static juce::String serialiseCountMaskValue(uint64_t mask)
{
    auto text = juce::String::toHexString((juce::uint64)mask).toUpperCase();
    return text.paddedLeft('0', 16);
}

static bool varToBool(const juce::var& value, bool defaultValue)
{
    if (value.isVoid())
        return defaultValue;

    if (value.isBool())
        return (bool)value;

    if (value.isInt())
        return (int)value != 0;

    if (value.isDouble())
        return (double)value != 0.0;

    if (value.isString())
        return value.toString().getIntValue() != 0;

    return defaultValue;
}

static int varToInt(const juce::var& value, int defaultValue)
{
    if (value.isVoid())
        return defaultValue;

    if (value.isInt())
        return (int)value;

    if (value.isDouble())
        return (int)std::round((double)value);

    if (value.isString())
        return value.toString().getIntValue();

    return defaultValue;
}

static OfflinePatternData createOfflinePatternDataFromCurrentState(SlotMachineAudioProcessor& processor)
{
    OfflinePatternData data;

    data.bpm = (double)*processor.apvts.getRawParameterValue("masterBPM");

    if (auto* timingParam = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("optTimingMode")))
        data.timingMode = timingParam->get();
    else
        data.timingMode = varToInt(processor.apvts.state.getProperty("optTimingMode"), data.timingMode);

    for (int slot = 0; slot < SlotMachineAudioProcessor::kNumSlots; ++slot)
    {
        const auto base = juce::String("slot") + juce::String(slot + 1) + "_";
        auto& slotData = data.slots[(size_t)slot];

        if (auto* param = processor.apvts.getRawParameterValue(base + "Mute"))
            slotData.mute = param->load();
        if (auto* param = processor.apvts.getRawParameterValue(base + "Solo"))
            slotData.solo = param->load();
        if (auto* param = processor.apvts.getRawParameterValue(base + "Rate"))
            slotData.rate = param->load();
        slotData.count = getSlotCountValue(processor.apvts, slot);
        if (auto* param = processor.apvts.getRawParameterValue(base + "Gain"))
            slotData.gainPercent = param->load();
        if (auto* param = processor.apvts.getRawParameterValue(base + "Pan"))
            slotData.pan = param->load();
        if (auto* param = processor.apvts.getRawParameterValue(base + "Decay"))
            slotData.decayUi = param->load();
        if (auto* param = processor.apvts.getRawParameterValue(base + "MidiChannel"))
        {
            const int choiceIndex = juce::jlimit(0, 15, (int)std::round(param->load()));
            slotData.midiChannel = juce::jlimit(1, 16, choiceIndex + 1);
        }

        slotData.filePath = processor.getSlotFilePath(slot);
        slotData.countMask = processor.getSlotCountMask(slot);
    }

    return data;
}

static OfflinePatternData createOfflinePatternDataFromValueTree(SlotMachineAudioProcessor& processor, const juce::ValueTree& pattern)
{
    OfflinePatternData data;

    float bpmValue = 0.0f;
    if (varToFloat(pattern.getProperty(kPatternMasterBpmProperty), bpmValue))
        data.bpm = bpmValue;
    else
        data.bpm = (double)*processor.apvts.getRawParameterValue("masterBPM");

    data.timingMode = varToInt(pattern.getProperty(kPatternTimingModeProperty), data.timingMode);

    for (int slot = 0; slot < SlotMachineAudioProcessor::kNumSlots; ++slot)
    {
        const juce::String base = juce::String("slot") + juce::String(slot + 1) + "_";
        auto& slotData = data.slots[(size_t)slot];

        slotData.mute = varToBool(pattern.getProperty(base + "Mute"), slotData.mute);
        slotData.solo = varToBool(pattern.getProperty(base + "Solo"), slotData.solo);
        float rateValue = slotData.rate;
        if (varToFloat(pattern.getProperty(base + "Rate"), rateValue))
            slotData.rate = rateValue;

        slotData.count = juce::jlimit(1, 64, varToInt(pattern.getProperty(base + "Count"), slotData.count));

        float gainValue = slotData.gainPercent;
        if (varToFloat(pattern.getProperty(base + "Gain"), gainValue))
            slotData.gainPercent = gainValue;

        float panValue = slotData.pan;
        if (varToFloat(pattern.getProperty(base + "Pan"), panValue))
            slotData.pan = panValue;

        float decayValue = slotData.decayUi;
        if (varToFloat(pattern.getProperty(base + "Decay"), decayValue))
            slotData.decayUi = decayValue;

        slotData.filePath = pattern.getProperty(base + "File").toString();

        int midiChoiceIndex = juce::jlimit(0, 15, slot);
        if (pattern.hasProperty(base + "MidiChannel"))
            midiChoiceIndex = juce::jlimit(0, 15, varToInt(pattern.getProperty(base + "MidiChannel"), midiChoiceIndex));
        slotData.midiChannel = juce::jlimit(1, 16, midiChoiceIndex + 1);

        const juce::var maskVar = pattern.getProperty(base + "CountMask");
        uint64_t mask = parseCountMaskVar(maskVar);
        if (slotData.count > 0)
            mask &= SlotMachineAudioProcessor::maskForBeats(slotData.count);
        else
            mask = 0ull;
        slotData.countMask = mask;
    }

    return data;
}

static int computePatternPlaythroughCycles(const juce::ValueTree& pattern)
{
    const auto repeatVar = pattern.getProperty(kPatternRepeatProperty);
    const int repeat = juce::jmax(1, varToInt(repeatVar, 1));
    return juce::jmax(1, repeat);
}

static bool writeAudioFile(const juce::File& destination,
    const juce::AudioBuffer<float>& sourceBuffer,
    int samplesToWrite,
    double sourceSampleRate,
    double targetSampleRate,
    juce::String& errorMessage)
{
    errorMessage.clear();

    if (samplesToWrite <= 0)
    {
        errorMessage = "Export length is zero.";
        return false;
    }

    const int numChannels = sourceBuffer.getNumChannels();
    const juce::AudioBuffer<float>* bufferToWrite = &sourceBuffer;
    juce::AudioBuffer<float> resampledBuffer;
    int outputSamples = samplesToWrite;

    if (targetSampleRate > 0.0 && std::abs(targetSampleRate - sourceSampleRate) > 1.0e-6)
    {
        const double resampleRatio = targetSampleRate / sourceSampleRate;
        outputSamples = juce::jmax(1, juce::roundToIntAccurate((double)samplesToWrite * resampleRatio));
        resampledBuffer.setSize(numChannels, outputSamples);

        const double sampleRatio = sourceSampleRate / targetSampleRate;
        const int maxSourceIndex = juce::jmax(0, samplesToWrite - 1);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* src = sourceBuffer.getReadPointer(channel);
            float* dst = resampledBuffer.getWritePointer(channel);

            for (int i = 0; i < outputSamples; ++i)
            {
                const double srcIndex = (double)i * sampleRatio;
                int index = (int)srcIndex;
                double frac = srcIndex - (double)index;

                index = juce::jlimit(0, maxSourceIndex, index);
                const int nextIndex = juce::jlimit(0, maxSourceIndex, index + 1);

                const float s0 = src[index];
                const float s1 = src[nextIndex];
                dst[i] = s0 + (s1 - s0) * (float)frac;
            }
        }

        bufferToWrite = &resampledBuffer;
    }

    if (destination.existsAsFile())
    {
        if (!destination.deleteFile())
        {
            errorMessage = "Couldn't overwrite existing file:\n" + destination.getFullPathName();
            return false;
        }
    }

    std::unique_ptr<juce::FileOutputStream> stream(destination.createOutputStream());
    if (stream == nullptr || !stream->openedOk())
    {
        errorMessage = "Couldn't open file for writing:\n" + destination.getFullPathName();
        return false;
    }

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), targetSampleRate > 0.0 ? targetSampleRate : sourceSampleRate,
        (unsigned int)bufferToWrite->getNumChannels(), 24, {}, 0));

    if (writer == nullptr)
    {
        errorMessage = "Couldn't create WAV writer.";
        return false;
    }

    stream.release();

    const bool ok = writer->writeFromAudioSampleBuffer(*bufferToWrite, 0, outputSamples);
    writer.reset();

    if (!ok)
    {
        errorMessage = "Failed to write audio data.";
        return false;
    }

    return true;
}
}

bool renderPatternAudio(SlotMachineAudioProcessor& processor,
    const OfflinePatternData& patternData,
    int cyclesToExport,
    double engineSampleRate,
    RenderedPatternAudio& rendered,
    juce::String& errorMessage)
{
    struct OfflineSlot
    {
        std::unique_ptr<SlotMachineAudioProcessor::SlotVoice> voice;
        int num = 0;
        int den = 1;
        int count = 0;
        float gain = 1.0f;
        uint64_t mask = kDefaultCountMask;
        std::vector<int> triggers;
    };

    errorMessage.clear();

    if (engineSampleRate <= 0.0)
    {
        errorMessage = "Audio engine is not initialised.";
        return false;
    }

    const double bpm = patternData.bpm;
    if (bpm <= 0.0)
    {
        errorMessage = "Master BPM must be greater than zero.";
        return false;
    }

    const int timingMode = patternData.timingMode;
    const double secondsPerBeat = 60.0 / bpm;
    const double countModeCycleBeats = (double)SlotMachineAudioProcessor::kCountModeBaseBeats;

    bool anySolo = false;
    for (const auto& slot : patternData.slots)
        anySolo = anySolo || slot.solo;

    const int maxDen = 32;
    int cycleLengthNumerator = 1;
    int cycleLengthDenominator = 1;
    bool hasCycleLength = false;
    juce::StringArray missingFiles;

    std::vector<OfflineSlot> slotsToRender;
    slotsToRender.reserve(SlotMachineAudioProcessor::kNumSlots);

    for (int i = 0; i < SlotMachineAudioProcessor::kNumSlots; ++i)
    {
        const auto& slotData = patternData.slots[(size_t)i];
        if (slotData.mute)
            continue;

        if (anySolo && !slotData.solo)
            continue;

        const juce::String path = slotData.filePath;
        if (path.isEmpty())
            continue;

        auto voice = std::make_unique<SlotMachineAudioProcessor::SlotVoice>();
        voice->prepare(engineSampleRate);

        bool loaded = false;
        juce::String missingIdentifier = path;

#if __has_include("BinaryData.h")
        {
            int resourceSize = 0;
            if (const void* data = BinaryData::getNamedResource(path.toRawUTF8(), resourceSize))
            {
                if (resourceSize > 0)
                {
                    voice->loadFromMemory(data, resourceSize, path);
                    loaded = voice->hasSample();
                }
            }
        }
#endif

        if (!loaded)
        {
            const juce::File audioFile(path);

            if (!audioFile.existsAsFile())
            {
                missingFiles.add(missingIdentifier);
                continue;
            }

            voice->loadFile(audioFile);
            loaded = voice->hasSample();
            missingIdentifier = audioFile.getFullPathName();
        }

        if (!loaded)
        {
            missingFiles.add(missingIdentifier);
            continue;
        }

        const float rateParam = slotData.rate;
        const int count = juce::jlimit(1, 64, slotData.count);
        const float gainPercent = slotData.gainPercent;
        const float pan = slotData.pan;
        const float decayUi = slotData.decayUi;

        voice->setPan(pan);
        voice->setDecayMs(decayUiToMilliseconds(decayUi));

        OfflineSlot offline;
        offline.voice = std::move(voice);
        offline.gain = juce::jlimit(0.0f, 1.0f, gainPercent * 0.01f);
        offline.mask = slotData.countMask;

        if (timingMode == 0)
        {
            const double rate = juce::jmax(0.0001f, rateParam);
            int num = 0, den = 1;
            approximateRational(rate, maxDen, num, den);
            const int g = igcd(num, den);
            if (g != 0)
            {
                num /= g;
                den /= g;
            }

            if (num <= 0 || den <= 0)
                continue;

            accumulateCycleLength(den, num, cycleLengthNumerator, cycleLengthDenominator, hasCycleLength);

            offline.num = num;
            offline.den = den;
        }
        else
        {
            offline.count = count;
        }

        slotsToRender.push_back(std::move(offline));
    }

    if (!missingFiles.isEmpty())
    {
        errorMessage = "Missing audio files:\n" + missingFiles.joinIntoString("\n");
        return false;
    }

    if (slotsToRender.empty())
    {
        errorMessage = "No active slots to export.";
        return false;
    }

    double cycleBeats = 1.0;
    if (timingMode == 0)
    {
        if (!hasCycleLength)
        {
            cycleLengthNumerator = 1;
            cycleLengthDenominator = 1;
        }

        cycleBeats = juce::jlimit(1.0e-6, 512.0,
            (double)cycleLengthNumerator / (double)cycleLengthDenominator);
    }
    else
    {
        cycleBeats = juce::jlimit(1.0e-6, 512.0, countModeCycleBeats);
    }

    if (cyclesToExport <= 0)
    {
        errorMessage = "Number of cycles must be positive.";
        return false;
    }

    const double samplesPerBeat = secondsPerBeat * engineSampleRate;
    const double totalBeats = cycleBeats * (double)cyclesToExport;
    const double totalSamplesExact = totalBeats * samplesPerBeat;
    const int totalSamplesTarget = juce::jmax(1, (int)std::round(totalSamplesExact));

    int totalSamplesNeeded = totalSamplesTarget;
    bool anyTriggers = false;

    for (auto& slot : slotsToRender)
    {
        auto* voicePtr = slot.voice.get();
        if (voicePtr == nullptr)
            continue;

        voicePtr->stopImmediate();

        if (!voicePtr->hasSample())
            continue;

        if (timingMode == 0)
        {
            const int hitsPerCycle = juce::jmax(1, slot.num);
            const double spacingBeats = (hitsPerCycle > 0 ? cycleBeats / (double)hitsPerCycle : 0.0);
            if (spacingBeats <= 0.0)
                continue;

            const int sampleLength = voicePtr->sample.getNumSamples();

            slot.triggers.clear();
            slot.triggers.reserve(hitsPerCycle * juce::jmax(1, cyclesToExport));

            for (int cycle = 0; cycle < cyclesToExport; ++cycle)
            {
                const double cycleBeatOffset = (double)cycle * cycleBeats;

                for (int hit = 0; hit < hitsPerCycle; ++hit)
                {
                    const double beatPosition = cycleBeatOffset + spacingBeats * (double)hit;
                    const double timeSeconds = beatPosition * secondsPerBeat;
                    const int triggerSample = juce::roundToIntAccurate(timeSeconds * engineSampleRate);

                    if (triggerSample < 0 || triggerSample >= totalSamplesTarget)
                        continue;

                    slot.triggers.push_back(triggerSample);
                    anyTriggers = true;

                    const int endSample = triggerSample + sampleLength;
                    totalSamplesNeeded = std::max(totalSamplesNeeded, endSample);
                }
            }
        }
        else
        {
            const int hitsPerCycle = juce::jmax(1, slot.count);
            const double stepBeats = (hitsPerCycle > 0 ? countModeCycleBeats / (double)hitsPerCycle : 0.0);
            if (stepBeats <= 0.0)
                continue;

            const uint64_t mask = slot.mask & SlotMachineAudioProcessor::maskForBeats(slot.count);
            if (mask == 0)
                continue;

            const int sampleLength = voicePtr->sample.getNumSamples();

            slot.triggers.clear();
            slot.triggers.reserve(hitsPerCycle * juce::jmax(1, cyclesToExport));

            for (int cycle = 0; cycle < cyclesToExport; ++cycle)
            {
                const double cycleBeatOffset = (double)cycle * cycleBeats;

                for (int hit = 0; hit < hitsPerCycle; ++hit)
                {
                    if (((mask >> hit) & 1ull) == 0)
                        continue;

                    const double beatPosition = cycleBeatOffset + stepBeats * (double)hit;
                    const double timeSeconds = beatPosition * secondsPerBeat;
                    const int triggerSample = juce::roundToIntAccurate(timeSeconds * engineSampleRate);

                    if (triggerSample < 0 || triggerSample >= totalSamplesTarget)
                        continue;

                    slot.triggers.push_back(triggerSample);
                    anyTriggers = true;

                    const int endSample = triggerSample + sampleLength;
                    totalSamplesNeeded = std::max(totalSamplesNeeded, endSample);
                }
            }
        }
    }

    if (!anyTriggers || totalSamplesNeeded <= 0)
    {
        errorMessage = "Export length is zero.";
        return false;
    }

    const int numChannels = 2;
    juce::AudioBuffer<float> renderBuffer(numChannels, totalSamplesNeeded);
    renderBuffer.clear();

    for (auto& slot : slotsToRender)
    {
        auto* voicePtr = slot.voice.get();
        if (voicePtr == nullptr || !voicePtr->hasSample())
            continue;

        for (int triggerSample : slot.triggers)
        {
            if (triggerSample < 0 || triggerSample >= totalSamplesNeeded)
                continue;

            voicePtr->trigger();

            const int remaining = totalSamplesNeeded - triggerSample;
            if (remaining <= 0)
                continue;

            juce::AudioBuffer<float> view(renderBuffer.getArrayOfWritePointers(),
                renderBuffer.getNumChannels(), triggerSample, remaining);

            voicePtr->mixInto(view, view.getNumSamples(), slot.gain);
        }
    }

    // Use totalSamplesNeeded to preserve the tail of the last beat
    const int finalSampleCount = totalSamplesNeeded;
    renderBuffer.setSize(numChannels, finalSampleCount, true, true, true);

    rendered.buffer = std::move(renderBuffer);
    rendered.samples = finalSampleCount;                  // Full length including tail
    rendered.beatAlignedSamples = totalSamplesTarget;     // Beat-aligned boundary for positioning
    return true;
}

bool renderPatternMidi(SlotMachineAudioProcessor& processor,
    const OfflinePatternData& patternData,
    int cyclesToExport,
    double bpm,
    int ppq,
    bool useFixedNoteLength,
    RenderedPatternMidi& out,
    juce::String& errorMessage)
{
    struct OfflineSlot
    {
        std::unique_ptr<SlotMachineAudioProcessor::SlotVoice> voice;
        int num = 0;
        int den = 1;
        int count = 0;
        float gain = 1.0f;
        uint64_t mask = kDefaultCountMask;
        int midiChannel = 1;
        double noteLengthBeats = 0.25;
        std::vector<double> triggerBeats;
    };

    errorMessage.clear();
    out.sequence.clear();
    out.totalTicks = 0;

    const double engineSampleRate = processor.currentSampleRate;
    if (engineSampleRate <= 0.0)
    {
        errorMessage = "Audio engine is not initialised.";
        return false;
    }

    if (cyclesToExport <= 0)
    {
        errorMessage = "Number of cycles must be positive.";
        return false;
    }

    if (ppq <= 0)
    {
        errorMessage = "PPQ must be positive.";
        return false;
    }

    const double patternBpm = patternData.bpm;
    if (patternBpm <= 0.0)
    {
        errorMessage = "Master BPM must be greater than zero.";
        return false;
    }

    const double tempoForMeta = (bpm > 0.0) ? bpm : patternBpm;
    const int timingMode = patternData.timingMode;
    const double secondsPerBeat = 60.0 / patternBpm;
    const double countModeCycleBeats = (double)SlotMachineAudioProcessor::kCountModeBaseBeats;

    bool anySolo = false;
    for (const auto& slot : patternData.slots)
        anySolo = anySolo || slot.solo;

    const int maxDen = 32;
    int cycleLengthNumerator = 1;
    int cycleLengthDenominator = 1;
    bool hasCycleLength = false;
    juce::StringArray missingFiles;

    std::vector<OfflineSlot> slotsToRender;
    slotsToRender.reserve(SlotMachineAudioProcessor::kNumSlots);

    for (int i = 0; i < SlotMachineAudioProcessor::kNumSlots; ++i)
    {
        const auto& slotData = patternData.slots[(size_t)i];
        if (slotData.mute)
            continue;

        if (anySolo && !slotData.solo)
            continue;

        const juce::String path = slotData.filePath;
        if (path.isEmpty())
            continue;

        auto voice = std::make_unique<SlotMachineAudioProcessor::SlotVoice>();
        voice->prepare(engineSampleRate);

        bool loaded = false;
        juce::String missingIdentifier = path;

#if __has_include("BinaryData.h")
        {
            int resourceSize = 0;
            if (const void* data = BinaryData::getNamedResource(path.toRawUTF8(), resourceSize))
            {
                if (resourceSize > 0)
                {
                    voice->loadFromMemory(data, resourceSize, path);
                    loaded = voice->hasSample();
                }
            }
        }
#endif

        if (!loaded)
        {
            const juce::File audioFile(path);

            if (!audioFile.existsAsFile())
            {
                missingFiles.add(missingIdentifier);
                continue;
            }

            voice->loadFile(audioFile);
            loaded = voice->hasSample();
            missingIdentifier = audioFile.getFullPathName();
        }

        if (!loaded)
        {
            missingFiles.add(missingIdentifier);
            continue;
        }

        const float rateParam = slotData.rate;
        const int count = juce::jlimit(1, 64, slotData.count);
        const float gainPercent = slotData.gainPercent;
        const float decayUi = slotData.decayUi;

        voice->setDecayMs(decayUiToMilliseconds(decayUi));

        OfflineSlot offline;
        offline.voice = std::move(voice);
        offline.gain = juce::jlimit(0.0f, 1.0f, gainPercent * 0.01f);
        offline.mask = slotData.countMask;
        offline.midiChannel = juce::jlimit(1, 16, slotData.midiChannel);

        double noteLengthBeats;
        if (useFixedNoteLength)
        {
            // Fixed 32nd note length: 1/8 of a quarter note = 0.125 beats
            noteLengthBeats = 0.125;
        }
        else
        {
            // Match sample length (original behavior)
            int noteSamples = offline.voice->sample.getNumSamples();
            if (offline.voice->envMaxSamples > 0)
                noteSamples = juce::jmin(noteSamples, offline.voice->envMaxSamples);
            noteSamples = juce::jmax(1, noteSamples);

            const double noteSeconds = (double)noteSamples / engineSampleRate;
            noteLengthBeats = noteSeconds / secondsPerBeat;
            if (noteLengthBeats <= 0.0)
                noteLengthBeats = 1.0 / (double)ppq;
        }
        offline.noteLengthBeats = noteLengthBeats;

        if (timingMode == 0)
        {
            const double rate = juce::jmax(0.0001f, rateParam);
            int num = 0, den = 1;
            approximateRational(rate, maxDen, num, den);
            const int g = igcd(num, den);
            if (g != 0)
            {
                num /= g;
                den /= g;
            }

            if (num <= 0 || den <= 0)
                continue;

            accumulateCycleLength(den, num, cycleLengthNumerator, cycleLengthDenominator, hasCycleLength);

            offline.num = num;
            offline.den = den;
        }
        else
        {
            offline.count = count;
        }

        slotsToRender.push_back(std::move(offline));
    }

    if (!missingFiles.isEmpty())
    {
        errorMessage = "Missing audio files:\n" + missingFiles.joinIntoString("\n");
        return false;
    }

    if (slotsToRender.empty())
    {
        errorMessage = "No active slots to export.";
        return false;
    }

    double cycleBeats = 1.0;
    if (timingMode == 0)
    {
        if (!hasCycleLength)
        {
            cycleLengthNumerator = 1;
            cycleLengthDenominator = 1;
        }

        cycleBeats = juce::jlimit(1.0e-6, 512.0,
            (double)cycleLengthNumerator / (double)cycleLengthDenominator);
    }
    else
    {
        cycleBeats = juce::jlimit(1.0e-6, 512.0, countModeCycleBeats);
    }

    const double totalBeats = cycleBeats * (double)cyclesToExport;

    bool anyTriggers = false;

    for (auto& slot : slotsToRender)
    {
        auto* voicePtr = slot.voice.get();
        if (voicePtr == nullptr)
            continue;

        if (!voicePtr->hasSample())
            continue;

        if (timingMode == 0)
        {
            const int hitsPerCycle = juce::jmax(1, slot.num);
            const double spacingBeats = (hitsPerCycle > 0 ? cycleBeats / (double)hitsPerCycle : 0.0);
            if (spacingBeats <= 0.0)
                continue;

            slot.triggerBeats.clear();
            slot.triggerBeats.reserve(hitsPerCycle * juce::jmax(1, cyclesToExport));

            for (int cycle = 0; cycle < cyclesToExport; ++cycle)
            {
                const double cycleBeatOffset = (double)cycle * cycleBeats;

                for (int hit = 0; hit < hitsPerCycle; ++hit)
                {
                    const double beatPosition = cycleBeatOffset + spacingBeats * (double)hit;
                    if (beatPosition < 0.0 || beatPosition > totalBeats)
                        continue;

                    slot.triggerBeats.push_back(beatPosition);
                    anyTriggers = true;
                }
            }
        }
        else
        {
            const int hitsPerCycle = juce::jmax(1, slot.count);
            const double stepBeats = (hitsPerCycle > 0 ? countModeCycleBeats / (double)hitsPerCycle : 0.0);
            if (stepBeats <= 0.0)
                continue;

            const uint64_t mask = slot.mask & SlotMachineAudioProcessor::maskForBeats(slot.count);
            if (mask == 0)
                continue;

            slot.triggerBeats.clear();
            slot.triggerBeats.reserve(hitsPerCycle * juce::jmax(1, cyclesToExport));

            for (int cycle = 0; cycle < cyclesToExport; ++cycle)
            {
                const double cycleBeatOffset = (double)cycle * cycleBeats;

                for (int hit = 0; hit < hitsPerCycle; ++hit)
                {
                    if (((mask >> hit) & 1ull) == 0)
                        continue;

                    const double beatPosition = cycleBeatOffset + stepBeats * (double)hit;
                    if (beatPosition < 0.0 || beatPosition > totalBeats)
                        continue;

                    slot.triggerBeats.push_back(beatPosition);
                    anyTriggers = true;
                }
            }
        }
    }

    if (!anyTriggers)
    {
        errorMessage = "Export length is zero.";
        return false;
    }

    out.sequence.addEvent(juce::MidiMessage::tempoMetaEvent((int)std::round(60000000.0 / tempoForMeta)), 0.0);
    out.sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 2), 0.0);

    int64_t maxTick = 0;
    const int noteNumber = 60;

    for (const auto& slot : slotsToRender)
    {
        const int midiChannel = juce::jlimit(1, 16, slot.midiChannel);
        const int velocity = juce::jlimit(1, 127, (int)std::round(slot.gain * 127.0f));
        const double noteTicksExact = juce::jmax(1.0, slot.noteLengthBeats * (double)ppq);
        const int noteTicks = juce::jmax(1, (int)std::llround(noteTicksExact));

        for (double beatPosition : slot.triggerBeats)
        {
            const double startTickDouble = beatPosition * (double)ppq;
            const int startTick = juce::jmax(0, (int)std::llround(startTickDouble));
            const int offTick = juce::jmax(startTick + 1, startTick + noteTicks);

            out.sequence.addEvent(juce::MidiMessage::noteOn(midiChannel, noteNumber, (juce::uint8)velocity), (double)startTick);
            out.sequence.addEvent(juce::MidiMessage::noteOff(midiChannel, noteNumber), (double)offTick);

            maxTick = std::max<int64_t>(maxTick, offTick);
        }
    }

    const int totalTicksFromBeats = juce::jmax(1, (int)std::llround(totalBeats * (double)ppq));
    maxTick = std::max<int64_t>(maxTick, totalTicksFromBeats);

    if (maxTick > (int64_t)std::numeric_limits<int>::max())
    {
        errorMessage = "Export length is too large.";
        return false;
    }

    out.sequence.sort();
    out.sequence.addEvent(juce::MidiMessage::endOfTrack(), (double)maxTick);
    out.totalTicks = (int)maxTick;                  // Full length including note tails
    out.beatAlignedTicks = totalTicksFromBeats;     // Beat-aligned boundary for positioning
    out.bpm = tempoForMeta;

    return true;
}

//
// SlotVoice implementation
void SlotMachineAudioProcessor::SlotVoice::prepare(double sr)
{
    sampleRate = sr;
    resetPhase(true);
    playIndex = -1;
    playLength = 0;
    env = 0.0f; envAlpha = 1.0f; envSamplesElapsed = 0; envMaxSamples = 0;
    tailSample.setSize(0, 0);
    tailIndex = -1;
    tailLength = 0;
    tailEnv = 0.0f; tailEnvAlpha = 1.0f; tailEnvSamplesElapsed = 0; tailEnvMaxSamples = 0;
    tailPanL = panL;
    tailPanR = panR;
    tailActive = false;
}

void SlotMachineAudioProcessor::SlotVoice::resetPhase(bool hard)
{
    if (hard)
    {
        phase = 0.0;
        framesUntilHit = 0.0;
    }
}

void SlotMachineAudioProcessor::SlotVoice::setPan(float pan)
{
    const float a = juce::jlimit(-1.0f, 1.0f, pan);
    const float theta = juce::jmap(a, -1.0f, 1.0f, 0.0f, juce::MathConstants<float>::halfPi);
    panL = std::cos(theta);
    panR = std::sin(theta);
}

void SlotMachineAudioProcessor::SlotVoice::loadFile(const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));

    active = false;
    sample.setSize(0, 0);
    filePath = {};

    if (r != nullptr)
    {
        juce::AudioBuffer<float> decoded;
        if (decodeReaderToStereoBuffer(*r, sampleRate, decoded))
        {
            sample = std::move(decoded);
            active = (sample.getNumSamples() > 0);
            filePath = f.getFullPathName();

            playIndex = -1;
            playLength = 0;
            env = 0.0f; envSamplesElapsed = 0; envMaxSamples = 0;
        }
    }

}

void SlotMachineAudioProcessor::SlotVoice::loadFromMemory(const void* data, int sizeBytes, const juce::String& pseudoName)
{
    active = false;
    sample.setSize(0, 0);
    filePath = pseudoName;

    if (data == nullptr || sizeBytes <= 0)
        return;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto reader = makeReaderFromMemory(fm, data, sizeBytes);
    if (reader == nullptr)
        return;

    juce::AudioBuffer<float> decoded;
    if (decodeReaderToStereoBuffer(*reader, sampleRate, decoded))
    {
        sample = std::move(decoded);
        active = (sample.getNumSamples() > 0);

        playIndex = -1;
        playLength = 0;
        env = 0.0f;
        envSamplesElapsed = 0;
        envMaxSamples = 0;
    }
}

void SlotMachineAudioProcessor::SlotVoice::trigger()
{
    if (!hasSample())
        return;

    playIndex = 0;
    playLength = sample.getNumSamples();
    ++hitCounter;
    env = 1.0f; envSamplesElapsed = 0;
}

void SlotMachineAudioProcessor::SlotVoice::mixInto(juce::AudioBuffer<float>& io, int numSamples, float gain)
{
    auto mixBuffer = [&io](const juce::AudioBuffer<float>& src, int& index, int length,
        float& envLevel, float& envAlphaRef, int& envSamples, int envSamplesMax,
        float panLeft, float panRight, int numSamplesToProcess, float gainScale)
    {
        if (index < 0 || length <= 0)
            return 0;

        const int remain = length - index;
        const int n = juce::jmin(numSamplesToProcess, remain);

        auto* dstL = io.getWritePointer(0);
        auto* dstR = io.getNumChannels() > 1 ? io.getWritePointer(1) : nullptr;

        const float gL = gainScale * panLeft;
        const float gR = gainScale * panRight;

        const float* srcL = src.getNumSamples() > 0 ? src.getReadPointer(0, index) : nullptr;
        const float* srcR = (src.getNumChannels() > 1 && src.getNumSamples() > 0)
            ? src.getReadPointer(1, index)
            : nullptr;

        if (srcL == nullptr)
            return 0;

        if (dstR != nullptr && srcR != nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                const float envValue = envLevel;
                dstL[i] += srcL[i] * gL * envValue;
                dstR[i] += srcR[i] * gR * envValue;
                envLevel *= envAlphaRef;
                ++envSamples;
            }
        }
        else if (dstR != nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                const float s = srcL[i];
                const float envValue = envLevel;
                dstL[i] += s * gL * envValue;
                dstR[i] += s * gR * envValue;
                envLevel *= envAlphaRef;
                ++envSamples;
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                dstL[i] += srcL[i] * gainScale * envLevel;
                envLevel *= envAlphaRef;
                ++envSamples;
            }
        }

        index += n;
        if (envSamplesMax > 0 && envSamples >= envSamplesMax && envLevel < 1.0e-4f)
            index = -1;
        else if (index >= length)
            index = -1;

        return n;
    };

    if (tailActive)
    {
        const int mixed = mixBuffer(tailSample, tailIndex, tailLength,
            tailEnv, tailEnvAlpha, tailEnvSamplesElapsed, tailEnvMaxSamples,
            tailPanL, tailPanR, numSamples, gain);
        if (tailIndex < 0 || mixed <= 0)
        {
            tailSample.setSize(0, 0);
            tailIndex = -1;
            tailLength = 0;
            tailEnv = 0.0f; tailEnvAlpha = 1.0f; tailEnvSamplesElapsed = 0; tailEnvMaxSamples = 0;
            tailPanL = panL; tailPanR = panR;
            tailActive = false;
        }
    }

    mixBuffer(sample, playIndex, playLength,
        env, envAlpha, envSamplesElapsed, envMaxSamples,
        panL, panR, numSamples, gain);
}

void SlotMachineAudioProcessor::SlotVoice::stopImmediate() noexcept
{
    playIndex = -1;
    playLength = 0;
    env = 0.0f;
    envSamplesElapsed = 0;

    tailSample.setSize(0, 0);
    tailIndex = -1;
    tailLength = 0;
    tailEnv = 0.0f;
    tailEnvAlpha = 1.0f;
    tailEnvSamplesElapsed = 0;
    tailEnvMaxSamples = 0;
    tailPanL = panL;
    tailPanR = panR;
    tailActive = false;
}

void SlotMachineAudioProcessor::SlotVoice::clear(bool allowTail) noexcept
{
    if (allowTail && playIndex >= 0 && playLength > playIndex && sample.getNumSamples() > 0)
    {
        tailSample = std::move(sample);
        tailIndex = playIndex;
        tailLength = playLength;
        tailEnv = env;
        tailEnvAlpha = envAlpha;
        tailEnvSamplesElapsed = envSamplesElapsed;
        tailEnvMaxSamples = envMaxSamples;
        tailPanL = panL;
        tailPanR = panR;
        tailActive = true;
    }
    else if (!allowTail)
    {
        tailSample.setSize(0, 0);
        tailIndex = -1;
        tailLength = 0;
        tailEnv = 0.0f; tailEnvAlpha = 1.0f; tailEnvSamplesElapsed = 0; tailEnvMaxSamples = 0;
        tailPanL = panL; tailPanR = panR;
        tailActive = false;
    }
    else if (!tailActive)
    {
        // allowTail was requested but nothing is currently ringing, ensure clean state
        tailSample.setSize(0, 0);
        tailIndex = -1;
        tailLength = 0;
        tailEnv = 0.0f; tailEnvAlpha = 1.0f; tailEnvSamplesElapsed = 0; tailEnvMaxSamples = 0;
        tailPanL = panL; tailPanR = panR;
        tailActive = false;
    }

    sample.setSize(0, 0);
    active = false;
    filePath = {};
    playIndex = -1;
    playLength = 0;
    phase = 0.0;
    framesUntilHit = 0.0;
    env = 0.0f; envAlpha = 1.0f; envSamplesElapsed = 0; envMaxSamples = 0;
    if (!tailActive)
        tailEnv = 0.0f;
}


//==============================================================================
// Processor
SlotMachineAudioProcessor::SlotMachineAudioProcessor()
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
{
    apvts.state.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);
    if (!apvts.state.hasProperty(kAutoInitialiseProperty))
        apvts.state.setProperty(kAutoInitialiseProperty, true, nullptr);
    initialiseOnFirstEditor = static_cast<bool>(apvts.state.getProperty(kAutoInitialiseProperty, true));

    refreshSlotCountMasksFromState();

    for (auto& beatIndex : currentBeatIndices)
        beatIndex.store(-1, std::memory_order_relaxed);
}

SlotMachineAudioProcessor::~SlotMachineAudioProcessor() {}

//==============================================================================
// Parameters (master, slots, and Options)
APVTS::ParameterLayout SlotMachineAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;

    // Master
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "masterBPM", "Master BPM",
        juce::NormalisableRange<float>(10.0f, 400.0f, 0.01f, 0.33f), 120.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "masterRun", "Master Run", false));

    // Per-slot
    juce::StringArray midiChannelChoices;
    for (int ch = 1; ch <= 16; ++ch)
        midiChannelChoices.add("Ch " + juce::String(ch));

    for (int i = 1; i <= kNumSlots; ++i)
    {
        const auto base = juce::String("slot") + juce::String(i) + "_";

        layout.add(std::make_unique<juce::AudioParameterBool>(base + "Mute", "Slot " + juce::String(i) + " Mute", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(base + "Solo", "Slot " + juce::String(i) + " Solo", false));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            base + "Rate", "Slot " + juce::String(i) + " Rate",
            juce::NormalisableRange<float>(0.0625f, 4.00f, 0.0001f, 0.5f), 1.0f));

        layout.add(std::make_unique<juce::AudioParameterInt>(
            base + "Count", "Slot " + juce::String(i) + " Beats/Cycle",
            1, 64, 4));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            base + "Gain", "Slot " + juce::String(i) + " Gain",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f, 1.0f), 80.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            base + "Pan", "Slot " + juce::String(i) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f, 1.0f), 0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            base + "Decay", "Slot " + juce::String(i) + " Decay (ms)",
            juce::NormalisableRange<float>(kDecayUiMin, kDecayUiMax, kDecayUiStep, kDecayUiSkew), kDecayUiMax));

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            base + "MidiChannel", "Slot " + juce::String(i) + " MIDI Channel",
            midiChannelChoices, juce::jlimit(0, midiChannelChoices.size() - 1, i - 1)));
    }

    // ===== Options (persisted) =====
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optShowMasterBar", "Show Master Progress Bar", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optShowSlotBars", "Show Slot Progress Bars", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optShowVisualizer", "Show Visualizer", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "optVisualizerEdgeWalk", "Visualizer Mode", 0, 2, 0));  // 0=Edge Walk, 1=Orbit, 2=Mixed
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerMasterPulse", "Visualizer Master Pulse", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerBreathe", "Visualizer Breathe", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerElectricArc", "Visualizer Neural Chaos", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerStarlightTwinkle", "Visualizer Starlight Twinkle", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerAlternatingRotation", "Visualizer Alternating Rotation", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerColorwave", "Visualizer Colorwave", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerNebulaDrift", "Visualizer Nebula Drift", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "optVisualizerNeonSweep", "Visualizer Neon Sweep", false));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "optSampleRate", "Export Sample Rate (Hz)", 44100, 48000, 48000));

    auto mkRGB = [](uint8_t r, uint8_t g, uint8_t b) -> int { return (int)((r << 16) | (g << 8) | b); };

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "optSlotScale", "Slot Row Scale",
        juce::NormalisableRange<float>(0.75f, 1.0f, 0.05f), 0.8f));

    // Glow (selected frame)
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "optGlowColor", "Selected Glow Color (RGB)", 0x000000, 0xFFFFFF, mkRGB(0x69, 0x94, 0xFC)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "optGlowAlpha", "Selected Glow Alpha", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.431f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "optGlowWidth", "Selected Glow Width", juce::NormalisableRange<float>(0.5f, 24.0f, 0.01f), 1.34f));

    // Pulse
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "optPulseColor", "Pulse Color (RGB)", 0x000000, 0xFFFFFF, mkRGB(0xD5, 0xCF, 0xEE)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "optPulseAlpha", "Pulse Alpha", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "optPulseWidth", "Pulse Width (px)", juce::NormalisableRange<float>(0.5f, 36.0f, 0.01f), 4.0f));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "optTimingMode", "Timing Mode", 0, 1, 1));

    return layout;
}

//==============================================================================
// Prepare / Release
void SlotMachineAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    masterBeatsAccum = 0.0;

    for (auto& beatIndex : currentBeatIndices)
        beatIndex.store(-1, std::memory_order_relaxed);

    for (auto& s : slots)
        s.prepare(sampleRate);

    {
        const juce::SpinLock::ScopedLockType lock(previewLock);
        previewVoice.reset();
    }

    scopeQueue.reset();
    scratchMono.setSize(1, juce::jmax(1, samplesPerBlock));
    scratchMono.clear();

    pendingTabSwitchIndex.store(-1, std::memory_order_relaxed);
    blockStartIsDownbeat = true;
    suppressHitsForSamples = 0;
    samplesUntilNextDownbeat = 0.0;

    resetAllPhases(true);
}

void SlotMachineAudioProcessor::releaseResources() {}

//==============================================================================
// Processing (MASTER-LOCKED PHASE/HITS)
void SlotMachineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Stamp the wall-clock time at the very start of this block so the render thread
    // can extrapolate masterPhase forward from the last-known audio position.
    lastProcessBlockWallTimeMs.store(juce::Time::getMillisecondCounter(),
                                     std::memory_order_relaxed);

    const int  numSamples = buffer.getNumSamples();
    const int  totalOut = getTotalNumOutputChannels();
    const int  totalIn = getTotalNumInputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    const bool run = apvts.getRawParameterValue("masterRun")->load();
    const float masterBPM = *apvts.getRawParameterValue("masterBPM");
    const double spb = (masterBPM > 0.0f ? 60.0 / (double)masterBPM : 0.0); // seconds per beat
    const bool transportRunning = run && spb > 0.0;
    const double samplesPerBeat = transportRunning ? spb * currentSampleRate : 0.0;

#if JUCE_DEBUG
    ++gDebugBlockCounter;
#endif

    if (!transportRunning)
    {
        blockStartIsDownbeat = true;
        samplesUntilNextDownbeat = 0.0;
    }
    else
    {
        blockStartIsDownbeat = (samplesUntilNextDownbeat <= 0.5);
    }

    const int requestedTab = pendingTabSwitchIndex.load(std::memory_order_acquire);
    if (requestedTab >= 0 && blockStartIsDownbeat)
    {
#if JUCE_DEBUG
        const int patternBefore = getCurrentPatternIndex();
        DBG("PB[" << gDebugBlockCounter << "] TAB_SWITCH_APPLY requestedTab=" << requestedTab
            << " beatsAccum=" << masterBeatsAccum
            << " phase01=" << currentCyclePhase01
            << " sampToNext=" << samplesUntilNextDownbeat
            << " oldPat=" << patternBefore);
#endif
        applyTabSwitchAtBlockStart_NoResetTails(requestedTab);
        pendingTabSwitchIndex.store(-1, std::memory_order_release);
        suppressHitsForSamples = kDownbeatDebounceSamples;
#if JUCE_DEBUG
        const int patternAfter = getCurrentPatternIndex();
        DBG("PB[" << gDebugBlockCounter << "] TAB_SWITCH_DONE newPat=" << patternAfter);
#endif
    }

    const int suppressedHead = juce::jlimit(0, numSamples, suppressHitsForSamples);

    // Always emit both audio and MIDI
    const bool wantAudio = true;
    const bool wantMidi = true;

    bpmAtomic.store((double) masterBPM, std::memory_order_relaxed);
    numeratorAtomic.store(kCountModeBaseBeats, std::memory_order_relaxed);

    // Solo mask
    bool anySolo = false;
    bool soloMask[kNumSlots] = {};
    for (int i = 0; i < kNumSlots; ++i)
    {
        const bool solo = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Solo")->load();
        soloMask[i] = solo;
        anySolo = anySolo || solo;
    }

    // Advance master beats accumulator once per block
    const double dtSec = (double)numSamples / currentSampleRate;
    const double prevBeats = masterBeatsAccum;
    if (transportRunning)
        masterBeatsAccum += dtSec / spb;
    const double currBeats = masterBeatsAccum;
    const int timingMode = (int)std::round(apvts.getRawParameterValue("optTimingMode")->load());
    const double countModeCycleBeats = (double)kCountModeBaseBeats;

    // --- Compute current poly-cycle (in beats), matching Export MIDI logic ---
    int cycleLengthNumerator = 1;
    int cycleLengthDenominator = 1;
    bool hasCycleLength = false;
    const int maxDen = 32;

    for (int i = 0; i < kNumSlots; ++i)
    {
        const bool mute = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Mute")->load();
        if (mute) continue;
        if (anySolo && !soloMask[i]) continue;
        if (!slots[i].hasSample()) continue;

        if (timingMode == 0)
        {
            const float rateF = *apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Rate");
            const double rate = juce::jmax(0.0001f, rateF);

            int num = 0, den = 1;
            approximateRational(rate, maxDen, num, den);
            int g = igcd(num, den);
            num /= g;
            den /= g;
            accumulateCycleLength(den, num, cycleLengthNumerator, cycleLengthDenominator, hasCycleLength);
        }
        // Beats/Cycle mode does not alter the master cycle length
    }

    double cycleBeats = 1.0;
    if (timingMode == 0)
    {
        if (!hasCycleLength)
        {
            cycleLengthNumerator = 1;
            cycleLengthDenominator = 1;
        }

        cycleBeats = juce::jlimit(1.0e-6, 512.0,
            (double)cycleLengthNumerator / (double)cycleLengthDenominator);
    }
    else
    {
        cycleBeats = juce::jlimit(1.0e-6, 512.0, countModeCycleBeats);
    }

    // Cache for editor
    currentCycleBeats = cycleBeats;
    currentCycleBeatsCached.store(cycleBeats, std::memory_order_relaxed);
    if (currentCycleBeats > 0.0)
        currentCyclePhase01 = std::fmod(masterBeatsAccum, currentCycleBeats) / currentCycleBeats;
    else
        currentCyclePhase01 = 0.0;

#if JUCE_DEBUG
    if (blockStartIsDownbeat)
    {
        logProcessBlockState(*this,
            numSamples,
            transportRunning,
            blockStartIsDownbeat,
            samplesUntilNextDownbeat,
            masterBeatsAccum,
            currentCycleBeats,
            currentCyclePhase01,
            getCurrentPatternIndex());
    }
#endif

    // Per-slot timing/render
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slots[i];

        const bool mute = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Mute")->load();
        const bool solo = soloMask[i];
        const bool slotAudible = !mute && (!anySolo || solo);

        const float rate = *apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Rate");
        int count = 4;
        if (auto* countParam = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Count"))
            count = juce::jlimit(1, 64, (int)std::round(countParam->load()));
        const float gainPercent = *apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Gain");
        const float gain = gainPercent * 0.01f;
        const float pan = *apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Pan");
        const float decayUi = *apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Decay");
        const float decayMs = decayUiToMilliseconds(decayUi);
        const auto* midiChoiceRaw = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_MidiChannel");
        int midiChoiceIndex = i;
        if (midiChoiceRaw != nullptr)
            midiChoiceIndex = juce::jlimit(0, 15, (int)std::round(midiChoiceRaw->load()));

        const int midiChannel = juce::jlimit(1, 16, midiChoiceIndex + 1);
       

        s.setPan(pan);
        s.setDecayMs(decayMs);

        int currentBeatIndex = -1;

        // Always keep visual phase tied to master beat phase (even if muted or idle)
        const double rateD = (double)rate;
        if (spb > 0.0)
        {
            if (timingMode == 0 && rateD > 0.0)
            {
                s.phase = std::fmod(currBeats * rateD, 1.0);
            }
            else if (timingMode == 1)
            {
                // --- BeatsPerCycle mode ---
                const double stepBeats = (count > 0 ? countModeCycleBeats / (double)count : 0.0);
                if (stepBeats > 0.0)
                {
                    const double cycleBeats = stepBeats * (double)count;
                    double beatPosition = std::fmod(currBeats, cycleBeats);
                    if (beatPosition < 0.0)
                        beatPosition += cycleBeats;

                    const double stepIndex = beatPosition / stepBeats;
                    int computedIndex = (int)std::floor(stepIndex + 1.0e-9);
                    if (computedIndex >= count)
                        computedIndex = count - 1;
                    if (computedIndex < 0)
                        computedIndex = 0;

                    if (run)
                        currentBeatIndex = computedIndex;

                    s.phase = std::fmod(currBeats, stepBeats) / stepBeats;
                }
            }
        }
        else
        {
            // keep previous phase
        }

        if (timingMode != 1 || !run)
            currentBeatIndex = -1;

#if JUCE_DEBUG
        if (i == 0)
        {
            const int previousBeat = currentBeatIndices[(size_t)i].load(std::memory_order_relaxed);
            if (previousBeat != currentBeatIndex)
            {
                DBG("PB[" << gDebugBlockCounter << "] SLOT0_BEAT prev=" << previousBeat
                    << " new=" << currentBeatIndex
                    << " phase01=" << currentCyclePhase01
                    << " beatsAccum=" << masterBeatsAccum);
            }
        }
#endif

        currentBeatIndices[(size_t)i].store(currentBeatIndex, std::memory_order_relaxed);

        // --- manual click triggers (editor requests) ---
        const int manualHits = pendingManualTriggers[(size_t)i].exchange(0, std::memory_order_relaxed);
        if (manualHits > 0)
        {
            if (suppressedHead > 0)
            {
                // Skip manual triggers during the debounce window at the top of the block.
            }
            else if (s.hasSample() && slotAudible)
            {
                s.trigger();

                if (wantMidi) {
                    const int noteNumber = 60; // Middle C for all slots
                    const int velocity = juce::jlimit(1, 127, (int)std::round(gain * 127.0f));
                    const int onPos = 0;
                    const int offPos = juce::jmin(numSamples - 1, (int)std::round(0.010 * currentSampleRate));
                    midi.addEvent(juce::MidiMessage::noteOn(midiChannel, noteNumber, (juce::uint8)velocity), onPos);
                    midi.addEvent(juce::MidiMessage::noteOff(midiChannel, noteNumber), offPos);
                }

              
            }
        }

        if (slotAudible && !s.wasAudibleLastBlock)
            s.stopImmediate();

        // Render any currently ringing sample (works even when transport is stopped)
        if (wantAudio)
        {
            const float mixGain = slotAudible ? gain : 0.0f;
            s.mixInto(buffer, numSamples, mixGain);
        }

        // No processing if no sample or transport stopped (but visuals still update)
        if (!s.hasSample() || !run || spb <= 0.0)
        {
            s.wasAudibleLastBlock = slotAudible;
            continue;
        }

        if (timingMode == 0)
        {
            if (rateD <= 0.0)
            {
                s.wasAudibleLastBlock = slotAudible;
                continue;
            }

            // Render any currently ringing sample (only if Audio/Both and not muted/soloed out)
            //if (wantAudio && !mute && (!anySolo || solo))
            //    s.mixInto(buffer, numSamples, gain);

            // Compute how many slot-beats occur within this block
            const double slotBeatsStart = prevBeats * rateD;
            const double slotBeatsEnd = currBeats * rateD;
            const double epsilon = 1e-9;
            const int firstHitCount = (int)std::ceil(slotBeatsStart - epsilon);
            const int endHitExclusive = (int)std::ceil(slotBeatsEnd - epsilon);
            const int hitsThisBlock = juce::jmax(0, endHitExclusive - firstHitCount);

            if (hitsThisBlock > 0)
            {
                for (int h = 0; h < hitsThisBlock; ++h)
                {
                    const double targetCount = (double)(firstHitCount + h);

                    // position inside this block 0..1 for this hit
                    const double denom = (slotBeatsEnd - slotBeatsStart);
                    double fracBlock = 0.0;
                    if (std::abs(denom) > 1e-12)
                        fracBlock = (targetCount - slotBeatsStart) / denom;
                    fracBlock = juce::jlimit(0.0, 1.0, fracBlock);

                    const int hitOffset = juce::jlimit(0, numSamples - 1,
                        (int)std::floor(fracBlock * (double)numSamples + 0.5));

                    if (hitOffset < suppressedHead)
                        continue;

                    // Fire and mix from hit point to block end
                    s.trigger();

                    // MIDI: emit note at exact in-block position
                    if (wantMidi && slotAudible)
                    {
                        const int noteNumber = 60; // Middle C for all slots
                        const int velocity = juce::jlimit(1, 127, (int)std::round(gain * 127.0f));

                        const int onPos = hitOffset;
                        const int offPos = juce::jmin(numSamples - 1, hitOffset + (int)std::round(0.010 * currentSampleRate)); // ~10ms

                        midi.addEvent(juce::MidiMessage::noteOn(midiChannel, noteNumber, (juce::uint8)velocity), onPos);
                        midi.addEvent(juce::MidiMessage::noteOff(midiChannel, noteNumber), offPos);
                    }

                    // Audio: mix hit tail from the hit point forward (Audio/Both only)
                    if (wantAudio)
                    {
                        const float mixGain = slotAudible ? gain : 0.0f;
                        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(),
                            buffer.getNumChannels(),
                            hitOffset,
                            numSamples - hitOffset);
                        s.mixInto(view, view.getNumSamples(), mixGain);
                    }
                }
            }
        }
        else
        {
            // --- BeatsPerCycle mode ---
            const double stepBeats = (count > 0 ? countModeCycleBeats / (double)count : 0.0);
            const double denomBeats = currBeats - prevBeats;
            if (stepBeats <= 0.0 || denomBeats <= 0.0)
            {
                s.wasAudibleLastBlock = slotAudible;
                continue;
            }

            const uint64_t activeMask = getSlotCountMask(i) & maskForBeats(count);
            if (activeMask == 0)
            {
                s.wasAudibleLastBlock = slotAudible;
                continue;
            }

            const int firstIndex = (int)std::ceil(prevBeats / stepBeats);
            for (int n = firstIndex;; ++n)
            {
                const double hitBeat = (double)n * stepBeats;
                if (hitBeat >= currBeats)
                    break;

                const int beatIndex = (count > 0) ? (n % count) : 0;
                if (((activeMask >> beatIndex) & 1ull) == 0)
                    continue;

                double fracBlock = (hitBeat - prevBeats) / denomBeats;
                fracBlock = juce::jlimit(0.0, 1.0, fracBlock);

                const int hitOffset = juce::jlimit(0, numSamples - 1,
                    (int)std::floor(fracBlock * (double)numSamples + 0.5));

                if (hitOffset < suppressedHead)
                    continue;

                s.trigger();

                if (wantMidi && slotAudible)
                {
                    const int noteNumber = 60; // Middle C for all slots
                    const int velocity = juce::jlimit(1, 127, (int)std::round(gain * 127.0f));

                    const int onPos = hitOffset;
                    const int offPos = juce::jmin(numSamples - 1, hitOffset + (int)std::round(0.010 * currentSampleRate));

                    midi.addEvent(juce::MidiMessage::noteOn(midiChannel, noteNumber, (juce::uint8)velocity), onPos);
                    midi.addEvent(juce::MidiMessage::noteOff(midiChannel, noteNumber), offPos);
                }

                if (wantAudio)
                {
                    const float mixGain = slotAudible ? gain : 0.0f;
                    juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(),
                        buffer.getNumChannels(),
                        hitOffset,
                        numSamples - hitOffset);
                    s.mixInto(view, view.getNumSamples(), mixGain);
                }
            }
        }

        s.wasAudibleLastBlock = slotAudible;
    }

    if (transportRunning && samplesPerBeat > 0.0)
    {
        samplesUntilNextDownbeat -= numSamples;
        while (samplesUntilNextDownbeat <= 0.0)
            samplesUntilNextDownbeat += samplesPerBeat;
    }
    else
    {
        samplesUntilNextDownbeat = 0.0;
    }

    if (suppressHitsForSamples > 0)
        suppressHitsForSamples = juce::jmax(0, suppressHitsForSamples - suppressedHead);

    if (wantAudio)
    {
        juce::SpinLock::ScopedTryLockType guard(previewLock);
        if (guard.isLocked())
            previewVoice.mixInto(buffer, numSamples);
    }

    if (wantAudio && numSamples > 0)
    {
        if (scratchMono.getNumSamples() < numSamples)
            scratchMono.setSize(1, numSamples, false, false, true);

        scratchMono.clear(0, 0, numSamples);

        auto* mono = scratchMono.getWritePointer(0);
        const float* left  = buffer.getReadPointer(0);
        const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;

        if (right != nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
                mono[i] = 0.5f * (left[i] + right[i]);
        }
        else if (left != nullptr)
        {
            juce::FloatVectorOperations::copy(mono, left, numSamples);
        }

        int remaining = numSamples;
        int offset = 0;
        while (remaining > 0)
        {
            const int chunk = juce::jmin(remaining, kScopeBlockSize);
            scopeQueue.push(mono + offset, chunk);
            offset += chunk;
            remaining -= chunk;
        }
    }
}

void SlotMachineAudioProcessor::scheduleTabSwitchOnNextDownbeat(int newTabIndex)
{
    pendingTabSwitchIndex.store(newTabIndex, std::memory_order_release);
}

void SlotMachineAudioProcessor::applyTabSwitchAtBlockStart_NoResetTails(int newTabIndex)
{
    juce::ignoreUnused(newTabIndex);
    // Pattern state is swapped on the message thread; the audio thread simply
    // honours the debounce and keeps any ringing tails alive.
}

//==============================================================================
// Editor
juce::AudioProcessorEditor* SlotMachineAudioProcessor::createEditor()
{
    return new SlotMachineAudioProcessorEditor(*this, apvts);
}

void SlotMachineAudioProcessor::initialiseStateForFirstEditor()
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());

    clearAllSlots();
    resetAllPhases(false);

    for (int slotIndex = 0; slotIndex < kNumSlots; ++slotIndex)
    {
        const int countValue = getSlotCountValue(apvts, slotIndex);
        const uint64_t fullMask = maskForBeats(countValue);
        setSlotCountMask(slotIndex, fullMask);
    }

    apvts.state.setProperty(kAutoInitialiseProperty, false, nullptr);
}

bool SlotMachineAudioProcessor::consumeInitialiseOnFirstEditor()
{
    const bool shouldInitialise = initialiseOnFirstEditor;
    initialiseOnFirstEditor = false;
    return shouldInitialise;
}

//==============================================================================
// State
void SlotMachineAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Mirror file paths into state
    for (int i = 0; i < kNumSlots; ++i)
    {
        juce::Identifier prop("slot" + juce::String(i + 1) + "_File");
        if (slots[i].getFilePath().isNotEmpty())
            apvts.state.setProperty(prop, slots[i].getFilePath(), nullptr);
        else
            apvts.state.removeProperty(prop, nullptr);
    }

    apvts.state.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);

    auto patterns = getPatternsTree();
    const int patternCount = patterns.getNumChildren();
    if (patternCount > 0)
    {
        const int clampedIndex = juce::jlimit(0, patternCount - 1, getCurrentPatternIndex());
        patterns.setProperty(kCurrentPatternIndexProperty, clampedIndex, nullptr);
        storeCurrentStateInPattern(patterns.getChild(clampedIndex));
    }

    auto stateCopy = copyStateWithVersion();
    if (auto xml = stateCopy.createXml())
        copyXmlToBinary(*xml, destData);
}

void SlotMachineAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        upgradeLegacySlotParameters();
        refreshSlotCountMasksFromState();

        // Each new session should start from a blank state regardless of what was stored in the
        // host project.  Force the first editor to reinitialise the processor to its defaults and
        // scrub any persisted slot/tab data from the restored ValueTree.
        apvts.state.setProperty(kAutoInitialiseProperty, true, nullptr);
        initialiseOnFirstEditor = true;

        for (int i = 0; i < kNumSlots; ++i)
        {
            clearSlot(i);

            const auto base = juce::String("slot") + juce::String(i + 1) + "_";
            for (auto& suffix : kSlotParamSuffixes)
                if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(base + suffix)))
                    param->setValueNotifyingHost(param->getDefaultValue());
        }

        if (auto patterns = apvts.state.getChildWithName(kPatternsNodeId); patterns.isValid())
            apvts.state.removeChild(patterns, nullptr);
    }
}

//==============================================================================
// Helpers for editor
void SlotMachineAudioProcessor::resetAllPhases(bool immediate)
{
    for (auto& s : slots)
        s.resetPhase(immediate);
    if (immediate)
    {
        masterBeatsAccum = 0.0;
        currentCyclePhase01 = 0.0;
        samplesUntilNextDownbeat = 0.0;
        blockStartIsDownbeat = true;
        suppressHitsForSamples = 0;
        pendingTabSwitchIndex.store(-1, std::memory_order_relaxed);
    }
}

bool SlotMachineAudioProcessor::slotHasSample(int index) const
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    return slots[(size_t)index].hasSample();
}

juce::String SlotMachineAudioProcessor::getSlotFilePath(int index) const
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    return slots[(size_t)index].getFilePath();
}

void SlotMachineAudioProcessor::setSlotFilePath(int index, const juce::String& path)
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    slots[(size_t)index].setFilePath(path);
    apvts.state.setProperty("slot" + juce::String(index + 1) + "_File", path, nullptr);
}

void SlotMachineAudioProcessor::refreshSlotCountMasksFromState()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        const juce::String propertyId = slotParamId(i, "CountMask");
        const juce::var storedValue = apvts.state.getProperty(propertyId);
        uint64_t mask = parseCountMaskVar(storedValue);

        const int countValue = getSlotCountValue(apvts, i);
        if (countValue > 0)
            mask &= maskForBeats(countValue);
        else
            mask = 0ull;

        countBeatMasks[(size_t)i].store(mask, std::memory_order_relaxed);

        const juce::String serialised = serialiseCountMaskValue(mask);
        if (storedValue.toString() != serialised)
            apvts.state.setProperty(propertyId, serialised, nullptr);
    }
}

bool SlotMachineAudioProcessor::loadSampleForSlot(int index, const juce::File& f, bool allowTail)
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    slots[(size_t)index].clear(allowTail);

    slots[(size_t)index].loadFile(f);

    if (slots[(size_t)index].hasSample())
    {
        apvts.state.setProperty("slot" + juce::String(index + 1) + "_File", f.getFullPathName(), nullptr);
        return true;
    }

    apvts.state.removeProperty("slot" + juce::String(index + 1) + "_File", nullptr);
    return false;
}

bool SlotMachineAudioProcessor::loadSampleForSlotFromMemory(int index, const void* data, int sizeBytes, const juce::String& pseudoName)
{
    if (!juce::isPositiveAndBelow(index, kNumSlots) || data == nullptr || sizeBytes <= 0)
        return false;

    auto& slot = slots[(size_t)index];
    const bool allowTail = apvts.getRawParameterValue("masterRun")->load();
    slot.clear(allowTail);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto reader = makeReaderFromMemory(fm, data, sizeBytes);

    if (reader == nullptr)
    {
        slot.setFilePath({});
        apvts.state.removeProperty("slot" + juce::String(index + 1) + "_File", nullptr);
        return false;
    }

    juce::AudioBuffer<float> decoded;
    if (!decodeReaderToStereoBuffer(*reader, slot.sampleRate, decoded))
    {
        slot.setFilePath({});
        apvts.state.removeProperty("slot" + juce::String(index + 1) + "_File", nullptr);
        return false;
    }

    slot.sample = std::move(decoded);
    slot.active = (slot.sample.getNumSamples() > 0);
    slot.filePath = pseudoName;
    slot.playIndex = -1;
    slot.playLength = slot.sample.getNumSamples();
    slot.env = 0.0f;
    slot.envSamplesElapsed = 0;
    slot.envMaxSamples = 0;

    apvts.state.removeProperty("slot" + juce::String(index + 1) + "_File", nullptr);

    return slot.hasSample();
}

void SlotMachineAudioProcessor::previewEmbeddedWav(const void* data, int sizeBytes)
{
    if (data == nullptr || sizeBytes <= 0)
        return;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto reader = makeReaderFromMemory(fm, data, sizeBytes);
    if (reader == nullptr)
        return;

    juce::AudioBuffer<float> decoded;
    if (!decodeReaderToStereoBuffer(*reader, currentSampleRate, decoded))
        return;

    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewVoice.start(std::move(decoded));
}

juce::ValueTree SlotMachineAudioProcessor::copyStateWithVersion()
{
    auto stateCopy = apvts.copyState();
    stateCopy.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);
    return stateCopy;
}

void SlotMachineAudioProcessor::upgradeLegacySlotParameters()
{
    const int loadedVersion = (int)apvts.state.getProperty(kStateVersionProperty, 0);
    const bool loadedLegacyVersion = loadedVersion < kCurrentStateVersion;

    bool legacyGainsDetected = false;

    auto deriveCountFromRate = [](float rateValue, int minCount, int maxCount)
    {
        if (!std::isfinite(rateValue))
            rateValue = 1.0f;

        const int candidate = juce::roundToInt(rateValue * 4.0f);
        const int clampedMin = juce::jmax(minCount, 1);
        return juce::jlimit(clampedMin, juce::jmax(clampedMin, maxCount), candidate);
    };

    for (int i = 0; i < kNumSlots; ++i)
    {
        const juce::String gainId = "slot" + juce::String(i + 1) + "_Gain";
        if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(gainId)))
        {
            const auto valueVar = apvts.state.getProperty(gainId);
            float stored = 0.0f;
            if (varToFloat(valueVar, stored))
            {
                const auto& range = param->range;
                if (stored <= 1.0f)
                {
                    legacyGainsDetected = true;
                    const float upgraded = juce::jlimit(range.start, range.end, stored * 100.0f);
                    param->setValueNotifyingHost(range.convertTo0to1(upgraded));
                    apvts.state.setProperty(gainId, upgraded, nullptr);
                }
                else if (loadedLegacyVersion)
                {
                    apvts.state.setProperty(gainId, juce::jlimit(range.start, range.end, stored), nullptr);
                }
            }
        }

        const juce::String decayId = "slot" + juce::String(i + 1) + "_Decay";
        if (auto* decayParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(decayId)))
        {
            const auto valueVar = apvts.state.getProperty(decayId);
            float legacyMs = 0.0f;
            if (varToFloat(valueVar, legacyMs))
            {
                const bool shouldUpgradeDecay = legacyGainsDetected || loadedLegacyVersion || legacyMs > kDecayUiMax;
                if (shouldUpgradeDecay)
                {
                    const float upgraded = legacyDecayMsToUi(legacyMs);
                    decayParam->setValueNotifyingHost(decayParam->range.convertTo0to1(upgraded));
                    apvts.state.setProperty(decayId, upgraded, nullptr);
                }
                else if (loadedLegacyVersion)
                {
                    const auto& range = decayParam->range;
                    apvts.state.setProperty(decayId, juce::jlimit(range.start, range.end, legacyMs), nullptr);
                }
            }
        }

        const juce::String rateId = "slot" + juce::String(i + 1) + "_Rate";
        const juce::String countId = "slot" + juce::String(i + 1) + "_Count";

        if (auto* countParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(countId)))
        {
            const auto& countRange = countParam->getNormalisableRange();
            const int minCount = (int)std::round(countRange.start);
            const int maxCount = (int)std::round(countRange.end);

            const bool hasCountProperty = apvts.state.hasProperty(countId);
            if (!hasCountProperty)
            {
                float rateValue = 1.0f;

                if (auto* rateParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(rateId)))
                    rateValue = rateParam->get();
                else
                {
                    const auto rateVar = apvts.state.getProperty(rateId);
                    varToFloat(rateVar, rateValue);
                }

                const int derivedCount = deriveCountFromRate(rateValue, minCount, maxCount);

                apvts.state.setProperty(countId, derivedCount, nullptr);

                const float normalised = countParam->convertTo0to1((float)derivedCount);
                countParam->beginChangeGesture();
                countParam->setValueNotifyingHost(normalised);
                countParam->endChangeGesture();
            }
        }

        const juce::String maskId = slotParamId(i, "CountMask");
        const uint64_t maskValue = parseCountMaskVar(apvts.state.getProperty(maskId));
        apvts.state.setProperty(maskId, serialiseCountMaskValue(maskValue), nullptr);
    }

    if (auto patterns = apvts.state.getChildWithName(kPatternsNodeId); patterns.isValid())
    {
        const int numPatterns = patterns.getNumChildren();
        for (int p = 0; p < numPatterns; ++p)
        {
            auto pattern = patterns.getChild(p);
            for (int slot = 0; slot < kNumSlots; ++slot)
            {
                const juce::String rateId = slotParamId(slot, "Rate");
                const juce::String countId = slotParamId(slot, "Count");

                if (pattern.hasProperty(countId))
                    continue;

                float rateValue = 1.0f;
                const auto rateVar = pattern.getProperty(rateId);
                varToFloat(rateVar, rateValue);

                int minCount = 1;
                int maxCount = 64;
                if (auto* countParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(countId)))
                {
                    const auto& countRange = countParam->getNormalisableRange();
                    minCount = (int)std::round(countRange.start);
                    maxCount = (int)std::round(countRange.end);
                }

                const int derivedCount = deriveCountFromRate(rateValue, minCount, maxCount);
                pattern.setProperty(countId, derivedCount, nullptr);

                const juce::String maskId = slotParamId(slot, "CountMask");
                if (!pattern.hasProperty(maskId))
                    pattern.setProperty(maskId, serialiseCountMaskValue(kDefaultCountMask), nullptr);
            }
        }
    }

    apvts.state.setProperty(kStateVersionProperty, kCurrentStateVersion, nullptr);
    apvts.state.setProperty(kAutoInitialiseProperty, false, nullptr);

    refreshSlotCountMasksFromState();
}

void SlotMachineAudioProcessor::clearSlot(int index, bool allowTail)
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    slots[(size_t)index].clear(allowTail);
    apvts.state.removeProperty("slot" + juce::String(index + 1) + "_File", nullptr);
}

void SlotMachineAudioProcessor::clearAllSlots()
{
    for (int i = 0; i < kNumSlots; ++i)
        clearSlot(i);
}

void SlotMachineAudioProcessor::resetSlotParametersToDefault(int index)
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));

    const juce::String base = "slot" + juce::String(index + 1) + "_";

    // Reset Mute to false
    if (auto* param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(base + "Mute")))
        *param = false;

    // Reset Solo to false
    if (auto* param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(base + "Solo")))
        *param = false;

    // Reset Rate to 1.0
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(base + "Rate")))
        *param = 1.0f;

    // Reset Count to 4
    if (auto* param = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(base + "Count")))
        *param = 4;

    // Reset Gain to 80.0
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(base + "Gain")))
        *param = 80.0f;

    // Reset Pan to 0.0
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(base + "Pan")))
        *param = 0.0f;

    // Reset Decay to max (100.0)
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(base + "Decay")))
        *param = kDecayUiMax;

    // Reset MidiChannel to slot index (slot 1 = 0, slot 2 = 1, etc.)
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(base + "MidiChannel")))
        *param = index;

    // Reset CountMask to all beats enabled
    setSlotCountMask(index, kDefaultCountMask);
}

uint32_t SlotMachineAudioProcessor::getSlotHitCounter(int index) const
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    return slots[(size_t)index].hitCounter;
}

uint64_t SlotMachineAudioProcessor::getSlotCountMask(int index) const
{
    if (!juce::isPositiveAndBelow(index, kNumSlots))
        return kDefaultCountMask;

    return countBeatMasks[(size_t)index].load(std::memory_order_relaxed);
}

void SlotMachineAudioProcessor::setSlotCountMask(int index, uint64_t mask)
{
    if (!juce::isPositiveAndBelow(index, kNumSlots))
        return;

    countBeatMasks[(size_t)index].store(mask, std::memory_order_relaxed);

    const juce::String propertyId = slotParamId(index, "CountMask");
    const juce::String serialised = serialiseCountMaskValue(mask);
    if (apvts.state.getProperty(propertyId).toString() != serialised)
        apvts.state.setProperty(propertyId, serialised, nullptr);
}

uint64_t SlotMachineAudioProcessor::maskForBeats(int beats)
{
    if (beats <= 0)
        return 0ull;

    if (beats >= 64)
        return std::numeric_limits<uint64_t>::max();

    return (1ull << beats) - 1ull;
}

int SlotMachineAudioProcessor::getSlotCurrentBeatIndex(int index) const
{
    if (!juce::isPositiveAndBelow(index, kNumSlots))
        return -1;

    return currentBeatIndices[(size_t)index].load(std::memory_order_relaxed);
}

double SlotMachineAudioProcessor::getSlotPhase(int index) const
{
    jassert(juce::isPositiveAndBelow(index, kNumSlots));
    return slots[(size_t)index].phase;
}

double SlotMachineAudioProcessor::getMasterPhase() const
{
    // Averages slot phases that have samples; returns 0 if none
    //double sum = 0.0;
    //int count = 0;
    //for (int i = 0; i < kNumSlots; ++i)
    //{
    //    if (slots[i].hasSample())
    //    {
    //        sum += slots[i].phase;
    //        ++count;
    //    }
    //}
    //return count > 0 ? sum / count : 0.0;
    return currentCyclePhase01; // 0..1 over the full polyrhythmic cycle
}

bool SlotMachineAudioProcessor::exportAudioCycles(const juce::File& destination, int cyclesToExport, juce::String& errorMessage)
{
    errorMessage.clear();

    const double engineSampleRate = currentSampleRate;
    if (engineSampleRate <= 0.0)
    {
        errorMessage = "Audio engine is not initialised.";
        return false;
    }

    double targetSampleRate = engineSampleRate;
    if (auto* sampleRateParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("optSampleRate")))
    {
        const int requested = sampleRateParam->get();

        if (requested == 44100 || requested == 48000)
            targetSampleRate = static_cast<double>(requested);
    }

    OfflinePatternData patternData = createOfflinePatternDataFromCurrentState(*this);
    RenderedPatternAudio rendered;
    if (!::renderPatternAudio(*this, patternData, cyclesToExport, engineSampleRate, rendered, errorMessage))
        return false;

    return writeAudioFile(destination, rendered.buffer, rendered.samples, engineSampleRate, targetSampleRate, errorMessage);
}

bool SlotMachineAudioProcessor::exportAudioPlaythroughCycles(const juce::File& destination, int playthroughCycles, juce::String& errorMessage)
{
    errorMessage.clear();

    const double engineSampleRate = currentSampleRate;
    if (engineSampleRate <= 0.0)
    {
        errorMessage = "Audio engine is not initialised.";
        return false;
    }

    if (playthroughCycles <= 0)
    {
        errorMessage = "Number of cycles must be positive.";
        return false;
    }

    double targetSampleRate = engineSampleRate;
    if (auto* sampleRateParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("optSampleRate")))
    {
        const int requested = sampleRateParam->get();

        if (requested == 44100 || requested == 48000)
            targetSampleRate = static_cast<double>(requested);
    }

    auto patterns = getPatternsTree();
    const int patternCount = patterns.getNumChildren();
    if (patternCount <= 0)
    {
        errorMessage = "No patterns to export.";
        return false;
    }

    std::vector<RenderedPatternAudio> renderedPatterns;
    renderedPatterns.reserve((size_t)patternCount);

    int64_t beatAlignedSamplesPerPlaythrough = 0;
    int maxTailSamples = 0;

    for (int i = 0; i < patternCount; ++i)
    {
        auto pattern = patterns.getChild(i);
        OfflinePatternData data = createOfflinePatternDataFromValueTree(*this, pattern);
        const int patternCycles = computePatternPlaythroughCycles(pattern);

        RenderedPatternAudio rendered;
        if (!::renderPatternAudio(*this, data, patternCycles, engineSampleRate, rendered, errorMessage))
            return false;

        beatAlignedSamplesPerPlaythrough += rendered.beatAlignedSamples;
        const int tailSamples = rendered.samples - rendered.beatAlignedSamples;
        maxTailSamples = juce::jmax(maxTailSamples, tailSamples);

        renderedPatterns.emplace_back();
        auto& stored = renderedPatterns.back();
        stored.samples = rendered.samples;
        stored.beatAlignedSamples = rendered.beatAlignedSamples;
        stored.buffer.makeCopyOf(rendered.buffer);
    }

    if (beatAlignedSamplesPerPlaythrough <= 0)
    {
        errorMessage = "Export length is zero.";
        return false;
    }

    // Calculate total: beat-aligned duration for all playthroughs, plus tail space for the final pattern
    const int64_t totalSamplesInt64 = beatAlignedSamplesPerPlaythrough * (int64_t)playthroughCycles + (int64_t)maxTailSamples;
    if (totalSamplesInt64 <= 0 || totalSamplesInt64 > std::numeric_limits<int>::max())
    {
        errorMessage = "Export length is too large.";
        return false;
    }

    const int totalSamples = (int)totalSamplesInt64;
    const int numChannels = 2;

    juce::AudioBuffer<float> combinedBuffer(numChannels, totalSamples);
    combinedBuffer.clear();

    int writePosition = 0;
    int lastPatternEnd = 0;  // Track where the previous pattern's buffer ends (including tail)

    for (int cycle = 0; cycle < playthroughCycles; ++cycle)
    {
        for (const auto& rendered : renderedPatterns)
        {
            if (rendered.beatAlignedSamples <= 0)
                continue;

            const int remaining = combinedBuffer.getNumSamples() - writePosition;
            if (remaining <= 0)
                break;

            const int samplesToCopy = juce::jmin(rendered.samples, remaining);

            // Check if this pattern overlaps with the previous pattern's tail
            if (lastPatternEnd > writePosition)
            {
                // There's an overlap - we need to mix the overlapping region
                const int overlapSamples = juce::jmin(lastPatternEnd - writePosition, samplesToCopy);
                const int nonOverlapSamples = samplesToCopy - overlapSamples;

                for (int channel = 0; channel < numChannels; ++channel)
                {
                    // Mix overlapping region where previous pattern's tail extends
                    combinedBuffer.addFrom(channel, writePosition, rendered.buffer, channel, 0, overlapSamples);

                    // Copy non-overlapping region
                    if (nonOverlapSamples > 0)
                        combinedBuffer.copyFrom(channel, writePosition + overlapSamples, rendered.buffer, channel, overlapSamples, nonOverlapSamples);
                }
            }
            else
            {
                // No overlap - just copy everything
                for (int channel = 0; channel < numChannels; ++channel)
                    combinedBuffer.copyFrom(channel, writePosition, rendered.buffer, channel, 0, samplesToCopy);
            }

            lastPatternEnd = writePosition + samplesToCopy;
            writePosition += rendered.beatAlignedSamples;
        }
    }

    return writeAudioFile(destination, combinedBuffer, totalSamples, engineSampleRate, targetSampleRate, errorMessage);
}

bool SlotMachineAudioProcessor::exportMidiCycles(const juce::File& destination, int cyclesToExport, bool useFixedNoteLength, juce::String& errorMessage)
{
    errorMessage.clear();

    const int ppq = 960;

    OfflinePatternData patternData = createOfflinePatternDataFromCurrentState(*this);
    const double bpm = patternData.bpm;

    RenderedPatternMidi rendered;
    if (!::renderPatternMidi(*this, patternData, cyclesToExport, bpm, ppq, useFixedNoteLength, rendered, errorMessage))
        return false;

    if (rendered.totalTicks <= 0 || rendered.sequence.getNumEvents() == 0)
    {
        errorMessage = "Export length is zero.";
        return false;
    }

    rendered.sequence.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(ppq);
    mf.addTrack(rendered.sequence);

    // Delete existing file if it exists (to allow overwriting)
    if (destination.existsAsFile())
    {
        if (!destination.deleteFile())
        {
            errorMessage = "Couldn't overwrite existing file:\n" + destination.getFullPathName();
            return false;
        }
    }

    juce::FileOutputStream os(destination);
    if (!os.openedOk())
    {
        errorMessage = "Couldn't open file for writing:\n" + destination.getFullPathName();
        return false;
    }

    if (!mf.writeTo(os))
    {
        errorMessage = "Failed to write MIDI data.";
        return false;
    }

    return true;
}

bool SlotMachineAudioProcessor::exportMidiPlaythroughCycles(const juce::File& destination, int playthroughCycles, bool useFixedNoteLength, juce::String& errorMessage)
{
    errorMessage.clear();

    if (playthroughCycles <= 0)
    {
        errorMessage = "Number of cycles must be positive.";
        return false;
    }

    const int ppq = 960;
    const double engineSampleRate = currentSampleRate;
    if (engineSampleRate <= 0.0)
    {
        errorMessage = "Audio engine is not initialised.";
        return false;
    }

    auto patterns = getPatternsTree();
    const int patternCount = patterns.getNumChildren();
    if (patternCount <= 0)
    {
        errorMessage = "No patterns to export.";
        return false;
    }

    // Structure to hold prepared pattern data
    struct PreparedPattern
    {
        OfflinePatternData data;
        int cycles;
        double cycleBeats;
        double bpm;
    };

    std::vector<PreparedPattern> preparedPatterns;
    preparedPatterns.reserve((size_t)patternCount);

    // Load and prepare all pattern data
    for (int i = 0; i < patternCount; ++i)
    {
        auto pattern = patterns.getChild(i);
        OfflinePatternData data = createOfflinePatternDataFromValueTree(*this, pattern);
        const int patternCycles = computePatternPlaythroughCycles(pattern);

        if (patternCycles <= 0)
            continue;

        PreparedPattern prep;
        prep.data = data;
        prep.cycles = patternCycles;
        prep.bpm = data.bpm;
        prep.cycleBeats = 0.0;  // Will calculate below

        preparedPatterns.push_back(prep);
    }

    if (preparedPatterns.empty())
    {
        errorMessage = "No patterns to export.";
        return false;
    }

    // Build unified MIDI timeline
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 2), 0.0);

    double globalBeatPosition = 0.0;  // Track position in beats
    double currentBPM = -1.0;         // Track current BPM for tempo changes
    const int noteNumber = 60;
    int64_t maxNoteOffTick = 0;       // Track the last note-off event for end-of-track positioning

    // Process each playthrough cycle
    for (int cycle = 0; cycle < playthroughCycles; ++cycle)
    {
        // Process each pattern
        for (auto& prep : preparedPatterns)
        {
            const auto& patternData = prep.data;
            const double patternBpm = prep.bpm;

            // Add tempo change if BPM changed
            if (currentBPM != patternBpm)
            {
                const int64_t tempoTick = (int64_t)std::llround(globalBeatPosition * (double)ppq);
                const int microsecondsPerQuarterNote = (int)std::round(60000000.0 / patternBpm);
                sequence.addEvent(juce::MidiMessage::tempoMetaEvent(microsecondsPerQuarterNote), (double)tempoTick);
                currentBPM = patternBpm;
            }

            const int timingMode = patternData.timingMode;
            const double secondsPerBeat = 60.0 / patternBpm;
            const double countModeCycleBeats = (double)kCountModeBaseBeats;

            // Check for solo slots
            bool anySolo = false;
            for (const auto& slot : patternData.slots)
                anySolo = anySolo || slot.solo;

            // Calculate cycle length for this pattern
            const int maxDen = 32;
            int cycleLengthNumerator = 1;
            int cycleLengthDenominator = 1;
            bool hasCycleLength = false;

            // Structure for slot data
            struct SlotInfo
            {
                int num = 0;
                int den = 1;
                int count = 0;
                uint64_t mask = kDefaultCountMask;
                int midiChannel = 1;
                float gain = 1.0f;
                double noteLengthBeats = 0.25;
            };

            std::vector<SlotInfo> activeSlots;
            activeSlots.reserve(kNumSlots);

            // Prepare active slots
            for (int slotIndex = 0; slotIndex < kNumSlots; ++slotIndex)
            {
                const auto& slotData = patternData.slots[(size_t)slotIndex];

                if (slotData.mute)
                    continue;

                if (anySolo && !slotData.solo)
                    continue;

                if (slotData.filePath.isEmpty())
                    continue;

                // Load sample to calculate note length
                auto voice = std::make_unique<SlotVoice>();
                voice->prepare(engineSampleRate);

                bool loaded = false;

#if __has_include("BinaryData.h")
                {
                    int resourceSize = 0;
                    if (const void* data = BinaryData::getNamedResource(slotData.filePath.toRawUTF8(), resourceSize))
                    {
                        if (resourceSize > 0)
                        {
                            voice->loadFromMemory(data, resourceSize, slotData.filePath);
                            loaded = voice->hasSample();
                        }
                    }
                }
#endif

                if (!loaded)
                {
                    const juce::File audioFile(slotData.filePath);
                    if (audioFile.existsAsFile())
                    {
                        voice->loadFile(audioFile);
                        loaded = voice->hasSample();
                    }
                }

                if (!loaded)
                    continue;

                voice->setDecayMs(decayUiToMilliseconds(slotData.decayUi));

                SlotInfo info;
                info.gain = juce::jlimit(0.0f, 1.0f, slotData.gainPercent * 0.01f);
                info.mask = slotData.countMask;
                info.midiChannel = juce::jlimit(1, 16, slotData.midiChannel);

                // Calculate note length
                double noteLengthBeats;
                if (useFixedNoteLength)
                {
                    // Fixed 32nd note length: 1/8 of a quarter note = 0.125 beats
                    noteLengthBeats = 0.125;
                }
                else
                {
                    // Match sample length (original behavior)
                    int noteSamples = voice->sample.getNumSamples();
                    if (voice->envMaxSamples > 0)
                        noteSamples = juce::jmin(noteSamples, voice->envMaxSamples);
                    noteSamples = juce::jmax(1, noteSamples);

                    const double noteSeconds = (double)noteSamples / engineSampleRate;
                    noteLengthBeats = noteSeconds / secondsPerBeat;
                    if (noteLengthBeats <= 0.0)
                        noteLengthBeats = 1.0 / (double)ppq;
                }
                info.noteLengthBeats = noteLengthBeats;

                // Calculate timing parameters
                if (timingMode == 0)  // Polyrhythm mode
                {
                    const double rate = juce::jmax(0.0001f, slotData.rate);
                    int num = 0, den = 1;
                    approximateRational(rate, maxDen, num, den);
                    const int g = igcd(num, den);
                    if (g != 0)
                    {
                        num /= g;
                        den /= g;
                    }

                    if (num <= 0 || den <= 0)
                        continue;

                    accumulateCycleLength(den, num, cycleLengthNumerator, cycleLengthDenominator, hasCycleLength);

                    info.num = num;
                    info.den = den;
                }
                else  // Count mode
                {
                    info.count = juce::jlimit(1, 64, slotData.count);
                }

                activeSlots.push_back(info);
            }

            // Finalize cycle length (must be calculated before checking if slots are empty)
            double cycleBeats = 1.0;
            if (timingMode == 0)
            {
                if (!hasCycleLength)
                {
                    cycleLengthNumerator = 1;
                    cycleLengthDenominator = 1;
                }
                cycleBeats = juce::jlimit(1.0e-6, 512.0,
                    (double)cycleLengthNumerator / (double)cycleLengthDenominator);
            }
            else
            {
                cycleBeats = juce::jlimit(1.0e-6, 512.0, countModeCycleBeats);
            }

            prep.cycleBeats = cycleBeats;  // Store for later

            if (activeSlots.empty())
            {
                // No active slots, but still advance time to maintain proper pattern sequencing
                globalBeatPosition += cycleBeats * (double)prep.cycles;
                continue;
            }

            // Generate MIDI events for this pattern's cycles
            for (int patternCycle = 0; patternCycle < prep.cycles; ++patternCycle)
            {
                const double patternCycleBeatStart = globalBeatPosition + (double)patternCycle * cycleBeats;

                for (const auto& slotInfo : activeSlots)
                {
                    if (timingMode == 0)  // Polyrhythm mode
                    {
                        const int hitsPerCycle = juce::jmax(1, slotInfo.num);
                        const double spacingBeats = cycleBeats / (double)hitsPerCycle;

                        for (int hit = 0; hit < hitsPerCycle; ++hit)
                        {
                            const double triggerBeat = patternCycleBeatStart + spacingBeats * (double)hit;
                            const int64_t startTick = (int64_t)std::llround(triggerBeat * (double)ppq);
                            const int64_t noteTicks = juce::jmax(1LL, (int64_t)std::llround(slotInfo.noteLengthBeats * (double)ppq));
                            const int64_t offTick = startTick + noteTicks;

                            const int velocity = juce::jlimit(1, 127, (int)std::round(slotInfo.gain * 127.0f));

                            sequence.addEvent(juce::MidiMessage::noteOn(slotInfo.midiChannel, noteNumber, (juce::uint8)velocity), (double)startTick);
                            sequence.addEvent(juce::MidiMessage::noteOff(slotInfo.midiChannel, noteNumber), (double)offTick);
                            maxNoteOffTick = juce::jmax(maxNoteOffTick, offTick);
                        }
                    }
                    else  // Count mode
                    {
                        const int hitsPerCycle = juce::jmax(1, slotInfo.count);
                        const double stepBeats = countModeCycleBeats / (double)hitsPerCycle;
                        const uint64_t mask = slotInfo.mask & maskForBeats(slotInfo.count);

                        for (int hit = 0; hit < hitsPerCycle; ++hit)
                        {
                            if (((mask >> hit) & 1ull) == 0)
                                continue;

                            const double triggerBeat = patternCycleBeatStart + stepBeats * (double)hit;
                            const int64_t startTick = (int64_t)std::llround(triggerBeat * (double)ppq);
                            const int64_t noteTicks = juce::jmax(1LL, (int64_t)std::llround(slotInfo.noteLengthBeats * (double)ppq));
                            const int64_t offTick = startTick + noteTicks;

                            const int velocity = juce::jlimit(1, 127, (int)std::round(slotInfo.gain * 127.0f));

                            sequence.addEvent(juce::MidiMessage::noteOn(slotInfo.midiChannel, noteNumber, (juce::uint8)velocity), (double)startTick);
                            sequence.addEvent(juce::MidiMessage::noteOff(slotInfo.midiChannel, noteNumber), (double)offTick);
                            maxNoteOffTick = juce::jmax(maxNoteOffTick, offTick);
                        }
                    }
                }
            }

            // Advance global position by this pattern's total beat length
            globalBeatPosition += cycleBeats * (double)prep.cycles;
        }
    }

    // Finalize sequence
    // Place end-of-track after the last note-off to ensure all notes play fully
    const int64_t finalTick = juce::jmax(maxNoteOffTick, (int64_t)std::llround(globalBeatPosition * (double)ppq));
    sequence.sort();
    sequence.updateMatchedPairs();
    sequence.addEvent(juce::MidiMessage::endOfTrack(), (double)finalTick);

    // Write to file
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(ppq);
    mf.addTrack(sequence);

    if (destination.existsAsFile())
    {
        if (!destination.deleteFile())
        {
            errorMessage = "Couldn't overwrite existing file:\n" + destination.getFullPathName();
            return false;
        }
    }

    juce::FileOutputStream os(destination);
    if (!os.openedOk())
    {
        errorMessage = "Couldn't open file for writing:\n" + destination.getFullPathName();
        return false;
    }

    if (!mf.writeTo(os))
    {
        errorMessage = "Failed to write MIDI data.";
        return false;
    }

    return true;
}


//==============================================================================
// Pattern helpers
juce::ValueTree SlotMachineAudioProcessor::getPatternsTree()
{
    auto patterns = apvts.state.getChildWithName(kPatternsNodeId);
    if (!patterns.isValid())
    {
        patterns = juce::ValueTree(kPatternsNodeId);
        apvts.state.addChild(patterns, -1, nullptr);
    }

    if (!patterns.hasProperty(kCurrentPatternIndexProperty))
        patterns.setProperty(kCurrentPatternIndexProperty, 0, nullptr);

    if (patterns.getNumChildren() == 0)
    {
        auto initial = createPatternTreeFromCurrentState("A");
        patterns.addChild(initial, -1, nullptr);
    }

    return patterns;
}

juce::ValueTree SlotMachineAudioProcessor::createDefaultPatternTree(const juce::String& name) const
{
    juce::ValueTree pattern(kPatternNodeType);
    pattern.setProperty(kPatternNameProperty, name, nullptr);
    pattern.setProperty(kPatternRepeatProperty, 1, nullptr);

    if (auto* masterParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("masterBPM")))
    {
        const auto* ranged = static_cast<juce::RangedAudioParameter*>(masterParam);
        const float defaultValue = masterParam->convertFrom0to1(ranged->getDefaultValue());
        pattern.setProperty(kPatternMasterBpmProperty, defaultValue, nullptr);
    }

    if (auto* timingParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("optTimingMode")))
    {
        const int currentValue = timingParam->get();
        pattern.setProperty(kPatternTimingModeProperty, currentValue, nullptr);
    }

    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        for (auto& suffix : kSlotParamSuffixes)
        {
            const juce::String paramId = slotParamId(slot, suffix);
            if (auto* parameter = apvts.getParameter(paramId))
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(parameter))
                    {
                        const bool defaultValue = ranged->getDefaultValue() >= 0.5f;
                        pattern.setProperty(paramId, defaultValue, nullptr);
                    }
                    else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(parameter))
                    {
                        const float defaultNormalised = ranged->getDefaultValue();
                        const int defaultValue = (int)std::round(intParam->convertFrom0to1(defaultNormalised));
                        pattern.setProperty(paramId, defaultValue, nullptr);
                    }
                    else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(parameter))
                    {
                        const float defaultValue = floatParam->convertFrom0to1(ranged->getDefaultValue());
                        pattern.setProperty(paramId, defaultValue, nullptr);
                    }
                    else if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(parameter))
                    {
                        const int defaultIndex = juce::jlimit(0, choiceParam->choices.size() - 1,
                            (int)std::round(choiceParam->convertFrom0to1(ranged->getDefaultValue())));
                        pattern.setProperty(paramId, defaultIndex, nullptr);
                    }
                }
            }
        }

        const juce::String fileId = slotParamId(slot, "File");
        pattern.setProperty(fileId, juce::String(), nullptr);
    }

    return pattern;
}

juce::ValueTree SlotMachineAudioProcessor::createPatternTreeFromCurrentState(const juce::String& name) const
{
    juce::ValueTree pattern = createDefaultPatternTree(name);
    storeCurrentStateInPattern(pattern);
    return pattern;
}

void SlotMachineAudioProcessor::storeCurrentStateInPattern(juce::ValueTree pattern) const
{
    if (!pattern.isValid())
        return;

    if (auto* masterParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("masterBPM")))
        pattern.setProperty(kPatternMasterBpmProperty, masterParam->get(), nullptr);

    if (auto* timingParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("optTimingMode")))
        pattern.setProperty(kPatternTimingModeProperty, timingParam->get(), nullptr);

    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        for (auto& suffix : kSlotParamSuffixes)
        {
            const juce::String paramId = slotParamId(slot, suffix);

            if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId)))
            {
                pattern.setProperty(paramId, boolParam->get(), nullptr);
            }
            else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramId)))
            {
                pattern.setProperty(paramId, intParam->get(), nullptr);
            }
            else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
            {
                pattern.setProperty(paramId, floatParam->get(), nullptr);
            }
            else if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
            {
                pattern.setProperty(paramId, choiceParam->getIndex(), nullptr);
            }
        }

        const juce::String fileId = slotParamId(slot, "File");
        pattern.setProperty(fileId, slots[(size_t)slot].getFilePath(), nullptr);

        const juce::String maskId = slotParamId(slot, "CountMask");
        pattern.setProperty(maskId, apvts.state.getProperty(maskId), nullptr);
    }
}

void SlotMachineAudioProcessor::applyPatternTree(const juce::ValueTree& pattern, juce::Array<int>* failedSlots, bool allowTailRelease)
{
    if (failedSlots)
        failedSlots->clear();

    if (!pattern.isValid())
        return;

    if (auto* masterParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("masterBPM")))
    {
        const auto valueVar = pattern.getProperty(kPatternMasterBpmProperty);
        const float current = masterParam->get();
        float target = valueVar.isVoid() ? current : (float)valueVar;
        target = juce::jlimit(masterParam->range.start, masterParam->range.end, target);

        masterParam->beginChangeGesture();
        masterParam->setValueNotifyingHost(masterParam->convertTo0to1(target));
        masterParam->endChangeGesture();
    }

    if (auto* timingParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("optTimingMode")))
    {
        const auto valueVar = pattern.getProperty(kPatternTimingModeProperty);
        const int current = timingParam->get();
        int target = valueVar.isVoid() ? current : (int)valueVar;
        const auto& range = timingParam->getNormalisableRange();
        const int minValue = (int)std::round(range.start);
        const int maxValue = (int)std::round(range.end);
        target = juce::jlimit(minValue, maxValue, target);

        timingParam->beginChangeGesture();
        timingParam->setValueNotifyingHost(timingParam->convertTo0to1((float)target));
        timingParam->endChangeGesture();
    }

    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        for (auto& suffix : kSlotParamSuffixes)
        {
            const juce::String paramId = slotParamId(slot, suffix);
            const auto valueVar = pattern.getProperty(paramId);

            if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramId)))
            {
                const bool target = valueVar.isVoid() ? boolParam->get() : (bool)valueVar;
                boolParam->beginChangeGesture();
                *boolParam = target;
                boolParam->endChangeGesture();
            }
            else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramId)))
            {
                const int current = intParam->get();
                int target = valueVar.isVoid() ? current : (int)valueVar;
                const auto range = intParam->getNormalisableRange();
                const int minValue = (int)std::round(range.start);
                const int maxValue = (int)std::round(range.end);
                target = juce::jlimit(minValue, maxValue, target);
                intParam->beginChangeGesture();
                intParam->setValueNotifyingHost(intParam->convertTo0to1((float)target));
                intParam->endChangeGesture();
            }
            else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramId)))
            {
                const float current = floatParam->get();
                const float target = valueVar.isVoid() ? current : (float)valueVar;
                floatParam->beginChangeGesture();
                floatParam->setValueNotifyingHost(floatParam->convertTo0to1(target));
                floatParam->endChangeGesture();
            }
            else if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
            {
                const int current = choiceParam->getIndex();
                int target = valueVar.isVoid() ? current : (int)valueVar;
                target = juce::jlimit(0, choiceParam->choices.size() - 1, target);
                choiceParam->beginChangeGesture();
                choiceParam->setValueNotifyingHost(choiceParam->convertTo0to1(target));
                choiceParam->endChangeGesture();
            }
        }

        const juce::String fileId = slotParamId(slot, "File");
        const juce::String path = pattern.getProperty(fileId).toString();

        const juce::String maskId = slotParamId(slot, "CountMask");
        const uint64_t maskValue = parseCountMaskVar(pattern.getProperty(maskId));
        setSlotCountMask(slot, maskValue);

        if (path.isNotEmpty())
        {
            bool loadedEmbedded = false;

#if __has_include("BinaryData.h")
            {
                int resourceSize = 0;
                if (const void* data = BinaryData::getNamedResource(path.toRawUTF8(), resourceSize))
                {
                    if (resourceSize > 0 && loadSampleForSlotFromMemory(slot, data, resourceSize, path))
                    {
                        loadedEmbedded = true;
                    }
                    else
                    {
                        clearSlot(slot, allowTailRelease);
                        if (failedSlots)
                            failedSlots->addIfNotAlreadyThere(slot);
                        continue;
                    }
                }
            }
#endif

            if (!loadedEmbedded)
            {
                const juce::File file(path);
                if (!loadSampleForSlot(slot, file, allowTailRelease))
                {
                    clearSlot(slot, allowTailRelease);
                    setSlotFilePath(slot, path);
                    if (failedSlots)
                        failedSlots->addIfNotAlreadyThere(slot);
                }
            }
        }
        else
        {
            clearSlot(slot, allowTailRelease);
        }
    }
}

void SlotMachineAudioProcessor::setCurrentPatternIndex(int index)
{
    auto patterns = getPatternsTree();
    patterns.setProperty(kCurrentPatternIndexProperty, juce::jmax(0, index), nullptr);
}

int SlotMachineAudioProcessor::getCurrentPatternIndex() const
{
    auto patterns = apvts.state.getChildWithName(kPatternsNodeId);
    if (!patterns.isValid())
        return 0;

    return (int)patterns.getProperty(kCurrentPatternIndexProperty, 0);
}

//==============================================================================
// Factory
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SlotMachineAudioProcessor();
}

void SlotMachineAudioProcessor::SlotVoice::setDecayMs(float ms)
{
    if (ms <= 0.0f || sampleRate <= 0.0)
    { envAlpha = 1.0f; envMaxSamples = 0; return; }
    const double samples = (ms / 1000.0) * sampleRate;
    envMaxSamples = (int) std::round(samples);
    envAlpha = (float) std::pow(0.001, 1.0 / juce::jmax(1.0, samples));
}

void SlotMachineAudioProcessor::PreviewVoice::reset() noexcept
{
    playIndex = -1;
    playLength = 0;
    env = 0.0f;
    envAlpha = 1.0f;
    envSamplesElapsed = 0;
    envMaxSamples = 0;
    sample.setSize(0, 0);
}

void SlotMachineAudioProcessor::PreviewVoice::start(juce::AudioBuffer<float> newSample) noexcept
{
    sample = std::move(newSample);
    playLength = sample.getNumSamples();
    playIndex = (playLength > 0) ? 0 : -1;
    env = 1.0f;
    envSamplesElapsed = 0;
    envMaxSamples = playLength;

    if (envMaxSamples > 0)
        envAlpha = (float)std::pow(0.001, 1.0 / (double)envMaxSamples);
    else
        envAlpha = 1.0f;
}

void SlotMachineAudioProcessor::PreviewVoice::mixInto(juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (playIndex < 0 || playLength <= 0)
        return;

    const int remaining = playLength - playIndex;
    if (remaining <= 0)
    {
        reset();
        return;
    }

    const int toProcess = juce::jmin(numSamples, remaining);
    auto* dstL = buffer.getWritePointer(0);
    auto* dstR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    const float* srcL = sample.getReadPointer(0, playIndex);
    const float* srcR = (sample.getNumChannels() > 1) ? sample.getReadPointer(1, playIndex) : nullptr;

    if (srcL == nullptr)
    {
        reset();
        return;
    }

    for (int i = 0; i < toProcess; ++i)
    {
        const float currentEnv = env;
        if (dstL != nullptr)
            dstL[i] += srcL[i] * currentEnv;
        if (dstR != nullptr)
        {
            const float rightSample = (srcR != nullptr) ? srcR[i] : srcL[i];
            dstR[i] += rightSample * currentEnv;
        }

        env *= envAlpha;
        ++envSamplesElapsed;
    }

    playIndex += toProcess;

    const bool finished = playIndex >= playLength
        || (envMaxSamples > 0 && envSamplesElapsed >= envMaxSamples);
    if (finished)
        reset();
}

void SlotMachineAudioProcessor::requestManualTrigger(int index)
{
    if (index < 0 || index >= kNumSlots)
        return;

    pendingManualTriggers[(size_t)index].fetch_add(1, std::memory_order_relaxed);
}
