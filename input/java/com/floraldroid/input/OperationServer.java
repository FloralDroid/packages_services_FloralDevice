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
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;

final class OperationServer
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

  OperationServer(Context context) {
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
      try (FilesystemSocketServer server = FilesystemSocketServer.create(socketPath)) {
        waitingLogged = false;
        Log.i(TAG, "Listening for host operation connections: " + socketPath);
        while (true) {
          try (LocalSocket socket = server.accept()) {
            Log.i(TAG, "Accepted host operation connection: " + socketPath);
            processConnection(socket);
          } finally {
            synchronized (outputLock) {
              activeOutput = null;
              activeSocket = null;
            }
            stateMachine.resetAll();
          }
        }
      } catch (IOException error) {
        if (!waitingLogged) {
          Log.e(TAG, "Operation socket server failed for " + socketPath, error);
          waitingLogged = true;
        }
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

  private static final class FilesystemSocketServer implements AutoCloseable {
    private static final int SOCKET_MODE = 0660;

    private final String path;
    private final LocalSocket boundSocket;
    private final LocalServerSocket serverSocket;
    private final long pathDevice;
    private final long pathInode;

    static FilesystemSocketServer create(String path) throws IOException {
      removeSocketNode(path);
      final LocalSocket boundSocket = new LocalSocket(LocalSocket.SOCKET_STREAM);
      try {
        boundSocket.bind(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM));
        Os.chmod(path, SOCKET_MODE);
        final StructStat status = Os.lstat(path);
        return new FilesystemSocketServer(
            path,
            boundSocket,
            new LocalServerSocket(boundSocket.getFileDescriptor()),
            status.st_dev,
            status.st_ino);
      } catch (ErrnoException error) {
        closeAfterCreateFailure(path, boundSocket);
        throw error.rethrowAsIOException();
      } catch (IOException error) {
        closeAfterCreateFailure(path, boundSocket);
        throw error;
      }
    }

    private FilesystemSocketServer(
        String path,
        LocalSocket boundSocket,
        LocalServerSocket serverSocket,
        long pathDevice,
        long pathInode) {
      this.path = path;
      this.boundSocket = boundSocket;
      this.serverSocket = serverSocket;
      this.pathDevice = pathDevice;
      this.pathInode = pathInode;
    }

    LocalSocket accept() throws IOException {
      return serverSocket.accept();
    }

    @Override
    public void close() throws IOException {
      IOException closeError = null;
      try {
        serverSocket.close();
      } catch (IOException error) {
        closeError = error;
      }
      try {
        boundSocket.close();
      } catch (IOException error) {
        if (closeError == null) {
          closeError = error;
        }
      }
      try {
        removeOwnedSocketNode(path, pathDevice, pathInode);
      } catch (IOException error) {
        if (closeError == null) {
          closeError = error;
        }
      }
      if (closeError != null) {
        throw closeError;
      }
    }

    private static void closeAfterCreateFailure(String path, LocalSocket socket) {
      try {
        socket.close();
      } catch (IOException ignored) {
      }
      try {
        removeSocketNode(path);
      } catch (IOException ignored) {
      }
    }

    private static void removeSocketNode(String path) throws IOException {
      final StructStat status;
      try {
        status = Os.lstat(path);
      } catch (ErrnoException error) {
        if (error.errno == OsConstants.ENOENT) {
          return;
        }
        throw error.rethrowAsIOException();
      }
      if (!OsConstants.S_ISSOCK(status.st_mode)) {
        throw new IOException("socket path already exists and is not a Unix socket");
      }
      boolean active = false;
      try (LocalSocket probe = new LocalSocket(LocalSocket.SOCKET_STREAM)) {
        try {
          probe.connect(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM));
          active = true;
        } catch (IOException ignored) {
        }
      }
      if (active) {
        throw new IOException("socket path already has an active listener");
      }
      try {
        Os.unlink(path);
      } catch (ErrnoException error) {
        throw error.rethrowAsIOException();
      }
    }

    private static void removeOwnedSocketNode(String path, long pathDevice, long pathInode)
        throws IOException {
      final StructStat status;
      try {
        status = Os.lstat(path);
      } catch (ErrnoException error) {
        if (error.errno == OsConstants.ENOENT) {
          return;
        }
        throw error.rethrowAsIOException();
      }
      if (!OsConstants.S_ISSOCK(status.st_mode)
          || status.st_dev != pathDevice
          || status.st_ino != pathInode) {
        return;
      }
      try {
        Os.unlink(path);
      } catch (ErrnoException error) {
        throw error.rethrowAsIOException();
      }
    }
  }
}
