# FSA1 encoded audio channel

The audio channel is a separate Unix `SOCK_STREAM`. The container listens at
`/ipc/floral_stream/audio.sock` by default and the host connects to it.
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
