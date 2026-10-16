# Input broker security modes

`FloralInputService` (`com.floraldroid.input`) is the platform-signed remote-input
broker, not an input method. Its FDO1 packet format and touch state machine are
unchanged.

## Component-owned policy

Boards include `packages/services/FloralDevice/sepolicy/BoardConfig.mk`.
The system_ext public policy exports the `floral_input` domain name; private
policy defines its app behavior and maps **platform signer + privileged package
name**, not package name alone. Android still compiles these rules and the
kernel enforces them.

The broker has ordinary app API lookup and the required system_server Binder
calls, not the entire `platform_app` or `priv_app` policy. It cannot use the
generic transport file type to create arbitrary sibling sockets. Only
`operate.sock` transitions to its dedicated socket-file type. Generic app
domains cannot mutate the directory or create/replace/write the input endpoint;
the native FloralDevice daemon cannot write/unlink that socket either. Platform
init initializes and labels the supplied transport; vendor_init gets no new
generic tmpfs/rootfs initialization permissions.

The broker uses `s0`, like a device-wide native broker, for the host-shared `s0`
transport. It does **not** become `mlstrustedsubject` or weaken MLS constraints
globally. Its data remains protected by app UID/DAC; ordinary apps retain their
existing MCS categories. An upgrade from the old generic app mapping needs the
normal package-data relabel and an actual startup regression test, not a data
wipe. Offline AV tests include denial of opening category-labelled other-app
data; already-transferred file descriptors keep Android's normal semantics.

## Boot configuration

`androidboot.floral_input_strict` becomes the init-supplied read-only
`ro.boot.floral_input_strict` property. It is not an app-writable runtime switch.

| Value | Behavior |
| --- | --- |
| `0` | Development transport without a SELinux state query: keep the original credentials behavior, socket mode `0660`, and configured path override. |
| `1` | Require confirmed Enforcing, `u:r:floral_input:s0`, `/ipc/floral_stream/operate.sock`, and its initialized directory. Invalid configuration leaves the endpoint disabled. |
| unset | Strict with confirmed Enforcing; development only with confirmed Permissive or disabled/unmounted SELinux. Unknown state or query failure disables the endpoint. |
| anything else | Endpoint disabled. |

The native state reader preserves Enforcing, Permissive, confirmed disabled/
unmounted, and Unknown/Error rather than folding query errors into a boolean.
JNI loading and state-query failures do not enable development mode. The input
domain gets only `selinuxfs:file { open read }` for the query, not enforcement
control or an enforce-file-only path grant. Explicit `0` skips the query.

Updated Lupine sends `1` only for enforcing + configured host seccomp, and `0`
if either mechanism is disabled for development. With older launchers, set the
flag explicitly to `0` for enforcing/no-seccomp development. A development flag
does not remove the compiled SELinux restrictions while SELinux is enforcing.
For custom socket paths under Enforcing, the integrator must supply suitable
labels and narrow policy; the flag does not add arbitrary-path access.

## Strict peer authorization

Before reading any FDO1 bytes, query the accepted connection's kernel
`SO_PEERCRED`. Require UID 0, GID 0, and peer PID 0 in the receiving guest PID
namespace. Guest root, system, adb shell and apps have visible positive PIDs and
are rejected. Missing credentials fail closed. The strict socket is mode `0600`.
Stale sockets owned by another UID are not removed; failed bind cleanup does
not unlink somebody else's listener.

**External boundary requirement:** the trusted root gateway must connect from
outside the guest PID namespace using a **private per-instance** host directory,
with no other guest able to access that directory or endpoint. PID 0 means
"not visible here", not a cryptographic host identity: sibling-namespace root
can also have PID 0. Never share this IPC directory between tenants. A non-root
gateway, a gateway inside the guest, or a shared host transport requires a
separate authentication design; do not silently fall back in strict mode.

The parent directory still has legacy `0773 root:root` DAC for the unknown-at-
build-time app UID and native daemon. SELinux narrows guest access in production;
host-side ancestor directories/mount visibility must also be protected. Changing
one permission or peer check alone is not proof of cross-tenant isolation.

## IPC initialization and shared memory

Deploy with matching Lupine private-IPC support and system/core patch
`0014-init-keep-Floral-IPC-mount-private.patch`. Lupine owns a distinct source
directory for every instance generation, binds it at `/ipc/floral_stream`, and
checks its identity, flags and propagation. Android init restores the leaf to
private after making the root recursively shared and before cloning bootstrap/
default mount namespaces. The root keeps Android's normal shared propagation.
Platform early-init then initializes and labels the directory; an early-init rc
action alone is too late to fix both mount namespaces.

Strict input requires the directory itself (not a symlink), `root:root`, exact
mode `0773`, and `u:object_r:floral_stream_file:s0` before touching the endpoint.
Platform init has `floral_stream_file:dir mounton` for the propagation change;
this is a type-based permission, not an arbitrary-path or vendor_init grant.

FloralDevice-created FMQ memfd/ashmem objects use `floral_device_tmpfs`. The only
new consumer grants are `{ read write map getattr }` for `hal_sensors_default`,
`hal_gnss_default`, and `hal_audio_default`; existing Binder rules supply fd use.
Synchronized consumers also write read pointers/event words. This adds no
generic tmpfs write grant and does not change FMQ, simulation or audio algorithms.

## Validation still required on a built image

Host tests cover the pure authorization/mode decisions, old protocol/state
machine tests, and a real disposable PC PID namespace credential check. Offline
policy/neverallow, combined seapp and MLS-aware allow/deny checks also pass.
These are not device integration tests.

After a user-authorized build/deploy, check the real package domain, endpoint
label/mode, host FDO1 touch round trip, denied app/guest-root connections,
separate-tenant directory isolation across both init mount namespaces, actual FMQ
object labels and consumer round trips/reconnects, and disabled/permissive/
no-seccomp developer configurations. Do not wipe userdata or remove other ADB
authorizations.
