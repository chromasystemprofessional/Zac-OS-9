#!/usr/bin/env python3
"""Exercise real AFP/FUSE teardown against an isolated, minimal DSI server."""

import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def packet(command, request_id, body=b"", code=0, flags=1):
    return struct.pack(">BBHiII", flags, command, request_id, code, len(body), 0) + body


def receive(connection, count):
    result = b""
    while len(result) < count:
        part = connection.recv(count - len(result))
        if not part:
            raise EOFError
        result += part
    return result


def status():
    data = bytearray(10)
    data += b"\x0bTest Server"
    offsets = []
    for value in (b"\x04Test", b"\x01\x06AFP2.2", b"\x01\x0fNo User Authent"):
        offsets.append(len(data))
        data += value
    struct.pack_into(">HHH", data, 0, *offsets)
    return bytes(data)


def wait_for(condition, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if condition():
            return
        time.sleep(0.02)
    raise AssertionError("condition did not become true within timeout")


def exercise(binary, mode):
    daemon = mode == "daemon-idle-eof"
    scenario = "idle-eof" if daemon else mode
    with tempfile.TemporaryDirectory(prefix="zacos9-afp-disconnect-") as temporary:
        root = Path(temporary)
        mountpoint = root / "adam's home"
        mountpoint.mkdir()
        notification = socket.socket(socket.AF_UNIX)
        notification.bind(str(root / "zacos9-finder.test.sock"))
        notification.listen()
        notification.settimeout(1)
        server = socket.socket()
        server.bind(("127.0.0.1", 0))
        server.listen()
        server.settimeout(10)
        disconnect = threading.Event()
        errors = []

        def serve():
            try:
                with server.accept()[0] as connection:
                    header = receive(connection, 16)
                    _, command, request_id, _, length, _ = struct.unpack(">BBHiII", header)
                    receive(connection, length)
                    connection.sendall(packet(command, request_id, status()))
                with server.accept()[0] as connection:
                    connection.settimeout(0.1)
                    while True:
                        if scenario in ("idle-eof", "idle-close", "idle-reset") and disconnect.is_set():
                            if scenario == "idle-close":
                                connection.sendall(packet(5, 75, flags=0) +
                                                   packet(8, 76, b"\x00\x00", flags=0) +
                                                   packet(1, 77, flags=0))
                            elif scenario == "idle-reset":
                                connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                                      struct.pack("ii", 1, 0))
                            return
                        try:
                            header = receive(connection, 16)
                        except socket.timeout:
                            continue
                        except (EOFError, ConnectionResetError):
                            return
                        flags, command, request_id, _, length, _ = struct.unpack(">BBHiII", header)
                        body = receive(connection, length)
                        if flags == 1:
                            continue
                        if command == 1:
                            return
                        if command == 5:
                            continue
                        result = b""
                        code = 0
                        if command == 2 and body[0] == 24:
                            result = struct.pack(">HH", 1 << 5, 1)
                        elif command == 2 and body[0] == 34:
                            if b"read-break" in body:
                                return
                            if b"missing-file" in body:
                                code = -5018
                            elif b"io-error" in body:
                                code = -5014
                            elif b"session-closed" in body:
                                code = -5022
                            elif b"server-shutdown" in body:
                                code = -5027
                            else:
                                result = struct.pack(">HHBBI", 0, 1 << 8, 128, 0, 2)
                        elif command == 2 and body[0] == 17:
                            result = struct.pack(">HQQ", 0, 4096, 8192)
                        connection.sendall(packet(command, request_id, result, code))
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        environment = dict(os.environ, XDG_RUNTIME_DIR=str(root), WAYLAND_DISPLAY="test")
        process = subprocess.Popen(
            [binary, "mount"] + ([] if daemon else ["--foreground"]) +
            [f"127.0.0.1:{server.getsockname()[1]}",
             "adam's home", str(mountpoint)],
            env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        try:
            wait_for(lambda: os.path.ismount(mountpoint) or process.poll() is not None)
            if process.poll() is not None and not os.path.ismount(mountpoint):
                _, error = process.communicate()
                if b"Operation not permitted" in error or b"Permission denied" in error:
                    print("SKIP: FUSE mounts are not permitted")
                    sys.exit(77)
                raise AssertionError(error.decode())
            try:
                os.stat(mountpoint / "missing-file")
            except FileNotFoundError:
                pass
            else:
                raise AssertionError("ordinary missing-file error was not returned")
            assert os.path.ismount(mountpoint), "ordinary AFP errors must not disconnect a disk"
            try:
                os.stat(mountpoint / "io-error")
            except OSError as error:
                assert error.errno == errno.EIO, error
            else:
                raise AssertionError("ordinary server I/O error was not returned")
            assert os.path.ismount(mountpoint), "file-level I/O errors must not disconnect a disk"
            start = time.monotonic()
            if mode == "normal-eject":
                subprocess.run(["fusermount3", "-u", str(mountpoint)], check=True)
            elif mode in ("active-eof", "session-closed", "server-shutdown"):
                try:
                    os.stat(mountpoint / ("read-break" if mode == "active-eof" else mode))
                except OSError:
                    pass
            else:
                disconnect.set()
            if daemon:
                wait_for(lambda: not os.path.ismount(mountpoint), timeout=4)
            process.wait(timeout=4)
            elapsed = time.monotonic() - start
            assert not os.path.ismount(mountpoint), "failed AFP mount was left behind"
            if mode == "normal-eject":
                assert process.returncode == 0, "normal eject should succeed"
                try:
                    notification.accept()
                except socket.timeout:
                    pass
                else:
                    raise AssertionError("normal eject generated a disconnection dialog")
            else:
                assert elapsed < 2.5, f"detected disconnect took {elapsed:.2f}s"
                with notification.accept()[0] as client:
                    message = b""
                    while True:
                        part = client.recv(8192)
                        if not part:
                            break
                        message += part
                expected = b"afp-disconnected " + os.fsencode(mountpoint).hex().encode() + b"\n"
                assert message == expected, message
                if not daemon:
                    assert process.returncode != 0, "unexpected disconnect should not report success"
            worker.join(timeout=2)
            assert not worker.is_alive(), "test server did not stop"
            assert not errors, errors
            print(f"ok: {mode}, unmounted in {elapsed:.2f}s")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if os.path.ismount(mountpoint):
                subprocess.run(["fusermount3", "-uz", str(mountpoint)], check=True)
            server.close()
            notification.close()
            process.communicate()


if not os.access("/dev/fuse", os.R_OK | os.W_OK):
    print("SKIP: /dev/fuse is unavailable")
    sys.exit(77)

for scenario in ("idle-eof", "idle-close", "idle-reset", "active-eof", "session-closed",
                 "server-shutdown", "normal-eject", "daemon-idle-eof"):
    exercise(sys.argv[1], scenario)
