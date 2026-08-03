/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.floraldroid.input;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.view.Display;
import android.view.DisplayAddress;
import android.view.Surface;

final class DisplayTargetResolver implements DisplayManager.DisplayListener {
  interface Listener {
    void onDisplayChanged(int displayId, InputStateMachine.TargetDescriptor descriptor);
  }

  private final DisplayManager displayManager;
  private final Listener listener;

  DisplayTargetResolver(Context context, Listener listener) {
    displayManager = context.getSystemService(DisplayManager.class);
    this.listener = listener;
  }

  void start() {
    displayManager.registerDisplayListener(this, null);
  }

  InputStateMachine.TargetDescriptor resolvePort(int port) {
    if (port < 0 || port > 255) {
      return null;
    }
    if (port == 0) {
      return descriptorFor(displayManager.getDisplay(Display.DEFAULT_DISPLAY), 0);
    }
    for (Display display : displayManager.getDisplays()) {
      final DisplayAddress address = display.getAddress();
      if (address instanceof DisplayAddress.Physical
          && ((DisplayAddress.Physical) address).getPort() == port) {
        return descriptorFor(display, port);
      }
    }
    return null;
  }

  @Override
  public void onDisplayAdded(int displayId) {}

  @Override
  public void onDisplayRemoved(int displayId) {
    listener.onDisplayChanged(displayId, null);
  }

  @Override
  public void onDisplayChanged(int displayId) {
    final Display display = displayManager.getDisplay(displayId);
    if (display == null) {
      listener.onDisplayChanged(displayId, null);
      return;
    }
    final DisplayAddress address = display.getAddress();
    final int port;
    if (displayId == Display.DEFAULT_DISPLAY) {
      port = 0;
    } else if (address instanceof DisplayAddress.Physical) {
      port = ((DisplayAddress.Physical) address).getPort();
    } else {
      listener.onDisplayChanged(displayId, null);
      return;
    }
    listener.onDisplayChanged(displayId, descriptorFor(display, port));
  }

  private static InputStateMachine.TargetDescriptor descriptorFor(Display display, int port) {
    if (display == null || !display.isValid()) {
      return null;
    }
    final Point size = new Point();
    display.getRealSize(size);
    if (size.x <= 0 || size.y <= 0) {
      return null;
    }
    return new InputStateMachine.TargetDescriptor(
        display.getDisplayId(), port, size.x, size.y, rotationDegrees(display.getRotation()));
  }

  private static int rotationDegrees(int rotation) {
    switch (rotation) {
      case Surface.ROTATION_90:
        return 90;
      case Surface.ROTATION_180:
        return 180;
      case Surface.ROTATION_270:
        return 270;
      case Surface.ROTATION_0:
      default:
        return 0;
    }
  }
}
