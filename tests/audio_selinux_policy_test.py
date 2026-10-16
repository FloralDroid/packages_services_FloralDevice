#!/usr/bin/env python3
# Copyright 2026 FloralDroid
# SPDX-License-Identifier: Apache-2.0
"""Check a compiled Android policy in userspace, including MLS constraints.

Usage: python3 tests/audio_selinux_policy_test.py policy.bin [--json result.json]
Requires host libsepol >= 3.4, libsepol.a/headers and a C compiler. Never loads a
policy into the kernel or starts a guest.
--before-fix checks the known failures against a saved pre-fix policy.
"""

import argparse
import ctypes as C
import ctypes.util
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


# sepol_transition_sid is available in libsepol.a but is not exported by some
# shared-library versions. Use a disposable native helper instead of replacing
# a true transition query with a textual search for a type_transition rule.
TRANSITION_HELPER = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sepol/sepol.h>
#include <sepol/policydb/services.h>
int main(int argc, char **argv) {
    assert(argc == 5);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(sepol_set_policydb_from_file(file) == 0);
    fclose(file);
    sepol_security_id_t source, target, result;
    sepol_security_class_t object_class;
    assert(sepol_context_to_sid(argv[2], strlen(argv[2]) + 1, &source) == 0);
    assert(sepol_context_to_sid(argv[3], strlen(argv[3]) + 1, &target) == 0);
    assert(sepol_string_to_security_class(argv[4], &object_class) == 0);
    assert(sepol_transition_sid(source, target, object_class, &result) == 0);
    char *context = NULL;
    size_t size;
    assert(sepol_sid_to_context(result, &context, &size) == 0);
    puts(context);
    free(context);
    return 0;
}
"""


class Decision(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("allowed", "decided", "auditallow", "auditdeny", "seqno")]


class Policy:
    def __init__(self, path):
        self.path = path
        self.helper_directory = None
        self.lib = C.CDLL(ctypes.util.find_library("sepol"))
        self.libc = C.CDLL(None)
        self.libc.fopen.argtypes = [C.c_char_p, C.c_char_p]
        self.libc.fopen.restype = C.c_void_p
        self.libc.fclose.argtypes = [C.c_void_p]
        signatures = {
            "sepol_set_policydb_from_file": [C.c_void_p],
            "sepol_context_to_sid": [C.c_char_p, C.c_size_t, C.POINTER(C.c_uint32)],
            "sepol_string_to_security_class": [C.c_char_p, C.POINTER(C.c_uint16)],
            "sepol_string_to_av_perm": [C.c_uint16, C.c_char_p, C.POINTER(C.c_uint32)],
            "sepol_compute_av_reason": [C.c_uint32, C.c_uint32, C.c_uint16, C.c_uint32,
                                        C.POINTER(Decision), C.POINTER(C.c_uint)],
        }
        for name, args in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes = args
            function.restype = C.c_int
        file = self.libc.fopen(str(path).encode(), b"rb")
        if not file:
            raise OSError("cannot open policy: " + str(path))
        try:
            self.checked(self.lib.sepol_set_policydb_from_file(file))
        finally:
            self.libc.fclose(file)

    @staticmethod
    def checked(result):
        if result:
            raise RuntimeError("libsepol returned " + str(result))

    def sid(self, context):
        value = C.c_uint32()
        text = context.encode()
        self.checked(self.lib.sepol_context_to_sid(text, len(text) + 1, C.byref(value)))
        return value.value

    def object_class(self, name):
        value = C.c_uint16()
        self.checked(self.lib.sepol_string_to_security_class(name.encode(), C.byref(value)))
        return value.value

    def query(self, source, target, object_class, permission):
        cls = self.object_class(object_class)
        bit = C.c_uint32()
        self.checked(self.lib.sepol_string_to_av_perm(cls, permission.encode(), C.byref(bit)))
        decision, reason = Decision(), C.c_uint()
        self.checked(self.lib.sepol_compute_av_reason(
            self.sid(source), self.sid(target), cls, bit.value, C.byref(decision), C.byref(reason)))
        return bool(decision.allowed & bit.value), reason.value

    def transition(self, source, target, object_class):
        if self.helper_directory is None:
            preferred = Path("/tmp/opencode")
            self.helper_directory = tempfile.TemporaryDirectory(
                prefix="floral-audio-policy-", dir=preferred if preferred.is_dir() else None)
            self.helper = Path(self.helper_directory.name) / "transition"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-x", "c", "-", "-o", str(self.helper),
                            "-Wl,-Bstatic", "-lsepol", "-Wl,-Bdynamic"],
                           input=TRANSITION_HELPER, text=True, check=True)
        result = subprocess.run([str(self.helper), str(self.path), source, target, object_class],
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def close(self):
        if self.helper_directory is not None:
            self.helper_directory.cleanup()


def process(domain, level="s0"):
    return "u:r:" + domain + ":" + level


def file_type(name, level="s0"):
    return "u:object_r:" + name + ":" + level


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("policy", type=Path)
    parser.add_argument("--before-fix", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    policy = Policy(args.policy)
    checks = []

    def av(source, target, object_class, permissions, expected, reason=None):
        for permission in permissions.split():
            allowed, actual_reason = policy.query(source, target, object_class, permission)
            passed = allowed == expected and (reason is None or actual_reason == reason)
            checks.append({"source": source, "target": target, "class": object_class,
                           "permission": permission, "allowed": allowed, "expected": expected,
                           "denialReasonBits": actual_reason, "passed": passed})

    def transition(domain, expected):
        source, target = process(domain), file_type("tmpfs")
        actual = policy.transition(source, target, "file")
        checks.append({"source": source, "target": target, "operation": "file-type-transition",
                       "actual": actual, "expected": file_type(expected),
                       "passed": actual == file_type(expected)})

    audio_region = file_type("audioserver_tmpfs")
    for domain in ["untrusted_app", "untrusted_app_25", "untrusted_app_27",
                   "untrusted_app_29", "priv_app", "isolated_app"]:
        for level in ["s0:c72,c256,c512,c768", "s0:c73,c256,c512,c768"]:
            client = process(domain, level)
            av(client, audio_region, "file", "read getattr map", True)
            av(client, audio_region, "file", "write", not args.before_fix,
               reason=2 if args.before_fix else 0)
            av(client, process("audioserver"), "fd", "use", True)
            av(client, audio_region, "file", "open execute execute_no_trans relabelto", False)
    transition("audioserver", "audioserver_tmpfs")
    transition("floral_device", "floral_device_tmpfs")
    transition("hal_audio_default", "tmpfs" if args.before_fix else "hal_audio_default_tmpfs")
    if not args.before_fix:
        hal_region = file_type("hal_audio_default_tmpfs")
        for domain in ["hal_audio_default", "audioserver"]:
            av(process(domain), hal_region, "file", "read write getattr map", True)
            av(process(domain), hal_region, "file", "execute execute_no_trans relabelto", False)
        for domain in ["untrusted_app", "isolated_app", "shell", "floral_device", "floral_input",
                       "mediacodec", "hal_sensors_default", "hal_gnss_default", "hal_camera_default"]:
            av(process(domain), hal_region, "file", "read write map", False)
        # Ordinary stream FMQs are same-level objects, not MLS-trusted objects.
        av(process("hal_audio_default", "s0:c72,c256,c512,c768"), hal_region,
           "file", "write", False, reason=2)

    # Preserve the three earlier local fixes and the independent Floral FMQ path.
    av(process("hal_sensors_default"), file_type("system_server_tmpfs"),
       "file", "read write getattr map", True)
    for domain in ["mediacodec", "mediaswcodec"]:
        for target in [file_type("appdomain_tmpfs", "s0:c73,c256,c512,c768"),
                       file_type("system_server_tmpfs")]:
            av(process(domain), target, "file", "read write getattr map", True)
    for domain in ["hal_audio_default", "hal_sensors_default", "hal_gnss_default"]:
        av(process(domain), file_type("floral_device_tmpfs"), "file", "read write getattr map", True)
        av(process(domain), file_type("tmpfs"), "file", "read write map", False)
    av(process("hal_audio_default"), audio_region, "file", "read write getattr map", False)
    av(process("audioserver"), process("hal_audio_default"), "fd", "use", True)

    # The object-specific exception must not weaken app data or input isolation.
    app = process("untrusted_app", "s0:c72,c256,c512,c768")
    av(app, file_type("app_data_file", "s0:c73,c256,c512,c768"),
       "file", "open", False, reason=2)
    av(app, file_type("appdomain_tmpfs", "s0:c73,c256,c512,c768"),
       "file", "open", False)
    for domain in ["untrusted_app", "isolated_app", "shell"]:
        av(process(domain), file_type("floral_input_socket_file"), "sock_file", "write", False)
        av(process(domain), file_type("floral_device_tmpfs"), "file", "read write map", False)
    for domain in ["untrusted_app", "hal_audio_default", "audioserver"]:
        av(process(domain), process("kernel"), "security", "setenforce load_policy", False)

    report = {"passed": all(check["passed"] for check in checks), "checks": checks,
              "checkCount": len(checks), "beforeFix": args.before_fix,
              "policySha256": hashlib.sha256(args.policy.read_bytes()).hexdigest(),
              "scope": "userspace TE+MLS decisions and creation transitions; not runtime playback"}
    text = json.dumps(report, indent=2) + "\n"
    policy.close()
    if args.json:
        args.json.write_text(text)
    for check in checks:
        if not check["passed"]:
            print(json.dumps(check))
    print("{}: {} checks ({})".format("PASS" if report["passed"] else "FAIL", len(checks),
                                     "known pre-fix failures" if args.before_fix else "fixed policy"))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
