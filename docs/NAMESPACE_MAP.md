# Namespace Map

## Proposed public AIDL namespaces

```text
floral.device.display
    IFrameConsumer
    display buffers, frames, and modes

floral.device.display.topology
    IDisplayTopologyState
    topology snapshots and listeners

floral.device.audio
    Floral audio configuration and status contracts

floral.device.operate
    Device-operation contracts when an AIDL surface is required
```

The Android platform contract remains unchanged:

```text
android.hardware.audio@7.0
```

`floral.device.audio` is an auxiliary Floral configuration/status surface; it
does not replace the platform Audio HAL interface.

## Native and repository names

```text
packages_services_FloralDevice  Android-side service and control center
floral_device_service          service binary
libfloral_hal_control          FHC1 transport and framing
libfloral_hal_control_center   configuration command dispatch
hardware_floral_display        HWC HAL
hardware_floral_audio          primary audio HAL
```

Media data-plane libraries keep their `floral.stream` namespace:

```text
floral.stream.codec
floral.stream.session
floral.stream.transport
```

The namespace migration is design-only in this scaffold. Existing source,
frozen AIDL snapshots, manifests, and build files are intentionally untouched.
