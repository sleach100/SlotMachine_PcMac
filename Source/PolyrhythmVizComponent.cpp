#include "PolyrhythmVizComponent.h"

#include <cmath>

namespace
{
    constexpr float kBackgroundBrightness = 0.065f;
    constexpr float kPolygonAlpha = 0.6f;
    constexpr float kBeadRadius = 6.0f;
    constexpr float kFlashDecay = 0.06f;
    constexpr float kTwinkleDecay = 0.04f;  // Slower decay for starlight effect
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

    // Check if effects are enabled
    bool masterPulseEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerMasterPulse"))
        masterPulseEnabled = param->load() >= 0.5f;

    bool breatheEnabled = true;
    if (auto* param = apvts.getRawParameterValue("optVisualizerBreathe"))
        breatheEnabled = param->load() >= 0.5f;

    bool electricArcEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerElectricArc"))
        electricArcEnabled = param->load() >= 0.5f;

    bool starlightTwinkleEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerStarlightTwinkle"))
        starlightTwinkleEnabled = param->load() >= 0.5f;

    bool alternatingRotationEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerAlternatingRotation"))
        alternatingRotationEnabled = param->load() >= 0.5f;

    // Calculate breathing effect (slow expansion/contraction over entire cycle)
    // Uses sine wave: 0 at start, 1 at middle (phase 0.5), 0 at end
    const float breathingAmount = breatheEnabled
        ? std::sin((float)masterPhase * juce::MathConstants<float>::pi)
        : 0.0f;

    // Apply combined breathing and master pulse zoom effect to entire visualization
    juce::Graphics::ScopedSaveState saveState(g);

    // Combine breathing (continuous) with master pulse (triggered at wrap)
    float totalScale = 1.0f;
    if (breatheEnabled)
        totalScale += breathingAmount * 0.05f;  // Breathing: 5% expansion at peak

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

    // Neural Chaos effect: draw faint energy lines from fired vertices to random points on other polygons
    if (electricArcEnabled)
    {
        constexpr float kArcThreshold = 0.3f;  // Minimum arc intensity to draw connection
        constexpr float kArcAlphaMax = 0.4f;   // Maximum arc alpha
        constexpr float kArcWidth = 1.2f;      // Base arc line width

        // Helper lambda to apply rotation to a point for a given slot
        auto rotatePointForSlot = [alternatingRotationEnabled](const SlotVisual& s, juce::Point<float> p) -> juce::Point<float>
        {
            if (alternatingRotationEnabled && std::abs(s.rotationAngle) > 0.0001f)
            {
                const auto transform = juce::AffineTransform::rotation(s.rotationAngle, s.centre.x, s.centre.y);
                return p.transformedBy(transform);
            }
            return p;
        };

        // Collect all active slots with high arc intensity (recently fired)
        std::vector<int> firedSlots;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto& slot = slotVisuals[(size_t)i];
            if (slot.active && slot.arcIntensity >= kArcThreshold &&
                slot.flashVertex >= 0 && slot.flashVertex < (int)slot.vertices.size())
            {
                firedSlots.push_back(i);
            }
        }

