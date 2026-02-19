#include "PolyrhythmVizComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float kBackgroundBrightness = 0.065f;
    constexpr float kPolygonAlpha = 0.6f;
    constexpr float kBeadRadius = 6.0f;
    constexpr float kFlashDecay = 0.06f;
    constexpr float kTwinkleDecay = 0.04f;  // Slower decay for starlight effect
    // NEW: Throttling and Persistence Constants for Electric Arc Cache
    constexpr int kArcRebuildIntervalMs = 80;   // Rebuild at max ~12.5 Hz
    constexpr float kArcIntensityDeltaThreshold = 0.05f; // Rebuild if intensity changes by 5%
    constexpr float kArcCachePersistenceThreshold = 0.02f; // Keep cache allocated until this intensity (avoids churn)
    constexpr float kArcThreshold = 0.3f;
    constexpr float kArcAlphaMax = 0.4f;
    constexpr float kArcWidth = 1.2f;

    float hashToUnit(int x, int y, uint32_t seed)
    {
        auto v = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(y * 19349663) ^ (seed * 83492791u);
        v ^= (v >> 13);
        v *= 1274126177u;
        return (float)(v & 0x00ffffffu) / (float)0x01000000u;
    }

    float interpolatedNoise(float x, float y, uint32_t seed)
    {
        const int x0 = (int)std::floor(x);
        const int y0 = (int)std::floor(y);
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;

        const float tx = x - (float)x0;
        const float ty = y - (float)y0;

        const float n00 = hashToUnit(x0, y0, seed);
        const float n10 = hashToUnit(x1, y0, seed);
        const float n01 = hashToUnit(x0, y1, seed);
        const float n11 = hashToUnit(x1, y1, seed);

        const float nx0 = juce::jmap(tx, n00, n10);
        const float nx1 = juce::jmap(tx, n01, n11);
        return juce::jmap(ty, nx0, nx1);
    }

    float fbm(float x, float y, uint32_t seed)
    {
        float value = 0.0f;
        float amplitude = 0.6f;
        float frequency = 1.2f;

        for (int octave = 0; octave < 4; ++octave)
        {
            value += interpolatedNoise(x * frequency, y * frequency, seed + (uint32_t)(octave * 97)) * amplitude;
            amplitude *= 0.55f;
            frequency *= 1.7f;
        }

        return juce::jlimit(0.0f, 1.0f, value);
    }
}

PolyrhythmVizComponent::PolyrhythmVizComponent(SlotMachineAudioProcessor& proc, APVTS& state)
    : processor(proc), apvts(state)
{
    setOpaque(true);
#if JUCE_MAC
    // macOS: VBlankAttachment (backed by CVDisplayLink) fires at the hardware vsync rate,
    // eliminating the NSTimer run-loop jitter that causes the visualizer to lag behind hits.
    vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this] { timerCallback(); });
#else
    // Windows/other: 120 Hz timer.
    startTimerHz(120);
#endif

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
#if JUCE_MAC
    // Unregister VBlankAttachment first so no callbacks fire after shuttingDown is set.
    vblankAttachment.reset();
#endif
    shuttingDown = true;
    stopTimer();
}

PolyrhythmVizComponent::NebulaSettings PolyrhythmVizComponent::computeNebulaSettings() const
{
    NebulaSettings settings;
    const float dpiScale = (float)getDesktopScaleFactor();
    settings.dpiScale = dpiScale;
    settings.overscanScale = 1.45f;

    const float scaledWidth = (float)getWidth() * settings.overscanScale * dpiScale;
    const float scaledHeight = (float)getHeight() * settings.overscanScale * dpiScale;

    settings.width = juce::jmax(1, (int)std::ceil(scaledWidth));
    settings.height = juce::jmax(1, (int)std::ceil(scaledHeight));
    settings.contrast = 1.35f;
    settings.brightness = 1.0f;
    settings.seed = 20240529;

    return settings;
}

void PolyrhythmVizComponent::ensureNebulaCache()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    const auto settings = computeNebulaSettings();

    const bool sizeChanged = settings.width != cachedNebulaSettings.width
        || settings.height != cachedNebulaSettings.height
        || std::abs(settings.dpiScale - cachedNebulaSettings.dpiScale) > 0.001f
        || std::abs(settings.overscanScale - cachedNebulaSettings.overscanScale) > 0.001f;

    const bool paletteChanged = settings.paletteA != cachedNebulaSettings.paletteA
        || settings.paletteB != cachedNebulaSettings.paletteB
        || settings.seed != cachedNebulaSettings.seed
        || std::abs(settings.contrast - cachedNebulaSettings.contrast) > 0.0001f
        || std::abs(settings.brightness - cachedNebulaSettings.brightness) > 0.0001f;

    if (!nebulaImage.isValid() || sizeChanged || paletteChanged)
        requestNebulaRender(settings);
}

