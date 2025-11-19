// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/JuceHeader.h"

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

using namespace juce;

// Check if we can include the JUCE standalone header
// If this fails, please add "C:\JUCE\modules\juce_audio_plugin_client" to your include path
#if defined(_MSC_VER)
    #pragma message("Attempting to locate juce_audio_plugin_client module...")
#endif

// Try including with different possible paths
#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
    #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
    #define JUCE_STANDALONE_HEADER_FOUND 1
#else
    // Fall back to forward declaration if header not found
    #define JUCE_STANDALONE_HEADER_FOUND 0
    #if defined(_MSC_VER)
        #pragma message("Warning: Could not find JUCE standalone header, using forward declaration")
        #pragma message("This may cause linker errors. Consider adding the JUCE module path to include directories.")
    #endif

    // Forward declaration approach (may not link correctly)
    class StandalonePluginHolder
    {
    public:
        static StandalonePluginHolder* getInstance();
        juce::AudioDeviceManager deviceManager;
        std::unique_ptr<juce::AudioProcessor> processor;
    };
#endif

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
