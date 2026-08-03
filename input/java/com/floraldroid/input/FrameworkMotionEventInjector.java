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

import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.MotionEvent;

final class FrameworkMotionEventInjector implements InputStateMachine.MotionSink {
  private final InputManager inputManager = InputManager.getInstance();

  @Override
  public boolean inject(InputStateMachine.MotionFrame frame) {
    if (frame.pointers.isEmpty()) {
      return false;
    }
    final int count = frame.pointers.size();
    final MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
    final MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[count];
    for (int index = 0; index < count; ++index) {
      final InputStateMachine.PointerFrame pointer = frame.pointers.get(index);
      final MotionEvent.PointerProperties property = new MotionEvent.PointerProperties();
      property.id = pointer.pointerId;
      property.toolType = MotionEvent.TOOL_TYPE_FINGER;
      properties[index] = property;

      final MotionEvent.PointerCoords coordinate = new MotionEvent.PointerCoords();
      coordinate.x = pointer.x;
      coordinate.y = pointer.y;
      coordinate.pressure = pointer.pressure;
      coordinate.size = pointer.size;
      coordinate.touchMajor = pointer.touchMajor;
      coordinates[index] = coordinate;
    }

    final MotionEvent event =
        MotionEvent.obtain(
            frame.downTimeMillis,
            frame.eventTimeMillis,
            frame.action,
            count,
            properties,
            coordinates,
            0,
            0,
            1.0f,
            1.0f,
            KeyCharacterMap.VIRTUAL_KEYBOARD,
            0,
            InputDevice.SOURCE_TOUCHSCREEN,
            frame.displayId,
            0);
    if (event == null) {
      return false;
    }
    try {
      return inputManager.injectInputEvent(
          event, InputManager.INJECT_INPUT_EVENT_MODE_WAIT_FOR_RESULT);
    } finally {
      event.recycle();
    }
  }
}
