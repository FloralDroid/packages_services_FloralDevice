# FHC1 HAL/device control channel

The control channel is a bidirectional Unix `SOCK_STREAM`. The container listens
at `/ipc/floral_stream/control.sock` by default and the host connects to
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

## Sensor and GNSS simulation commands

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

## Power simulation commands

Command ids `0x0600` through `0x0603` control and inspect the coherent battery
and thermal model. They are FHC1 operations and do not add an FDO1 operation.
When no power command has ever been received, the device autonomously
discharges, connects a virtual charger near a randomized low threshold, and
disconnects it near a randomized high threshold. Noise, temperature inertia,
and cycle thresholds are generated inside Android.

| Command | Name | Request payload | Response payload |
| ---: | --- | --- | --- |
| `0x0600` | SetPowerControl | 24-byte manual control | 16-byte update |
| `0x0601` | ReleasePowerControl | Empty | 16-byte update |
| `0x0602` | GetPowerSnapshot | Empty | 96-byte snapshot |
| `0x0603` | GetPowerCapabilities | Empty | 24-byte capabilities |

The `0x0600` request contains version `1` and size `24` at offsets 0 and 2,
followed by a 32-bit mode, nonnegative current magnitude in microamperes, and lease
duration in milliseconds at offsets 4, 8, and 12. The final eight bytes are
reserved and zero. Modes are `1` charge, `2` discharge, and `3` idle. A zero
current selects the model default; idle requires zero current. The lease is
between 1 and 60000 ms. Idle stops state-of-charge movement immediately while
internal temperature and voltage processes continue. Explicit release, lease
expiry, or the FHC1 authority lease expiry resumes autonomous cycling from the
current state without a level jump.

Update responses contain version and size at offsets 0 and 2, a result at 4,
and generation at 8. Results are `0` applied, `1` unchanged, and `2` invalid.

The `0x0602` snapshot begins with version, size, and flags at offsets 0, 2, and
4. Flag bits 0 and 1 mean externally controlled and charger online. Generation,
guest boot timestamp, control mode, battery status, level, voltage, current,
average current, charge counter, and full charge are at offsets 8 through 52.
Time to full is at 56. Battery, skin, CPU, and GPU temperatures occupy offsets
64 through 76; skin, CPU, GPU, and battery throttling severities are at 80, 84,
88, and 92. Battery statuses are `0` unknown, `1` charging,
`2` discharging, `3` not charging, and `4` full.

The `0x0603` response contains version and size, a zero reserved word, a 64-bit
mode mask at offset 8, maximum lease at 16, and capability flags at 20. Mode
mask bits 0 through 2 mean charge, discharge, and idle. Capability bits 0
through 2 mean autonomous default, internal entropy, and persisted state.

## Radio simulation commands

Command ids `0x0700` through `0x0715` control and inspect cellular registration,
signal, cells, SIM state, calls, and SMS events. A bounded external-control lease
applies to registration, signal, cell, and SIM mutations. Explicit release,
lease expiry, or FHC1 authority expiry restores the autonomous radio state.

| Command | Name | Request payload | Response payload |
| ---: | --- | --- | --- |
| `0x0700` | SetRadioRegistration | 24-byte registration request | 24-byte update |
| `0x0701` | SetRadioSignal | 32-byte signal request | 24-byte update |
| `0x0702` | ReplaceRadioCells | 16-byte header plus cell records | 24-byte update |
| `0x0703` | SetSimState | 16-byte SIM request | 24-byte update |
| `0x0704` | InjectIncomingCall | 12-byte header plus number | 24-byte update |
| `0x0705` | SetRadioCallState | 24-byte call request | 24-byte update |
| `0x0706` | InjectIncomingSms | 16-byte header plus address and body | 24-byte update |
| `0x0707` | ReleaseRadioControl | Empty | 24-byte update |
| `0x0708` | PushRadioSampleBatch | Reserved; currently unsupported | Generic error |
| `0x0710` | GetRadioProfile | Empty | 48-byte header plus strings |
| `0x0711` | GetRadioSnapshot | Empty | 80-byte snapshot |
| `0x0712` | ListRadioCells | Empty | 16-byte header plus cell records |
| `0x0713` | ListRadioCalls | Empty | 16-byte header plus call records |
| `0x0714` | ListRadioSmsEvents | Empty | 16-byte header plus SMS records |
| `0x0715` | GetRadioCapabilities | Empty | 32-byte capabilities |

