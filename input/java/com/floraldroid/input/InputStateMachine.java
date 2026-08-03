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

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class InputStateMachine {
  static final int RESULT_APPLIED = 0;
  static final int RESULT_UNCHANGED = 1;
  static final int RESULT_INVALID_TARGET = 2;
  static final int RESULT_TARGET_BUSY = 3;
  static final int RESULT_UNKNOWN_STREAM = 4;
  static final int RESULT_STALE_EPOCH = 5;
  static final int RESULT_INVALID_STATE = 6;
  static final int RESULT_INJECTION_FAILED = 7;

  static final int MOTION_ACTION_DOWN = 0;
  static final int MOTION_ACTION_UP = 1;
  static final int MOTION_ACTION_MOVE = 2;
  static final int MOTION_ACTION_CANCEL = 3;
  static final int MOTION_ACTION_POINTER_DOWN = 5;
  static final int MOTION_ACTION_POINTER_UP = 6;
  static final int MOTION_ACTION_POINTER_INDEX_SHIFT = 8;

  interface Clock {
    long uptimeMillis();
  }

  interface MotionSink {
    boolean inject(MotionFrame frame);
  }

  interface InvalidationListener {
    void onTargetInvalidated(InvalidatedTarget target);
  }

  static final class TargetDescriptor {
    final int displayId;
    final int displayPort;
    final int logicalWidth;
    final int logicalHeight;
    final int rotation;

    TargetDescriptor(
        int displayId, int displayPort, int logicalWidth, int logicalHeight, int rotation) {
      this.displayId = displayId;
      this.displayPort = displayPort;
      this.logicalWidth = logicalWidth;
      this.logicalHeight = logicalHeight;
      this.rotation = rotation;
    }

    boolean sameGeometry(TargetDescriptor other) {
      return other != null
          && displayId == other.displayId
          && displayPort == other.displayPort
          && logicalWidth == other.logicalWidth
          && logicalHeight == other.logicalHeight
          && rotation == other.rotation;
    }
  }

  static final class BindOutcome {
    final int result;
    final int targetSlot;
    final long streamId;
    final long inputEpoch;
    final TargetDescriptor descriptor;

    BindOutcome(
        int result, int targetSlot, long streamId, long inputEpoch, TargetDescriptor descriptor) {
      this.result = result;
      this.targetSlot = targetSlot;
      this.streamId = streamId;
      this.inputEpoch = inputEpoch;
      this.descriptor = descriptor;
    }
  }

  static final class InvalidatedTarget {
    final int targetSlot;
    final int reason;
    final long streamId;
    final long inputEpoch;

    InvalidatedTarget(int targetSlot, int reason, long streamId, long inputEpoch) {
      this.targetSlot = targetSlot;
      this.reason = reason;
      this.streamId = streamId;
      this.inputEpoch = inputEpoch;
    }
  }

  static final class PointerFrame {
    final int pointerId;
    final float x;
    final float y;
    final float pressure;
    final float size;
    final float touchMajor;

    PointerFrame(int pointerId, float x, float y, float pressure, float size, float touchMajor) {
      this.pointerId = pointerId;
      this.x = x;
      this.y = y;
      this.pressure = pressure;
      this.size = size;
      this.touchMajor = touchMajor;
    }
  }

  static final class MotionFrame {
    final int displayId;
    final long downTimeMillis;
    final long eventTimeMillis;
    final int action;
    final List<PointerFrame> pointers;

    MotionFrame(
        int displayId,
        long downTimeMillis,
        long eventTimeMillis,
        int action,
        List<PointerFrame> pointers) {
      this.displayId = displayId;
      this.downTimeMillis = downTimeMillis;
      this.eventTimeMillis = eventTimeMillis;
      this.action = action;
      this.pointers = pointers;
    }
  }

  private static final class PointerState {
    int x;
    int y;
    int pressure;
    int touchMajor;

    PointerState(OperationProtocol.TouchEvent event) {
      update(event);
    }

    void update(OperationProtocol.TouchEvent event) {
      x = event.x;
      y = event.y;
      pressure = event.pressure;
      touchMajor = event.touchMajor;
    }
  }

  private static final class TargetState {
    final int targetSlot;
    final long streamId;
    final long inputEpoch;
    final TargetDescriptor descriptor;
    final TreeMap<Integer, PointerState> pointers = new TreeMap<>();
    long lastSequence;
    long downTimeMillis;

    TargetState(int targetSlot, long streamId, long inputEpoch, TargetDescriptor descriptor) {
      this.targetSlot = targetSlot;
      this.streamId = streamId;
      this.inputEpoch = inputEpoch;
      this.descriptor = descriptor;
    }
  }

  private final Clock clock;
  private final MotionSink sink;
  private final InvalidationListener invalidationListener;
  private final TreeMap<Integer, TargetState> targets = new TreeMap<>();
  private long nextEpoch = 1;

  InputStateMachine(Clock clock, MotionSink sink, InvalidationListener invalidationListener) {
    this.clock = clock;
    this.sink = sink;
    this.invalidationListener = invalidationListener;
  }

  synchronized BindOutcome bind(
      int targetSlot, int mode, long streamId, TargetDescriptor descriptor) {
    if (targetSlot < 0
        || targetSlot > 255
        || mode != OperationProtocol.MODE_EXCLUSIVE
        || streamId == 0
        || descriptor == null
        || descriptor.logicalWidth <= 0
        || descriptor.logicalHeight <= 0
        || !isKnownRotation(descriptor.rotation)) {
      return new BindOutcome(RESULT_INVALID_TARGET, targetSlot, streamId, 0, descriptor);
    }
    final TargetState current = targets.get(targetSlot);
    if (current != null
        && current.streamId == streamId
        && current.descriptor.sameGeometry(descriptor)) {
      return new BindOutcome(
          RESULT_UNCHANGED, targetSlot, streamId, current.inputEpoch, descriptor);
    }
    for (TargetState candidate : targets.values()) {
      if (candidate.targetSlot != targetSlot
          && (candidate.descriptor.displayId == descriptor.displayId
              || candidate.streamId == streamId)) {
        return new BindOutcome(RESULT_TARGET_BUSY, targetSlot, streamId, 0, descriptor);
      }
    }
    if (current != null) {
      cancelPointers(current);
      targets.remove(targetSlot);
    }
    final long epoch = allocateEpoch();
    targets.put(targetSlot, new TargetState(targetSlot, streamId, epoch, descriptor));
    return new BindOutcome(RESULT_APPLIED, targetSlot, streamId, epoch, descriptor);
  }

  synchronized int unbind(int targetSlot) {
    final TargetState target = targets.remove(targetSlot);
    if (target == null) {
      return RESULT_INVALID_TARGET;
    }
    cancelPointers(target);
    return RESULT_APPLIED;
  }

  synchronized void cancelTarget(int targetSlot) {
    final TargetState target = targets.get(targetSlot);
    if (target != null) {
      cancelPointers(target);
    }
  }

  synchronized int onTouch(OperationProtocol.TouchEvent event) {
    final TargetState target = targets.get(event.targetSlot);
    if (target == null) {
      return RESULT_INVALID_TARGET;
    }
    if (event.inputEpoch != target.inputEpoch) {
      return RESULT_STALE_EPOCH;
    }
    if (event.sequence <= target.lastSequence) {
      cancelPointers(target);
      return RESULT_INVALID_STATE;
    }
    target.lastSequence = event.sequence;
    switch (event.action) {
      case OperationProtocol.ACTION_DOWN:
        return pointerDown(target, event);
      case OperationProtocol.ACTION_MOVE:
        return pointerMove(target, event);
      case OperationProtocol.ACTION_UP:
        return pointerUp(target, event);
      case OperationProtocol.ACTION_CANCEL:
        if (target.pointers.isEmpty()) {
          return RESULT_INVALID_STATE;
        }
        return cancelPointers(target) ? RESULT_APPLIED : RESULT_INJECTION_FAILED;
      default:
        cancelPointers(target);
        return RESULT_INVALID_STATE;
    }
  }

  void handleDisplayChange(int displayId, TargetDescriptor currentDescriptor) {
    final List<InvalidatedTarget> invalidated = new ArrayList<>();
    synchronized (this) {
      final List<Integer> slotsToRemove = new ArrayList<>();
      for (TargetState target : targets.values()) {
        if (target.descriptor.displayId != displayId) {
          continue;
        }
        final int reason =
            currentDescriptor == null
                ? OperationProtocol.INVALIDATION_DISPLAY_REMOVED
                : OperationProtocol.INVALIDATION_GEOMETRY_CHANGED;
        if (currentDescriptor == null || !target.descriptor.sameGeometry(currentDescriptor)) {
          cancelPointers(target);
          slotsToRemove.add(target.targetSlot);
          invalidated.add(
              new InvalidatedTarget(target.targetSlot, reason, target.streamId, target.inputEpoch));
        }
      }
      for (int slot : slotsToRemove) {
        targets.remove(slot);
      }
    }
    if (invalidationListener != null) {
      for (InvalidatedTarget target : invalidated) {
        invalidationListener.onTargetInvalidated(target);
      }
    }
  }

  synchronized void resetAll() {
    for (TargetState target : targets.values()) {
      cancelPointers(target);
    }
    targets.clear();
  }

  private int pointerDown(TargetState target, OperationProtocol.TouchEvent event) {
    if (target.pointers.containsKey(event.pointerId) || target.pointers.size() >= 32) {
      cancelPointers(target);
      return RESULT_INVALID_STATE;
    }
    if (target.pointers.isEmpty()) {
      target.downTimeMillis = clock.uptimeMillis();
    }
    target.pointers.put(event.pointerId, new PointerState(event));
    final int pointerIndex = target.pointers.headMap(event.pointerId).size();
    final int action =
        target.pointers.size() == 1
            ? MOTION_ACTION_DOWN
            : MOTION_ACTION_POINTER_DOWN | (pointerIndex << MOTION_ACTION_POINTER_INDEX_SHIFT);
    if (!inject(target, action)) {
      cancelPointers(target);
      return RESULT_INJECTION_FAILED;
    }
    return RESULT_APPLIED;
  }

  private int pointerMove(TargetState target, OperationProtocol.TouchEvent event) {
    final PointerState pointer = target.pointers.get(event.pointerId);
    if (pointer == null) {
      cancelPointers(target);
      return RESULT_INVALID_STATE;
    }
    pointer.update(event);
    if (!inject(target, MOTION_ACTION_MOVE)) {
      cancelPointers(target);
      return RESULT_INJECTION_FAILED;
    }
    return RESULT_APPLIED;
  }

  private int pointerUp(TargetState target, OperationProtocol.TouchEvent event) {
    final PointerState pointer = target.pointers.get(event.pointerId);
    if (pointer == null) {
      cancelPointers(target);
      return RESULT_INVALID_STATE;
    }
    pointer.update(event);
    final int pointerIndex = target.pointers.headMap(event.pointerId).size();
    final int action =
        target.pointers.size() == 1
            ? MOTION_ACTION_UP
            : MOTION_ACTION_POINTER_UP | (pointerIndex << MOTION_ACTION_POINTER_INDEX_SHIFT);
    if (!inject(target, action)) {
      cancelPointers(target);
      return RESULT_INJECTION_FAILED;
    }
    target.pointers.remove(event.pointerId);
    if (target.pointers.isEmpty()) {
      target.downTimeMillis = 0;
    }
    return RESULT_APPLIED;
  }

  private boolean cancelPointers(TargetState target) {
    if (target.pointers.isEmpty()) {
      return true;
    }
    final boolean injected = inject(target, MOTION_ACTION_CANCEL);
    target.pointers.clear();
    target.downTimeMillis = 0;
    return injected;
  }

  private boolean inject(TargetState target, int action) {
    final List<PointerFrame> pointers = new ArrayList<>(target.pointers.size());
    final float maximumTouchMajor =
        Math.min(target.descriptor.logicalWidth, target.descriptor.logicalHeight);
    for (Map.Entry<Integer, PointerState> entry : target.pointers.entrySet()) {
      final PointerState pointer = entry.getValue();
      pointers.add(
          new PointerFrame(
              entry.getKey(),
              normalizeCoordinate(pointer.x, target.descriptor.logicalWidth),
              normalizeCoordinate(pointer.y, target.descriptor.logicalHeight),
              pointer.pressure / 65535.0f,
              pointer.touchMajor / 65535.0f,
              pointer.touchMajor / 65535.0f * maximumTouchMajor));
    }
    return sink.inject(
        new MotionFrame(
            target.descriptor.displayId,
            target.downTimeMillis,
            clock.uptimeMillis(),
            action,
            pointers));
  }

  private long allocateEpoch() {
    final long epoch = nextEpoch;
    nextEpoch = nextEpoch == 0xffffffffL ? 1 : nextEpoch + 1;
    return epoch;
  }

  private static float normalizeCoordinate(int value, int dimension) {
    return value / 65535.0f * Math.max(0, dimension - 1);
  }

  private static boolean isKnownRotation(int rotation) {
    return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
  }
}
