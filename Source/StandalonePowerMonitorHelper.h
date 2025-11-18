#pragma once

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

#include <juce_audio_devices/juce_audio_devices.h>

// Forward declarations
class WindowsPowerMonitor;
namespace juce { class Component; }

/**
 * Helper to initialize WindowsPowerMonitor in standalone builds.
 * This is separated into its own file to avoid linker issues with
 * StandalonePluginHolder in shared code.
 */
namespace StandalonePowerMonitorHelper
{
    /**
     * Attempts to create and attach a WindowsPowerMonitor if running standalone.
     * Returns the created monitor, or nullptr if not in standalone mode or if creation failed.
     */
    WindowsPowerMonitor* createAndAttachPowerMonitor(juce::Component* editorComponent);
}

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