void PolyrhythmVizComponent::requestNebulaRender(const NebulaSettings& settings)
{
    if (!settings.isValid())
        return;

    if (nebulaImage.isValid()
        && settings.width == cachedNebulaSettings.width
        && settings.height == cachedNebulaSettings.height
        && std::abs(settings.dpiScale - cachedNebulaSettings.dpiScale) < 0.001f
        && std::abs(settings.overscanScale - cachedNebulaSettings.overscanScale) < 0.001f
        && settings.paletteA == cachedNebulaSettings.paletteA
        && settings.paletteB == cachedNebulaSettings.paletteB
        && settings.seed == cachedNebulaSettings.seed
        && std::abs(settings.contrast - cachedNebulaSettings.contrast) < 0.0001f
        && std::abs(settings.brightness - cachedNebulaSettings.brightness) < 0.0001f)
    {
        return;
    }

    queuedNebulaSettings = settings;

    if (nebulaRenderInProgress.exchange(true))
        return;

    startNebulaRender(settings);
}

void PolyrhythmVizComponent::startNebulaRender(const NebulaSettings& settings)
{
    juce::Component::SafePointer<PolyrhythmVizComponent> safeThis(this);

    std::thread([safeThis, settings]() mutable
    {
        auto image = renderNebulaImage(settings);

        juce::MessageManager::callAsync([safeThis, settings, image = std::move(image)]() mutable
        {
            if (auto* owner = safeThis.getComponent())
            {
                if (owner->shuttingDown.load())
                    return;

                owner->nebulaImage = std::move(image);
                owner->cachedNebulaSettings = settings;
                owner->nebulaRenderInProgress = false;

                if (owner->queuedNebulaSettings.has_value()
                    && (owner->queuedNebulaSettings->width != settings.width
                        || owner->queuedNebulaSettings->height != settings.height
                        || std::abs(owner->queuedNebulaSettings->dpiScale - settings.dpiScale) > 0.001f
                        || std::abs(owner->queuedNebulaSettings->overscanScale - settings.overscanScale) > 0.001f
                        || owner->queuedNebulaSettings->paletteA != settings.paletteA
                        || owner->queuedNebulaSettings->paletteB != settings.paletteB
                        || owner->queuedNebulaSettings->seed != settings.seed
                        || std::abs(owner->queuedNebulaSettings->contrast - settings.contrast) > 0.0001f
                        || std::abs(owner->queuedNebulaSettings->brightness - settings.brightness) > 0.0001f))
                {
                    auto nextSettings = *owner->queuedNebulaSettings;
                    owner->queuedNebulaSettings.reset();
                    owner->requestNebulaRender(nextSettings);
                }
                else
                {
                    owner->queuedNebulaSettings.reset();
                    owner->repaint();
                }
            }
        });
    }).detach();
}

