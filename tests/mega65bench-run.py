#!/usr/bin/env python3
"""Read the native benchmark's four rows via Xemu's UART monitor.

Launch Xemu with -uartmon /tmp/chess-mega65.sock and the benchmark PRG.
This reader never injects input or modifies emulated RAM.
"""
import argparse
import json
import pathlib
import re
import socket
import struct
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', default='/tmp/chess-mega65.sock')
p.add_argument('--map', type=pathlib.Path, default=pathlib.Path('build/mega65/bench.map'))
p.add_argument('--timeout', type=float, default=900)
a = p.parse_args()
symbols = {}
for line in a.map.read_text().splitlines():
    fields = line.split()
    if len(fields) == 5 and fields[-1] in ('bench', 'benchDone'):
        symbols[fields[-1]] = int(fields[0], 16)
if len(symbols) != 2:
    raise SystemExit('bench/benchDone missing from linker map')

with socket.socket(socket.AF_UNIX) as s:
    s.settimeout(5)
    s.connect(a.socket)

    def memory(address):
        s.sendall(f'm {address:x}\r'.encode())
        response = b''
        while not response.endswith(b'.\r\n'):
            chunk = s.recv(4096)
            if not chunk:
                raise RuntimeError('Xemu monitor closed')
            response += chunk
        match = re.search(rb':[0-9A-F]+:([0-9A-F]{32})', response)
        if match is None:
            raise RuntimeError(response.decode(errors='replace'))
        return bytes.fromhex(match[1].decode())

    deadline = time.monotonic() + a.timeout
    while not memory(symbols['benchDone'])[0]:
        if time.monotonic() >= deadline:
            raise TimeoutError('benchmark did not finish')
        time.sleep(1)
    if memory(symbols['benchDone'])[0] != 1:
        raise RuntimeError('fixed game replay failed')
    base = symbols['bench']
    data = b''.join(memory(base + offset) for offset in (0, 16, 32))
    rows = []
    for level in range(4):
        nodes, millis, digest = struct.unpack_from('<IIH', data, level * 10)
        rows.append(dict(level=level + 1, nodes=nodes, millis=millis,
                         digest=f'{digest:04x}'))
    print(json.dumps(rows, indent=2))
