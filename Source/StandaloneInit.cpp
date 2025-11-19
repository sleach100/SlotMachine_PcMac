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

// Timer-based initializer that starts automatically
// JUCE's Timer mechanism is safe to start during static initialization -
// it will queue up and start firing once the message loop is running
struct StandalonePowerMonitorInitializer : public Timer
{
    int checkCount = 0;

    StandalonePowerMonitorInitializer()
    {
        // Start the timer immediately - JUCE will handle deferred execution safely
        // The timer won't actually fire until the message loop is running
        startTimer(250);  // Check every 250ms (less frequent to reduce overhead)
    }

    void timerCallback() override
    {
        checkCount++;

        // First callback - log that we're starting
        if (checkCount == 1)
        {
            DBG("StandalonePowerMonitor: Timer started, beginning initialization checks...");
        }

        // Stop checking after 40 attempts (10 seconds at 250ms intervals)
        if (checkCount > 40)
        {
            DBG("StandalonePowerMonitor: Failed to initialize after 10 seconds, giving up");
            stopTimer();
            return;
        }

        // Get the standalone plugin holder instance
        auto* holder = StandalonePluginHolder::getInstance();
        if (!holder)
        {
            if (checkCount % 4 == 0)  // Log every 1 second (4 * 250ms)
                DBG("StandalonePowerMonitor: Waiting for StandalonePluginHolder... (attempt " + String(checkCount) + ")");
            return;
        }

        // Try to get the editor from the processor
        auto* processor = holder->processor.get();
        if (!processor)
        {
            if (checkCount % 4 == 0)
                DBG("StandalonePowerMonitor: Waiting for processor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(processor->getActiveEditor());
        if (!editor)
        {
            if (checkCount % 4 == 0)
                DBG("StandalonePowerMonitor: Waiting for editor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* topLevel = editor->getTopLevelComponent();
        if (!topLevel)
        {
            if (checkCount % 4 == 0)
                DBG("StandalonePowerMonitor: Waiting for top level component... (attempt " + String(checkCount) + ")");
            return;
        }

        // Initialize power monitor if not already done and editor is ready
        if (g_powerMonitor == nullptr)
        {
            DBG("StandalonePowerMonitor: All components ready, initializing power monitor...");
            g_powerMonitor = std::make_unique<WindowsPowerMonitor>();
            g_powerMonitor->setReconnectDelayMs(5000);  // 5-second delay after wake
            g_powerMonitor->attachToWindow(editor, &holder->deviceManager);
            DBG("WindowsPowerMonitor successfully initialized and attached to editor");
            stopTimer();  // Stop checking once initialized
        }
    }
};

// Global instance - creates the timer and starts it automatically during static initialization
// JUCE's Timer mechanism safely defers execution until the message loop is running
static StandalonePowerMonitorInitializer g_initializer;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
