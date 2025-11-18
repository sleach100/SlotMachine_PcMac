#include "StandalonePowerMonitorHelper.h"

// Always compile this file since it's in the StandalonePlugin project
// The preprocessor guards in the header ensure this is only called on Windows standalone

#if JUCE_WINDOWS

#include "WindowsPowerMonitor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Include JUCE standalone wrapper to access StandalonePluginHolder
// This file is ONLY compiled in standalone builds
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

namespace StandalonePowerMonitorHelper
{

WindowsPowerMonitor* createAndAttachPowerMonitor(juce::Component* editorComponent)
{
    if (editorComponent == nullptr)
        return nullptr;

    // Get the standalone plugin holder (only available in standalone builds)
    auto* holder = StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return nullptr;

    // Create the power monitor
    auto* monitor = new WindowsPowerMonitor();
    monitor->setReconnectDelayMs(5000);  // 5-second delay after wake
    monitor->attachToWindow(editorComponent, &holder->deviceManager);

    DBG("WindowsPowerMonitor created and attached via StandalonePowerMonitorHelper");

    return monitor;
}

} // namespace StandalonePowerMonitorHelper

#else

// Provide stub implementation for non-Windows builds (shouldn't be called)
namespace StandalonePowerMonitorHelper
{
    WindowsPowerMonitor* createAndAttachPowerMonitor(juce::Component*)
    {
        return nullptr;
    }
}

#endif // JUCE_WINDOWS
