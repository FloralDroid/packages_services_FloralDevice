# FloralStream Host Video Protocol

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
