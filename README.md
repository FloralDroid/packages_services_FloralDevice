# FloralStream

FloralStream owns container-side media encoding sessions for FloralDroid. The
current milestone connects final display buffers to low-latency H.264 software
or hardware encoding and sends the encoded access units to a host Unix socket.

Current modules:

- `libfloral_stream_codec`: a common encoder session with two explicit
  backends. The software backend selects `OMX.google.h264.encoder` and blits
  cached AHardwareBuffers into its MediaCodec input surface. The x86_64 VA-API
  backend exports buffers as DRM PRIME, converts them to NV12 with VA video
  processing, then encodes with FFmpeg `h264_vaapi`. VA VPP rotation is the
  fast path. GPUs that do not advertise the required rotation use the same EGL
  shader to rotate into one reusable coded-size AHardwareBuffer before VA VPP.
- `libfloral_stream_transport`: versioned host video framing and a bounded Unix
  stream sender that keeps socket backpressure off the encoder thread.
- `libfloral_stream_session`: single-worker encoder-to-host orchestration,
  submission timestamp correlation, and key-frame recovery policy.
- `floral.stream.display`: versioned system/vendor AIDL contract for final
  display buffer registration and acquire/release fence exchange.
- `libfloral_stream_display_ingress`: clones transported native handles into
  service-owned AHardwareBuffers without borrowing Binder parcel descriptors.
- `floral_stream_codec_tests`: software MediaCodec selection, EGL surface
  input, dynamic bitrate, output integrity, rotated portrait buffer reuse,
  host transport, and an x86_64 VA-API DRM PRIME/VPP integration test.
- `floral_stream_display_tests`: cross-process HardwareBuffer import ownership
  and descriptor validation.

- `floral_stream_service`: production Binder endpoint and single-display video
  backend. It stays inactive until the host video socket is available, then
  owns the selected encoder session and reconnect generation.

The service reads `ro.boot.floral_video_encoder`. `software` is the default;
`vaapi` selects the FFmpeg hardware backend without automatic fallback.
`ro.boot.floral_vaapi_device` optionally changes the default
`/dev/dri/renderD128` device. MediaCodec updates bitrate in place. VA-API drains
pending packets and reopens the codec at the new bitrate while retaining the
registered display buffers and VA device.

The service keeps socket backpressure away from SurfaceFlinger. The current
software frame submission remains synchronous only until EGL has queued the
source read and produced its release fence. VA-API submission currently waits
for VPP completion before returning buffer ownership. The GLES rotation
fallback also waits for its intermediate render before starting VPP because VA
does not accept the Android native fence directly. Codec output dequeueing
remains non-blocking.

Audio and input are not part of this milestone.
