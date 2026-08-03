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
import static org.junit.Assert.fail;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.Test;

public final class OperationProtocolTest {
  private interface ThrowingRunnable {
    void run() throws Exception;
  }

  @Test
  public void roundTripsHeaderAndBindPort() throws Exception {
    final byte[] bindPayload = new byte[] {3, 0, 7, 0, 0, 0, 0, 101};
    final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    OperationProtocol.writePacket(
        new DataOutputStream(bytes),
        OperationProtocol.OP_BIND_INPUT_TARGET,
        OperationProtocol.ROUTE_REQUEST,
        42,
        bindPayload);

    final DataInputStream input =
        new DataInputStream(new ByteArrayInputStream(bytes.toByteArray()));
    final OperationProtocol.Header header = OperationProtocol.readHeader(input);
    assertEquals(OperationProtocol.OP_BIND_INPUT_TARGET, header.operationCode);
    assertEquals(OperationProtocol.ROUTE_REQUEST, header.routeKind);
    assertEquals(42, header.requestId);

    final OperationProtocol.BindRequest request =
        OperationProtocol.parseBindRequest(
            OperationProtocol.readPayload(input, header.payloadSize));
    assertEquals(3, request.targetSlot);
    assertEquals(7, request.displayPort);
    assertEquals(101, request.streamId);
  }

  @Test
  public void rejectsEventWithRequestId() {
    expectIOException(
        () ->
            OperationProtocol.writePacket(
                new DataOutputStream(new ByteArrayOutputStream()),
                OperationProtocol.OP_TOUCH,
                OperationProtocol.ROUTE_EVENT,
                1,
                new byte[24]));
  }

  @Test
  public void parsesNormalizedTouchAndRejectsMalformedCancel() throws Exception {
    final ByteBuffer payload = ByteBuffer.allocate(24).order(ByteOrder.BIG_ENDIAN);
    payload.put((byte) 2);
    payload.put((byte) OperationProtocol.ACTION_MOVE);
    payload.put((byte) 5);
    payload.put((byte) 0);
    payload.putInt(9);
    payload.putInt(11);
    payload.putShort((short) 32768);
    payload.putShort((short) 65535);
    payload.putShort((short) 40000);
    payload.putShort((short) 1000);
    payload.putInt(0);
    final OperationProtocol.TouchEvent event = OperationProtocol.parseTouchEvent(payload.array());
    assertEquals(2, event.targetSlot);
    assertEquals(5, event.pointerId);
    assertEquals(32768, event.x);
    assertEquals(65535, event.y);

    payload.put(1, (byte) OperationProtocol.ACTION_CANCEL);
    expectIOException(() -> OperationProtocol.parseTouchEvent(payload.array()));
  }

  private static void expectIOException(ThrowingRunnable runnable) {
    try {
      runnable.run();
      fail("expected IOException");
    } catch (IOException expected) {
      // Expected.
    } catch (Exception unexpected) {
      throw new AssertionError(unexpected);
    }
  }
}