juce::Image PolyrhythmVizComponent::renderNebulaImage(const NebulaSettings& settings)
{
    juce::Image image(juce::Image::ARGB, settings.width, settings.height, true);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);

    static constexpr float kNebulaFloor = 0.0f;
    static constexpr float kContrastPower = 4.0f;

    const float invWidth = 1.0f / (float)settings.width;
    const float invHeight = 1.0f / (float)settings.height;
    const auto centre = juce::Point<float>((float)settings.width * 0.5f, (float)settings.height * 0.5f);
    const float maxRadius = centre.getDistanceFrom({ 0.0f, 0.0f });

    for (int y = 0; y < settings.height; ++y)
    {
        for (int x = 0; x < settings.width; ++x)
        {
            const float nx = (float)x * invWidth;
            const float ny = (float)y * invHeight;

            // Multi-octave value noise for soft nebula texture
            float noise = fbm(nx * 3.6f, ny * 3.6f, (uint32_t)settings.seed);

            // Gentle warp to avoid grid patterns
            const float warp = fbm((nx + 11.3f) * 1.2f, (ny - 7.1f) * 1.2f, (uint32_t)(settings.seed + 17));
            noise = juce::jlimit(0.0f, 1.0f, juce::jmap(warp, 0.25f, 0.75f, noise * 0.8f, noise * 1.2f));

            noise = juce::jmap(noise, 0.0f, 1.0f, kNebulaFloor, 1.0f);
            noise = std::pow(noise, kContrastPower);
            noise = juce::jlimit(0.0f, 1.0f, noise);

            // Soft vignette baked into the texture to keep edges gentle
            const auto p = juce::Point<float>((float)x, (float)y);
            const float dist = p.getDistanceFrom(centre);
            const float edgeFade = juce::jlimit(0.0f, 1.0f, 1.05f - (dist / maxRadius) * 0.75f);

            float intensity = juce::jlimit(0.0f, 1.0f, noise * edgeFade * settings.brightness);
            intensity = std::pow(intensity, settings.contrast);

            const float mix = juce::jlimit(0.0f, 1.0f, intensity * 1.1f);
            auto colour = settings.paletteA.interpolatedWith(settings.paletteB, mix);
            const float alpha = juce::jlimit(0.0f, 1.0f, 0.35f + mix * 0.55f);
            colour = colour.withAlpha(alpha);

            data.setPixelColour(x, y, colour);
        }
    }

    return image;
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

    // Nebula Drift: draw cached, overscanned texture with subtle translation
    if (nebulaDriftEnabledEarly)
    {
        ensureNebulaCache();

        auto cachedImage = nebulaImage;
        if (cachedImage.isValid() && cachedNebulaSettings.isValid())
        {
            const float inverseScale = cachedNebulaSettings.dpiScale > 0.0f
                ? 1.0f / cachedNebulaSettings.dpiScale
                : 1.0f;

            const float destWidth = (float)cachedNebulaSettings.width * inverseScale;
            const float destHeight = (float)cachedNebulaSettings.height * inverseScale;

            juce::Rectangle<float> dest(bounds.getCentreX() - destWidth * 0.5f,
                                        bounds.getCentreY() - destHeight * 0.5f,
                                        destWidth,
                                        destHeight);

            const float marginX = juce::jmax(0.0f, (destWidth - bounds.getWidth()) * 0.5f);
            const float marginY = juce::jmax(0.0f, (destHeight - bounds.getHeight()) * 0.5f);

            const float driftX = std::sin(nebulaTime * 0.018f) * marginX * 0.6f;
            const float driftY = std::cos(nebulaTime * 0.015f + 0.7f) * marginY * 0.6f;
            dest.translate(driftX, driftY);

            const float opacity = juce::jlimit(0.08f, 0.85f, 0.45f * (1.0f + nebulaEnergy * 0.12f));
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.setOpacity(opacity);
            g.drawImage(cachedImage, dest);
            g.setOpacity(1.0f);

            // Soft vignette to keep focus toward the center and hide far edges of overscan
            juce::ColourGradient vignette(juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                         juce::Colours::black.withAlpha(0.20f),
                                         bounds.getCentreX() + bounds.getWidth() * 0.75f,
                                         bounds.getCentreY(), true);
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
        for (const auto& slot : slotVisuals)
        {
            if (!slot.active || slot.arcIntensity < kArcThreshold || !slot.cachedArcGlow.isValid())
                continue;

            if (slot.cachedArcBounds.isEmpty())
                continue;

            juce::Graphics::ScopedSaveState arcSave(g);
            g.setOpacity(juce::jlimit(0.0f, 1.0f, slot.arcIntensity));
            g.drawImage(slot.cachedArcGlow, slot.cachedArcBounds);
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
        slot.cachedArcGlow = {};
        slot.cachedArcBounds = {};
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
    const int64_t nowMs = juce::Time::getMillisecondCounter();
    const float dt = (lastCallbackMs == 0)
        ? 1.0f
        : juce::jlimit(0.0f, 5.0f, (float)(nowMs - lastCallbackMs) * (60.0f / 1000.0f));
    lastCallbackMs = nowMs;

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
    wrapFlash = juce::jmax(0.0f, wrapFlash * std::pow(0.88f, dt) - 0.01f * dt);
    masterPulse = juce::jmax(0.0f, masterPulse * std::pow(0.88f, dt) - 0.01f * dt);  // Same decay as wrapFlash

    // Nebula Drift: advance time for continuous animation (dt-normalised for display-rate independence)
    nebulaTime += dt / 60.0f;

    // Nebula energy: very slow decay (~2-3 second half-life), will be boosted by hits below
    nebulaEnergy = juce::jmax(0.0f, nebulaEnergy * std::pow(0.988f, dt) - 0.001f * dt);

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

    bool electricArcEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerElectricArc"))
        electricArcEnabled = param->load() >= 0.5f;

    bool alternatingRotationEnabled = false;
    if (auto* param = apvts.getRawParameterValue("optVisualizerAlternatingRotation"))
        alternatingRotationEnabled = param->load() >= 0.5f;
    lastAlternatingRotationEnabled = alternatingRotationEnabled;

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
            slot.flash = juce::jmax(0.0f, slot.flash - kFlashDecay * dt);
            slot.arcIntensity = 0.0f;
            slot.lastArcRebuildTime = 0;
            slot.lastArcIntensityQuantized = 0.0f;
            slot.arcGeometryNeedsRebuild = false;
            slot.lastArcRotationAngle = 0.0f;
            slot.cachedArcGlow = {};
            slot.cachedArcBounds = {};
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
            slot.arcGeometryNeedsRebuild = true;
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
            slot.flash = juce::jmax(0.0f, slot.flash - kFlashDecay * dt);
            slot.sweepGain = juce::jmax(0.0f, slot.sweepGain * std::pow(0.88f, dt) - 0.015f * dt);  // Fast decay for sweep boost
        }

        // Decay starlight twinkle brightness for all vertices (even when hit, for independent decay)
        if (starlightTwinkleEnabled && !slot.twinkleBrightness.empty())
        {
            for (auto& brightness : slot.twinkleBrightness)
                brightness = juce::jmax(0.0f, brightness - kTwinkleDecay * dt);
        }

        if (!electricArcEnabled)
        {
            slot.arcIntensity = 0.0f;
            slot.lastArcRebuildTime = 0;
            slot.lastArcIntensityQuantized = 0.0f;
            slot.arcGeometryNeedsRebuild = false;
            slot.lastArcRotationAngle = 0.0f;
            slot.cachedArcGlow = {};
            slot.cachedArcBounds = {};
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

    // Second pass: update electric arc cache using freshly computed rotations and geometry
    if (electricArcEnabled)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& slot = slotVisuals[(size_t)i];
            if (!slot.active)
                continue;

            slot.arcIntensity = juce::jmax(0.0f, slot.arcIntensity - kFlashDecay * dt);

            bool shouldRebuild = false;

            const bool rotationDrifted = alternatingRotationEnabled
                && slot.cachedArcGlow.isValid()
                && std::abs(slot.rotationAngle - slot.lastArcRotationAngle) > 0.0005f;

            if (slot.arcIntensity > kArcCachePersistenceThreshold)
            {
                if (rotationDrifted)
                {
                    // Drop stale geometry before the vertices rotate away from cached endpoints
                    slot.cachedArcGlow = {};
                    slot.cachedArcBounds = {};
                    shouldRebuild = true;
                }

                if (slot.arcGeometryNeedsRebuild)
                {
                    shouldRebuild = true;
                    slot.arcGeometryNeedsRebuild = false;
                }
                else if (nowMs - slot.lastArcRebuildTime >= kArcRebuildIntervalMs)
                {
                    float currentArcIntensityQuantized = std::round(slot.arcIntensity / kArcIntensityDeltaThreshold) * kArcIntensityDeltaThreshold;

                    if (std::fabs(currentArcIntensityQuantized - slot.lastArcIntensityQuantized) > 0.001f)
                    {
                        shouldRebuild = true;
                        slot.lastArcIntensityQuantized = currentArcIntensityQuantized;
                    }
                    else
                    {
                        shouldRebuild = true;
                    }
                }
            }
            else if (slot.cachedArcGlow.isValid())
            {
                slot.cachedArcGlow = juce::Image();
                repaint();
            }

            if (shouldRebuild)
            {
                slot.lastArcIntensityQuantized = std::round(slot.arcIntensity / kArcIntensityDeltaThreshold) * kArcIntensityDeltaThreshold;
                drawElectricArc(i);
                slot.lastArcRebuildTime = nowMs;
                slot.lastArcRotationAngle = slot.rotationAngle;
                repaint();
            }
            else if (slot.arcIntensity > 0.001f && slot.cachedArcGlow.isValid())
            {
                repaint();
            }
        }
    }

    repaint();
}

