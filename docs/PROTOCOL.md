# Floral Device Host Protocols / Floral Device 宿主协议

The video channel is a Unix `SOCK_STREAM`. The container listens at
`/mnt/vendor/floral_stream/video.sock` by default and the host connects to it.
Every H.264 access unit starts with a fixed 80-byte header followed by exactly
`payload_size` bytes. Integer fields use network byte order. C or C++ structure
layout is not part of the protocol.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `FSV2` |
| 4 | 2 | Protocol version, currently `2` |
| 6 | 2 | Header size, currently `80` |
| 8 | 4 | Stream id |
| 12 | 4 | Stream generation |
| 16 | 8 | Packet sequence |
| 24 | 8 | Presentation timestamp in microseconds |
| 32 | 4 | Packet flags |
| 36 | 4 | Codec id, `1` for H.264 |
| 40 | 4 | Coded width passed to the video encoder |
| 44 | 4 | Coded height passed to the video encoder |
| 48 | 4 | Android logical display width |
| 52 | 4 | Android logical display height |
| 56 | 4 | Payload size |
| 60 | 4 | Clockwise display rotation in degrees: `0`, `90`, `180`, or `270` |
| 64 | 8 | Frame submission `CLOCK_MONOTONIC` timestamp in nanoseconds, or zero |
| 72 | 8 | Reserved, must be zero |

The receiver applies `display_rotation` clockwise to decoded coded pixels to
recover the Android logical display orientation. For example, a logical
`720x1280` display is sent as a coded `1280x720` stream with rotation `90`.
The container performs the inverse counter-clockwise rotation while drawing the
final display buffer into the MediaCodec input surface. That rotation is fused
into the existing EGL draw and does not add another full-frame copy.

Logical dimensions remain the coordinate space for Android input. Coded
dimensions only describe the decoder output and must not be used directly for
touch coordinates. A `90` or `270` degree stream has a transposed coded aspect;
the other rotations retain the logical aspect.

Packet flags identify codec configuration, key frames, end of stream, and
discontinuities. A resolution change increments the stream generation and is
followed by codec configuration and a key frame for the new generation.

The frame submission timestamp is captured when the stream session accepts an
input frame. The host records `CLOCK_MONOTONIC` after receiving the complete
payload and subtracts the submission timestamp to measure submission-to-host
latency. Codec configuration and end-of-stream packets without an input frame
use zero. The container and host monotonic clock domains must be verified before
these values are compared directly.

`HostVideoSink` owns a bounded asynchronous queue. Queue saturation is reported
to the caller instead of blocking the encoder thread. The session manager is
responsible for dropping dependent frames, requesting an IDR, and marking the
next recoverable packet as a discontinuity.

## FSA1 encoded audio channel / FSA1 编码音频通道

The audio channel is a separate Unix `SOCK_STREAM`. The container listens at
`/mnt/vendor/floral_stream/audio.sock` by default and the host connects to it.
Every Opus packet starts with a fixed 64-byte header followed by exactly
`payload_size` bytes. Integer fields use network byte order. One packet
represents 240 stereo samples per channel at 48 kHz, or 5 milliseconds. C or
C++ structure layout is not part of the protocol.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `FSA1` |
| 4 | 2 | Protocol version, currently `1` |
| 6 | 2 | Header size, currently `64` |
| 8 | 4 | Stream id |
| 12 | 4 | Stream generation |
| 16 | 8 | Packet sequence within the generation |
| 24 | 8 | First sample `CLOCK_MONOTONIC` presentation time in nanoseconds |
| 32 | 4 | Sample rate, currently `48000` |
| 36 | 2 | Channel count, currently `2` |
| 38 | 2 | Codec id, `1` for Opus; `2` is reserved for diagnostic PCM S16_LE |
| 40 | 4 | PCM frame count represented by this packet, currently `240` |
| 44 | 4 | Encoded payload size, at most `1275` bytes |
| 48 | 4 | Packet flags |
| 52 | 4 | Reserved, must be zero |
| 56 | 8 | First frame position since the HAL output opened |

Flag bit 0 marks the first delivered packet of a generation. Bit 1 marks a
discontinuity caused by stream start, FMQ or socket queue overflow, service
reconnection, or an input gap. Bit 2 is reserved for an explicit stream-end
packet. A bitrate-only update does not change the generation and requires no
decoder reconfiguration. The receiver uses sequence gaps with Opus PLC or a
short silence interval; it must not accumulate old packets to repair latency.

