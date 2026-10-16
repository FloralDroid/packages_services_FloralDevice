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

import static com.floraldroid.input.InputSecurityPolicy.SelinuxState.DISABLED;
import static com.floraldroid.input.InputSecurityPolicy.SelinuxState.ENFORCING;
import static com.floraldroid.input.InputSecurityPolicy.SelinuxState.PERMISSIVE;
import static com.floraldroid.input.InputSecurityPolicy.SelinuxState.UNKNOWN;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

public final class InputSecurityPolicyTest {
  @Test
  public void strictDirectoryMustBeInitialized() {
    final String label = "u:object_r:floral_stream_file:s0";
    InputSecurityPolicy.requireTrustedDirectory(true, 0, 0, 0773, label);
    for (int mode : new int[] {0700, 0755, 0777, 01773}) {
      try {
        InputSecurityPolicy.requireTrustedDirectory(true, 0, 0, mode, label);
        fail("unsafe directory mode");
      } catch (IllegalArgumentException expected) {
      }
    }
    for (String other : new String[] {null, "u:object_r:tmpfs:s0"}) {
      try {
        InputSecurityPolicy.requireTrustedDirectory(true, 0, 0, 0773, other);
        fail("uninitialized directory label");
      } catch (IllegalArgumentException expected) {
      }
    }
    for (int[] attributes : new int[][] {{0, 0, 0}, {1, 1000, 0}, {1, 0, 1000}}) {
      try {
        InputSecurityPolicy.requireTrustedDirectory(
            attributes[0] != 0, attributes[1], attributes[2], 0773, label);
        fail("unsafe directory type/owner");
      } catch (IllegalArgumentException expected) {
      }
    }
  }

  @Test
  public void developmentModeRemainsAvailable() {
    for (InputSecurityPolicy.SelinuxState state : InputSecurityPolicy.SelinuxState.values()) {
      assertFalse(InputSecurityPolicy.resolveStrictMode("0", state));
    }
    assertFalse(InputSecurityPolicy.resolveStrictMode("", DISABLED));
    assertFalse(InputSecurityPolicy.resolveStrictMode("", PERMISSIVE));
  }

  @Test
  public void enforcingDefaultsToStrict() {
    assertTrue(InputSecurityPolicy.resolveStrictMode("", ENFORCING));
    assertTrue(InputSecurityPolicy.resolveStrictMode("1", ENFORCING));
  }

  @Test
  public void invalidConfigurationFailsClosed() {
    for (String value : new String[] {"", "1", "false", "garbage"}) {
      try {
        InputSecurityPolicy.resolveStrictMode(value, UNKNOWN);
        fail("configuration must not disable authentication: " + value);
      } catch (IllegalArgumentException expected) {
      }
    }
    for (InputSecurityPolicy.SelinuxState state : new InputSecurityPolicy.SelinuxState[] {DISABLED, PERMISSIVE}) {
      try {
        InputSecurityPolicy.resolveStrictMode("1", state);
        fail("strict must require Enforcing");
      } catch (IllegalArgumentException expected) {
      }
    }
    for (InputSecurityPolicy.SelinuxState state : InputSecurityPolicy.SelinuxState.values()) {
      for (String value : new String[] {"false", "garbage", "true", " 1"}) {
        try {
          InputSecurityPolicy.resolveStrictMode(value, state);
          fail("invalid flag accepted");
        } catch (IllegalArgumentException expected) {
        }
      }
    }
  }

  @Test
  public void outsideNamespaceRootIsAcceptedAndGuestProcessesAreRejected() {
    assertTrue(InputSecurityPolicy.isTrustedHostPeer(0, 0, 0));
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(123, 0, 0)); // guest root
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(123, 1000, 1000)); // system
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(123, 2000, 2000)); // adb shell
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(123, 10001, 10001)); // app
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(0, 65534, 65534)); // unmapped
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(0, 0, 1000));
    assertFalse(InputSecurityPolicy.isTrustedHostPeer(-1, -1, -1));
  }
}
