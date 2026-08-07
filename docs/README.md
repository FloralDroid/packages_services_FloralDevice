# FloralDevice

[简体中文](README.zh-CN.md)

FloralDevice owns container-side media encoding sessions for FloralDroid. The
current milestone connects final display buffers to low-latency H.264 software
or hardware encoding and sends encoded access units to a host Unix socket.

## Modules

- `libfloral_stream_codec` provides one encoder-session interface with two
  explicit backends. The software backend selects
  `OMX.google.h264.encoder` and blits cached AHardwareBuffers into its
  MediaCodec input surface. The x86_64 VA-API backend exports buffers as DRM
  PRIME, converts them to NV12 with VA video processing, and encodes with
  FFmpeg `h264_vaapi`.
- VA VPP rotation is the hardware fast path. A GPU without the required VA
  rotation uses the same EGL shader to draw into one reusable coded-size
  AHardwareBuffer before VA VPP. The fallback does not allocate a new buffer
  for every frame.
- `libfloral_stream_transport` implements versioned host-video framing and a
  bounded Unix stream sender that keeps socket backpressure off the encoder
  thread.
- `libfloral_hal_control` implements the bidirectional FHC1 control channel,
  bounded framing, reconnect handling, and authority-lease lifecycle.
- `FloralInputService` owns the FDO1 operation connection, per-display input
  leases, multi-pointer state, and direct Framework `MotionEvent` injection.
  It does not create a kernel input device.
- `libfloral_stream_session` uses one worker to orchestrate encoder output,
  host delivery, submission timestamps, and key-frame recovery.
- `floral.device.display` is the versioned system/vendor AIDL contract for
  final-buffer registration and acquire/release fence exchange.
- `libfloral_device_display_ingress` clones transported native handles into
  service-owned AHardwareBuffers instead of borrowing Binder parcel file
  descriptors.
- `libfloral_hal_control_center` is the single topology controller,
  simulation controller, read-only state publisher, and FHC1 command handler.
- `floral.device.simulation` is the stable system/vendor AIDL contract used by
  the sensor and GNSS HALs. Configuration changes use Binder callbacks, while
  optional high-rate ground truth uses one independent synchronized FMQ per
  HAL reader.
- `floral.device.power`, `floral.device.radio`, and `floral.device.wifi` are
  stable system/vendor AIDL contracts owned by their simulation services.
  FloralDevice validates FHC1 payloads and forwards control or query calls.
- `floral_device_service` is the production Binder endpoint and current
  single-display video backend. It activates only while the host video socket
  is available and owns the selected encoder session and reconnect generation.

`floral_stream_codec_tests` covers software MediaCodec selection, EGL surface
input, dynamic bitrate, output integrity, portrait rotation, buffer reuse,
transport, and x86_64 VA-API DRM PRIME/VPP integration.
`floral_device_display_tests` covers cross-process HardwareBuffer ownership,
descriptor validation, topology publication, FHC1 parsing including sensor and
GNSS queries, power, Radio, and Wi-Fi control, external-state validation, and
control-lease behavior.
`floral_input_protocol_tests_host` covers FDO1 framing, target leases, epochs,
multi-pointer actions, coordinate conversion, and cancellation cleanup.

## Host protocols

- [FSV2](protocols/FSV2.md) transports encoded video access units.
- [FSA1](protocols/FSA1.md) transports encoded audio packets.
- [FHC1](protocols/FHC1.md) carries device configuration and simulation control.
- [FDO1](protocols/FDO1.md) carries latency-sensitive device operations.

## Primary display configuration

FloralDevice and Floral HWC share the same logical display properties:

| Property | Default | Valid range | Consumer |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | HWC and video session geometry. |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | HWC and video session geometry. |
| `ro.boot.floral_fps` | `60` | `1`-`60` | HWC refresh selection and encoder frame rate. |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | HWC density only. |
| `ro.boot.floral_allow_secure_capture` | `0` | `0` or `1` | Allow shell-owned virtual displays to show secure, non-DRM layers. |

Invalid values fall back to deterministic defaults. Portrait logical displays
may use transposed coded dimensions and an explicit rotation in the FSV2
header; Android input remains in logical-display coordinates.

Secure capture is disabled by default. Passing
`androidboot.floral_allow_secure_capture=1` promotes virtual displays created by
the shell UID, including the display used by scrcpy on Android 12, to secure
displays. It does not grant this capability to applications and does not enable
protected-buffer composition on those recording displays.

## Video backend

`ro.boot.floral_video_encoder` selects the encoder. `software` is the default;
`vaapi` selects the FFmpeg hardware backend without automatic software
fallback. `ro.boot.floral_vaapi_device` changes the default
`/dev/dri/renderD128` device.

MediaCodec updates bitrate in place. VA-API drains pending packets and reopens
the codec at the new bitrate while retaining registered display buffers and the
VA device.

## Control and lifecycle

The container listens on the four filesystem endpoints `video.sock`,
`audio.sock`, `control.sock`, and `operate.sock` below
`/ipc/floral_stream`; the host gateway connects to each endpoint. Docker should
bind the host transport directory to `/ipc/floral_stream`. The corresponding
`ro.boot.floral_*_socket` properties change their paths. A valid full topology
snapshot owns the external displays until another full snapshot replaces it.

```text
-v /root/floral/instance-01:/ipc/floral_stream:rw
```

After a disconnect, the service retains external displays for three seconds so
a host process can reconnect without hotplug churn.
`ro.boot.floral_control_disconnect_lease_ms` changes this bounded lease. Lease
expiry removes external displays but never changes the permanent primary.

Socket backpressure never blocks SurfaceFlinger. Software submission remains
synchronous only until the source is safe to reuse. The software gralloc path
maps its shared-memory client target and uploads it to a persistent GLES
texture; other gralloc backends keep the EGLImage import path. VA-API currently
waits for VPP completion before returning source-buffer ownership. The GLES
rotation fallback also waits for its intermediate render before VPP because VA
does not accept the Android native fence directly. Codec output dequeueing
remains non-blocking.

FDO1 touch control supports the permanent primary and hotplug external displays.
Encoded video currently consumes the permanent primary display only.
