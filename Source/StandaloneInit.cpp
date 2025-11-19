// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"

// Include the standalone filter app header which defines StandalonePluginHolder
#define Point JUCEPoint  // Avoid conflicts with Windows Point
#define JUCE_USE_WINRT_MIDI 0
#include "../../JuceLibraryCode/JuceHeader.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterApp.h>
#undef Point

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

using namespace juce;

// Global power monitor instance
static std::unique_ptr<WindowsPowerMonitor> g_powerMonitor;

// Timer to check for editor readiness and initialize power monitor
struct StandalonePowerMonitorInitializer : public Timer
{
    StandalonePowerMonitorInitializer()
    {
        startTimer(100);  // Check every 100ms
    }

    void timerCallback() override
    {
        // Get the standalone plugin holder instance
        if (auto* holder = StandalonePluginHolder::getInstance())
        {
            // Try to get the main window
            if (auto* window = dynamic_cast<StandaloneFilterWindow*>(holder->getTopLevelComponent()))
            {
                // Try to get the editor from the processor
                if (auto* processor = holder->processor.get())
                {
                    if (auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(processor->getActiveEditor()))
                    {
                        // Initialize power monitor if not already done
                        if (g_powerMonitor == nullptr && editor->getTopLevelComponent() != nullptr)
                        {
                            g_powerMonitor = std::make_unique<WindowsPowerMonitor>();
                            g_powerMonitor->setReconnectDelayMs(5000);  // 5-second delay after wake
                            g_powerMonitor->attachToWindow(editor, &holder->deviceManager);
                            DBG("WindowsPowerMonitor initialized and attached");
                            stopTimer();  // Stop checking once initialized
                        }
                    }
                }
            }
        }
    }
};

// Global instance that starts checking
static StandalonePowerMonitorInitializer g_initializer;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