音频使用独立 Unix `SOCK_STREAM`。每个 Opus 包由固定 64 字节大端协议头和紧随其后
的 `payload_size` 字节负载组成。每包表示 48 kHz 双声道每声道 240 个采样，即
5 毫秒。bit 0 表示 generation 首包，bit 1 表示输入、FMQ、宿主队列或重连造成的
不连续，bit 2 保留给显式流结束。仅修改码率不会改变 generation，也不要求解码器
重新配置；接收端应使用 Opus PLC 或短静音处理序列缺口，不能补发旧包并累积延迟。

## FHC1 HAL/device control channel / HAL 设备控制面

The control channel is a bidirectional Unix `SOCK_STREAM`. The container listens
at `/mnt/vendor/floral_stream/control.sock` by default and the host connects to
it. Every FHC1 message starts with a fixed 24-byte header. Integer fields use
network byte order.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `FHC1` |
| 4 | 2 | Protocol version, currently `1` |
| 6 | 2 | Header size, currently `24` |
| 8 | 2 | Command id |
| 10 | 2 | Route/kind word |
| 12 | 4 | Nonzero request id copied into the response |
| 16 | 4 | Payload size, at most 65536 bytes |
| 20 | 4 | Reserved, must be zero |

The route/kind word is independent from the command id. The high nibble selects
the FHC1 control plane (`0x1000`), bits 11..8 select the packet kind (`0`
request, `1` response, `2` event, `3` error response), and bits 7..0 are
reserved for future route flags and must currently be zero. The route values
are therefore `0x1000`, `0x1100`, `0x1200`, and `0x1300`. This keeps direction
and routing visible in a binary dump without consuming command ids.

Command id `0x0100` replaces the complete desired external-display topology.
Its response uses the same command id and route kind `0x1100`. Unknown
requests receive command id `0x0000` with route kind `0x1300`. Full snapshots
make reconnection idempotent and avoid ordering dependencies between
incremental add and remove commands.

The replace-topology payload begins with:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | External display count, at most 64 |
| 4 | 4 | Reserved, must be zero |

Each display then uses a variable-size record:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Record size including zero padding to four bytes |
| 4 | 8 | Display id; 0 and 1 are reserved |
| 12 | 4 | Physical port, range 1 through 255 |
| 16 | 4 | Logical width |
| 20 | 4 | Logical height |
| 24 | 4 | Density in DPI |
| 28 | 4 | Active refresh rate in Hz |
| 32 | 2 | Supported refresh-rate count, at most 16 |
| 34 | 2 | UTF-8 display-name byte length, at most 128 |
| 36 | variable | Supported refresh rates as 32-bit integers |
| next | variable | Display name without a trailing null |
| next | 0-3 | Zero padding included in record size |

The 16-byte response payload contains a 32-bit result, a zero reserved word,
and the resulting 64-bit topology generation. Result values are `0` applied,
`1` unchanged, `2` invalid display, `3` duplicate display id, `4`
duplicate port, and `5` controller unavailable.

A successfully applied or identical snapshot renews control authority. If the
socket disconnects, external displays remain for the configured three-second
lease. A valid full snapshot received after reconnection cancels the pending
expiry without changing the topology generation when content is identical. If
the lease expires, the controller publishes an empty external-display snapshot.
The primary display is not represented in FHC1 and cannot be removed through
this channel. FHC1 is reserved for HAL/device configuration, capability, and
status operations; touch, keyboard, and other device actions do not use this
channel.

Command id `0x0200` updates one Opus audio stream. Its fixed 16-byte request is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Stream id |
| 4 | 4 | Update-field mask; bit 0 selects bitrate |
| 8 | 4 | Opus bitrate in bits per second |
| 12 | 4 | Reserved, must be zero |

The current implementation requires mask value `0x00000001` and accepts
bitrates from 16000 through 512000 bit/s. The fixed 20-byte response contains
result, supported-field mask, current generation, effective bitrate, and codec
id at offsets 0, 4, 8, 12, and 16. Results are `0` applied, `1` invalid
configuration, `2` unsupported, and `3` unknown stream. Applying bitrate uses
`OPUS_SET_BITRATE`; it does not recreate the encoder or increment generation.

