#include "PolyrhythmVizComponent.h"

#include <cmath>

namespace
{
    constexpr float kBackgroundBrightness = 0.065f;
    constexpr float kPolygonAlpha = 0.6f;
    constexpr float kBeadRadius = 6.0f;
    constexpr float kFlashDecay = 0.06f;
}

PolyrhythmVizComponent::PolyrhythmVizComponent(SlotMachineAudioProcessor& proc, APVTS& state)
    : processor(proc), apvts(state)
{
    setOpaque(true);
    startTimerHz(60);

    lastPhase = processor.getMasterPhase();
    masterPhase = lastPhase;

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& slot = slotVisuals[(size_t)i];
        slot.colour = juce::Colour::fromHSV(std::fmod((float)i * 0.12f, 1.0f), 0.82f, 0.92f, 1.0f);
        slot.lastHitCounter = processor.getSlotHitCounter(i);
    }
}

PolyrhythmVizComponent::~PolyrhythmVizComponent()
{
    stopTimer();
}

void PolyrhythmVizComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const juce::Colour background = juce::Colours::black.withBrightness(kBackgroundBrightness);
    g.fillAll(background);

    const auto centre = bounds.getCentre();
    const float margin = 28.0f;
    const float maxRadius = juce::jmax(0.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - margin);

    // Check if master pulse effect is enabled
    bool masterPulseEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerMasterPulse"))
        masterPulseEnabled = param->load() >= 0.5f;

    // Calculate breathing effect (slow expansion/contraction over entire cycle)
    // Uses sine wave: 0 at start, 1 at middle (phase 0.5), 0 at end
    const float breathingAmount = std::sin((float)masterPhase * juce::MathConstants<float>::pi);

    // Apply combined breathing and master pulse zoom effect to entire visualization
    juce::Graphics::ScopedSaveState saveState(g);

    // Combine breathing (continuous) with master pulse (triggered at wrap)
    float totalScale = 1.0f;
    totalScale += breathingAmount * 0.03f;  // Breathing: subtle 3% expansion at peak

    if (masterPulseEnabled && masterPulse > 0.001f)
    {
        const float pulseAmount = juce::jlimit(0.0f, 1.0f, masterPulse);
        totalScale += pulseAmount * 0.05f;  // Master pulse: additional 5% zoom at peak
    }

    if (std::abs(totalScale - 1.0f) > 0.0001f)
    {
        // Transform around the center point
        auto transform = juce::AffineTransform::translation(-centre.x, -centre.y)
                            .scaled(totalScale, totalScale)
                            .translated(centre.x, centre.y);
        g.addTransform(transform);
    }

    if (wrapFlash > 0.001f && maxRadius > 4.0f)
    {
        const float alpha = juce::jlimit(0.0f, 1.0f, wrapFlash);
        g.setColour(juce::Colours::white.withAlpha(0.12f * alpha));
        const float diameter = maxRadius * 2.0f;
        g.drawEllipse(centre.x - maxRadius, centre.y - maxRadius, diameter, diameter, 2.0f + 6.0f * alpha);
    }

    for (int order = activeCount - 1; order >= 0; --order)
    {
        const int slotIndex = activeOrder[(size_t)order];
        const auto& slot = slotVisuals[(size_t)slotIndex];
        if (!slot.active || slot.polygonPath.isEmpty())
            continue;

        const auto colour = slot.colour;
        g.setColour(colour.withAlpha(kPolygonAlpha));
        g.strokePath(slot.polygonPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (slot.flash > 0.001f && slot.flashVertex >= 0 && slot.flashVertex < (int)slot.vertices.size())
        {
            const float flashAlpha = juce::jlimit(0.0f, 1.0f, slot.flash);
            const float flashRadius = 5.0f + 4.0f * flashAlpha;
            g.setColour(colour.brighter(0.6f).withAlpha(0.65f * flashAlpha));
            const auto point = slot.vertices[(size_t)slot.flashVertex];
            g.fillEllipse(point.x - flashRadius, point.y - flashRadius, flashRadius * 2.0f, flashRadius * 2.0f);
        }

        // Apply breathing and master pulse effects to bead size
        float beadScale = 1.0f;
        beadScale += breathingAmount * 0.3f;  // Breathing: 30% expansion at peak

        if (masterPulseEnabled && masterPulse > 0.001f)
        {
            const float pulseAmount = juce::jlimit(0.0f, 1.0f, masterPulse);
            beadScale += pulseAmount * 0.8f;  // Master pulse: additional 80% expansion at peak
        }

        const float beadRadius = kBeadRadius * beadScale;
        const float beadAlpha = juce::jlimit(0.5f, 1.0f, 0.9f + (beadScale - 1.0f) * 0.1f);

        g.setColour(colour.withAlpha(beadAlpha));
        g.fillEllipse(slot.beadPos.x - beadRadius,
                      slot.beadPos.y - beadRadius,
                      beadRadius * 2.0f,
                      beadRadius * 2.0f);
    }
}

