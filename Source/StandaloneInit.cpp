// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

// Global power monitor instance
static std::unique_ptr<WindowsPowerMonitor> g_powerMonitor;

// Hook to initialize power monitor when the editor is created
struct StandalonePowerMonitorInitializer : public juce::Timer
{
    StandalonePowerMonitorInitializer()
    {
        // Start a timer to check for the editor and initialize
        startTimer(100);
    }

    void timerCallback() override
    {
        // Try to get the standalone plugin holder (it's in global namespace, not juce::)
        if (auto* holder = StandalonePluginHolder::getInstance())
        {
            // Try to get the editor component
            if (auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(holder->editor.get()))
            {
                // Initialize power monitor if not already done
                if (g_powerMonitor == nullptr && editor->getTopLevelComponent() != nullptr)
                {
                    g_powerMonitor = std::make_unique<WindowsPowerMonitor>();
                    g_powerMonitor->setReconnectDelayMs(5000);  // 5-second delay after wake
                    g_powerMonitor->attachToWindow(editor, &holder->deviceManager);
                    DBG("WindowsPowerMonitor initialized and attached via StandaloneInit");
                    stopTimer();  // Stop checking once initialized
                }
            }
        }
    }
};

// Global instance that starts the initialization process
static StandalonePowerMonitorInitializer g_initializer;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