命令 `0x0200` 动态调整一个 Opus 音频流。16 字节请求依次包含 stream id、字段
掩码、码率和零保留字段；当前只允许掩码 bit 0，码率范围为 16000 至 512000 bit/s。
20 字节响应依次返回结果、支持字段、当前 generation、实际码率和 codec id。实现
通过 `OPUS_SET_BITRATE` 原位生效，不重建编码器，也不递增 generation。

Command id `0x0300` updates one active video encoder. Its fixed 48-byte request
payload is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | Target display id |
| 8 | 4 | Stream id |
| 12 | 4 | Update-field mask |
| 16 | 4 | Encoder backend: `0` MediaCodec software, `1` FFmpeg VA-API |
| 20 | 4 | Codec id: `1` H.264 |
| 24 | 4 | Coded width |
| 28 | 4 | Coded height |
| 32 | 4 | Frame rate in frames per second |
| 36 | 4 | Bitrate in bits per second |
| 40 | 4 | I-frame interval in seconds |
| 44 | 4 | Reserved, must be zero |

Mask bits `0` through `5` select bitrate, frame rate, coded resolution,
I-frame interval, encoder backend, and codec respectively. At least one bit
must be set. A field not selected by the mask must be zero, making partial
updates unambiguous in packet captures. The coded resolution must preserve the
logical display aspect ratio and current rotation; it changes encoder output
size without changing the Android display or its input coordinate space.

The fixed 32-byte response payload is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Result |
| 4 | 4 | Supported update-field mask |
| 8 | 4 | Current active generation |
| 12 | 4 | Pending flag, `0` or `1` |
| 16 | 4 | Effective bitrate |
| 20 | 4 | Effective frame rate |
| 24 | 4 | Desired coded width |
| 28 | 4 | Desired coded height |

Results are `0` applied, `1` pending session replacement, `2` invalid
configuration, `3` unsupported value, and `4` unknown display or stream.
Bitrate-only updates are applied to the running encoder and retain the current
generation. Frame-rate, coded-resolution, I-frame-interval, backend, or codec
changes replace the encoder session. The replacement increments generation,
invalidates registered source buffers, and starts with new codec configuration
and a key frame. Lower frame rates are also enforced at frame ingress so the
encoder does not continue receiving every compositor frame.

### Sensor and GNSS simulation commands

Command ids `0x0400` through `0x0406` control and inspect ordinary phone
sensors. Command ids `0x0500` through `0x0505` do the same for GNSS. These are
FHC1 request/response operations; no sensor or GNSS operation is added to FDO1.
All floats and doubles below are IEEE 754 values serialized in network byte
order. Simulation payload version is `1`.

The host selects coarse ground truth. Final measurements are never supplied by
the host: noise, bias drift, environmental drift, GNSS error, satellite state,
and NMEA output are generated inside Android. Source `0` selects autonomous
generation and source `1` selects the optional external ground-truth stream.
Motion profiles are `0` stationary, `1` walking, `2` running, and `3` vehicle.

| Command | Name | Request payload | Response payload |
| ---: | --- | --- | --- |
| `0x0400` | SetMotionConfig | 24-byte motion config | 16-byte update |
| `0x0401` | SetEnvironmentConfig | 24-byte environment config | 16-byte update |
| `0x0402` | PushExternalPoseBatch | 8-byte batch header plus records | 16-byte update |
| `0x0403` | ResetSensorSimulation | Empty | 16-byte update |
| `0x0404` | GetSensorSimulationConfig | Empty | 40-byte sensor config |
| `0x0405` | ListSensors | Empty | Variable sensor catalog |
| `0x0406` | GetSensorSnapshot | Empty | Variable sensor snapshot |
| `0x0500` | SetGnssConfig | 56-byte GNSS config | 16-byte update |
| `0x0501` | PushExternalGnssBatch | 8-byte batch header plus records | 16-byte update |
| `0x0502` | ResetGnssSimulation | Empty | 16-byte update |
| `0x0503` | GetGnssConfig | Empty | 64-byte GNSS config |
| `0x0504` | GetGnssCapabilities | Empty | 16-byte capabilities |
| `0x0505` | GetGnssSnapshot | Empty | Variable GNSS snapshot |

Every mutating command returns this update payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Result: `0` applied, `1` unchanged, `2` invalid, `3` not ready |
| 4 | 4 | Applied external-record count; zero for configuration operations |
| 8 | 8 | Current simulation generation |

Applied and unchanged mutations renew the FHC1 authority lease. Queries do not.
When the control lease expires, external sources return to autonomous mode;
configured autonomous values remain available. Reset restores the corresponding
sensor or GNSS defaults and also selects autonomous mode.

