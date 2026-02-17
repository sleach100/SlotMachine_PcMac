// Standalone-specific initialization code for macOS
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX.
// It is an Objective-C++ (.mm) file because it uses NSWorkspace notification APIs
// that are not accessible from plain C++.

#include "PluginEditor.h"
#include "../JuceLibraryCode/JuceHeader.h"

#if JUCE_MAC && JUCE_STANDALONE_APPLICATION

#import <AppKit/AppKit.h>

// Check if we can include the JUCE standalone header (same guard as StandaloneInit.cpp)
#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
    #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
    #define JUCE_MAC_STANDALONE_HEADER_FOUND 1
#else
    #define JUCE_MAC_STANDALONE_HEADER_FOUND 0

    // Forward declaration fallback if the header is not available
    class StandalonePluginHolder
    {
    public:
        static StandalonePluginHolder* getInstance();
        juce::AudioDeviceManager deviceManager;
        std::unique_ptr<juce::AudioProcessor> processor;
    };
#endif

// ---------------------------------------------------------------------------
// MacSleepWakeObserver
//
// Objective-C class that registers with NSWorkspace to receive
// NSWorkspaceWillSleepNotification and NSWorkspaceDidWakeNotification.
// On sleep: saves the active AudioDeviceManager setup then closes the device
// so CoreAudio isn't left with a dangling stream across sleep.
// On wake:  schedules a one-shot NSTimer (default 5 s, same as Windows) so
// the OS has time to re-enumerate audio hardware before we try to reopen it.
// ---------------------------------------------------------------------------
@interface MacSleepWakeObserver : NSObject
{
    // C++ members stored as ivars (Objective-C++ allows this with ARC)
    juce::AudioDeviceManager*                  _deviceManager;    // not owned
    juce::AudioDeviceManager::AudioDeviceSetup _savedSetup;
    BOOL                                        _hasStoredSetup;
    int                                         _reconnectDelayMs;
    NSTimer*                                    _reconnectTimer;
}
- (instancetype)initWithDeviceManager:(juce::AudioDeviceManager*)dm;
@end

@implementation MacSleepWakeObserver

- (instancetype)initWithDeviceManager:(juce::AudioDeviceManager*)dm
{
    if ((self = [super init]))
    {
        _deviceManager    = dm;
        _hasStoredSetup   = NO;
        _reconnectDelayMs = 5000;  // Match Windows reconnect delay

        NSNotificationCenter* wsnc = [[NSWorkspace sharedWorkspace] notificationCenter];
        [wsnc addObserver:self
                 selector:@selector(systemWillSleep:)
                     name:NSWorkspaceWillSleepNotification
                   object:nil];
        [wsnc addObserver:self
                 selector:@selector(systemDidWake:)
                     name:NSWorkspaceDidWakeNotification
                   object:nil];

        DBG("[MacSleepWakeMonitor] Observer registered with NSWorkspace notification centre");
    }
    return self;
}

- (void)dealloc
{
    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
    [_reconnectTimer invalidate];
    // ARC: do NOT call [super dealloc]; it is inserted automatically
}

// -------------------------------------------------------------------------
// Sleep handler
// -------------------------------------------------------------------------
- (void)systemWillSleep:(NSNotification*)note
{
    juce::ignoreUnused(note);
    DBG("[MacSleepWakeMonitor] NSWorkspaceWillSleepNotification received — "
        "saving and closing audio device");

    if (_deviceManager != nullptr)
    {
        _deviceManager->getAudioDeviceSetup(_savedSetup);
        _hasStoredSetup = YES;
        DBG("[MacSleepWakeMonitor] Saved device: " + _savedSetup.outputDeviceName);

        // Close the audio device so CoreAudio isn't holding an active stream
        // across the sleep transition (mirrors Windows behaviour).
        _deviceManager->closeAudioDevice();
        DBG("[MacSleepWakeMonitor] Audio device closed before sleep");
    }
    else
    {
        DBG("[MacSleepWakeMonitor] Warning: AudioDeviceManager unavailable at sleep");
    }
}

// -------------------------------------------------------------------------
// Wake handler
// -------------------------------------------------------------------------
- (void)systemDidWake:(NSNotification*)note
{
    juce::ignoreUnused(note);
    DBG("[MacSleepWakeMonitor] NSWorkspaceDidWakeNotification received — "
        "scheduling audio reconnect in "
        + juce::String(_reconnectDelayMs) + " ms");

    // Cancel any outstanding reconnect attempt before scheduling a new one
    [_reconnectTimer invalidate];
    _reconnectTimer = [NSTimer scheduledTimerWithTimeInterval:(_reconnectDelayMs / 1000.0)
                                                        target:self
                                                      selector:@selector(reconnectAudio)
                                                      userInfo:nil
                                                       repeats:NO];
}

