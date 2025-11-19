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
struct StandalonePowerMonitorInitializer : public Timer, public DeletedAtShutdown
{
    int checkCount = 0;

    StandalonePowerMonitorInitializer()
    {
        // Don't start timer in constructor - JUCE message manager may not be running yet
    }

    void startChecking()
    {
        DBG("StandalonePowerMonitorInitializer: Starting initialization checks");
        startTimer(100);  // Check every 100ms
    }

    void timerCallback() override
    {
        checkCount++;

        // Stop checking after 100 attempts (10 seconds)
        if (checkCount > 100)
        {
            DBG("StandalonePowerMonitor: Failed to initialize after 10 seconds, giving up");
            stopTimer();
            return;
        }

        // Get the standalone plugin holder instance
        auto* holder = StandalonePluginHolder::getInstance();
        if (!holder)
        {
            if (checkCount % 10 == 0)  // Log every 1 second
                DBG("StandalonePowerMonitor: Waiting for StandalonePluginHolder... (attempt " + String(checkCount) + ")");
            return;
        }

        // Try to get the editor from the processor
        auto* processor = holder->processor.get();
        if (!processor)
        {
            if (checkCount % 10 == 0)
                DBG("StandalonePowerMonitor: Waiting for processor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(processor->getActiveEditor());
        if (!editor)
        {
            if (checkCount % 10 == 0)
                DBG("StandalonePowerMonitor: Waiting for editor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* topLevel = editor->getTopLevelComponent();
        if (!topLevel)
        {
            if (checkCount % 10 == 0)
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandalonePowerMonitorInitializer)
};

// Start initialization after JUCE is fully running
// This uses JUCE's initialization callback mechanism
struct PowerMonitorStarter : public CallbackMessage
{
    void messageCallback() override
    {
        DBG("PowerMonitorStarter: JUCE message thread is running, creating initializer");
        auto* initializer = new StandalonePowerMonitorInitializer();
        initializer->startChecking();
    }
};

// Launcher that safely starts after static initialization
struct PowerMonitorLauncher
{
    PowerMonitorLauncher()
    {
        // Post a message to start initialization once the message loop is running
        // This is safe to call even if the message manager isn't running yet
        (new PowerMonitorStarter())->post();
    }
};

// Global launcher - this is safe to construct at static init time
static PowerMonitorLauncher g_launcher;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
