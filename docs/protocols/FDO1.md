# FDO1 device-operation channel

The operation channel is a separate bidirectional Unix `SOCK_STREAM` at
`/ipc/floral_stream/operate.sock`. It is reserved for device actions
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

## Input target binding

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

## Touch event

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
