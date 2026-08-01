# Control Surfaces

## Socket ownership

The host gateway owns the pathname sockets with `bind()` and `listen()`.
Android-side services connect to them after the runtime bind mount is ready.
The socket nodes are runtime kernel endpoints, not image files.

```text
/mnt/vendor/floral_stream/control.sock
/mnt/vendor/floral_stream/operate.sock
/mnt/vendor/floral_stream/video/<stream_id>.sock
/mnt/vendor/floral_stream/audio.sock
```

For multiple containers, every container receives the same paths inside its
own mount namespace. The host directory is unique per `instance_id`.

## FHC1 control plane

FHC1 is the only configuration entry point. Initial command families are:

```text
HELLO
GET_CAPABILITIES
GET_STATUS
SET_DISPLAY_TOPOLOGY
SET_DISPLAY_MODE
SET_AUDIO_PROFILE
SET_VIDEO_ENCODER_CONFIG
REQUEST_VIDEO_KEYFRAME
RESET_DEVICE_SESSION
```

`SET_DISPLAY_TOPOLOGY` remains a complete desired external-display snapshot.
The primary display is not removable through this command. The existing
bounded disconnect lease remains a property of the topology authority.

## FDO1 operation plane

FDO1 is separate from configuration. Initial operation families are:

```text
TOUCH
KEY
MOUSE
GESTURE
FORCE_DISPLAY_HOTPLUG
RESET_DISPLAY
```

Operation messages use their own bounded queue and rate limits. They do not
modify the persistent HAL configuration unless a future command explicitly
defines that behavior.

## Not in this scaffold

- No command payloads are frozen here.
- No AIDL interface is generated here.
- No socket listener or connector is implemented here.
- No input injection backend is selected here.
