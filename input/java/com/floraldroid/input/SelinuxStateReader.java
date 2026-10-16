/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.floraldroid.input;

import java.io.IOException;

/** Unlike SELinux.isSELinuxEnforced(), this preserves failed queries as errors. */
final class SelinuxStateReader {
  static InputSecurityPolicy.SelinuxState read() throws IOException {
    return decode(Native.query());
  }

  static InputSecurityPolicy.SelinuxState decode(int value) throws IOException {
    switch (value) {
      case 1: return InputSecurityPolicy.SelinuxState.DISABLED;
      case 2: return InputSecurityPolicy.SelinuxState.PERMISSIVE;
      case 3: return InputSecurityPolicy.SelinuxState.ENFORCING;
      default: throw new IOException("Invalid native SELinux state: " + value);
    }
  }

  // Load only when a state query is required. Explicit development mode neither
  // reads selinuxfs nor depends on this library being available.
  private static final class Native {
    static {
      System.loadLibrary("floral_input_security");
    }
    static native int query() throws IOException;
  }

  private SelinuxStateReader() {}
}
