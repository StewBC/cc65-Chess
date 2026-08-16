# AltirraSDL / AltirraBridge — agent usage guide (Atari)

Practical notes for agents working on **cc65-Chess** (or any Atari 8-bit
binary) with the AltirraSDL emulator and its JSON-over-socket bridge.

Captured from a live session on macOS arm64 (2026-08-08) against:

- Installed package: `~/.local/share/AltirraSDL` (AltirraSDL 4.50)
- Source tree: `~/Develop/github/external/AltirraSDL`
- Chess artifacts: `build/atari/cc65-Chess.atr`, `build/atari/cc65-Chess`, `build/atari/cc65-Chess.map`

**Do not modify the chess source tree while another agent owns that work.**
The `.atr` / `.atari` binaries are safe to boot read-only.

---

## 1. What is what

| Piece | Role |
|-------|------|
| **AltirraSDL** | Full GUI emulator (SDL3 + Dear ImGui). Optional scripting via `--bridge`. |
| **AltirraBridgeServer** | Headless lean binary: same bridge protocol, no UI, best for agents/CI. |
| **AltirraBridge protocol** | Newline-terminated JSON over a local TCP (or Unix) socket. |
| **Python SDK** | `altirra_bridge` package — pure stdlib (`socket` + `json`). |
| **Token file** | Server writes host:port + session token; client must read it to connect. |

There is also an older **UI test-mode** Unix-socket harness (`--test-mode`)
for ImGui automation. Prefer the **bridge** for CPU/memory/boot/screenshot
work; test-mode is for UI widgets.

Upstream docs in the AltirraSDL tree:

- `src/AltirraSDL/AltirraBridge/README.md`
- `src/AltirraSDL/AltirraBridge/docs/GETTING_STARTED.md`
- `src/AltirraSDL/AltirraBridge/docs/COMMANDS.md`
- `src/AltirraSDL/AltirraBridge/docs/PROTOCOL.md`
- `BUILD.md` (bridge section)
- Claude skill: `src/AltirraSDL/AltirraBridge/skills/altirra-bridge/`

---

## 2. Paths that matter (this machine)

```text
# Packaged GUI (user install)
APP=~/.local/share/AltirraSDL/AltirraSDL.app/Contents/MacOS/AltirraSDL
PKG=~/.local/share/AltirraSDL

# AltirraSDL source / build (development machine)
ALTIRRA=~/Develop/github/external/AltirraSDL
BRIDGE_SERVER=$ALTIRRA/build/bridge-server/src/AltirraBridgeServer/AltirraBridgeServer
SDK_PYTHON=$ALTIRRA/src/AltirraSDL/AltirraBridge/sdk/python

# Chess binaries (read-only for boot)
CHESS=~/Develop/github/personal/cc65-Chess
ATR=$CHESS/build/atari/cc65-Chess.atr
XEX=$CHESS/build/atari/cc65-Chess
MAP=$CHESS/build/atari/cc65-Chess.map
```

### What the package contains vs does not

**In** `~/.local/share/AltirraSDL`:

- `AltirraSDL.app` — GUI binary **with bridge code linked in** (`--bridge` works)
- `extras/`, `BUILD-INFO.txt`, license

**Not** in the package (use the source tree / separate build):

- `AltirraBridgeServer` headless binary
- Python SDK (`sdk/python`)
- Claude skill installer assets

Confirm bridge is in the GUI binary:

```bash
strings "$APP" | grep -E 'altirra-bridge|--bridge' | head
# expect: --bridge, altirra-bridge-%d.token, etc.
```

---

## 3. Start the emulator

### 3A. Headless BridgeServer (recommended for agents)

Built with CMake `-DALTIRRA_BRIDGE_SERVER=ON` (already present as
`build/bridge-server` on this machine):

```bash
"$BRIDGE_SERVER" --bridge=tcp:127.0.0.1:6502
# or let the OS pick a free port:
"$BRIDGE_SERVER" --bridge=tcp:127.0.0.1:0
```

