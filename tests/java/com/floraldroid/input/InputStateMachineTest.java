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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

public final class InputStateMachineTest {
  private static final class FakeClock implements InputStateMachine.Clock {
    long now = 100;

    @Override
    public long uptimeMillis() {
      return now++;
    }
  }

  private static final class RecordingSink implements InputStateMachine.MotionSink {
    final List<InputStateMachine.MotionFrame> frames = new ArrayList<>();

    @Override
    public boolean inject(InputStateMachine.MotionFrame frame) {
      frames.add(frame);
      return true;
    }
  }

  @Test
  public void emitsCompleteMultiPointerGestureForBoundDisplay() {
    final FakeClock clock = new FakeClock();
    final RecordingSink sink = new RecordingSink();
    final InputStateMachine machine = new InputStateMachine(clock, sink, null);
    final InputStateMachine.BindOutcome binding =
        machine.bind(
            2,
            OperationProtocol.MODE_EXCLUSIVE,
            101,
            new InputStateMachine.TargetDescriptor(7, 3, 1920, 1080, 0));
    assertEquals(InputStateMachine.RESULT_APPLIED, binding.result);

    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(2, OperationProtocol.ACTION_DOWN, 1, binding.inputEpoch, 1, 0, 0, 65535)));
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(
                2, OperationProtocol.ACTION_DOWN, 7, binding.inputEpoch, 2, 65535, 65535, 65535)));
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(
                2, OperationProtocol.ACTION_MOVE, 1, binding.inputEpoch, 3, 32768, 16384, 50000)));
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(2, OperationProtocol.ACTION_UP, 1, binding.inputEpoch, 4, 32768, 16384, 0)));
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(2, OperationProtocol.ACTION_UP, 7, binding.inputEpoch, 5, 65535, 65535, 0)));

    assertEquals(5, sink.frames.size());
    assertEquals(InputStateMachine.MOTION_ACTION_DOWN, sink.frames.get(0).action);
    assertEquals(
        InputStateMachine.MOTION_ACTION_POINTER_DOWN
            | (1 << InputStateMachine.MOTION_ACTION_POINTER_INDEX_SHIFT),
        sink.frames.get(1).action);
    assertEquals(InputStateMachine.MOTION_ACTION_MOVE, sink.frames.get(2).action);
    assertEquals(2, sink.frames.get(2).pointers.size());
    assertEquals(7, sink.frames.get(2).displayId);
    assertEquals(InputStateMachine.MOTION_ACTION_POINTER_UP, sink.frames.get(3).action);
    assertEquals(InputStateMachine.MOTION_ACTION_UP, sink.frames.get(4).action);
    assertEquals(1919.0f, sink.frames.get(1).pointers.get(1).x, 0.01f);
    assertEquals(1079.0f, sink.frames.get(1).pointers.get(1).y, 0.01f);
  }

  @Test
  public void unbindAndDisconnectCancelActivePointers() {
    final RecordingSink sink = new RecordingSink();
    final InputStateMachine machine = new InputStateMachine(new FakeClock(), sink, null);
    InputStateMachine.BindOutcome binding =
        machine.bind(
            1,
            OperationProtocol.MODE_EXCLUSIVE,
            100,
            new InputStateMachine.TargetDescriptor(3, 1, 800, 600, 0));
    machine.onTouch(
        touch(1, OperationProtocol.ACTION_DOWN, 0, binding.inputEpoch, 1, 10, 20, 65535));
    assertEquals(InputStateMachine.RESULT_APPLIED, machine.unbind(1));
    assertEquals(InputStateMachine.MOTION_ACTION_CANCEL, sink.frames.get(1).action);

    binding =
        machine.bind(
            1,
            OperationProtocol.MODE_EXCLUSIVE,
            100,
            new InputStateMachine.TargetDescriptor(3, 1, 800, 600, 0));
    machine.onTouch(
        touch(1, OperationProtocol.ACTION_DOWN, 0, binding.inputEpoch, 1, 10, 20, 65535));
    machine.resetAll();
    assertEquals(InputStateMachine.MOTION_ACTION_CANCEL, sink.frames.get(3).action);
  }

  @Test
  public void geometryChangeCancelsAndInvalidatesEpoch() {
    final RecordingSink sink = new RecordingSink();
    final List<InputStateMachine.InvalidatedTarget> invalidated = new ArrayList<>();
    final InputStateMachine machine =
        new InputStateMachine(new FakeClock(), sink, invalidated::add);
    final InputStateMachine.BindOutcome binding =
        machine.bind(
            4,
            OperationProtocol.MODE_EXCLUSIVE,
            104,
            new InputStateMachine.TargetDescriptor(8, 4, 1280, 720, 0));
    machine.onTouch(
        touch(4, OperationProtocol.ACTION_DOWN, 0, binding.inputEpoch, 1, 10, 20, 65535));

    machine.handleDisplayChange(8, new InputStateMachine.TargetDescriptor(8, 4, 720, 1280, 90));
    assertEquals(InputStateMachine.MOTION_ACTION_CANCEL, sink.frames.get(1).action);
    assertEquals(1, invalidated.size());
    assertEquals(OperationProtocol.INVALIDATION_GEOMETRY_CHANGED, invalidated.get(0).reason);
    assertEquals(
        InputStateMachine.RESULT_INVALID_TARGET,
        machine.onTouch(
            touch(4, OperationProtocol.ACTION_MOVE, 0, binding.inputEpoch, 2, 20, 30, 65535)));
  }

  @Test
  public void rejectsConflictingLeaseAndStaleSequence() {
    final RecordingSink sink = new RecordingSink();
    final InputStateMachine machine = new InputStateMachine(new FakeClock(), sink, null);
    final InputStateMachine.TargetDescriptor descriptor =
        new InputStateMachine.TargetDescriptor(9, 5, 1000, 1000, 0);
    final InputStateMachine.BindOutcome binding =
        machine.bind(1, OperationProtocol.MODE_EXCLUSIVE, 201, descriptor);
    assertEquals(
        InputStateMachine.RESULT_TARGET_BUSY,
        machine.bind(2, OperationProtocol.MODE_EXCLUSIVE, 202, descriptor).result);
    assertEquals(
        InputStateMachine.RESULT_TARGET_BUSY,
        machine.bind(
                2,
                OperationProtocol.MODE_EXCLUSIVE,
                201,
                new InputStateMachine.TargetDescriptor(10, 6, 1000, 1000, 0))
            .result);
    machine.onTouch(touch(1, OperationProtocol.ACTION_DOWN, 0, binding.inputEpoch, 4, 1, 1, 65535));
    assertEquals(
        InputStateMachine.RESULT_STALE_EPOCH,
        machine.onTouch(
            touch(1, OperationProtocol.ACTION_MOVE, 0, binding.inputEpoch + 1, 100, 2, 2, 65535)));
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(1, OperationProtocol.ACTION_MOVE, 0, binding.inputEpoch, 5, 2, 2, 65535)));
    assertEquals(
        InputStateMachine.RESULT_INVALID_STATE,
        machine.onTouch(
            touch(1, OperationProtocol.ACTION_MOVE, 0, binding.inputEpoch, 5, 3, 3, 65535)));
    assertTrue(
        sink.frames.stream()
            .anyMatch(frame -> frame.action == InputStateMachine.MOTION_ACTION_CANCEL));
  }

  @Test
  public void malformedEventCancellationCanClearOnlyItsTargetGesture() {
    final RecordingSink sink = new RecordingSink();
    final InputStateMachine machine = new InputStateMachine(new FakeClock(), sink, null);
    final InputStateMachine.BindOutcome first =
        machine.bind(
            1,
            OperationProtocol.MODE_EXCLUSIVE,
            301,
            new InputStateMachine.TargetDescriptor(11, 7, 1000, 1000, 0));
    final InputStateMachine.BindOutcome second =
        machine.bind(
            2,
            OperationProtocol.MODE_EXCLUSIVE,
            302,
            new InputStateMachine.TargetDescriptor(12, 8, 1000, 1000, 0));
    machine.onTouch(touch(1, OperationProtocol.ACTION_DOWN, 0, first.inputEpoch, 1, 1, 1, 65535));
    machine.onTouch(touch(2, OperationProtocol.ACTION_DOWN, 0, second.inputEpoch, 1, 1, 1, 65535));

    machine.cancelTarget(1);

    assertEquals(InputStateMachine.MOTION_ACTION_CANCEL, sink.frames.get(2).action);
    assertEquals(11, sink.frames.get(2).displayId);
    assertEquals(
        InputStateMachine.RESULT_APPLIED,
        machine.onTouch(
            touch(2, OperationProtocol.ACTION_MOVE, 0, second.inputEpoch, 2, 2, 2, 65535)));
  }

  private static OperationProtocol.TouchEvent touch(
      int slot, int action, int pointerId, long epoch, long sequence, int x, int y, int pressure) {
    return new OperationProtocol.TouchEvent(
        slot, action, pointerId, epoch, sequence, x, y, pressure, 1000);
  }
}