void PolyrhythmVizComponent::resized()
{
    for (auto& slot : slotVisuals)
    {
        slot.radius = 0.0f;
        slot.centre = {};
        slot.polygonPath.clear();
    }
}

void PolyrhythmVizComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
    }
}

void PolyrhythmVizComponent::timerCallback()
{
    const double currentPhase = processor.getMasterPhase();
    const bool wrapped = (currentPhase + 0.02) < lastPhase;
    if (wrapped)
    {
        wrapFlash = 1.0f;
        masterPulse = 1.0f;  // Trigger master pulse on wrap
    }

    lastPhase = currentPhase;
    masterPhase = currentPhase;
    wrapFlash = juce::jmax(0.0f, wrapFlash * 0.88f - 0.01f);
    masterPulse = juce::jmax(0.0f, masterPulse * 0.88f - 0.01f);  // Same decay as wrapFlash

    std::array<bool, kNumSlots> soloMask{};
    bool anySolo = false;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (auto* soloParam = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Solo"))
        {
            const bool solo = soloParam->load() >= 0.5f;
            soloMask[(size_t)i] = solo;
            anySolo = anySolo || solo;
        }
        else
        {
            soloMask[(size_t)i] = false;
        }
    }

    int timingMode = 0;
    if (auto* timingParam = apvts.getRawParameterValue("optTimingMode"))
        timingMode = juce::jlimit(0, 1, (int)std::round(timingParam->load()));

    int visualizerMode = 0;  // 0=Edge Walk, 1=Orbit, 2=Mixed
    if (auto* modeParam = apvts.getRawParameterValue("optVisualizerEdgeWalk"))
        visualizerMode = juce::jlimit(0, 2, (int)std::round(modeParam->load()));

    activeCount = 0;

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& slot = slotVisuals[(size_t)i];
        // Default edgeWalk to true, will be updated per-slot in mixed mode
        slot.edgeWalk = (visualizerMode == 0);
        const bool mute = [this, i]()
        {
            if (auto* muteParam = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Mute"))
                return muteParam->load() >= 0.5f;
            return false;
        }();

        const bool hasSample = processor.slotHasSample(i);
        const bool soloAllowed = (!anySolo || soloMask[(size_t)i]);
        const bool renderable = hasSample && !mute && soloAllowed;

        if (!renderable)
        {
            slot.active = false;
            slot.flash = juce::jmax(0.0f, slot.flash - kFlashDecay);
            continue;
        }

        slot.active = true;
        activeOrder[(size_t)activeCount++] = i;

        int sides = 1;
        if (timingMode == 1)
        {
            if (auto* countParam = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Count"))
                sides = juce::jlimit(1, 32, (int)std::round(countParam->load()));
        }
        else
        {
            double rate = 1.0;
            if (auto* rateParam = apvts.getRawParameterValue("slot" + juce::String(i + 1) + "_Rate"))
                rate = juce::jmax(0.0001f, rateParam->load());

            int num = 0, den = 1;
            approximateRational(rate, 32, num, den);
            sides = juce::jlimit(1, 32, num);
        }

        if (sides <= 0)
            sides = 1;

        if (slot.sides != sides)
        {
            slot.sides = sides;
            slot.polygonPath.clear();
        }

        slot.beadPhase = juce::jlimit(0.0, 1.0, masterPhase);
        slot.beadAngle = slot.beadPhase * juce::MathConstants<double>::twoPi - juce::MathConstants<double>::halfPi;

        const uint32_t hits = processor.getSlotHitCounter(i);
        if (hits != slot.lastHitCounter)
        {
            slot.lastHitCounter = hits;
            slot.flash = 1.0f;
            const int sides = juce::jmax(1, slot.sides);
            const int corner = sides > 0
                ? (int)std::floor(masterPhase * (double)sides + 0.5) % sides
                : -1;
            slot.flashVertex = corner;
        }
        else
        {
            slot.flash = juce::jmax(0.0f, slot.flash - kFlashDecay);
        }
    }

    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const float margin = 28.0f;
    const float maxRadius = juce::jmax(0.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - margin);

    if (activeCount > 0 && maxRadius > 2.0f)
    {
        const float spacing = maxRadius / (float)(activeCount + 1);
        for (int order = 0; order < activeCount; ++order)
        {
            const int slotIndex = activeOrder[(size_t)order];
            auto& slot = slotVisuals[(size_t)slotIndex];

            // In mixed mode, every 3rd bead from center (order % 3 == 2) uses orbit
            if (visualizerMode == 2)
            {
                slot.edgeWalk = (order % 3 != 2);  // Edge walk unless it's every 3rd
            }

            const float radius = spacing * (float)(order + 1);
            updateSlotGeometry(slotIndex, centre, radius);
        }
    }

    repaint();
}

void PolyrhythmVizComponent::updateSlotGeometry(int slotIndex, juce::Point<float> centre, float radius)
{
    auto& slot = slotVisuals[(size_t)slotIndex];
    const int sides = juce::jmax(1, slot.sides);

    const bool centreChanged = slot.centre.getDistanceFrom(centre) > 0.1f;
    const bool radiusChanged = std::abs(slot.radius - radius) > 0.1f;
    const bool sizeChanged = slot.vertices.size() != (size_t)sides;
    const bool needsGeometry = centreChanged || radiusChanged || sizeChanged || slot.polygonPath.isEmpty();

    slot.centre = centre;
    slot.radius = radius;

    if (needsGeometry)
    {
        slot.vertices.resize((size_t)sides);
        slot.polygonPath.clear();

        const float angleStep = juce::MathConstants<float>::twoPi / (float)sides;
        float angle = -juce::MathConstants<float>::halfPi;

        for (int i = 0; i < sides; ++i)
        {
            const auto point = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * radius;
            slot.vertices[(size_t)i] = point;
            if (i == 0)
                slot.polygonPath.startNewSubPath(point);
            else
                slot.polygonPath.lineTo(point);
            angle += angleStep;
        }

        slot.polygonPath.closeSubPath();
    }

    const bool canEdgeWalk = slot.edgeWalk && slot.vertices.size() >= 2;

    if (canEdgeWalk)
    {
        const int sides = (int)slot.vertices.size();
        const double u = juce::jlimit(0.0, 1.0, slot.beadPhase);
        const double segF = u * (double)sides;
        const double segIndex = std::floor(segF);
        const int i0 = ((int)segIndex % sides + sides) % sides;
        const int i1 = (i0 + 1) % sides;
        const float w = (float)(segF - segIndex);

        const juce::Point<float> p0 = slot.vertices[(size_t)i0];
        const juce::Point<float> p1 = slot.vertices[(size_t)i1];
        const juce::Point<float> beadPos = p0 + (p1 - p0) * w;

        slot.beadPos = beadPos;
        slot.beadAngle = std::atan2(beadPos.y - centre.y, beadPos.x - centre.x);
    }
    else
    {
        slot.beadPos = centre + juce::Point<float>(std::cos((float)slot.beadAngle), std::sin((float)slot.beadAngle)) * radius;
    }
}

void PolyrhythmVizComponent::approximateRational(double value, int maxDenominator, int& num, int& den)
{
    int a0 = (int)std::floor(value);
    if (a0 > maxDenominator)
    {
        num = a0;
        den = 1;
        return;
    }

    int n0 = 1, d0 = 0;
    int n1 = a0, d1 = 1;
    double frac = value - (double)a0;

    while (frac > 1e-12 && d1 <= maxDenominator)
    {
        const double inv = 1.0 / frac;
        const int ai = (int)std::floor(inv);
        const int n2 = n0 + ai * n1;
        const int d2 = d0 + ai * d1;
        if (d2 > maxDenominator)
            break;

        n0 = n1; d0 = d1;
        n1 = n2; d1 = d2;
        frac = inv - (double)ai;
    }

    num = n1;
    den = d1;
}