Useful flags:

| Flag | Meaning |
|------|---------|
| `--bridge=tcp:127.0.0.1:PORT` | Listen address (`0` = ephemeral port) |
| `--bridge=unix:/path` | POSIX Unix domain socket |
| `--pacing=unlimited` | Default — run as fast as host CPU (good for automation) |
| `--pacing=realtime` | Sleep to PAL/NTSC wall clock |
| `--settings=none` | Default — deterministic in-memory defaults, no `settings.ini` |
| `--settings=user` | Inherit AltirraSDL settings store |
| `--no-basic` | BASIC off |
| `--machine=800XL` | Hardware (default 800XL) |
| `--memory=320K` | Memory size |
| `-h` / `--help` | Prints help and exits |

On startup, **stderr** prints something like:

```text
[Bridge] listening on tcp:127.0.0.1:6502
[Bridge] token-file: /var/folders/.../T/altirra-bridge-16677.token
[Bridge] log-file:   /var/folders/.../T/altirra-bridge-16677.log
[Bridge] token: <hex>
```

**macOS note:** the token file is under `$TMPDIR` (often
`/var/folders/.../T/`), **not** always `/tmp`. Always use the path the
server prints. Never hard-code `/tmp/altirra-bridge-*.token` on macOS.

Token file format (two lines):

```text
tcp:127.0.0.1:6502
2f9adf27e6fa049b7451a2cc443a9e84
```

Leave the server running in the background while the client connects.

### 3B. Packaged GUI + bridge

```bash
# Window + bridge
"$APP" --bridge
# or fixed port
"$APP" --bridge=tcp:127.0.0.1:6502
```

Boot a disk/XEX from the CLI (no bridge required for interactive play):

```bash
"$APP" "$ATR"
# Media flags also exist: --disk / --run / --cart / --tape ...
# Short usage is printed at startup; full list: Help → Command-Line Help
```

**Headless GUI warning:** `"$APP" --headless --bridge` on this machine
failed with `Could not initialize OpenGL / GLES library`. Prefer
**AltirraBridgeServer** for headless automation. Normal GUI (with a
window) works; OpenGL 3.3 Core on Apple M2 was fine when not headless.

### 3C. Just play (no scripting)

```bash
open -a ~/.local/share/AltirraSDL/AltirraSDL.app
# or pass the ATR as above
```

Settings live in `~/.config/altirra/settings.ini`.

---

## 4. Python client setup

The SDK is pure stdlib — **no pip required** if you set `PYTHONPATH`.

```bash
export PYTHONPATH="$SDK_PYTHON"
# verify
python3 -c "from altirra_bridge import AltirraBridge; print('ok')"
```

Optional durable install:

```bash
pip install "$SDK_PYTHON"
```

### Why `python -m altirra_bridge.install_skills` “didn’t work”

Without `PYTHONPATH` (or pip install):

```text
ModuleNotFoundError: No module named 'altirra_bridge'
```

Correct:

```bash
export PYTHONPATH="$SDK_PYTHON"
python3 -m altirra_bridge.install_skills           # → ./.claude/skills/
python3 -m altirra_bridge.install_skills --user    # → ~/.claude/skills/
```

(Use `python3`, not bare `python`, on macOS.)

---

## 5. Minimal connect → boot → screenshot

```python
#!/usr/bin/env python3
import sys
from pathlib import Path
from altirra_bridge import AltirraBridge

# token path from BridgeServer stderr
token = sys.argv[1]
image = sys.argv[2]   # .atr or .xex / .atari

with AltirraBridge.from_token_file(token) as a:
    a.ping()
    a.config("basic", "off")          # binary boots want BASIC off
    print(a.config())                 # machine, memory, video, ...

    print(a.boot(image))              # returns after media is accepted
    # BOOT does NOT wait for the program to finish loading/running.
    a.frame(300)                      # advance emulated frames (then re-pause)

    png = a.screenshot()
    Path("/tmp/atari-shot.png").write_bytes(png)
    print(a.regs())                   # PC, A, X, Y, S, P, cycles, ...
```