        // Collect all active slots (potential arc targets)
        std::vector<int> activeSlots;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (slotVisuals[(size_t)i].active && !slotVisuals[(size_t)i].vertices.empty())
                activeSlots.push_back(i);
        }

        // Draw arcs from each fired vertex to random points on other polygons
        for (int srcIdx : firedSlots)
        {
            const auto& srcSlot = slotVisuals[(size_t)srcIdx];
            // Apply rotation to source vertex position
            const auto srcPoint = rotatePointForSlot(srcSlot, srcSlot.vertices[(size_t)srcSlot.flashVertex]);

            // Create 1-3 arcs per fired vertex based on intensity
            const int numArcs = 1 + (int)(srcSlot.arcIntensity * 2.0f);

            for (int arcNum = 0; arcNum < numArcs && activeSlots.size() > 1; ++arcNum)
            {
                // Use deterministic "randomness" based on position and arc number for consistent look per frame
                const float seed1 = std::fmod(srcPoint.x * 0.17f + srcPoint.y * 0.23f + (float)arcNum * 0.31f + (float)masterPhase * 0.1f, 1.0f);
                const float seed2 = std::fmod(srcPoint.y * 0.13f + srcPoint.x * 0.19f + (float)arcNum * 0.37f + (float)masterPhase * 0.15f, 1.0f);

                // Pick a different target slot (not the source)
                int targetIdx = activeSlots[(size_t)(seed1 * (float)activeSlots.size()) % activeSlots.size()];
                if (targetIdx == srcIdx)
                    targetIdx = activeSlots[(size_t)((seed1 + 0.5f) * (float)activeSlots.size()) % activeSlots.size()];
                if (targetIdx == srcIdx)
                    continue;

                const auto& targetSlot = slotVisuals[(size_t)targetIdx];
                const int numVerts = (int)targetSlot.vertices.size();
                if (numVerts < 2)
                    continue;

                // Pick a random point along the polygon edge
                const float edgePos = seed2 * (float)numVerts;
                const int v0 = (int)edgePos % numVerts;
                const int v1 = (v0 + 1) % numVerts;
                const float t = edgePos - std::floor(edgePos);

                // Apply rotation to target vertex positions
                const auto p0 = rotatePointForSlot(targetSlot, targetSlot.vertices[(size_t)v0]);
                const auto p1 = rotatePointForSlot(targetSlot, targetSlot.vertices[(size_t)v1]);
                const auto targetPoint = p0 + (p1 - p0) * t;

                // Calculate distance for color tinting
                const auto diff = targetPoint - srcPoint;
                const float dist = diff.getDistanceFromOrigin();
                const float distNorm = juce::jlimit(0.0f, 1.0f, dist / (maxRadius * 2.0f));  // Normalize to 0-1

                // Calculate arc intensity
                const float intensity = srcSlot.arcIntensity * (0.6f + seed1 * 0.4f);

                // Distance tint: close = bright cyan (0.52), far = teal/blue (0.58) with more fade
                const float hue = 0.50f + seed1 * 0.06f + distNorm * 0.08f;  // Shift toward blue with distance
                const float alphaFade = 1.0f - distNorm * 0.4f;  // Fade more with distance
                const float alpha = kArcAlphaMax * intensity * alphaFade;

                const auto arcColour = juce::Colour::fromHSV(hue, 0.35f + distNorm * 0.15f, 1.0f, alpha);
                g.setColour(arcColour);

                // Draw a jagged arc path to simulate electrical discharge
                juce::Path arcPath;
                arcPath.startNewSubPath(srcPoint);

                const int segments = juce::jmax(2, (int)(dist / 35.0f));

                for (int seg = 1; seg < segments; ++seg)
                {
                    const float segT = (float)seg / (float)segments;
                    auto midPoint = srcPoint + diff * segT;

                    // Add perpendicular offset for jagged effect - varies along the arc
                    const float jitter = std::sin(seed1 * 6.28f + segT * 4.5f + seed2 * 3.14f) * 10.0f * intensity;
                    const auto perpRaw = juce::Point<float>(-diff.y, diff.x);
                    const float perpLen = perpRaw.getDistanceFromOrigin();
                    if (perpLen > 0.001f)
                        midPoint = midPoint + perpRaw * (jitter / perpLen);

                    arcPath.lineTo(midPoint);
                }

                arcPath.lineTo(targetPoint);
                g.strokePath(arcPath, juce::PathStrokeType(kArcWidth + intensity * 0.6f));

                // Draw a subtle glow around the arc
                if (intensity > 0.6f)
                {
                    g.setColour(arcColour.withAlpha(alpha * 0.25f));
                    g.strokePath(arcPath, juce::PathStrokeType(kArcWidth + 2.5f + intensity * 1.5f));
                }
            }
        }
    }

    for (int order = activeCount - 1; order >= 0; --order)
    {
        const int slotIndex = activeOrder[(size_t)order];
        const auto& slot = slotVisuals[(size_t)slotIndex];
        if (!slot.active || slot.polygonPath.isEmpty())
            continue;

        const auto colour = slot.colour;

        // Create rotation transform for this polygon (rotates around polygon center)
        const auto rotationTransform = alternatingRotationEnabled && std::abs(slot.rotationAngle) > 0.0001f
            ? juce::AffineTransform::rotation(slot.rotationAngle, slot.centre.x, slot.centre.y)
            : juce::AffineTransform();

        // Helper lambda to apply rotation to a point
        auto rotatePoint = [&rotationTransform, alternatingRotationEnabled, &slot](juce::Point<float> p) -> juce::Point<float>
        {
            if (alternatingRotationEnabled && std::abs(slot.rotationAngle) > 0.0001f)
                return p.transformedBy(rotationTransform);
            return p;
        };

        // Stroke the polygon path with rotation applied
        g.setColour(colour.withAlpha(kPolygonAlpha));
        if (alternatingRotationEnabled && std::abs(slot.rotationAngle) > 0.0001f)
        {
            juce::Path rotatedPath;
            rotatedPath.addPath(slot.polygonPath, rotationTransform);
            g.strokePath(rotatedPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.strokePath(slot.polygonPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Flash at hit vertex (rotates with polygon)
        if (slot.flash > 0.001f && slot.flashVertex >= 0 && slot.flashVertex < (int)slot.vertices.size())
        {
            const float flashAlpha = juce::jlimit(0.0f, 1.0f, slot.flash);
            const float flashRadius = 5.0f + 4.0f * flashAlpha;
            g.setColour(colour.brighter(0.6f).withAlpha(0.65f * flashAlpha));
            const auto point = rotatePoint(slot.vertices[(size_t)slot.flashVertex]);
            g.fillEllipse(point.x - flashRadius, point.y - flashRadius, flashRadius * 2.0f, flashRadius * 2.0f);
        }

        // Starlight Twinkle effect: draw bright star-like highlights at twinkling vertices
        // Twinkle stars rotate with the polygon
        if (starlightTwinkleEnabled && slot.twinkleBrightness.size() == slot.vertices.size())
        {
            for (size_t v = 0; v < slot.vertices.size(); ++v)
            {
                const float brightness = slot.twinkleBrightness[v];
                if (brightness > 0.01f)
                {
                    const auto point = rotatePoint(slot.vertices[v]);
                    const float alpha = juce::jlimit(0.0f, 1.0f, brightness);

                    // Draw a 4-pointed star shape for the twinkle
                    const float innerRadius = 2.0f + 3.0f * alpha;
                    const float outerRadius = 4.0f + 8.0f * alpha;

                    // Draw soft glow circle first
                    g.setColour(juce::Colours::white.withAlpha(0.25f * alpha));
                    g.fillEllipse(point.x - outerRadius, point.y - outerRadius,
                                  outerRadius * 2.0f, outerRadius * 2.0f);

                    // Draw 4-pointed star
                    juce::Path starPath;
                    for (int spike = 0; spike < 4; ++spike)
                    {
                        const float angle = (float)spike * juce::MathConstants<float>::halfPi;
                        const float nextAngle = angle + juce::MathConstants<float>::halfPi * 0.5f;

                        const auto outerPt = point + juce::Point<float>(
                            std::cos(angle) * outerRadius,
                            std::sin(angle) * outerRadius);
                        const auto innerPt = point + juce::Point<float>(
                            std::cos(nextAngle) * innerRadius,
                            std::sin(nextAngle) * innerRadius);

                        if (spike == 0)
                            starPath.startNewSubPath(outerPt);
                        else
                            starPath.lineTo(outerPt);
                        starPath.lineTo(innerPt);
                    }
                    starPath.closeSubPath();

                    // Fill with bright white/color
                    g.setColour(juce::Colours::white.withAlpha(0.7f * alpha));
                    g.fillPath(starPath);

                    // Bright center dot
                    g.setColour(juce::Colours::white.withAlpha(0.9f * alpha));
                    g.fillEllipse(point.x - 1.5f, point.y - 1.5f, 3.0f, 3.0f);
                }
            }
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
        ++cycleCount;        // Increment cycle count for slow rotation effect
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

    bool starlightTwinkleEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerStarlightTwinkle"))
        starlightTwinkleEnabled = param->load() >= 0.5f;

    bool alternatingRotationEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerAlternatingRotation"))
        alternatingRotationEnabled = param->load() >= 0.5f;

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
            slot.arcIntensity = 1.0f;  // Trigger arc intensity on hit
            const int sides = juce::jmax(1, slot.sides);
            const int corner = sides > 0
                ? (int)std::floor(masterPhase * (double)sides + 0.5) % sides
                : -1;
            slot.flashVertex = corner;

            // Starlight Twinkle: trigger random vertices on hit
            if (starlightTwinkleEnabled && sides > 0)
            {
                // Ensure twinkleBrightness is the right size
                if (slot.twinkleBrightness.size() != (size_t)sides)
                    slot.twinkleBrightness.resize((size_t)sides, 0.0f);

                // Use hit counter for deterministic "randomness" that varies per hit
                const uint32_t seed = hits * 31u + (uint32_t)i * 17u;

                // Trigger 1-3 random vertices (based on number of sides)
                const int numTwinkles = juce::jmin(sides, 1 + (int)(seed % 3u));
                for (int t = 0; t < numTwinkles; ++t)
                {
                    const int vertexIdx = (int)((seed * (31u + (uint32_t)t)) % (uint32_t)sides);
                    slot.twinkleBrightness[(size_t)vertexIdx] = 1.0f;
                }
            }
        }
        else
        {
            slot.flash = juce::jmax(0.0f, slot.flash - kFlashDecay);
            slot.arcIntensity = juce::jmax(0.0f, slot.arcIntensity * 0.92f - 0.008f);  // Slower decay for arcs
        }

        // Decay starlight twinkle brightness for all vertices (even when hit, for independent decay)
        if (starlightTwinkleEnabled && !slot.twinkleBrightness.empty())
        {
            for (auto& brightness : slot.twinkleBrightness)
                brightness = juce::jmax(0.0f, brightness - kTwinkleDecay);
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

            // Calculate rotation angle for alternating rotation effect
            if (alternatingRotationEnabled)
            {
                // Slow rotation: completes one full rotation every CPR cycles (e.g., CPR=4 = ¼ turn per cycle)
                // Even order (0, 2, 4...) = clockwise, Odd order (1, 3, 5...) = counter-clockwise
                constexpr float kCyclesPerRotation = 4.0f;  // Full rotation every 4 cycles
                const float direction = (order % 2 == 0) ? 1.0f : -1.0f;
                const float rotationPerCycle = juce::MathConstants<float>::twoPi / kCyclesPerRotation;
                // Total rotation accumulates across cycles: (completed cycles + current phase progress)
                const float totalCycles = (float)(cycleCount % 4) + (float)masterPhase;
                slot.rotationAngle = direction * rotationPerCycle * totalCycles;
            }
            else
            {
                slot.rotationAngle = 0.0f;
            }
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

        // Resize twinkleBrightness to match vertices (preserve existing values where possible)
        if (slot.twinkleBrightness.size() != (size_t)sides)
            slot.twinkleBrightness.resize((size_t)sides, 0.0f);

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
