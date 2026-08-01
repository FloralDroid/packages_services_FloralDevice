# Floral Device Host Protocols

The video channel is a Unix `SOCK_STREAM`. Every H.264 access unit starts with
a fixed 80-byte header followed by exactly `payload_size` bytes. Integer fields
use network byte order. C or C++ structure layout is not part of the protocol.

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

## FHC1 HAL/device control channel

The control channel is a bidirectional Unix `SOCK_STREAM`. The host listens at
`/mnt/vendor/floral_stream/control.sock` by default and the container connects
to it. Every FHC1 message starts with a fixed 24-byte header. Integer fields use
network byte order.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `FHC1` |
| 4 | 2 | Protocol version, currently `1` |
| 6 | 2 | Header size, currently `24` |
| 8 | 2 | Message type |
| 10 | 2 | Flags, must be zero |
| 12 | 4 | Nonzero request id copied into the response |
| 16 | 4 | Payload size, at most 65536 bytes |
| 20 | 4 | Reserved, must be zero |

Message type `0x0100` replaces the complete desired external-display
topology. Its response is `0x8100`. Unknown requests receive a generic
`0x8000` error response. Full snapshots make reconnection idempotent and avoid
ordering dependencies between incremental add and remove commands.

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

The 16-byte `0x8100` response contains a 32-bit result, a zero reserved word,
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

## FDO1 device-operation channel

The operation channel is a separate bidirectional Unix `SOCK_STREAM` at
`/mnt/vendor/floral_stream/operate.sock`. It is reserved for device actions
such as touch, keyboard, mouse, gestures, and explicit display operations.
The FDO1 header and operation payloads are not frozen in this repository yet.
No FDO1 listener or connector is implemented by the current milestone.
