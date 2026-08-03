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
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.Log;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;

final class OperationClient
    implements Runnable, InputStateMachine.InvalidationListener, DisplayTargetResolver.Listener {
  private static final String TAG = "FloralInput";
  private static final String DEFAULT_SOCKET_PATH = "/mnt/vendor/floral_stream/operate.sock";
  private static final long RECONNECT_DELAY_MILLIS = 250;

  private final Object outputLock = new Object();
  private final String socketPath;
  private final DisplayTargetResolver resolver;
  private final InputStateMachine stateMachine;

  private DataOutputStream activeOutput;
  private LocalSocket activeSocket;

  OperationClient(Context context) {
    socketPath = SystemProperties.get("ro.boot.floral_operate_socket", DEFAULT_SOCKET_PATH);
    resolver = new DisplayTargetResolver(context, this);
    stateMachine =
        new InputStateMachine(SystemClock::uptimeMillis, new FrameworkMotionEventInjector(), this);
  }

  void start() {
    resolver.start();
    final Thread thread = new Thread(this, "floral-input-operation");
    thread.start();
  }

  @Override
  public void run() {
    boolean waitingLogged = false;
    while (true) {
      try (LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_STREAM)) {
        socket.connect(new LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM));
        waitingLogged = false;
        Log.i(TAG, "Connected operation socket: " + socketPath);
        processConnection(socket);
      } catch (IOException error) {
        if (!waitingLogged) {
          Log.i(TAG, "Waiting for operation socket " + socketPath + ": " + error.getMessage());
          waitingLogged = true;
        }
      } finally {
        synchronized (outputLock) {
          activeOutput = null;
          activeSocket = null;
        }
        stateMachine.resetAll();
      }
      SystemClock.sleep(RECONNECT_DELAY_MILLIS);
    }
  }

  @Override
  public void onTargetInvalidated(InputStateMachine.InvalidatedTarget target) {
    final byte[] payload =
        OperationProtocol.targetInvalidated(
            target.targetSlot, target.reason, target.streamId, target.inputEpoch);
    writeEvent(OperationProtocol.OP_TARGET_INVALIDATED, payload);
  }

  @Override
  public void onDisplayChanged(int displayId, InputStateMachine.TargetDescriptor descriptor) {
    stateMachine.handleDisplayChange(displayId, descriptor);
  }

  private void processConnection(LocalSocket socket) throws IOException {
    final DataInputStream input =
        new DataInputStream(new BufferedInputStream(socket.getInputStream()));
    final DataOutputStream output =
        new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
    synchronized (outputLock) {
      activeSocket = socket;
      activeOutput = output;
    }
    while (true) {
      final OperationProtocol.Header header;
      try {
        header = OperationProtocol.readHeader(input);
      } catch (EOFException error) {
        return;
      }
      final byte[] payload = OperationProtocol.readPayload(input, header.payloadSize);
      dispatch(header, payload);
    }
  }

  private void dispatch(OperationProtocol.Header header, byte[] payload) {
    if (header.operationCode == OperationProtocol.OP_BIND_INPUT_TARGET
        && header.routeKind == OperationProtocol.ROUTE_REQUEST) {
      handleBind(header, payload);
      return;
    }
    if (header.operationCode == OperationProtocol.OP_UNBIND_INPUT_TARGET
        && header.routeKind == OperationProtocol.ROUTE_REQUEST) {
      handleUnbind(header, payload);
      return;
    }
    if (header.operationCode == OperationProtocol.OP_TOUCH
        && header.routeKind == OperationProtocol.ROUTE_EVENT) {
      handleTouch(payload);
      return;
    }
    if (header.requestId != 0) {
      writeError(
          header.requestId, header.operationCode, OperationProtocol.ERROR_UNSUPPORTED_MESSAGE);
    }
  }

  private void handleBind(OperationProtocol.Header header, byte[] payload) {
    final OperationProtocol.BindRequest request;
    try {
      request = OperationProtocol.parseBindRequest(payload);
    } catch (IOException error) {
      writeError(header.requestId, header.operationCode, OperationProtocol.ERROR_MALFORMED_MESSAGE);
      return;
    }
    final InputStateMachine.TargetDescriptor descriptor = resolver.resolvePort(request.displayPort);
    final InputStateMachine.BindOutcome outcome =
        stateMachine.bind(request.targetSlot, request.mode, request.streamId, descriptor);
    final InputStateMachine.TargetDescriptor responseDescriptor = outcome.descriptor;
    final boolean successful =
        outcome.result == InputStateMachine.RESULT_APPLIED
            || outcome.result == InputStateMachine.RESULT_UNCHANGED;
    final byte[] response =
        OperationProtocol.bindResponse(
            outcome.result,
            request.targetSlot,
            request.displayPort,
            request.streamId,
            outcome.inputEpoch,
            successful ? responseDescriptor.logicalWidth : 0,
            successful ? responseDescriptor.logicalHeight : 0,
            successful ? responseDescriptor.rotation : 0);
    writeResponse(OperationProtocol.OP_BIND_INPUT_TARGET, header.requestId, response);
  }

  private void handleUnbind(OperationProtocol.Header header, byte[] payload) {
    final int targetSlot;
    try {
      targetSlot = OperationProtocol.parseUnbindRequest(payload);
    } catch (IOException error) {
      writeError(header.requestId, header.operationCode, OperationProtocol.ERROR_MALFORMED_MESSAGE);
      return;
    }
    writeResponse(
        OperationProtocol.OP_UNBIND_INPUT_TARGET,
        header.requestId,
        OperationProtocol.unbindResponse(stateMachine.unbind(targetSlot), targetSlot));
  }

  private void handleTouch(byte[] payload) {
    try {
      stateMachine.onTouch(OperationProtocol.parseTouchEvent(payload));
    } catch (IOException error) {
      if (payload.length > 0) {
        stateMachine.cancelTarget(Byte.toUnsignedInt(payload[0]));
      }
      Log.w(TAG, "Dropping malformed touch event: " + error.getMessage());
    }
  }

  private void writeResponse(int operationCode, long requestId, byte[] payload) {
    writePacket(operationCode, OperationProtocol.ROUTE_RESPONSE, requestId, payload);
  }

  private void writeEvent(int operationCode, byte[] payload) {
    writePacket(operationCode, OperationProtocol.ROUTE_EVENT, 0, payload);
  }

  private void writeError(long requestId, int failedOperationCode, int error) {
    writePacket(
        OperationProtocol.OP_GENERIC_ERROR,
        OperationProtocol.ROUTE_ERROR,
        requestId,
        OperationProtocol.errorResponse(error, failedOperationCode));
  }

  private void writePacket(int operationCode, int routeKind, long requestId, byte[] payload) {
    synchronized (outputLock) {
      if (activeOutput == null) {
        return;
      }
      try {
        OperationProtocol.writePacket(activeOutput, operationCode, routeKind, requestId, payload);
      } catch (IOException error) {
        Log.w(TAG, "Operation socket write failed", error);
        if (activeSocket != null) {
          try {
            activeSocket.close();
          } catch (IOException ignored) {
          }
        }
      }
    }
  }
}