// -------------------------------------------------------------------------
// Delayed reconnect (fired by the NSTimer after wake)
// -------------------------------------------------------------------------
- (void)reconnectAudio
{
    _reconnectTimer = nil;

    if (_deviceManager == nullptr)
    {
        DBG("[MacSleepWakeMonitor] Error: AudioDeviceManager unavailable at reconnect");
        return;
    }

    if (!_hasStoredSetup)
    {
        DBG("[MacSleepWakeMonitor] Warning: No stored audio setup to restore");
        return;
    }

    DBG("[MacSleepWakeMonitor] Reconnecting: " + _savedSetup.outputDeviceName);
    juce::String error = _deviceManager->setAudioDeviceSetup(_savedSetup, true);

    if (error.isEmpty())
    {
        DBG("[MacSleepWakeMonitor] Audio device reconnection succeeded");
        if (auto* dev = _deviceManager->getCurrentAudioDevice())
            DBG("[MacSleepWakeMonitor] Active device: " + dev->getName()
                + " @ " + juce::String(dev->getCurrentSampleRate()) + " Hz, "
                + juce::String(dev->getCurrentBufferSizeSamples()) + " samples");
    }
    else
    {
        DBG("[MacSleepWakeMonitor] Reconnection failed (" + error
            + ") — falling back to default device");

        juce::String fallbackError = _deviceManager->initialiseWithDefaultDevices(2, 2);
        if (fallbackError.isEmpty())
            DBG("[MacSleepWakeMonitor] Fallback to default device succeeded");
        else
            DBG("[MacSleepWakeMonitor] Fallback also failed: " + fallbackError);
    }
}

@end // MacSleepWakeObserver

// ---------------------------------------------------------------------------
// Global observer handle
// ---------------------------------------------------------------------------
static MacSleepWakeObserver* g_macSleepWakeObserver = nil;

// ---------------------------------------------------------------------------
// Poll timer: waits for StandalonePluginHolder to become available
// (mirrors StandalonePowerMonitorInitializer in StandaloneInit.cpp)
// ---------------------------------------------------------------------------
struct MacStandaloneInitializer : public juce::Timer
{
    SlotMachineAudioProcessorEditor* editor;
    int checkCount = 0;

    explicit MacStandaloneInitializer(SlotMachineAudioProcessorEditor* ed)
        : editor(ed)
    {
        DBG("[MacSleepWakeMonitor] Initializer created, starting poll timer");
        startTimer(250);  // Check every 250 ms
    }

    void timerCallback() override
    {
        ++checkCount;

        // Give up after 40 attempts (~10 s) to avoid lingering if init fails
        if (checkCount > 40)
        {
            DBG("[MacSleepWakeMonitor] Failed to initialize after 10 s, giving up");
            stopTimer();
            delete this;
            return;
        }

        auto* holder = StandalonePluginHolder::getInstance();
        if (!holder)
        {
            if (checkCount % 4 == 0)
                DBG("[MacSleepWakeMonitor] Waiting for StandalonePluginHolder… "
                    "(attempt " + juce::String(checkCount) + ")");
            return;
        }

        if (editor->getTopLevelComponent() == nullptr)
        {
            if (checkCount % 4 == 0)
                DBG("[MacSleepWakeMonitor] Waiting for top-level component… "
                    "(attempt " + juce::String(checkCount) + ")");
            return;
        }

        if (g_macSleepWakeObserver == nil)
        {
            DBG("[MacSleepWakeMonitor] All components ready — attaching observer");
            g_macSleepWakeObserver =
                [[MacSleepWakeObserver alloc] initWithDeviceManager:&holder->deviceManager];
        }

        stopTimer();
        delete this;
    }
};

// ---------------------------------------------------------------------------
// Public API — called from PluginEditor.cpp (mirrors StandaloneInit.cpp API)
// ---------------------------------------------------------------------------

void initializeMacStandalonePowerMonitor(SlotMachineAudioProcessorEditor* editor)
{
    static bool initialized = false;
    if (initialized || editor == nullptr)
        return;

    initialized = true;
    DBG("[MacSleepWakeMonitor] initializeMacStandalonePowerMonitor called");

    // The initializer deletes itself once the observer is successfully attached
    new MacStandaloneInitializer(editor);
}

void shutdownMacStandalonePowerMonitor()
{
    if (g_macSleepWakeObserver != nil)
    {
        DBG("[MacSleepWakeMonitor] Shutting down sleep/wake observer");
        // ARC: setting to nil releases the object and dealloc unregisters notifications
        g_macSleepWakeObserver = nil;
    }
}

#endif // JUCE_MAC && JUCE_STANDALONE_APPLICATION
