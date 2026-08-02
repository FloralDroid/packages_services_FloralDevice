# Floral Device

[English](#english) | [中文](#中文)

## English

Floral Device owns Android-side device orchestration for FloralDroid. The
current milestone connects final display buffers to low-latency H.264 software
or hardware encoding, receives the AudioFlinger primary mix through FMQ,
encodes it as Opus, publishes display topology state, and sends encoded media
to separate host Unix sockets.

Current modules:

- `libfloral_stream_codec`: a common encoder session with two explicit
  backends. The software backend selects `OMX.google.h264.encoder` and blits
  cached AHardwareBuffers into its MediaCodec input surface. The x86_64 VA-API
  backend exports buffers as DRM PRIME, converts them to NV12 with VA video
  processing, then encodes with FFmpeg `h264_vaapi`. VA VPP rotation is the
  fast path. GPUs that do not advertise the required rotation use the same EGL
  shader to rotate into one reusable coded-size AHardwareBuffer before VA VPP.
- `libfloral_stream_transport`: versioned host video framing and a bounded Unix
  stream sender, plus FSA1 Opus framing and a bounded audio sender. Socket
  backpressure stays off both encoder threads.
- `libfloral_stream_audio`: synchronized FMQ ingestion, 5 ms Opus encoding,
  dynamic bitrate, discontinuity recovery, and the `floral.device.audio`
  Binder service used by the primary Audio HAL.
- `libfloral_hal_control`: the bidirectional FHC1 HAL/device configuration
  channel, bounded framing, reconnect handling, and authority lease lifecycle.
- `libfloral_stream_session`: single-worker encoder-to-host orchestration,
  submission timestamp correlation, and key-frame recovery policy.
- `floral.device.display`: versioned system/vendor AIDL contract for final
  display buffer registration and acquire/release fence exchange.
- `floral.device.display.topology`: read-only topology snapshot and listener
  contract used by the HWC adapter.
- `libfloral_device_display_ingress`: clones transported native handles into
  service-owned AHardwareBuffers without borrowing Binder parcel descriptors.
- `libfloral_hal_control_center`: the HAL/device configuration dispatch layer,
  topology adapter, read-only state publisher, and FHC1 command router.
- `floral_stream_codec_tests`: software MediaCodec selection, EGL surface
  input, dynamic bitrate, output integrity, rotated portrait buffer reuse,
  host transport, and an x86_64 VA-API DRM PRIME/VPP integration test.
- `floral_stream_display_tests`: cross-process HardwareBuffer import
  ownership, descriptor validation, topology state publication, FHC1 parsing,
  and control authority lease behavior.
- `floral_device_service`: production Binder endpoint and single-display video
  backend. It stays inactive until the host video socket is available, then
  owns the selected encoder session and reconnect generation.

The service reads `ro.boot.floral_video_encoder`. `software` is the default;
`vaapi` selects the FFmpeg hardware backend without automatic fallback.
`ro.boot.floral_vaapi_device` optionally changes the default
`/dev/dri/renderD128` device. MediaCodec updates bitrate in place. VA-API drains
pending packets and reopens the codec at the new bitrate while retaining the
registered display buffers and VA device.

FHC1 command `0x0300` changes the active video bitrate, frame rate, or coded
resolution at runtime. Bitrate-only changes remain in the current stream
generation. Frame-rate or resolution changes replace the encoder session,
publish a new generation and decoder access point, and require the display
producer to register its buffers against that generation. Lower frame rates
are enforced before encoder submission rather than being metadata-only.

FHC1 command `0x0200` changes the Opus bitrate from 16 through 512 kbit/s. The
default is 128 kbit/s stereo and can also be set at boot with
`ro.boot.floral_audio_bitrate`. `OPUS_SET_BITRATE` applies the update without
recreating the encoder or changing the FSA1 generation. The audio socket is
`/mnt/vendor/floral_stream/audio.sock` by default and can be changed with
`ro.boot.floral_audio_socket`.

The service connects to the host control endpoint at
`/mnt/vendor/floral_stream/control.sock`. The path can be changed with
`ro.boot.floral_control_socket`. A valid full topology snapshot owns the
external displays until it is replaced. After a disconnect, the service keeps
them for three seconds so a host process can reconnect without hotplug churn;
`ro.boot.floral_control_disconnect_lease_ms` changes that bounded lease.
Lease expiry removes external displays but never changes the permanent primary.

The separate `/mnt/vendor/floral_stream/operate.sock` endpoint is reserved for
the FDO1 device-operation plane. It is intentionally not implemented by this
milestone and is not parsed by the HAL configuration channel.

The service keeps socket backpressure away from SurfaceFlinger. The current
software frame submission remains synchronous only until EGL has queued the
source read and produced its release fence. VA-API submission currently waits
for VPP completion before returning buffer ownership. The GLES rotation
fallback also waits for its intermediate render before starting VPP because VA
does not accept the Android native fence directly. Codec output dequeueing
remains non-blocking.

The primary audio HAL is maintained in `hardware_floral_audio`. It publishes
48 kHz stereo PCM through the `floral.device.audio` AIDL/FMQ boundary and does
not perform encoding or host socket I/O. Device operation input is reserved for
the separate FDO1 channel.

## 中文

Floral Device 负责 FloralDroid 容器内的设备编排。当前实现接收最终显示缓冲区，
使用 MediaCodec 软件后端或 FFmpeg VA-API 硬件后端编码 H.264；同时通过
`floral.device.audio` AIDL/FMQ 接收 AudioFlinger 主混音，并编码为 Opus。视频、
音频、HAL 配置和设备操控分别使用独立的数据面或控制面。

主要模块包括：

- `libfloral_stream_codec`：视频编码会话、MediaCodec 软件后端和 x86_64 VA-API
  硬件后端。
- `libfloral_stream_session`：视频会话、generation、IDR 恢复和运行时参数切换。
- `libfloral_stream_audio`：同步 FMQ 输入、5 毫秒 Opus 编码、动态码率和 FSA1
  音频输出。
- `libfloral_stream_transport`：FSV2/FSA1 封装及有界异步宿主 Socket 发送。
- `libfloral_hal_control` 与 `libfloral_hal_control_center`：FHC1 HAL 配置控制面、
  命令分发和三秒拓扑控制权租约。
- `floral.device.display`、`floral.device.display.topology` 和
  `floral.device.audio`：显示输入、只读拓扑状态和 Audio HAL PCM FMQ 的稳定
  AIDL 边界。
- `floral_device_service`：统一注册上述 Binder 服务并管理媒体会话。

视频默认使用 `ro.boot.floral_video_encoder=software`；设为 `vaapi` 时显式选择
FFmpeg VA-API，不自动降级。`ro.boot.floral_video_bitrate` 设置启动码率，FHC1
命令 `0x0300` 可在运行时调整视频码率、帧率和编码分辨率。其中仅码率原位生效，
其他结构变化会重建会话并递增 generation。

音频固定从 48 kHz、双声道、PCM S16_LE 的主扬声器混音开始，每 240 帧编码一个
5 毫秒 Opus 包。默认码率为 128 kbit/s，可用
`ro.boot.floral_audio_bitrate` 设置启动值，也可通过 FHC1 `0x0200` 调整为
16 至 512 kbit/s。码率通过 `OPUS_SET_BITRATE` 原位更新，不重建编码器，也不
改变 FSA1 generation。

默认宿主端点为：

```text
/mnt/vendor/floral_stream/video.sock
/mnt/vendor/floral_stream/audio.sock
/mnt/vendor/floral_stream/control.sock
/mnt/vendor/floral_stream/operate.sock
```

前三个端点分别承载视频、音频和 HAL 配置。`operate.sock` 保留给 FDO1 触摸、
键盘、鼠标等设备操控，当前里程碑尚未实现。Socket 背压不会阻塞 SurfaceFlinger、
AudioFlinger 或编码线程；队列溢出会丢弃有界旧数据并通过 generation、序列缺口
或 discontinuity 通知宿主恢复。