The `0x0400` motion request is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Payload size, `24` |
| 4 | 4 | Motion source, `0` autonomous or `1` external |
| 8 | 4 | Motion profile, `0` through `3` |
| 12 | 4 | Transition duration in milliseconds, at most `60000` |
| 16 | 8 | Reserved, must be zero |

The `0x0401` environment request is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Payload size, `24` |
| 4 | 4 | Ambient light in lux, `0` through `100000` |
| 8 | 4 | Proximity distance in centimeters, `0` through `5` |
| 12 | 4 | Pressure in hPa, `300` through `1100` |
| 16 | 4 | Transition duration in milliseconds, at most `60000` |
| 20 | 4 | Reserved, must be zero |

The `0x0500` GNSS request is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Payload size, `56` |
| 4 | 4 | GNSS source, `0` autonomous or `1` external |
| 8 | 4 | Enabled, `0` or `1` |
| 12 | 4 | Reserved, must be zero |
| 16 | 8 | Anchor latitude in degrees, `-90` through `90` |
| 24 | 8 | Anchor longitude in degrees, `-180` through `180` |
| 32 | 8 | Anchor altitude in meters |
| 40 | 4 | Ground speed in m/s, `0` through `150` |
| 44 | 4 | Bearing in degrees, `0` inclusive through `360` exclusive |
| 48 | 4 | Transition duration in milliseconds, at most `60000` |
| 52 | 4 | Reserved, must be zero |

Both external-stream commands begin with the same batch header. A batch has 1
through 512 records and its total FHC1 payload must remain within 65536 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Record size, `56` |
| 4 | 2 | Record count, `1` through `512` |
| 6 | 2 | Reserved, must be zero |

Each `0x0402` pose record is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | Sample age relative to guest receipt time in nanoseconds, at most 2 seconds |
| 8 | 4 | Flags; bit 0 marks a discontinuity, all other bits zero |
| 12 | 4 | Reserved, must be zero |
| 16 | 16 | Device-to-world quaternion X, Y, Z, W |
| 32 | 12 | World-frame linear acceleration X, Y, Z in m/s2 |
| 44 | 12 | Device-frame angular velocity X, Y, Z in rad/s |

Each `0x0501` GNSS record is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | Sample age relative to guest receipt time in nanoseconds, at most 10 seconds |
| 8 | 4 | Flags; bit 0 marks a discontinuity, all other bits zero |
| 12 | 4 | Reserved, must be zero |
| 16 | 8 | Latitude in degrees |
| 24 | 8 | Longitude in degrees |
| 32 | 8 | Altitude in meters |
| 40 | 4 | Ground speed in m/s |
| 44 | 4 | Bearing in degrees |
| 48 | 4 | Horizontal ground-truth accuracy in meters |
| 52 | 4 | Vertical ground-truth accuracy in meters |

`age_ns` avoids assuming that host and guest monotonic clocks share an epoch.
The service subtracts it from guest `CLOCK_BOOTTIME`. Pose truth is considered
fresh for 500 ms and GNSS truth for 2 seconds. Once stale, generation continues
from the autonomous model instead of replaying old external samples.

The `0x0404` sensor-config response is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Header size, `40` |
| 4 | 4 | Reserved, zero |
| 8 | 8 | Generation |
| 16 | 4 | Motion source |
| 20 | 4 | Motion profile |
| 24 | 4 | Target light in lux |
| 28 | 4 | Target proximity in centimeters |
| 32 | 4 | Target pressure in hPa |
| 36 | 4 | Transition duration in milliseconds |

The `0x0405` catalog starts with a 16-byte header: version and header size at
offsets 0 and 2, sensor count at 4, and generation at 8. Each following record
has a 24-byte fixed part followed by its UTF-8 name and zero padding to four
bytes. The fixed part contains record size and name length as 16-bit values at
offsets 0 and 2, then handle, Android sensor type, flags, minimum delay in
microseconds, and maximum delay in microseconds as 32-bit values at offsets 4,
8, 12, 16, and 20.

The `0x0406` snapshot starts with a 24-byte header: version and header size at
offsets 0 and 2, reading count at 4, generation at 8, and guest boot timestamp
in nanoseconds at 16. Each reading has record size and float-value count as
16-bit values at offsets 0 and 2, handle and Android sensor type at 4 and 8, a
zero reserved word at 12, step count at 16, and values beginning at 24. The HAL
publishes this cache at no more than 10 Hz; it is for inspection rather than a
replacement high-rate telemetry channel.