Run:

```bash
export PYTHONPATH="$SDK_PYTHON"
# start BridgeServer in another terminal first, copy token path from stderr
python3 script.py /var/folders/.../T/altirra-bridge-XXXX.token "$ATR"
```

### Critical timing rules

1. **`frame(N)` is how time advances.** Sleeping in Python does nothing
   useful while the emulator is paused (default between commands).
2. **`boot()` returns when the image is mounted/accepted**, not when the
   game is at its title screen. Always `frame(N)` after boot.
3. **ATR with a DOS menu** needs more frames + input (see §6).
4. **XEX** usually needs fewer frames (60–300) to reach code entry.
5. After `frame()`, the next bridge command blocks until those frames
   complete server-side.

---

## 6. Booting cc65-Chess specifically

### Prefer XEX for automation

```python
a.config("basic", "off")
a.boot("/Users/swessels/Develop/github/personal/cc65-Chess/cc65-Chess.atari")
a.frame(200)
# Title: "cc65 Chess V2.0 by S. Wessels, 2026" (green GR.8 screen)
```

### ATR path (MyPicoDos menu)

```python
a.boot(".../cc65-Chess.atr")
a.frame(300)            # MyPicoDos 4.05 directory, file CC65CHES
a.key("RETURN")         # run highlighted file
a.frame(300)            # title screen
```

Verified live:

| Image | After enough frames | Notes |
|-------|---------------------|--------|
| `.atr` | MyPicoDos listing | Need RETURN to run `CC65CHES` |
| `.atari` XEX | Title screen | `runad` → `$2001`; load `$2000–$8749` |

Machine defaults under BridgeServer (`--settings=none`): **800XL**,
**BASIC off**, **AltirraOS**, **320K**, **PAL**, fast boot on.

---

## 7. Command cookbook (Python SDK)

```python
with AltirraBridge.from_token_file(token) as a:
    # Lifecycle
    a.ping()
    a.pause()
    a.resume()
    a.frame(60)
    # a.quit()   # asks emulator process to exit

    # Boot / media
    a.boot("/path/to/file.atr")       # or .xex / .atari
    a.mount(1, "/path/to/disk.atr")   # D1: without full boot path
    a.cold_reset()
    a.warm_reset()

    # Config (many keys cold-reset)
    a.config()                        # dump all
    a.config("basic", "off")
    a.config("memory", "64K")
    a.config("machine", "800XL")

    # CPU / memory (debug-safe reads — no HW side effects on $D000)
    r = a.regs()                      # dict; PC is like "$6940"
    b = a.peek(0x2000, 16)            # bytes
    w = a.peek16(0x2E5)               # MEMTOP
    a.poke(0x600, 0x42)
    a.memload(0x4000, data_bytes)
    data = a.memdump(0x4000, 0x100)

    # Chips / display
    a.antic()                         # includes DLIST address
    a.gtia()
    a.pokey()
    a.pia()
    a.dlist()                         # decoded display list entries
    a.hwstate()                       # combined snapshot
    png = a.screenshot()              # PNG bytes (inline)
    # a.screenshot("/tmp/out.png")    # or server-side path if supported

    # Input
    a.key("RETURN")                   # also SPACE, ESC, ...
    a.joy(0, "right", fire=True)      # port 0
    a.consol(start=True, select=False, option=False)

    # Debugger-ish
    a.disasm(0x2000, count=16)
    a.memmap()
    # breakpoints, profiler, symbols — see COMMANDS.md
```

Full command list: `AltirraBridge/docs/COMMANDS.md`.

### Useful Atari OS addresses for memory budget checks

