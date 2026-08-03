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

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

final class OperationProtocol {
  static final int MAGIC = 0x46444f31; // FDO1
  static final int VERSION = 1;
  static final int HEADER_SIZE = 24;
  static final int MAX_PAYLOAD_SIZE = 64 * 1024;

  static final int ROUTE_REQUEST = 0x2000;
  static final int ROUTE_RESPONSE = 0x2100;
  static final int ROUTE_EVENT = 0x2200;
  static final int ROUTE_ERROR = 0x2300;

  static final int OP_GENERIC_ERROR = 0x0000;
  static final int OP_BIND_INPUT_TARGET = 0x0100;
  static final int OP_UNBIND_INPUT_TARGET = 0x0101;
  static final int OP_TARGET_INVALIDATED = 0x0102;
  static final int OP_TOUCH = 0x0200;

  static final int ERROR_UNSUPPORTED_MESSAGE = 1;
  static final int ERROR_MALFORMED_MESSAGE = 2;
  static final int ERROR_INVALID_REQUEST_ID = 3;
  static final int ERROR_INTERNAL = 4;

  static final int MODE_EXCLUSIVE = 0;
  static final int ACTION_DOWN = 0;
  static final int ACTION_MOVE = 1;
  static final int ACTION_UP = 2;
  static final int ACTION_CANCEL = 3;

  static final int INVALIDATION_DISPLAY_REMOVED = 1;
  static final int INVALIDATION_GEOMETRY_CHANGED = 2;

  private OperationProtocol() {}

  static final class Header {
    final int operationCode;
    final int routeKind;
    final long requestId;
    final int payloadSize;

    Header(int operationCode, int routeKind, long requestId, int payloadSize) {
      this.operationCode = operationCode;
      this.routeKind = routeKind;
      this.requestId = requestId;
      this.payloadSize = payloadSize;
    }
  }

  static final class BindRequest {
    final int targetSlot;
    final int mode;
    final int displayPort;
    final long streamId;

    BindRequest(int targetSlot, int mode, int displayPort, long streamId) {
      this.targetSlot = targetSlot;
      this.mode = mode;
      this.displayPort = displayPort;
      this.streamId = streamId;
    }
  }

  static final class TouchEvent {
    final int targetSlot;
    final int action;
    final int pointerId;
    final long inputEpoch;
    final long sequence;
    final int x;
    final int y;
    final int pressure;
    final int touchMajor;

    TouchEvent(
        int targetSlot,
        int action,
        int pointerId,
        long inputEpoch,
        long sequence,
        int x,
        int y,
        int pressure,
        int touchMajor) {
      this.targetSlot = targetSlot;
      this.action = action;
      this.pointerId = pointerId;
      this.inputEpoch = inputEpoch;
      this.sequence = sequence;
      this.x = x;
      this.y = y;
      this.pressure = pressure;
      this.touchMajor = touchMajor;
    }
  }

  static Header readHeader(DataInputStream input) throws IOException {
    final int magic;
    try {
      magic = input.readInt();
    } catch (EOFException error) {
      throw error;
    }
    if (magic != MAGIC) {
      throw new IOException("operation packet magic does not match");
    }
    if (input.readUnsignedShort() != VERSION) {
      throw new IOException("operation packet version is unsupported");
    }
    if (input.readUnsignedShort() != HEADER_SIZE) {
      throw new IOException("operation packet header size does not match");
    }
    final int operationCode = input.readUnsignedShort();
    final int routeKind = input.readUnsignedShort();
    final long requestId = Integer.toUnsignedLong(input.readInt());
    final int payloadSize = input.readInt();
    if (input.readInt() != 0) {
      throw new IOException("operation packet reserved field is not zero");
    }
    if (!isKnownRoute(routeKind)) {
      throw new IOException("operation packet route or kind is unsupported");
    }
    if ((routeKind == ROUTE_EVENT) != (requestId == 0)) {
      throw new IOException("operation request id does not match packet kind");
    }
    if (payloadSize < 0 || payloadSize > MAX_PAYLOAD_SIZE) {
      throw new IOException("operation payload exceeds protocol limit");
    }
    return new Header(operationCode, routeKind, requestId, payloadSize);
  }

  static byte[] readPayload(DataInputStream input, int size) throws IOException {
    final byte[] payload = new byte[size];
    input.readFully(payload);
    return payload;
  }

  static void writePacket(
      DataOutputStream output, int operationCode, int routeKind, long requestId, byte[] payload)
      throws IOException {
    if (!isKnownRoute(routeKind)
        || (routeKind == ROUTE_EVENT) != (requestId == 0)
        || requestId < 0
        || requestId > 0xffffffffL
        || payload == null
        || payload.length > MAX_PAYLOAD_SIZE) {
      throw new IOException("invalid outgoing operation packet");
    }
    output.writeInt(MAGIC);
    output.writeShort(VERSION);
    output.writeShort(HEADER_SIZE);
    output.writeShort(operationCode);
    output.writeShort(routeKind);
    output.writeInt((int) requestId);
    output.writeInt(payload.length);
    output.writeInt(0);
    output.write(payload);
    output.flush();
  }