void PolyrhythmVizComponent::drawElectricArc(int slotIndex)
{
    auto& srcSlot = slotVisuals[(size_t)slotIndex];

    const bool validFlash = srcSlot.flashVertex >= 0 && srcSlot.flashVertex < (int)srcSlot.vertices.size();
    const bool shouldRender = srcSlot.active && srcSlot.arcIntensity >= kArcThreshold && validFlash && !srcSlot.vertices.empty();

    if (!shouldRender)
    {
        srcSlot.cachedArcGlow = {};
        srcSlot.cachedArcBounds = {};
        return;
    }

    std::vector<int> activeSlots;
    activeSlots.reserve(kNumSlots);
    for (int idx = 0; idx < kNumSlots; ++idx)
    {
        if (slotVisuals[(size_t)idx].active && !slotVisuals[(size_t)idx].vertices.empty())
            activeSlots.push_back(idx);
    }

    const int srcIdx = slotIndex;
    const int srcNumVerts = (int)srcSlot.vertices.size();
    if (srcNumVerts < 2)
    {
        srcSlot.cachedArcGlow = {};
        srcSlot.cachedArcBounds = {};
        return;
    }

    std::vector<int> adjacentSlots;
    auto it = std::find(activeSlots.begin(), activeSlots.end(), srcIdx);
    if (it != activeSlots.end())
    {
        if (it != activeSlots.begin())
            adjacentSlots.push_back(*(it - 1));
        if (it + 1 != activeSlots.end())
            adjacentSlots.push_back(*(it + 1));
    }

    if (adjacentSlots.empty())
    {
        srcSlot.cachedArcGlow = {};
        srcSlot.cachedArcBounds = {};
        return;
    }

    auto rotatePointForSlot = [this](const SlotVisual& s, juce::Point<float> p) -> juce::Point<float>
    {
        if (lastAlternatingRotationEnabled && std::abs(s.rotationAngle) > 0.0001f)
        {
            const auto transform = juce::AffineTransform::rotation(s.rotationAngle, s.centre.x, s.centre.y);
            return p.transformedBy(transform);
        }
        return p;
    };

    float maxRadius = 1.0f;
    for (int idx : activeSlots)
        maxRadius = juce::jmax(maxRadius, slotVisuals[(size_t)idx].radius);

    struct ArcStroke
    {
        juce::Path path;
        juce::Colour colour;
        float strokeWidth = 0.0f;
        bool hasGlow = false;
        float glowWidth = 0.0f;
        juce::Colour glowColour;
    };

    std::vector<ArcStroke> strokes;
    strokes.reserve(3);

    juce::Rectangle<float> arcBounds;
    bool hasBounds = false;

    const int numArcs = 1 + (int)(srcSlot.arcIntensity * 2.0f);
    for (int arcNum = 0; arcNum < numArcs; ++arcNum)
    {
        const float arcInt = srcSlot.arcIntensity;
        const float seed1 = std::fmod((float)srcIdx * 0.17f + (float)arcNum * 0.31f + arcInt * 7.3f + (float)masterPhase * 13.7f, 1.0f);
        const float seed2 = std::fmod((float)srcIdx * 0.13f + (float)arcNum * 0.37f + arcInt * 11.9f + (float)masterPhase * 17.3f, 1.0f);
        const float seed3 = std::fmod((float)srcIdx * 0.23f + (float)arcNum * 0.41f + arcInt * 5.7f + (float)masterPhase * 19.1f, 1.0f);

        const int srcVertIdx = (int)(seed3 * (float)srcNumVerts) % srcNumVerts;
        const auto srcPoint = rotatePointForSlot(srcSlot, srcSlot.vertices[(size_t)srcVertIdx]);

        const int targetIdx = adjacentSlots[(size_t)(seed1 * (float)adjacentSlots.size()) % adjacentSlots.size()];
        const auto& targetSlot = slotVisuals[(size_t)targetIdx];
        const int numVerts = (int)targetSlot.vertices.size();
        if (numVerts < 2)
            continue;

        int closestVertIdx = 0;
        float closestDist = std::numeric_limits<float>::max();
        for (int v = 0; v < numVerts; ++v)
        {
            const auto vPoint = rotatePointForSlot(targetSlot, targetSlot.vertices[(size_t)v]);
            const float d = srcPoint.getDistanceFrom(vPoint);
            if (d < closestDist)
            {
                closestDist = d;
                closestVertIdx = v;
            }
        }

        const auto targetPoint = rotatePointForSlot(targetSlot, targetSlot.vertices[(size_t)closestVertIdx]);
        const auto diff = targetPoint - srcPoint;
        const float dist = diff.getDistanceFromOrigin();
        const float distNorm = juce::jlimit(0.0f, 1.0f, dist / juce::jmax(1.0f, maxRadius * 2.0f));

        const float intensity = srcSlot.arcIntensity * (0.6f + seed1 * 0.4f);

        const float hue = 0.50f + seed1 * 0.06f + distNorm * 0.08f;
        const float alphaFade = 1.0f - distNorm * 0.4f;
        const float alpha = kArcAlphaMax * intensity * alphaFade;

        const auto arcColour = juce::Colour::fromHSV(hue, 0.35f + distNorm * 0.15f, 1.0f, alpha);

        juce::Path arcPath;
        arcPath.startNewSubPath(srcPoint);

        const int segments = juce::jmax(2, (int)(dist / 35.0f));

        for (int seg = 1; seg < segments; ++seg)
        {
            const float segT = (float)seg / (float)segments;
            auto midPoint = srcPoint + diff * segT;

            const float jitter = std::sin(seed1 * 6.28f + segT * 4.5f + seed2 * 3.14f) * 10.0f * intensity;
            const auto perpRaw = juce::Point<float>(-diff.y, diff.x);
            const float perpLen = perpRaw.getDistanceFromOrigin();
            if (perpLen > 0.001f)
                midPoint = midPoint + perpRaw * (jitter / perpLen);

            arcPath.lineTo(midPoint);
        }

        arcPath.lineTo(targetPoint);

        ArcStroke stroke{};
        stroke.path = std::move(arcPath);
        stroke.colour = arcColour;
        stroke.strokeWidth = kArcWidth + intensity * 0.6f;

        if (intensity > 0.6f)
        {
            stroke.hasGlow = true;
            stroke.glowWidth = kArcWidth + 2.5f + intensity * 1.5f;
            stroke.glowColour = arcColour.withAlpha(alpha * 0.25f);
        }

        const float strokeMargin = 0.5f * juce::jmax(stroke.strokeWidth, stroke.hasGlow ? stroke.glowWidth : stroke.strokeWidth) + 1.5f;
        const auto pathBounds = stroke.path.getBounds().expanded(strokeMargin);

        if (!hasBounds)
        {
            arcBounds = pathBounds;
            hasBounds = true;
        }
        else
        {
            arcBounds = arcBounds.getUnion(pathBounds);
        }

        strokes.push_back(std::move(stroke));
    }

    if (strokes.empty() || !hasBounds)
    {
        srcSlot.cachedArcGlow = {};
        srcSlot.cachedArcBounds = {};
        return;
    }

    arcBounds = arcBounds.expanded(1.0f);

    const int width = juce::jmax(1, (int)std::ceil(arcBounds.getWidth()));
    const int height = juce::jmax(1, (int)std::ceil(arcBounds.getHeight()));
    srcSlot.cachedArcBounds = { arcBounds.getX(), arcBounds.getY(), (float)width, (float)height };

    srcSlot.cachedArcGlow = juce::Image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(srcSlot.cachedArcGlow);
    g.addTransform(juce::AffineTransform::translation(-arcBounds.getX(), -arcBounds.getY()));

    for (const auto& stroke : strokes)
    {
        const auto& jaggedPath = stroke.path;
        const float maxGlowWidth = stroke.hasGlow ? stroke.glowWidth : juce::jmax(6.0f, stroke.strokeWidth + 2.0f);

        float alpha = srcSlot.arcIntensity * 0.15f;
        juce::Colour arcColour = srcSlot.colour.withHue(srcSlot.colour.getHue()).withSaturation(0.5f).withBrightness(1.0f);
        g.setColour(arcColour.withAlpha(alpha));
        g.strokePath(jaggedPath, juce::PathStrokeType(maxGlowWidth,
                                                      juce::PathStrokeType::JointStyle::mitered,
                                                      juce::PathStrokeType::EndCapStyle::butt));

        alpha = srcSlot.arcIntensity * 0.9f;
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.strokePath(jaggedPath, juce::PathStrokeType(2.0f,
                                                      juce::PathStrokeType::JointStyle::mitered,
                                                      juce::PathStrokeType::EndCapStyle::butt));
    }
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