| Symbol | Addr | Meaning |
|--------|------|---------|
| APPMHI | `$000E/$000F` | Highest app-used address (word) |
| MEMTOP | `$02E5/$02E6` | Top of free memory (word) |
| MEMLO  | `$02E7/$02E8` | Bottom of free memory (word) |
| RAMTOP | `$006A` | Top of RAM in pages (byte; `× $100`) |
| SDLSTL | `$0230/$0231` | OS shadow of display list pointer |
| RUNAD  | `$02E0/$02E1` | DOS run address |
| INITAD | `$02E2/$02E3` | DOS init address |

```python
def mem_bounds(a):
    memlo  = a.peek16(0x2E7)
    memtop = a.peek16(0x2E5)
    appmhi = a.peek16(0x0E)
    ramtop = a.peek(0x6A, 1)[0] * 256
    sdlstl = a.peek16(0x230)
    print(f"MEMLO=${memlo:04X} MEMTOP=${memtop:04X} APPMHI=${appmhi:04X}")
    print(f"RAMTOP=${ramtop:04X} SDLSTL=${sdlstl:04X}")
    print(f"free OS span={memtop - memlo} bytes")
```

After cc65-Chess XEX title:

```text
MEMLO=$0700  MEMTOP=$BC1F  APPMHI=$BC1F  RAMTOP=$C000
SDLSTL=$8500   (LMS mode F → screen $9100)
cc65 software SP (zp $82) ≈ $BC17
```

Parse display list:

```python
antic = a.antic()          # DLIST field
entries = a.dlist()        # or walk bytes at SDLSTL
# Live chess title: $70 $70 $70 $4F $00 $91 ... → GR.8 LMS $9100
```

---

## 8. Inspecting binaries without the emulator

XEX parser is local-only (no bridge):

```python
from altirra_bridge.loader import load_xex
img = load_xex("cc65-Chess.atari")
print(hex(img.runad), img.initads)
for s in img.segments:
    print(f"${s.start:04X}-${s.end:04X} len=${len(s.data):04X}")
```

cc65-Chess.atari (verified):

```text
SYSCHK  $2E00–$2EF5
MAIN    $2000–$8749   (load image)
INITAD  → $2E47
RUNAD   → $2001
```

Map file segment summary (`cc65-Chess.atari.map` — linker view):

```text
STARTUP  $2000–$2062
CODE     $206F–$7DA2   (~23.8K — largest)
RODATA   $7DA3–$8AA9
DATA     $8AAA–$8BC8
DLIST    $8C00–$8E49   (map; runtime title uses $8500 inside load image)
BSS      $8E4A–$9E2D   (~4K)
```

Linker budget (`src/atari/chessAtari.cfg`):

```text
MAIN size = $BC20 - __STACKSIZE__($800) - start($2000) = $9420 (~37.9K)
Static end ~$9E2D → ~85% of MAIN used → linker OK, tight
```

Runtime pressure (title GR.8): screen `$9100–$AEFF` **overlaps map BSS
`$8E4A–$9E2D`**. Stack near `$B417–$BC17` does not overlap the screen.
So: **linker not over budget; concurrent BSS + full GR.8 is over/colliding.**

---

## 9. End-to-end agent recipe (copy-paste)

```bash
# Terminal 1 — server
ALTIRRA=~/Develop/github/external/AltirraSDL
"$ALTIRRA/build/bridge-server/src/AltirraBridgeServer/AltirraBridgeServer" \
  --bridge=tcp:127.0.0.1:6502

# Note the token-file path from stderr (macOS $TMPDIR).
```

