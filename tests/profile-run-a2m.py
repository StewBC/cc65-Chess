#!/usr/bin/env python3
"""Run the search component profiler under windowed a2m-v2."""

import argparse
import pathlib
import subprocess
import sys
import time


COMPONENTS = 12
ROUNDS = 2


def expected_passes():
    result = []
    for round_number in range(ROUNDS):
        for component in range(1, COMPONENTS + 1):
            order = (component, 0) if round_number else (0, component)
            for profiled_component in order:
                result.append((component, profiled_component))
    return result


def metadata(text):
    result = {}
    for token in text.split():
        if "=" in token:
            key, value = token.split("=", 1)
            result[key] = value
    return result


def process_error(process):
    message = process.stderr.read().decode("utf-8", "replace").strip()
    return message or f"a2m-v2 exited with status {process.returncode}"


def connect(client_type, port, process, deadline):
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(process_error(process))
        try:
            return client_type(
                port=port,
                timeout=max(30.0, deadline - time.monotonic()),
            )
        except ConnectionRefusedError:
            time.sleep(0.1)
    raise TimeoutError("a2m-v2 control port did not open")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--marker", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--ack", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--port", type=int, default=17653)
    parser.add_argument("--timeout", type=float, default=1200.0)
    parser.add_argument("--a2m", type=pathlib.Path)
    args = parser.parse_args()

    tests_dir = pathlib.Path(__file__).resolve().parent
    repo_dir = tests_dir.parent
    a2m_root = repo_dir.parent / "a2m-v2"
    a2m = (args.a2m or (a2m_root / "build-release" / "a2m-v2")).resolve()
    image = args.image.resolve()
    sys.path.insert(0, str(a2m_root / "tools"))
    from a2m_control_client import Ctl

    command = [
        str(a2m),
        "--noini",
        f"--hd=s7d0={image}",
        f"--control-port={args.port}",
        "--turbo=max",
    ]
    process = subprocess.Popen(
        command,
        cwd=a2m_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    client = None
    deadline = time.monotonic() + args.timeout
    try:
        client = connect(Ctl, args.port, process, deadline)
        identity = client.ok("hello")
        if "protocol=A2M/6" not in identity:
            raise RuntimeError(f"unexpected a2m-v2 identity: {identity}")

        base_cycles = [0] * (COMPONENTS + 1)
        profile_cycles = [0] * (COMPONENTS + 1)

        def boundary(expected_marker):
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(process_error(process))
                if client.mem(args.ack, 1, "main")[0]:
                    client.ok("pause")
                    client.wait_paused(10000)
                    marker = client.mem(args.marker, 1, "main")[0]
                    state = metadata(client.ok("get-state"))
                    if marker != expected_marker:
                        raise RuntimeError(
                            f"marker {marker:#04x}, expected {expected_marker:#04x}"
                        )
                    cycle = int(state["cycle"], 0)
                    client.set_mem(args.ack, b"\x00", "main")
                    client.ok("run")
                    return cycle
                time.sleep(0.001)
            raise TimeoutError("profile boundary did not arrive")

        for measured_component, profiled_component in expected_passes():
            start_cycle = boundary(0x80 | profiled_component)
            end_cycle = boundary(profiled_component)
            elapsed = end_cycle - start_cycle
            target = profile_cycles if profiled_component else base_cycles
            target[measured_component] += elapsed
            print(
                f"component {measured_component:02d} "
                f"{'profile' if profiled_component else 'baseline'} "
                f"cycles={elapsed}"
            )
        boundary(0x7F)

        print("\ncomponent baseline profiled delta share")
        for component in range(1, COMPONENTS + 1):
            baseline = base_cycles[component]
            profiled = profile_cycles[component]
            delta = profiled - baseline
            share = 100.0 * delta / baseline
            print(
                f"{component:02d} {baseline} {profiled} {delta:+d} {share:6.2f}%"
            )
        return 0
    finally:
        if client is not None:
            client.close()
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
        print(f"profile-run-a2m: {error}", file=sys.stderr)
        raise SystemExit(1)
