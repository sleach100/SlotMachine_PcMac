// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/JuceHeader.h"

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

using namespace juce;

// IMPORTANT: StandalonePluginHolder is in the GLOBAL namespace, not juce::
// Based on linker errors, the actual JUCE class is: "class StandalonePluginHolder"
// We declare it here to match. The actual definition comes from JUCE's compiled code.
class StandalonePluginHolder
{
public:
    static StandalonePluginHolder* getInstance();

    // Public members that we need to access
    // These MUST match the actual JUCE definition exactly
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioProcessor> processor;
};

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
            // Try to get the editor from the processor
            if (auto* processor = holder->processor.get())
            {
                if (auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(processor->getActiveEditor()))
                {
                    // Initialize power monitor if not already done and editor is ready
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
};

// Global instance that starts checking
static StandalonePowerMonitorInitializer g_initializer;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