Every nonempty top-level Radio request or response begins with 16-bit version
`1` and a 16-bit total payload size. Leases range from 1 through 60000 ms.
Registration states are `0` not registered, `1` home, `2` searching, `3`
denied, `4` unknown, and `5` roaming. Technologies are `0` unknown, `1` GSM,
`2` WCDMA, and `3` LTE. SIM states are `0` absent, `1` ready, `2` PIN
required, and `3` PUK required. Call states are `0` active, `1` holding, `2`
dialing, `3` alerting, `4` incoming, and `5` waiting.

The `0x0700` request stores voice registration, data registration, technology,
lease duration, and a zero reserved word at offsets 4, 8, 12, 16, and 20. The
`0x0701` request stores RSSI dBm, RSRP dBm, RSRQ dB, RSSNR in tenths of a dB,
CQI, timing advance, and lease duration at offsets 4 through 28 in four-byte
steps. Valid ranges are `-120..-20`, `-140..-40`, `-30..0`, `-200..300`,
`0..15`, and `0..1282` respectively.

The `0x0702` request header stores cell count at offset 4, lease duration at 8,
and a zero reserved word at 12. It accepts 1 through 32 fixed 60-byte cell
records and requires exactly one serving cell:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | Nonzero cell identity |
| 8 | 4 | Registered flag, `0` or `1` |
| 12 | 4 | Tracking area code, `0` through `65535` |
| 16 | 8 | E-UTRAN cell identity, `0` through `268435455` |
| 24 | 4 | Physical cell id, `0` through `503` |
| 28 | 4 | EARFCN, `0` through `262143` |
| 32 | 4 | Bandwidth in kHz, `1400` through `20000` |
| 36 | 24 | Signal fields in the same order as `0x0701` |

The `0x0703` request stores SIM state, lease duration, and a zero reserved word
at offsets 4, 8, and 12. The `0x0704` request stores phone-number byte length at
offset 4, a zero reserved word at 8, and 1 through 20 ASCII bytes at offset 12.
A number contains decimal digits with an optional leading `+`. The `0x0705`
request reserves offset 4, stores a positive signed-64-bit call id at 8, stores
call state at 16, and reserves offset 20. The `0x0706` request stores address
length, message-body length, and a zero reserved word at offsets 4, 8, and 12,
followed by the address and body without terminators. The address follows the
same phone-number rules; the UTF-8 body is 1 through 1024 bytes.

Every 24-byte update response stores result at offset 4, generation at 8, and
an affected call id or SMS sequence at 16. Result values are `0` applied, `1`
unchanged on release, and `2` rejected. Query responses never acquire or renew
the Radio-specific external-control lease.

The `0x0710` profile response stores profile version at offset 4 and ten string
lengths at offsets 8 through 44. The corresponding bytes follow the 48-byte
header in this order: operator long name, operator short name, MCC, MNC, IMEI,
IMEISV, IMSI, ICCID, MSISDN, and baseband version. Strings have no terminators.

The `0x0711` snapshot stores flags, generation, guest boot timestamp, SIM state,
voice registration, data registration, technology, signal, and the cell, call,
and SMS counts at offsets 4, 8, 16, 24, 28, 32, 36, 40, 64, 68, and 72.
Flag bits 0 and 1 mean externally controlled and radio on. Offset 76 is
reserved. The cell-list response repeats the 60-byte cell record after a
16-byte header containing count at offset 4.

Each call-list record contains record size and state at offsets 0 and 4, call id
at 8, flags at 16, number length at 20, and the number at 24. Flag bits 0 and 1
mean incoming and multiparty. Each SMS-list record contains record size and an
incoming flag at offsets 0 and 4, sequence and realtime timestamp in nanoseconds
at 8 and 16, address and body lengths at 24 and 28, then the two strings at 32.
The list header reports the records actually returned; the SMS query keeps the
newest events when the FHC1 payload limit prevents returning the full history.

The `0x0715` capabilities response stores flags `0x0f`, maximum cells `32`,
maximum calls `8`, maximum SMS events `64`, maximum lease `60000`, and
technology mask `0x0e` at offsets 4, 8, 12, 16, 20, and 24. The flags mean
autonomous behavior, profile persistence, internal entropy, and runtime leases.

## Wi-Fi simulation commands

Command ids `0x0800` through `0x0813` control and inspect the multi-AP Wi-Fi
model. Actual packets continue to use Android's existing Ethernet network. A
valid `/mnt/vendor/floral_stream/wifi.json` supplies the boot profile and private
credentials. Missing or invalid JSON leaves simulation disabled with no APs.
Runtime mutations use bounded leases; explicit release or expiry restores the
mounted profile. Android Settings may toggle, connect, disconnect, or switch APs
only while no host lease is active.

