/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *      http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.floraldroid.input;

/** Boot-time mode selection and kernel peer-credential checks, not protocol-supplied identity. */
final class InputSecurityPolicy {
  enum SelinuxState { UNKNOWN, DISABLED, PERMISSIVE, ENFORCING }

  static void requireTrustedDirectory(boolean directory, int uid, int gid, int mode, String label) {
    if (!directory || uid != 0 || gid != 0 || (mode & 07777) != 0773
        || !"u:object_r:floral_stream_file:s0".equals(label)) {
      throw new IllegalArgumentException("Strict input requires its initialized IPC directory");
    }
  }

  static boolean resolveStrictMode(String value, SelinuxState state) {
    if (value.isEmpty()) {
      if (state == SelinuxState.ENFORCING) {
        return true;
      }
      if (state == SelinuxState.DISABLED || state == SelinuxState.PERMISSIVE) {
        return false;
      }
      throw new IllegalArgumentException("Cannot determine SELinux state; refusing input endpoint");
    }
    if (value.equals("0")) {
      return false;
    }
    if (value.equals("1") && state == SelinuxState.ENFORCING) {
      return true;
    }
    throw new IllegalArgumentException("Invalid input strict-mode configuration or enforcing state");
  }

  static boolean isTrustedHostPeer(int pid, int uid, int gid) {
    // SO_PEERCRED translates the sender PID into the receiver's PID namespace.
    // An outside-namespace host process is invisible (PID 0). Guest root is
    // visible with a positive PID and must NOT be treated as the trusted host.
    // This also requires a PRIVATE per-instance host transport directory:
    // sibling-namespace root is invisible too. Credentials are not a replacement
    // for that mount/DAC boundary or a cryptographic host identity.
    return pid == 0 && uid == 0 && gid == 0;
  }

  private InputSecurityPolicy() {}
}
