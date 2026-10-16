# Audio shared-memory policy

Boards already include `sepolicy/BoardConfig.mk`; no additional build hook is
needed. These rules cover two different object owners, not a generic
ashmem/memfd permission workaround.

## AudioFlinger to app clients

AudioFlinger creates its client control/data regions as `audioserver_tmpfs:s0`.
Apps carry per-app MLS categories. The platform already grants app consumers
`{ read write getattr map }` and Binder FD use, but the untrusted-to-s0 write
violates the ordinary MLS equivalence constraint. This can reject the FD in the
`getCblk()` Binder reply before the client can map it.

`sepolicy/vendor/audioserver.te` makes only `audioserver_tmpfs` an `mlstrustedobject`.
It does not make any process an `mlstrustedsubject`, add TE permissions, allow
path opens, or exempt app data or generic tmpfs from MLS. Access still requires
the existing TE rules and a received FD; this does not grant apps access to
another client's FD.

## Audio HAL to audioserver

The HIDL `StreamOut::prepareForWriting()` wrapper creates the ordinary command,
data and status FMQs in `hal_audio_default`. They must not retain generic
`tmpfs`, nor be mistaken for objects created by audioserver.

`sepolicy/vendor/hal_audio_default.te` assigns these objects
`hal_audio_default_tmpfs` via `tmpfs_domain(hal_audio_default)`. Explicit FMQ
access rules name only the creator and audioserver, with the four file
permissions needed for synchronized FMQ access. Existing platform administrative
rules for `file_type` are unchanged. Both consumers use s0; no MLS exception is
needed for this type. Existing audio Binder rules cover FD use.

FloralDevice's separate PCM FMQ retains `floral_device_tmpfs` and its existing
consumer rules. No generic tmpfs access, reverse
`hal_audio_default -> audioserver_tmpfs` allow, audio algorithm, or queue change
is introduced here.

## Host regression checks

After compiling the combined Android policy, run on a host with libsepol 3.4 or
newer, its development headers/static library, and a C compiler:

```sh
python3 tests/audio_selinux_policy_test.py /path/to/policy.bin --json /path/to/result.json
```

The test evaluates complete TE+MLS decisions, creation transitions, app-category
positive cases, direct-open and non-consumer negatives, previous Codec2/sensor
fixes, and retained app-data/input isolation. `--before-fix` checks the known
failures against a saved policy from before these two audio changes.

Passing this test is not playback acceptance. A newly built image must still be
tested with hardware graphics, Enforcing and seccomp: real-app AudioTrack
initialization, ordinary stream FMQ transfer, the Floral PCM/FSA1 path, and
security negatives. Recording and sustained hardware stability remain separate
validation items.
