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

    // Check nebula early to render background layer
    bool nebulaDriftEnabledEarly = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerNebulaDrift"))
        nebulaDriftEnabledEarly = param->load() >= 0.5f;

    // Nebula Drift: soft, dreamy cinematic background with blur and vignette
    if (nebulaDriftEnabledEarly)
    {
        // Render nebula layers directly to screen (no offscreen buffer needed)
        // Use very large, soft gradients for dreamy effect without expensive blur

        // Two-color palette: Teal #2BD7D1 → Magenta #FF4DD2
        const juce::Colour tealAnchor(0x2B, 0xD7, 0xD1);
        const juce::Colour magentaAnchor(0xFF, 0x4D, 0xD2);

        // Slow sinusoidal crossfade between the two colors
        const float paletteMix = std::sin(nebulaTime * 0.02f) * 0.5f + 0.5f;

        // Gentle breathing modulation (±10% of alpha, long decay)
        const float breathMod = 1.0f + nebulaEnergy * 0.10f;

        const float bw = bounds.getWidth();
        const float bh = bounds.getHeight();
        const float maxDim = juce::jmax(bw, bh);

        // === Layer 1: Base Aura (very large, covers entire canvas) ===
        {
            const float driftX = std::sin(nebulaTime * 0.015f) * 0.12f;
            const float driftY = std::cos(nebulaTime * 0.012f + 0.7f) * 0.10f;

            const float cx = bounds.getCentreX() + driftX * bw;
            const float cy = bounds.getCentreY() + driftY * bh;
            const float radius = maxDim * 0.95f;

            const auto layerColour = tealAnchor.interpolatedWith(magentaAnchor, paletteMix * 0.3f);
            const float alpha = 0.20f * breathMod;

            juce::ColourGradient gradient(layerColour.withAlpha(alpha), cx, cy,
                                          layerColour.withAlpha(0.0f), cx + radius, cy, true);
            g.setGradientFill(gradient);
            g.fillRect(bounds);
        }

        // === Layer 2: Mid Cloud ===
        {
            const float driftX = std::sin(nebulaTime * 0.022f + 1.2f) * 0.14f;
            const float driftY = std::cos(nebulaTime * 0.018f + 2.1f) * 0.13f;

            const float cx = bounds.getCentreX() + driftX * bw;
            const float cy = bounds.getCentreY() + driftY * bh;
            const float radius = maxDim * 0.65f;

            const auto layerColour = tealAnchor.interpolatedWith(magentaAnchor, paletteMix * 0.6f + 0.2f);
            const float alpha = 0.14f * breathMod;

            juce::ColourGradient gradient(layerColour.withAlpha(alpha), cx, cy,
                                          layerColour.withAlpha(0.0f), cx + radius, cy, true);
            g.setGradientFill(gradient);
            g.fillRect(bounds);
        }

        // === Layer 3: Small accent cloud ===
        {
            const float driftX = std::cos(nebulaTime * 0.025f + 3.5f) * 0.16f;
            const float driftY = std::sin(nebulaTime * 0.02f + 1.9f) * 0.15f;

            const float cx = bounds.getCentreX() + driftX * bw;
            const float cy = bounds.getCentreY() + driftY * bh;
            const float radius = maxDim * 0.5f;

            const auto layerColour = tealAnchor.interpolatedWith(magentaAnchor, paletteMix * 0.8f + 0.5f);
            const float alpha = 0.10f * breathMod;

            juce::ColourGradient gradient(layerColour.withAlpha(alpha), cx, cy,
                                          layerColour.withAlpha(0.0f), cx + radius, cy, true);
            g.setGradientFill(gradient);
            g.fillRect(bounds);
        }

        // Draw soft vignette (darken edges by 3-5%)
        {
            const float vignetteInnerRadius = juce::jmin(bw, bh) * 0.35f;
            const float vignetteOuterRadius = juce::jmin(bw, bh) * 0.7f;

            juce::ColourGradient vignette(
                juce::Colours::transparentBlack,
                bounds.getCentreX(), bounds.getCentreY(),
                juce::Colours::black.withAlpha(0.04f),
                bounds.getCentreX() + vignetteOuterRadius, bounds.getCentreY(),
                true);
            vignette.addColour(vignetteInnerRadius / vignetteOuterRadius, juce::Colours::transparentBlack);
            g.setGradientFill(vignette);
            g.fillRect(bounds);
        }
    }

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

    bool colorwaveEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerColorwave"))
        colorwaveEnabled = param->load() >= 0.5f;

    bool nebulaDriftEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerNebulaDrift"))
        nebulaDriftEnabled = param->load() >= 0.5f;

    bool neonSweepEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerNeonSweep"))
        neonSweepEnabled = param->load() >= 0.5f;

    // Calculate colorwave hue drift: ¼ turn per cycle (one full revolution every 4 cycles)
    // Uses same timing as Alternating Rotation for coherence
    float colorwaveHueDrift = 0.0f;
    if (colorwaveEnabled)
    {
        constexpr float kCyclesPerRotation = 4.0f;  // Full hue rotation every 4 cycles
        const float driftPerCycle = 1.0f / kCyclesPerRotation;  // 0.25 hue shift per cycle
        const float totalCycles = (float)(cycleCount % 4) + (float)masterPhase;
        colorwaveHueDrift = std::fmod(driftPerCycle * totalCycles, 1.0f);
    }

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

        // Draw arcs connecting random vertices across different rings
        for (int srcIdx : firedSlots)
        {
            const auto& srcSlot = slotVisuals[(size_t)srcIdx];
            const int srcNumVerts = (int)srcSlot.vertices.size();
            if (srcNumVerts < 2)
                continue;

            // Create 1-3 arcs per fired slot based on intensity
            const int numArcs = 1 + (int)(srcSlot.arcIntensity * 2.0f);

            for (int arcNum = 0; arcNum < numArcs && activeSlots.size() > 1; ++arcNum)
            {
                // Use arcIntensity and masterPhase for variation - these change with each firing
                const float intensity = srcSlot.arcIntensity;
                const float seed1 = std::fmod((float)srcIdx * 0.17f + (float)arcNum * 0.31f + intensity * 7.3f + (float)masterPhase * 13.7f, 1.0f);
                const float seed2 = std::fmod((float)srcIdx * 0.13f + (float)arcNum * 0.37f + intensity * 11.9f + (float)masterPhase * 17.3f, 1.0f);
                const float seed3 = std::fmod((float)srcIdx * 0.23f + (float)arcNum * 0.41f + intensity * 5.7f + (float)masterPhase * 19.1f, 1.0f);

                // Pick a random vertex from the source ring
                const int srcVertIdx = (int)(seed3 * (float)srcNumVerts) % srcNumVerts;
                const auto srcPoint = rotatePointForSlot(srcSlot, srcSlot.vertices[(size_t)srcVertIdx]);

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

                // Pick a random vertex from the target ring
                const int targetVertIdx = (int)(seed2 * (float)numVerts) % numVerts;

                // Apply rotation to target vertex position
                const auto targetPoint = rotatePointForSlot(targetSlot, targetSlot.vertices[(size_t)targetVertIdx]);

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

        // Stroke the polygon path - either with colorwave gradient or solid color
        if (colorwaveEnabled && slot.vertices.size() >= 2)
        {
            // Colorwave: draw each segment with a hue based on its angular position
            // The hue gradient rotates slowly around the ring (¼ turn per cycle)
            const int numVerts = (int)slot.vertices.size();
            const juce::PathStrokeType strokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            for (int i = 0; i < numVerts; ++i)
            {
                const int nextI = (i + 1) % numVerts;

                // Get vertices (apply rotation if enabled)
                const auto p0 = rotatePoint(slot.vertices[(size_t)i]);
                const auto p1 = rotatePoint(slot.vertices[(size_t)nextI]);

                // Calculate midpoint of segment for hue calculation
                const auto midpoint = (p0 + p1) * 0.5f;

                // Calculate angle from center to midpoint
                // Phase-zero at 12 o'clock (-π/2), so we add π/2 to shift it
                const float angle = std::atan2(midpoint.y - slot.centre.y, midpoint.x - slot.centre.x);
                // Normalize angle to 0-1 range with 12 o'clock as 0
                float normalizedAngle = (angle + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::twoPi;
                if (normalizedAngle < 0.0f) normalizedAngle += 1.0f;

                // Calculate final hue: base position + drift + per-ring offset
                float hue = std::fmod(normalizedAngle + colorwaveHueDrift + slot.colorwaveHueOffset, 1.0f);

                // Create segment color with calculated hue
                const auto segmentColour = juce::Colour::fromHSV(hue, 0.82f, 0.92f, kPolygonAlpha);
                g.setColour(segmentColour);

                // Draw this segment
                juce::Path segmentPath;
                segmentPath.startNewSubPath(p0);
                segmentPath.lineTo(p1);
                g.strokePath(segmentPath, strokeType);
            }
        }
        else
        {
            // Standard solid color stroke
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
        }

        // Neon Sweep effect: crisp edge-hugging glow that races around the polygon
        // Peak brightness at bead position, with trailing falloff in direction of travel
        if (neonSweepEnabled && slot.vertices.size() >= 2)
        {
            const int numVerts = (int)slot.vertices.size();
            const float beadU = (float)slot.beadPhase;  // Position along perimeter [0..1)

            // Sweep direction follows ring rotation parity
            // Even order = clockwise = increasing u, Odd order = counter-clockwise = decreasing u
            const bool sweepCW = (order % 2 == 0);

            // Sweep parameters
            constexpr float kSweepBaseBrightness = 0.4f;   // Base sweep alpha
            constexpr float kSweepPeakBrightness = 0.95f;  // Peak alpha at bead
            constexpr float kSweepTrailLength = 0.25f;     // Trail length as fraction of perimeter
            constexpr float kSweepWidth = 2.5f;            // Stroke width
            constexpr float kSweepGlowWidth = 5.0f;        // Outer glow width

            // Gain boost from hit (adds brightness pulse)
            const float hitBoost = juce::jlimit(0.0f, 1.0f, slot.sweepGain);

            // Calculate sweep color (neon version of slot color, brighter and more saturated)
            const auto sweepColour = colour.brighter(0.4f).withSaturation(juce::jmin(1.0f, colour.getSaturation() + 0.2f));

            // Draw each segment with brightness based on distance from bead
            for (int i = 0; i < numVerts; ++i)
            {
                const int nextI = (i + 1) % numVerts;

                // Segment endpoints (rotated)
                const auto p0 = rotatePoint(slot.vertices[(size_t)i]);
                const auto p1 = rotatePoint(slot.vertices[(size_t)nextI]);

                // Calculate segment center position along perimeter [0..1)
                const float segStart = (float)i / (float)numVerts;
                const float segEnd = (float)(i + 1) / (float)numVerts;
                const float segMid = (segStart + segEnd) * 0.5f;

                // Calculate distance from bead (accounting for wrap-around)
                // For CW sweep, trail is behind (lower u values)
                // For CCW sweep, trail is behind (higher u values)
                float distFromBead;
                if (sweepCW)
                {
                    // CW: bead moves toward higher u, trail is at lower u
                    distFromBead = beadU - segMid;
                    if (distFromBead < 0.0f) distFromBead += 1.0f;  // Wrap around
                }
                else
                {
                    // CCW: bead moves toward lower u, trail is at higher u
                    distFromBead = segMid - beadU;
                    if (distFromBead < 0.0f) distFromBead += 1.0f;  // Wrap around
                }

                // Calculate brightness with exponential falloff
                // Peak at bead position, falls off over trail length
                float brightness;
                if (distFromBead < 0.02f)
                {
                    // Very close to bead: peak brightness
                    brightness = kSweepPeakBrightness;
                }
                else if (distFromBead < kSweepTrailLength)
                {
                    // In trail: exponential falloff
                    const float t = distFromBead / kSweepTrailLength;
                    brightness = kSweepBaseBrightness + (kSweepPeakBrightness - kSweepBaseBrightness) * std::exp(-t * 3.0f);
                }
                else
                {
                    // Outside trail: minimal base brightness
                    brightness = kSweepBaseBrightness * 0.3f;
                }

                // Apply hit boost
                brightness = juce::jmin(1.0f, brightness + hitBoost * 0.4f);

                // Draw outer glow first (wider, more transparent)
                g.setColour(sweepColour.withAlpha(brightness * 0.25f));
                juce::Path glowPath;
                glowPath.startNewSubPath(p0);
                glowPath.lineTo(p1);
                g.strokePath(glowPath, juce::PathStrokeType(kSweepGlowWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // Draw core sweep line (thinner, brighter)
                g.setColour(sweepColour.withAlpha(brightness));
                g.strokePath(glowPath, juce::PathStrokeType(kSweepWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Draw bright point at bead position for extra emphasis
            const auto beadPosRotated = rotatePoint(slot.beadPos);
            const float beadGlowAlpha = 0.7f + hitBoost * 0.3f;
            g.setColour(sweepColour.withAlpha(beadGlowAlpha));
            g.fillEllipse(beadPosRotated.x - 4.0f, beadPosRotated.y - 4.0f, 8.0f, 8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.6f + hitBoost * 0.3f));
            g.fillEllipse(beadPosRotated.x - 2.0f, beadPosRotated.y - 2.0f, 4.0f, 4.0f);
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

        // Apply rotation to bead position so it stays welded to the polygon
        const auto beadPosRotated = rotatePoint(slot.beadPos);

        g.setColour(colour.withAlpha(beadAlpha));
        g.fillEllipse(beadPosRotated.x - beadRadius,
                      beadPosRotated.y - beadRadius,
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

    // Nebula Drift: advance time for continuous animation (60 FPS timer)
    nebulaTime += 1.0f / 60.0f;

    // Nebula energy: very slow decay (~2-3 second half-life), will be boosted by hits below
    nebulaEnergy = juce::jmax(0.0f, nebulaEnergy * 0.988f - 0.001f);

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
            slot.sweepGain = 1.0f;     // Boost neon sweep on hit
            nebulaEnergy = juce::jmin(1.0f, nebulaEnergy + 0.12f);  // Gentle boost to nebula energy on hit
            const int sides = juce::jmax(1, slot.sides);
            // Use floor (same as edge-walk segment calculation) for consistent vertex indexing
            // This ensures flash appears at the vertex the bead just passed, not ahead of it
            const int corner = sides > 0
                ? ((int)std::floor(masterPhase * (double)sides) % sides + sides) % sides
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
            slot.sweepGain = juce::jmax(0.0f, slot.sweepGain * 0.88f - 0.015f);  // Fast decay for sweep boost
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

            // Calculate colorwave hue offset for this ring
            // Each ring gets a unique base hue offset (spaced by golden ratio for pleasing variety)
            slot.colorwaveHueOffset = std::fmod((float)order * 0.618033988749895f, 1.0f);
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
