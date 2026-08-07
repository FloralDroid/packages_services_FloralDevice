# FloralDevice

[English](README.md)

FloralDevice 负责 FloralDroid 容器侧的媒体编码会话。当前里程碑将最终显示
缓冲区连接到低延迟 H.264 软件或硬件编码器，并把编码后的访问单元发送到宿主机
Unix Socket。

## 模块

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
- `floral.device.power`、`floral.device.radio` 和 `floral.device.wifi` 是各自
  模拟服务持有的稳定 system/vendor AIDL 契约；FloralDevice 校验 FHC1 负载并
  转发控制或查询调用。
- `floral_device_service` 是生产 Binder 端点和当前的单显示器视频后端。只有宿主
  视频 Socket 可用时才激活，并持有所选编码器会话和重连 generation。

`floral_stream_codec_tests` 覆盖软件 MediaCodec 选择、EGL Surface 输入、动态
码率、输出完整性、竖屏旋转、缓冲区复用、传输和 x86_64 VA-API DRM
PRIME/VPP 集成。`floral_device_display_tests` 覆盖跨进程 HardwareBuffer
所有权、描述符校验、拓扑发布、包括传感器、GNSS、电源、Radio 与 Wi-Fi 在内的
FHC1 解析、外部状态校验和控制权租约行为。
`floral_input_protocol_tests_host` 覆盖 FDO1 帧解析、目标租约、epoch、多指动作、
坐标转换和取消清理。

## 宿主协议

- [FSV2](protocols/FSV2.md) 传输编码后的视频访问单元。
- [FSA1](protocols/FSA1.md) 传输编码后的音频包。
- [FHC1](protocols/FHC1.md) 承载设备配置和模拟控制。
- [FDO1](protocols/FDO1.md) 承载对时延敏感的设备操作。

## 主屏配置

FloralDevice 与 Floral HWC 共用同一组逻辑显示属性：

| 属性 | 默认值 | 有效范围 | 使用者 |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | HWC 和视频会话几何尺寸。 |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | HWC 和视频会话几何尺寸。 |
| `ro.boot.floral_fps` | `60` | `1`-`60` | HWC 刷新率选择和编码帧率。 |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | 仅供 HWC 设置密度。 |
| `ro.boot.floral_allow_secure_capture` | `0` | `0` 或 `1` | 允许 shell 创建的虚拟屏显示安全但非 DRM 的图层。 |

非法值会回退到确定的默认值。竖屏逻辑显示可以使用宽高转置后的编码尺寸，并在
FSV2 头中携带明确的旋转角度；Android 输入坐标始终使用逻辑显示坐标系。

安全录制默认关闭。传入 `androidboot.floral_allow_secure_capture=1` 后，shell UID
创建的虚拟屏（包括 Android 12 上 scrcpy 使用的虚拟屏）会被提升为安全显示。
该开关不会向普通应用授予此能力，也不会让录制显示合成受保护的 DRM 缓冲区。

## 视频后端

`ro.boot.floral_video_encoder` 用于选择编码器。默认值为 `software`；设为
`vaapi` 时选择 FFmpeg 硬件后端，且不会自动回退到软件编码。
`ro.boot.floral_vaapi_device` 可修改默认的 `/dev/dri/renderD128` 设备。

MediaCodec 可以原地更新码率。VA-API 在码率变化时先排空待处理数据包，再使用
新码率重新打开编码器，同时保留已注册的显示缓冲区和 VA 设备。

## 控制与生命周期

容器默认在 `/ipc/floral_stream` 下监听 `video.sock`、`audio.sock`、
`control.sock` 和 `operate.sock` 四个文件系统端点，宿主网关分别连接这些端点；对应的
`ro.boot.floral_*_socket` 属性可修改路径。一个有效的完整拓扑快照将持有外屏
控制权，直到另一份完整快照替换它。

Docker 只需要把宿主传输目录 bind mount 到 `/ipc/floral_stream`。Android init
会在启动早期整理该目录的共享权限，不需要宿主机查询应用 UID 后再执行 `chown`。

```text
-v /root/floral/instance-01:/ipc/floral_stream:rw
```

连接断开后，服务保留外屏三秒，使宿主进程能够在不触发热插拔抖动的情况下重连。
`ro.boot.floral_control_disconnect_lease_ms` 可修改该有界租约。租约到期会
移除外屏，但永远不会修改永久主屏。

Socket 背压不会阻塞 SurfaceFlinger。软件提交仅同步到源缓冲区可安全复用为止。
software gralloc 路径会映射共享内存 client target，并上传到持久 GLES 纹理；
其他 gralloc 后端仍使用 EGLImage 导入。VA-API 当前会等待 VPP 完成后再归还源
缓冲区所有权。由于 VA 不能直接接收 Android 原生 fence，GLES 旋转回退也会等待
中间渲染完成后再进入 VPP。编码器输出 dequeue 始终保持非阻塞。

FDO1 触摸控制支持永久主屏和动态外屏；编码视频目前仍只消费永久主屏。
