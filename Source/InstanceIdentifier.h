#pragma once

#include <juce_core/juce_core.h>

namespace InstanceIdentifier
{
    /**
     * Get or create a unique identifier for this machine installation.
     * The ID is persistent across plugin sessions and is stored in the registry.
     * Used to track license activations on specific machines.
     */
    juce::String getOrCreateInstanceID();

    /**
     * Clear the stored instance ID (used during deactivation).
     */
    void clearInstanceID();

} // namespace InstanceIdentifier