The `0x0503` GNSS-config response is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Simulation payload version, `1` |
| 2 | 2 | Header size, `64` |
| 4 | 4 | Reserved, zero |
| 8 | 8 | Generation |
| 16 | 4 | GNSS source |
| 20 | 4 | Enabled, `0` or `1` |
| 24 | 8 | Anchor latitude in degrees |
| 32 | 8 | Anchor longitude in degrees |
| 40 | 8 | Anchor altitude in meters |
| 48 | 4 | Ground speed in m/s |
| 52 | 4 | Bearing in degrees |
| 56 | 4 | Transition duration in milliseconds |
| 60 | 4 | Reserved, zero |

The `0x0504` response contains version and size `16` at offsets 0 and 2, a zero
reserved word at 4, and a 64-bit capability mask at 8. Bits 0 through 4 mean
location, satellite status, NMEA, multiple constellations, and external truth
stream respectively.

The `0x0505` response has an 88-byte header followed by 24-byte satellite
records. The header contains version, header size, satellite count, generation,
guest elapsed-realtime nanoseconds, UTC milliseconds, and a 32-bit fix flag at
offsets 0, 2, 4, 8, 16, 24, and 32. Offset 36 is reserved. Latitude, longitude,
and altitude doubles are at 40, 48, and 56. Speed, bearing, horizontal accuracy,
vertical accuracy, speed accuracy, and bearing accuracy floats are at offsets
64 through 84. Each satellite record contains 16-bit record size and used-in-fix
flag, 32-bit SVID and constellation, then C/N0, elevation, and azimuth floats.

Internally, FloralDevice fans external truth out to independent HAL readers as
fixed 112-byte `FSS1` FMQ records. The record contains magic `FSS1`, version and
size at offsets 0, 4, and 6; generation at 8; flags at 16; reserved zero at 20;
guest boot timestamp at 24; pose fields at 32 through 68; GNSS fields at 72
through 108. Flag bits 0, 1, and 2 mean pose present, GNSS present, and
discontinuity. FSS1 is an internal system/vendor transport and is not sent over
the host socket.

### 中文约束摘要

FHC1 使用 24 字节固定大端帧头。`command_id` 只标识命令，`route_kind` 的高
四位固定为 `0001`，接着四位区分请求、响应、事件和错误，最低八位当前必须
为零。这样不会把请求/响应方向和命令编号混在一个字段里，也不需要为每个
响应消耗另一组命令号。当前实现包括完整快照拓扑命令 `0x0100`、音频编码配置
命令 `0x0200`、视频编码配置命令 `0x0300`、传感器命令 `0x0400` 至 `0x0406`
以及 GNSS 命令 `0x0500` 至 `0x0505`；主屏
`displayId=0` 永久存在，控制 socket 断开后外屏默认保留三秒，租约到期只
清空外屏。音频码率通过 `0x0200` 原位调整。视频配置通过字段掩码支持部分更新：
只改码率时沿用当前 generation，
帧率、编码分辨率、I 帧间隔、后端或编码格式变化时重建会话并递增 generation。
编码分辨率不会改变 Android 逻辑显示尺寸和输入坐标。控制面不承载触摸、键盘
或鼠标事件。宿主只提供低频配置和可选的高频真值，噪声、偏置漂移、GNSS 误差、
卫星状态和环境缓慢变化均在 Android 内部生成；查询命令返回当前配置、目录和
内部状态快照，不作为连续遥测通道。

## FDO1 device-operation channel

The operation channel is a separate bidirectional Unix `SOCK_STREAM` at
`/mnt/vendor/floral_stream/operate.sock`. It is reserved for device actions
such as touch, keyboard, mouse, gestures, and explicit display operations. The
container listens and the host connects. This is separate from FHC1 so a slow
configuration request cannot head-of-line block high-rate input. The container
path can be overridden with `ro.boot.floral_operate_socket`.

Every message starts with a fixed 24-byte big-endian header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `FDO1` |
| 4 | 2 | Protocol version, currently `1` |
| 6 | 2 | Header size, currently `24` |
| 8 | 2 | Operation code |
| 10 | 2 | Route/kind word |
| 12 | 4 | Request id |
| 16 | 4 | Payload size, at most 65536 bytes |
| 20 | 4 | Reserved, must be zero |