| Command | Name | Request payload | Response payload |
| ---: | --- | --- | --- |
| `0x0800` | SetWifiEnabled | 16-byte enabled request | 24-byte update |
| `0x0801` | ReplaceWifiAccessPoints | 16-byte header plus AP records | 24-byte update |
| `0x0802` | SetWifiConnection | 16-byte connection request | 24-byte update |
| `0x0803` | SetWifiLink | 32-byte link request | 24-byte update |
| `0x0804` | ReleaseWifiControl | Empty | 24-byte update |
| `0x0805` | PushWifiSampleBatch | 16-byte header plus sample records | 24-byte update |
| `0x0810` | GetWifiProfile | Empty | 48-byte profile |
| `0x0811` | GetWifiSnapshot | Empty | 96-byte snapshot |
| `0x0812` | ListWifiAccessPoints | Empty | 16-byte header plus AP records |
| `0x0813` | GetWifiCapabilities | Empty | 32-byte capabilities |

Every nonempty top-level Wi-Fi request or response starts with 16-bit version
`1` and a 16-bit structure or payload size. Security values are `0` open, `1`
WPA2-PSK, and `2` WPA3-SAE. Leases range from 1 through 60000 ms. Update
responses contain result at offset 4, generation at 8, and the affected AP id at
16. Results are `0` applied, `1` unchanged or lease refreshed, and `2` invalid.

The `0x0800` request stores enabled at offset 4, lease at 8, and a zero reserved
word at 12. The `0x0802` request stores the 64-bit AP id at offset 4 and lease at
12; AP id zero disconnects. The `0x0803` request stores AP id, RSSI dBm,
frequency MHz, channel width MHz, link speed Mbps, and lease at offsets 4, 12,
16, 20, 24, and 28.

The `0x0801` header stores count, lease, and a zero reserved word at offsets 4,
8, and 12. It accepts at most 64 fixed 72-byte AP records:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | Nonzero AP id, at most signed 64-bit maximum |
| 8 | 2 | SSID byte length, 1 through 32 |
| 10 | 2 | Security |
| 12 | 6 | BSSID |
| 18 | 2 | Reserved, zero |
| 20 | 4 | RSSI in dBm, `-127` through `-1` |
| 24 | 4 | Frequency in MHz |
| 28 | 4 | Channel width in MHz: `20`, `40`, `80`, or `160` |
| 32 | 4 | Link speed in Mbps, `1` through `10000` |
| 36 | 32 | UTF-8 SSID storage, unused bytes zero |
| 68 | 4 | Reserved, zero |

The `0x0805` header uses the same count, lease, and reserved offsets. It accepts
1 through 256 fixed 32-byte records. Each record stores a positive, strictly
increasing guest boot timestamp at 0, AP id at 8, RSSI at 16, frequency at 20,
link speed at 24, and a zero reserved word at 28.

The profile response stores profile version, enabled, two-byte country code,
station MAC, connected AP id, AP count, and capability mask at offsets 4, 8,
12, 16, 24, 32, and 36. The capability mask is `0x3f`: multiple APs, internal
entropy, mounted profile, control leases, sample batches, and Ethernet-backed
presentation.

The snapshot response stores flags, generation, guest boot timestamp, connected
AP id, security, RSSI, frequency, channel width, link speed, BSSID, SSID length,
and a fixed 32-byte SSID area at offsets 4, 8, 16, 24, 32, 36, 40, 44, 48, 52,
58, and 60. Flag bits 0 through 2 mean enabled, externally controlled, and
connected. The AP-list response repeats the 72-byte AP record after its header;
the header stores count at offset 4 and reserves offsets 8 through 15.

The capabilities response stores flags `0x3f`, maximum APs `64`, maximum samples
`256`, maximum lease `60000`, security mask `0x07`, and presentation type `1`
(Ethernet-backed) at offsets 4, 8, 12, 16, 20, and 24.

Internally, FloralDevice fans external truth out to independent HAL readers as
fixed 112-byte `FSS1` FMQ records. The record contains magic `FSS1`, version and
size at offsets 0, 4, and 6; generation at 8; flags at 16; reserved zero at 20;
guest boot timestamp at 24; pose fields at 32 through 68; GNSS fields at 72
through 108. Flag bits 0, 1, and 2 mean pose present, GNSS present, and
discontinuity. FSS1 is an internal system/vendor transport and is not sent over
the host socket.
