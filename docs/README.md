# FloralDevice

[English](#english) | [中文](#中文)

## English

FloralDevice owns container-side media encoding sessions for FloralDroid. The
current milestone connects final display buffers to low-latency H.264 software
or hardware encoding and sends encoded access units to a host Unix socket.

### Modules

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
- `floral_device_service` is the production Binder endpoint and current
  single-display video backend. It activates only while the host video socket
  is available and owns the selected encoder session and reconnect generation.

`floral_stream_codec_tests` covers software MediaCodec selection, EGL surface
input, dynamic bitrate, output integrity, portrait rotation, buffer reuse,
transport, and x86_64 VA-API DRM PRIME/VPP integration.
`floral_device_display_tests` covers cross-process HardwareBuffer ownership,
descriptor validation, topology publication, FHC1 parsing including sensor and
GNSS queries, external-state validation, and control-lease behavior.
`floral_input_protocol_tests_host` covers FDO1 framing, target leases, epochs,
multi-pointer actions, coordinate conversion, and cancellation cleanup.

### Primary display configuration

FloralDevice and Floral HWC share the same logical display properties:

| Property | Default | Valid range | Consumer |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | HWC and video session geometry. |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | HWC and video session geometry. |
| `ro.boot.floral_fps` | `60` | `1`-`60` | HWC refresh selection and encoder frame rate. |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | HWC density only. |

Invalid values fall back to deterministic defaults. Portrait logical displays
may use transposed coded dimensions and an explicit rotation in the FSV2
header; Android input remains in logical-display coordinates.

### Video backend

`ro.boot.floral_video_encoder` selects the encoder. `software` is the default;
`vaapi` selects the FFmpeg hardware backend without automatic software
fallback. `ro.boot.floral_vaapi_device` changes the default
`/dev/dri/renderD128` device.

MediaCodec updates bitrate in place. VA-API drains pending packets and reopens
the codec at the new bitrate while retaining registered display buffers and the
VA device.

### Control and lifecycle

The container listens on the four filesystem endpoints `video.sock`,
`audio.sock`, `control.sock`, and `operate.sock`; the host gateway connects to
each endpoint. The corresponding `ro.boot.floral_*_socket` properties change
their paths. A valid full topology
snapshot owns the external displays until another full snapshot replaces it.

After a disconnect, the service retains external displays for three seconds so
a host process can reconnect without hotplug churn.
`ro.boot.floral_control_disconnect_lease_ms` changes this bounded lease. Lease
expiry removes external displays but never changes the permanent primary.

Socket backpressure never blocks SurfaceFlinger. Software submission remains
synchronous only until EGL has queued the source read and produced its release
fence. VA-API currently waits for VPP completion before returning source-buffer
ownership. The GLES rotation fallback also waits for its intermediate render
before VPP because VA does not accept the Android native fence directly. Codec
output dequeueing remains non-blocking.

FDO1 touch control supports the permanent primary and hotplug external displays.
Encoded video currently consumes the permanent primary display only.

## 中文

FloralDevice 负责 FloralDroid 容器侧的媒体编码会话。当前里程碑将最终显示
缓冲区连接到低延迟 H.264 软件或硬件编码器，并把编码后的访问单元发送到宿主机
Unix Socket。

### 模块

- `libfloral_stream_codec` 提供统一的编码器会话接口和两个明确区分的后端。
  软件后端选择 `OMX.google.h264.encoder`，将缓存的 AHardwareBuffer 绘制到
  MediaCodec 输入 Surface。x86_64 VA-API 后端把缓冲区导出为 DRM PRIME，
  使用 VA 视频处理转换为 NV12，再通过 FFmpeg `h264_vaapi` 编码。
- VA VPP 旋转是硬件快速路径。GPU 不支持所需 VA 旋转时，使用同一套 EGL
  Shader 绘制到一个可复用、尺寸与编码分辨率一致的 AHardwareBuffer，再进入
  VA VPP；回退路径不会为每一帧重新分配缓冲区。
- `libfloral_stream_transport` 实现带版本的宿主视频封装和有界 Unix 流发送器，
  避免 Socket 背压阻塞编码器线程。
- `libfloral_hal_control` 实现双向 FHC1 控制通道、有界帧解析、断线重连和
  控制权租约生命周期。
- `FloralInputService` 负责 FDO1 操作连接、分屏输入租约、多指状态和直接的
  Framework `MotionEvent` 注入，不创建内核输入设备。
- `libfloral_stream_session` 使用单工作线程编排编码器输出、宿主投递、提交
  时间戳和关键帧恢复。
- `floral.device.display` 是最终缓冲区注册及 acquire/release fence 交换使用的
  带版本 system/vendor AIDL 契约。
- `libfloral_device_display_ingress` 把传输来的原生句柄克隆为服务持有的
  AHardwareBuffer，而不是借用 Binder Parcel 文件描述符。
- `libfloral_hal_control_center` 是唯一的拓扑与仿真控制器、只读状态发布者和
  FHC1 命令处理器。
- `floral.device.simulation` 是传感器及 GNSS HAL 使用的稳定 system/vendor
  AIDL 契约。配置变更通过 Binder 回调发送，可选的高频真值为每个 HAL 读取者
  分配独立的同步 FMQ。
- `floral_device_service` 是生产 Binder 端点和当前的单显示器视频后端。只有宿主
  视频 Socket 可用时才激活，并持有所选编码器会话和重连 generation。

`floral_stream_codec_tests` 覆盖软件 MediaCodec 选择、EGL Surface 输入、动态
码率、输出完整性、竖屏旋转、缓冲区复用、传输和 x86_64 VA-API DRM
PRIME/VPP 集成。`floral_device_display_tests` 覆盖跨进程 HardwareBuffer
所有权、描述符校验、拓扑发布、包括传感器与 GNSS 查询在内的 FHC1 解析、外部
状态校验和控制权租约行为。
`floral_input_protocol_tests_host` 覆盖 FDO1 帧解析、目标租约、epoch、多指动作、
坐标转换和取消清理。

### 主屏配置

FloralDevice 与 Floral HWC 共用同一组逻辑显示属性：

| 属性 | 默认值 | 有效范围 | 使用者 |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | HWC 和视频会话几何尺寸。 |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | HWC 和视频会话几何尺寸。 |
| `ro.boot.floral_fps` | `60` | `1`-`60` | HWC 刷新率选择和编码帧率。 |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | 仅供 HWC 设置密度。 |

非法值会回退到确定的默认值。竖屏逻辑显示可以使用宽高转置后的编码尺寸，并在
FSV2 头中携带明确的旋转角度；Android 输入坐标始终使用逻辑显示坐标系。

### 视频后端

`ro.boot.floral_video_encoder` 用于选择编码器。默认值为 `software`；设为
`vaapi` 时选择 FFmpeg 硬件后端，且不会自动回退到软件编码。
`ro.boot.floral_vaapi_device` 可修改默认的 `/dev/dri/renderD128` 设备。

MediaCodec 可以原地更新码率。VA-API 在码率变化时先排空待处理数据包，再使用
新码率重新打开编码器，同时保留已注册的显示缓冲区和 VA 设备。

### 控制与生命周期

容器默认监听 `video.sock`、`audio.sock`、`control.sock` 和 `operate.sock`
四个文件系统端点，宿主网关分别连接这些端点；对应的
`ro.boot.floral_*_socket` 属性可修改路径。一个有效的完整拓扑快照将持有外屏
控制权，直到另一份完整快照替换它。

连接断开后，服务保留外屏三秒，使宿主进程能够在不触发热插拔抖动的情况下重连。
`ro.boot.floral_control_disconnect_lease_ms` 可修改该有界租约。租约到期会
移除外屏，但永远不会修改永久主屏。

Socket 背压不会阻塞 SurfaceFlinger。软件提交仅同步到 EGL 已排入源图像读取并
生成 release fence 为止。VA-API 当前会等待 VPP 完成后再归还源缓冲区所有权。
由于 VA 不能直接接收 Android 原生 fence，GLES 旋转回退也会等待中间渲染完成
后再进入 VPP。编码器输出 dequeue 始终保持非阻塞。

FDO1 触摸控制支持永久主屏和动态外屏；编码视频目前仍只消费永久主屏。