The route/kind high nibble is `0x2`. Bits 11..8 are packet kind: `0` request,
`1` response, `2` event, or `3` error response. Low route flags are currently
zero, producing `0x2000`, `0x2100`, `0x2200`, and `0x2300`. Requests and their
responses use the same nonzero request id. Unacknowledged high-rate events use
request id zero; all other packet kinds require a nonzero request id.

The generic error operation is `0x0000`. Its 8-byte payload contains a 32-bit
error code, the failed 16-bit operation code, and a zero 16-bit reserved word.
Errors are `1` unsupported message, `2` malformed message, `3` invalid request
id, and `4` internal error.

### Input target binding

Operation `0x0100` binds one connection-local target slot to a stable stream.
The 8-byte `BIND_INPUT_TARGET` request is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Target slot, range 0 through 255 |
| 1 | 1 | Mode, currently `0` exclusive |
| 2 | 1 | Physical display port; `0` selects the permanent primary display |
| 3 | 1 | Reserved, must be zero |
| 4 | 4 | Nonzero stream id |

The 28-byte response is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Result |
| 4 | 1 | Target slot |
| 5 | 1 | Physical display port |
| 6 | 2 | Reserved, must be zero |
| 8 | 4 | Stream id |
| 12 | 4 | Input epoch |
| 16 | 4 | Logical display width |
| 20 | 4 | Logical display height |
| 24 | 2 | Clockwise rotation: 0, 90, 180, or 270 |
| 26 | 2 | Reserved, must be zero |

A successful response has a nonzero epoch and dimensions. The host must stop
sending input and bind again when the epoch changes. Results are `0` applied,
`1` unchanged, `2` invalid target, `3` target busy, `4` unknown stream, `5`
stale epoch, `6` invalid input state, and `7` injection failed.

Operation `0x0101` unbinds a target. Its 4-byte request contains the target
slot followed by three zero bytes. Its 8-byte response contains the 32-bit
result, target slot, and three zero bytes. Unbinding cancels every active touch
owned by that target before removing the mapping. Socket disconnection performs
the same cancellation for all slots immediately; it does not use the FHC1
three-second display-topology lease.

Slots are local to one FDO1 connection. Slot 1 on one connection does not name
slot 1 on another connection. A service supporting multiple connections must
also namespace pointer state by connection and apply exclusive target leases.
The current FloralDroid implementation uses one reconnecting Android connection;
the host gateway multiplexes its remote controllers into separate slots and
performs user authorization before forwarding input. Within that connection,
both physical displays and nonzero stream ids are leased exclusively.

Operation `0x0102` is a `TARGET_INVALIDATED` event sent by Android when a bound
display is removed or its geometry changes. Its 12-byte payload contains target
slot and reason at offsets 0 and 1, a zero 16-bit reserved field, stream id at
offset 4, and the invalidated input epoch at offset 8. Reasons are `1` display
removed and `2` geometry changed. The host must stop the old event sequence and
bind the slot again before sending more input.

### Touch event

Operation `0x0200` is an unacknowledged event (`route_kind=0x2200`,
`request_id=0`) with a fixed 24-byte payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Target slot |
| 1 | 1 | Action: `0` down, `1` move, `2` up, `3` cancel |
| 2 | 1 | Pointer id, range 0 through 31 |
| 3 | 1 | Reserved, must be zero |
| 4 | 4 | Nonzero input epoch returned by bind |
| 8 | 4 | Nonzero, monotonically increasing target sequence |
| 12 | 2 | Normalized X, 0 through 65535 |
| 14 | 2 | Normalized Y, 0 through 65535 |
| 16 | 2 | Normalized pressure, 0 through 65535 |
| 18 | 2 | Normalized touch-major size, 0 through 65535 |
| 20 | 4 | Reserved, must be zero |

DOWN and MOVE require nonzero pressure. CANCEL carries zero coordinates,
pressure, and touch-major size, uses pointer id zero, and cancels every active
pointer in the target gesture. The Android side converts normalized coordinates
to the logical dimensions fixed by the matching input epoch; coded video size is
never used for input. Events with an unknown slot or stale epoch are dropped.
Malformed events, repeated or decreasing sequences, and invalid pointer state
cancel the affected target gesture so that Android cannot retain a stuck pointer.

FDO1 only describes the userspace transport. FloralDroid input execution does
not create a kernel uinput/evdev device; the operation service injects framework
motion events directly and assigns the resolved Android display id.