```bash
# Terminal 2 — client
export PYTHONPATH=~/Develop/github/external/AltirraSDL/src/AltirraSDL/AltirraBridge/sdk/python
TOKEN=...paste from stderr...
XEX=~/Develop/github/personal/cc65-Chess/cc65-Chess.atari

python3 - <<PY
from altirra_bridge import AltirraBridge
from pathlib import Path

with AltirraBridge.from_token_file("$TOKEN") as a:
    a.config("basic", "off")
    a.boot("$XEX")
    a.frame(240)
    Path("/tmp/chess-title.png").write_bytes(a.screenshot())
    print(a.regs())
    print("MEMTOP", hex(a.peek16(0x2E5)), "SDLSTL", hex(a.peek16(0x230)))
    print("ANTIC", a.antic())
PY
```

Smoke example shipped with the SDK:

```bash
python3 "$SDK_PYTHON/examples/01_hello.py" "$TOKEN"
```

---

## 10. Pitfalls (learned the hard way)

1. **`PYTHONPATH` required** unless you `pip install` the SDK.
2. **Token path is `$TMPDIR` on macOS**, not always `/tmp`.
3. **`boot()` ≠ “game ready”** — always `frame(N)`.
4. **ATR ≠ auto-run** for this project — MyPicoDos needs RETURN.
5. **`--help` on AltirraSDL starts the GUI**; it does not exit like a
   typical CLI. BridgeServer `--help` exits cleanly.
6. **GUI `--headless` may fail OpenGL** on this host; use BridgeServer.
7. **Pause is sticky across reset** — a paused emu stays paused after
   cold/warm reset; use `frame`/`resume` deliberately.
8. **Don’t poll with wall-clock sleep** while expecting emulation to
   advance under the default paused bridge gate.
9. **librashader may be missing** in the package (shader presets off).
   Unrelated to bridge; only affects fancy CRT filters.
10. **Package has GUI bridge, not BridgeServer / SDK** — point agents at
    the source tree for SDK + headless binary.
11. **Another agent may own `cc65-Chess` sources** — boot binaries only;
    don’t edit `src/`, makefiles, or rewrite the ATR.

---

## 11. Building BridgeServer (if missing)

From the AltirraSDL repo:

```bash
cd ~/Develop/github/external/AltirraSDL
cmake -S . -B build/bridge-server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DALTIRRA_BRIDGE_SERVER=ON
cmake --build build/bridge-server --target AltirraBridgeServer -j"$(sysctl -n hw.ncpu)"
./build/bridge-server/src/AltirraBridgeServer/AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

GUI package rebuild: `./build.sh` and `./build.sh --package` (see
repo `BUILD.md` / `build.sh`). Bridge-in-GUI is on by default
(`-DALTIRRA_BRIDGE=OFF` to strip it).

---

## 12. Quick decision tree

```text
Need screenshots / peek / boot tests in a script?
  └─ Yes → AltirraBridgeServer + Python SDK + frame()
       Prefer .atari XEX for chess; ATR only if testing DOS path.

Need to watch the screen / click menus?
  └─ AltirraSDL.app (optionally --bridge for parallel scripting)

Need ImGui UI automation?
  └─ --test-mode (separate from bridge; see PORTING/BUILD.md)

Need headless CI?
  └─ BridgeServer only (not GUI --headless on this Mac)
```

---

## 13. References (in AltirraSDL tree)

| Doc | Contents |
|-----|----------|
| `AltirraBridge/README.md` | Overview, package layout |
| `AltirraBridge/docs/GETTING_STARTED.md` | First PING |
| `AltirraBridge/docs/COMMANDS.md` | Full command reference |
| `AltirraBridge/docs/PROTOCOL.md` | Wire format |
| `sdk/python/README.md` | Client install/quick start |
| `sdk/python/examples/` | `01_hello.py` … `05_state_checkpoints.py` |
| `skills/altirra-bridge/SKILL.md` | Claude skill |
| `BUILD.md` | Bridge build/package notes |

---

*Generated for future agents from a verified local run: BridgeServer boot of
`cc65-Chess.atari` / `.atr`, title-screen screenshots, MEMTOP/SDLSTL/DLIST
inspection, and package/binary bridge capability checks.*
