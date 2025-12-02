#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <vector>

#include "PluginProcessor.h"

class PolyrhythmVizComponent : public juce::Component, private juce::Timer
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    PolyrhythmVizComponent(SlotMachineAudioProcessor& processor, APVTS& state);
    ~PolyrhythmVizComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void()> onRightClick;

private:
    void timerCallback() override;
    void updateSlotGeometry(int slotIndex, juce::Point<float> centre, float radius);
    static void approximateRational(double value, int maxDenominator, int& num, int& den);

    SlotMachineAudioProcessor& processor;
    APVTS& apvts;

    struct SlotVisual
    {
        bool active = false;
        int sides = 0;
        double beadPhase = 0.0;
        double beadAngle = 0.0;
        juce::Point<float> centre{};
        float radius = 0.0f;
        juce::Path polygonPath;
        std::vector<juce::Point<float>> vertices;
        float flash = 0.0f;
        int flashVertex = -1;
        uint32_t lastHitCounter = 0;
        juce::Colour colour;
        juce::Point<float> beadPos{};
        bool edgeWalk = true;
        // Electric arc effect: track when this slot last fired
        float arcIntensity = 0.0f;  // Decays over time, used for arc brightness
        // Starlight Twinkle effect: per-vertex brightness values
        std::vector<float> twinkleBrightness;  // Brightness for each vertex (0-1), decays over time
        // Alternating Rotation effect: current rotation angle for this polygon
        float rotationAngle = 0.0f;  // Radians, positive = clockwise visual rotation
        // Colorwave effect: base hue offset for this ring (0-1), creates unique hue per ring
        float colorwaveHueOffset = 0.0f;
        // Neon Sweep effect: boost gain for sweep brightness (decays over time, boosted on hit)
        float sweepGain = 0.0f;
    };

    static constexpr int kNumSlots = SlotMachineAudioProcessor::kNumSlots;

    std::array<SlotVisual, kNumSlots> slotVisuals{};
    std::array<int, kNumSlots> activeOrder{};
    int activeCount = 0;

    double masterPhase = 0.0;
    double lastPhase = 0.0;
    float wrapFlash = 0.0f;
    float masterPulse = 0.0f;

    // Alternating Rotation: track cycle count for slow rotation across cycles
    uint64_t cycleCount = 0;

    // Nebula Drift effect: animation time and global energy tracker
    float nebulaTime = 0.0f;        // Continuous time accumulator for slow drift
    float nebulaEnergy = 0.0f;      // Smoothed global energy (RMS-like) for subtle brightness breath
};