  static BindRequest parseBindRequest(byte[] payload) throws IOException {
    requireSize(payload, 8, "bind input target request");
    if (u8(payload[3]) != 0) {
      throw new IOException("bind input target reserved field is not zero");
    }
    final ByteBuffer buffer = wrap(payload);
    final int targetSlot = u8(payload[0]);
    final int mode = u8(payload[1]);
    final int displayPort = u8(payload[2]);
    final long streamId = Integer.toUnsignedLong(buffer.getInt(4));
    if (mode != MODE_EXCLUSIVE || streamId == 0) {
      throw new IOException("bind input target request is invalid");
    }
    return new BindRequest(targetSlot, mode, displayPort, streamId);
  }

  static int parseUnbindRequest(byte[] payload) throws IOException {
    requireSize(payload, 4, "unbind input target request");
    if (u8(payload[1]) != 0 || u8(payload[2]) != 0 || u8(payload[3]) != 0) {
      throw new IOException("unbind input target reserved field is not zero");
    }
    return u8(payload[0]);
  }

  static TouchEvent parseTouchEvent(byte[] payload) throws IOException {
    requireSize(payload, 24, "touch event");
    final ByteBuffer buffer = wrap(payload);
    if (u8(payload[3]) != 0 || buffer.getInt(20) != 0) {
      throw new IOException("touch event reserved field is not zero");
    }
    final TouchEvent event =
        new TouchEvent(
            u8(payload[0]),
            u8(payload[1]),
            u8(payload[2]),
            Integer.toUnsignedLong(buffer.getInt(4)),
            Integer.toUnsignedLong(buffer.getInt(8)),
            Short.toUnsignedInt(buffer.getShort(12)),
            Short.toUnsignedInt(buffer.getShort(14)),
            Short.toUnsignedInt(buffer.getShort(16)),
            Short.toUnsignedInt(buffer.getShort(18)));
    if (event.action < ACTION_DOWN
        || event.action > ACTION_CANCEL
        || event.pointerId > 31
        || event.inputEpoch == 0
        || event.sequence == 0) {
      throw new IOException("touch event identity is invalid");
    }
    if ((event.action == ACTION_DOWN || event.action == ACTION_MOVE) && event.pressure == 0) {
      throw new IOException("touch down or move has zero pressure");
    }
    if (event.action == ACTION_CANCEL
        && (event.pointerId != 0
            || event.x != 0
            || event.y != 0
            || event.pressure != 0
            || event.touchMajor != 0)) {
      throw new IOException("touch cancel contains pointer data");
    }
    return event;
  }

  static byte[] bindResponse(
      int result,
      int targetSlot,
      int displayPort,
      long streamId,
      long inputEpoch,
      int logicalWidth,
      int logicalHeight,
      int rotation) {
    final ByteBuffer output = allocate(28);
    output.putInt(result);
    output.put((byte) targetSlot);
    output.put((byte) displayPort);
    output.putShort((short) 0);
    output.putInt((int) streamId);
    output.putInt((int) inputEpoch);
    output.putInt(logicalWidth);
    output.putInt(logicalHeight);
    output.putShort((short) rotation);
    output.putShort((short) 0);
    return output.array();
  }

  static byte[] unbindResponse(int result, int targetSlot) {
    final ByteBuffer output = allocate(8);
    output.putInt(result);
    output.put((byte) targetSlot);
    output.put(new byte[3]);
    return output.array();
  }

  static byte[] targetInvalidated(int targetSlot, int reason, long streamId, long inputEpoch) {
    final ByteBuffer output = allocate(12);
    output.put((byte) targetSlot);
    output.put((byte) reason);
    output.putShort((short) 0);
    output.putInt((int) streamId);
    output.putInt((int) inputEpoch);
    return output.array();
  }

  static byte[] errorResponse(int error, int failedOperationCode) {
    final ByteBuffer output = allocate(8);
    output.putInt(error);
    output.putShort((short) failedOperationCode);
    output.putShort((short) 0);
    return output.array();
  }

  private static boolean isKnownRoute(int routeKind) {
    return routeKind == ROUTE_REQUEST
        || routeKind == ROUTE_RESPONSE
        || routeKind == ROUTE_EVENT
        || routeKind == ROUTE_ERROR;
  }

  private static void requireSize(byte[] payload, int expected, String name) throws IOException {
    if (payload == null || payload.length != expected) {
      throw new IOException(name + " payload size is invalid");
    }
  }

  private static ByteBuffer wrap(byte[] payload) {
    return ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
  }

  private static ByteBuffer allocate(int size) {
    return ByteBuffer.allocate(size).order(ByteOrder.BIG_ENDIAN);
  }

  private static int u8(byte value) {
    return Byte.toUnsignedInt(value);
  }
}
