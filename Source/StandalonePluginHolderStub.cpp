// Stub implementation of StandalonePluginHolder for non-standalone builds
// This prevents linker errors in VST3/AU/AAX builds

#include <juce_audio_devices/juce_audio_devices.h>

// Only provide stub if NOT building standalone
#if JUCE_WINDOWS && !JUCE_STANDALONE_APPLICATION

// Provide minimal stub to satisfy linker
struct StandalonePluginHolder
{
    static StandalonePluginHolder* getInstance()
    {
        // Return nullptr - this should never be called in non-standalone builds
        // because the code is guarded by #if JUCE_STANDALONE_APPLICATION
        return nullptr;
    }

    juce::AudioDeviceManager deviceManager;
};

#endif
