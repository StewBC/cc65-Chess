#!/usr/bin/env python3
"""Run c64profile.prg under c64m and print its 40-column screen."""

import argparse
import pathlib
import socket
import subprocess
import sys
import time


def read_line(sock, pending):
    while b"\n" not in pending[0]:
        chunk = sock.recv(4096)
        if not chunk:
            raise EOFError("c64m control connection closed")
        pending[0] += chunk
    line, pending[0] = pending[0].split(b"\n", 1)
    return line.decode("latin1")


def memory(sock, pending, request_id, address, length):
    sock.sendall(
        f"{request_id} get-memory ${address:04X} {length} map\n".encode("ascii")
    )
    header = read_line(sock, pending).split()
    if len(header) < 4 or header[0] != str(request_id) or header[1] != "data":
        raise RuntimeError(f"unexpected c64m response: {' '.join(header)}")
    byte_count = int(header[3])
    while len(pending[0]) < byte_count + 1:
        pending[0] += sock.recv(4096)
    payload = pending[0][:byte_count]
    pending[0] = pending[0][byte_count + 1 :]
    return payload


def screen_text(data):
    def decode(value):
        if value == 0:
            return "@"
        if 1 <= value <= 26:
            return chr(ord("A") + value - 1)
        if 32 <= value <= 95:
            return chr(value)
        return " "

    lines = []
    for row in range(25):
        line = "".join(decode(value) for value in data[row * 40 : (row + 1) * 40])
        lines.append(line.rstrip())
    return "\n".join(lines).rstrip()


def process_error(process):
    message = process.stderr.read().decode("utf-8", "replace").strip()
    return message or f"c64m exited with status {process.returncode}"


def connect(port, deadline, process):
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(process_error(process))
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=5)
        except ConnectionRefusedError:
            time.sleep(0.1)
    raise TimeoutError("c64m control port did not open")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("prg", type=pathlib.Path)
    parser.add_argument("--c64m", type=pathlib.Path)
    parser.add_argument("--port", type=int, default=17652)
    parser.add_argument("--timeout", type=float, default=3600.0)
    parser.add_argument("--windowed", action="store_true")
    args = parser.parse_args()

    tests_dir = pathlib.Path(__file__).resolve().parent
    repo_dir = tests_dir.parent
    c64m = args.c64m or (repo_dir.parent / "c64m" / "build" / "c64m")
    c64m = c64m.resolve()
    c64m_root = c64m.parent.parent
    c64m_ini = c64m_root / "c64m.ini"
    prg = args.prg.resolve()
    deadline = time.monotonic() + args.timeout

    command = [
        str(c64m),
        f"--control-port={args.port}",
        f"--inifile={c64m_ini}",
        "--ntsc",
        "--turbo=2",
        "--nosaveini",
        f"--prg={prg}",
        "--autorun",
    ]
    if not args.windowed:
        command.insert(1, "--headless")
    process = subprocess.Popen(
        command,
        # scratch/using-c64m.md: launch from the emulator repository root so
        # its default ROM lookup finds roms/.
        cwd=c64m_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    sock = None
    try:
        sock = connect(args.port, deadline, process)
        pending = [b""]
        request_id = 1
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(process_error(process))
            screen = screen_text(memory(sock, pending, request_id, 0x0400, 1000))
            request_id += 1
            if "DONE." in screen or "DESYNC" in screen:
                print(screen)
                return 0 if "DONE." in screen and "BAD" not in screen else 1
            time.sleep(0.25)
        raise TimeoutError("profile did not finish before timeout")
    finally:
        if sock is not None:
            sock.close()
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"c64profile-run: {error}", file=sys.stderr)
        raise SystemExit(1)
