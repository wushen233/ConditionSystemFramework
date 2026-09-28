#pragma once

// CSF-to-NativeVisualEffectsFramework integration seam.
// The implementation is supplied by the CSF runtime and forwards the event
// through NativeVisualEffectsFramework's public F4SE interface. Energy-weapon
// fault handling uses this entry point to play the configured weapon effect.
namespace NVFXIntegration
{
    bool PlayConfiguredEvent(const char* a_eventName);
}
