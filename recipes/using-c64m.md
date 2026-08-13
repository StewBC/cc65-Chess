# Using c64m from an agent (control port)

This is the **consumer guide**: how a person or remote agent drives a running
c64m over the localhost control port. Goal-first recipes, gotchas first, then a
compact command map. Copy this file into other projects as the “how to talk to
c64m” brief; keep the canonical copy here and update it when wire behavior that
scripts rely on changes.

**Not this document:** implementation details, source maps, and the full wire
contract live in `control-port.md`. VICE comparison load flags live in
`vice-oracle.md`.

Current protocol identity: **C64M/6** (ask `hello` / `version` to confirm).

---

## 1. Launch

### This checkout's known-good launch

For cc65-Chess profiling, pass c64m's checked-in INI explicitly.  Merely launching from
the c64m repository root was not sufficient in an automated windowed run, and
`--defaults` bypassed the ROM paths in that INI and left the emulator in its error state.

```sh
cd ../c64m
./build/c64m --control-port 17652 --inifile "$PWD/c64m.ini" \
    --nosaveini --ntsc --turbo=2 --prg /absolute/path/to/profile.prg --autorun
```

The corresponding headless command adds `--headless`.  Keep the working directory at the
c64m repository root and do not add `--defaults`.  `c64m.ini` names `roms/system.rom`,
`roms/character.rom` and `roms/1541.rom`; those paths are relative to that root.

For cc65-Chess work with Stefan present, keep the emulator visible.  The retained wrapper
does all of the INI, ROM-directory, control-port and process-cleanup work and should be the
normal entry point:

```sh
cd tests
cl65 -t c64 -Or -I../src -I. -DSEARCH_PROFILE \
    -DPROFILE_FIRST_COMPONENT=10 -DPROFILE_LAST_COMPONENT=10 \
    -o /tmp/c64profile.prg \
    ../src/engine.c ../src/eval.c ../src/search.c c64profile.c
python3 -u c64profile-run.py --windowed /tmp/c64profile.prg
```

Omit the two `PROFILE_FIRST_COMPONENT` / `PROFILE_LAST_COMPONENT` definitions for the full
1-12 matrix.  Set both to one row for a narrow landing measurement; row 0 is an ordinary
baseline pair.  The wrapper launches `../c64m/build/c64m` from the c64m repository root,
passes `c64m.ini` explicitly, uses NTSC and turbo 2, polls screen RAM until `DONE.`, and always
terminates the emulator.  `--windowed` is intentional: do not remove it during a collaborative
run merely to save host time.

The profiler prints one dot per component outside the measured search.  Do not restore the old
`running r=? c=?` line: its second round scrolls at component 8 and charges screen movement to
some runs but not others.

Always bind a control port. Prefer headless for automation (lower latency; no
window present tax):

```sh
# From the c64m repo root so default ROM lookup finds roms/
./build/c64m --headless --control-port 6511 --pal --turbo=2
```

Windowed (play + remote control, e.g. co-op debug):

```sh
./build/c64m --control-port 6510 --pal
```

Useful startup flags:

| Flag | Meaning |
|------|---------|
| `--control-port N` | Enable TCP control (required for this guide) |
| `--headless` | No window/audio device; still needs a control port |
| `--pal` / `--ntsc` | Video standard |
| `--turbo=1\|2\|3` | Initial turbo mode (see § Gotchas) |
| `-p` / `--prg FILE` | Inject PRG at boot |
| `-a` / `--autorun` | After PRG/BASIC/disk load, paste `RUN`+Return (CLI only) |
| `--disk 8=image.d64` | Mount disk on device 8 |

The server binds **`127.0.0.1` only** and accepts **one client at a time**.
`quit-client` closes the socket; it does **not** kill a headless process — your
harness must terminate the emulator when done.

In-tree helper: `tools/c64_control_client.py` (`Ctl` class). Minimal framing is
also sketched in `control-port.md` § Minimal Python client.

---

## 2. Wire basics (enough to not desync)

Every request is one ASCII line ending in `\n`:

```text
<decimal-id> <command> [args...]\n
```

Responses:

```text
<id> ok [metadata...]\n
<id> error <code> <message>\n
<id> data <type> <byte_count> [metadata...]\n
<exactly byte_count raw bytes>
\n
```

Rules that matter for agents:

1. **Echo and match `id`.** Pipelining is allowed (up to 16 outstanding);
   responses may complete **out of order** — always correlate by id.
2. **Do not `readline()` a `data` payload.** Parse `byte_count` from the header,
   read exactly that many bytes, then one trailing `\n`.
3. **Most mutating commands return `ok accepted=1` when queued**, not when the
   machine finished. Synchronize with `wait-paused`, `wait-event`, `wait-frame`,
   or later `get-state` / queries.
4. Only **one wait** may be outstanding (`busy wait already active` otherwise).
5. Paths are unquoted; for path commands the path is usually the **rest of the
   line** (spaces allowed).

Payload-style requests (raw body after the header line):

```text
N paste-text-data <byte_count>\n
<raw bytes>\n

N paste-events-data <byte_count>\n
<raw bytes>\n

N set-memory <addr> <length> map|ram\n
<length raw bytes>\n
```

---

## 3. Gotchas (read these before scripting)

These are the failures people hit first. Treat them as part of the API contract.

### 3.1 `load-prg` injects; it does not start the program

`load-prg <path>` resets, boots through KERNAL/BASIC, and **injects** the PRG
payload at the load address in the file’s first two bytes. It does **not** type
`RUN` or jump to a SYS vector unless the emulator was started with **`-a` /
`--autorun`** (process-wide flag; there is no per-command autorun on the wire).

To start a normal BASIC/ML PRG over the port after load:

```text
N load-prg /abs/or/rel/path/game.prg
N wait-paused 10000
N paste-events RUN\[RT]
N run
```

(Or use `paste-text-data` with `RUN\r` — see § 3.2.)

CLI equivalent of inject-and-run: `--prg game.prg -a`.

### 3.2 `paste-text` cannot put RETURN on a single request line

The protocol is **line-based**. The first real `\n` ends the request, so you
cannot embed a Return character in:

```text
N paste-text RUN
```

If you send the two characters `\` and `n` (thinking of a C/Python escape):

- `\` is **unmapped** and skipped  
- `n` is typed as the letter **n**  
→ the screen shows `RUNn`, not “RUN + Return”.

**Ways that work:**

| Method | Example |
|--------|---------|
| `paste-events` + named Return | `paste-events RUN\[RT]` or `RUN\[RETURN]` |
| `paste-text-data` + real CR | payload `RUN\r` (byte `$0D`) |
| Explicit keys | `key-down return` then `key-up return` |

`paste-events` uses the same escape language as the debugger Type action
(`\[RT]`, `\[W:N]` waits, `\xHH` PETSCII, etc.). Full table: manual § Type text,
or `control-port.md` / paste parser.

### 3.3 Turbo 3 (warp) disables live painting

| Mode | Name | Pixels |
|------|------|--------|
| 1 | normal | Real-time, **live** framebuffer |
| 2 | max | Free-run, **live** framebuffer (good default for agents) |
| 3 | warp | Free-run, **paint off** — `get-frame` is a geometric **debug** snapshot |

After `set-turbo 3` the ok line includes:

```text
warning=warp-disables-live-framebuffer;get-frame-is-debug-only-until-turbo-is-1-or-2
```

To inspect a real screen: `set-turbo 1` or `set-turbo 2`, advance at least one
frame (`step-frame` or `run` + wait), then `get-frame`. Register/memory reads
are fine under turbo 3; only live pixels are not.

### 3.4 Other traps worth knowing early

- **Hex addresses need `$` or `0x`.** Bare numbers are decimal (`get-memory 49152`
  is wrong for `$C000`; use `$C000`).
- **`get-memory` length is decimal**, 1..65536, with `addr + length <= 65536`.
  Full dump: `get-memory $0000 65536 map`.
- **`get-frame` mid-raster while paused** can be a **partial** working buffer.
  Prefer `step-frame`, or `run-to-raster` into a stable line, then `get-frame`.
- **Free-run `wait-frame` + `get-frame` can alias** under load. Prefer
  **`step-frame`** for consecutive frames, or the frame ring (`get-frame-at`).
- **One-shot `set-memory` vs demos:** free-running code often rewrites VIC regs
  every frame; a poke is overwritten immediately. Use a breakpoint, stub, or
  count-only watch — there is no register-freeze command.
- **Headless `quit-client` ≠ process exit.** Kill the emulator yourself.

---

## 4. Recipes

Below, `N` is any client-chosen decimal id. In practice your client auto-numbers.

### 4.1 Connect and sanity-check

```text
1 hello
2 version
3 capabilities
4 get-state
```

Expect `protocol=C64M/6` (or whatever the build advertises).

### 4.2 Load a PRG and RUN it

```text
10 set-turbo 2
11 load-prg assets/prg/some-game.prg
12 wait-paused 15000
13 paste-events RUN\[RT]
14 run
```

If the process was started with `-a`, step 13 is already done after inject.

There is **no** wire command to poke PC directly. For ML that is not a BASIC
`RUN` target, prefer one of:

- `assemble ... auto-run=1 run-address=$C000` (local source),
- `break-create exec $C000` then `run` / `run-to $C000`,
- or inject a PRG that is itself a BASIC stub (`10 SYS ....`) and paste `RUN\[RT]`.

For stock collection/BASIC PRGs, inject + paste `RUN\[RT]` is the usual path.

### 4.3 Type a BASIC line and press Return

```text
20 paste-events LOAD"*",8\[RT]
21 wait-paused 30000
22 paste-events RUN\[RT]
```

Or multi-step keys if you need hold timing:

```text
key-down return
key-up return
```

### 4.4 Read the screen

```text
30 set-turbo 2
31 step-frame
32 get-frame format=indexed8
```

- **`format=indexed8`** (preferred for oracle compares): one palette index 0..15
  per pixel; payload size `height * width` (PAL 504×312).
- **Default `argb8888`:** Pepto RGB; **`stride` is always 2080** (520×4), not
  `width*4` — index rows by `stride`.
- Under turbo 3 this is not a live picture (§ 3.3).

Past frames after a late pause:

```text
frame-ring-info
get-frame-at frame=<n> format=indexed8
```

### 4.5 Pause, inspect CPU/memory, resume

```text
40 pause
41 wait-paused 2000
42 get-cpu
43 get-memory $0400 1000 map
44 run
```

`get-memory` modes: `map` (CPU visible), `ram`, `rom`, `drive8`, `drive9`.

Poke (pauses for the write):

```text
set-memory $D020 1 map\n
\x0E\n
```

### 4.6 Breakpoint, run until hit

```text
50 break-create exec $C000
51 run
52 wait-paused 60000
53 get-cpu
54 break-list
```

Guarded (up to 4 ANDed terms, no OR):

```text
break-create write $D021 when=i==1
break-create write $00C3 when=raster>=250,value==$06
```

Count-only (no pause): `actions=none`, then `break-list` and read `hits=`.

Access tokens: `exec`, `read`, `write`, `read-write`.  
`access` bits in list payloads: **1=exec, 2=read, 4=write** (not VICE’s order).

### 4.7 Wait for async completion

```text
load-state path/to/snap.c64state
wait-event load-state-complete 10000

save-state path/to/out.c64state
wait-event save-state-complete 10000
```

Sticky completion events (`load-state-complete`, `run-complete`,
`step-complete`, `reset-complete`, …) latch until a matching wait consumes them.
Execution-state latches (`paused`, `running`, `breakpoints`) clear when the next
execution-control command is dispatched so you don’t catch a *previous* stop.

### 4.8 Disk mount and autorun-style load

Wire mount does not by itself type `LOAD"*",8` unless the process was started
with disk + `--autorun`. Manual path:

```text
mount-d64 8 assets/disks/game.d64
# wait until READY if needed, then:
paste-events LOAD"*",8\[RT]
# after load finishes:
paste-events RUN\[RT]
```

### 4.9 Assemble and run from source

```text
assemble address=$0801 basic-run=1 samples/simple.bas
# or:
assemble address=$C000 auto-run=1 run-address=$C000 path/to/file.asm
```

`auto-run` and `basic-run` are mutually exclusive. Default `reset=1`.

---

## 5. Command map (by job)

Not exhaustive — full syntax in `control-port.md` / `manual/manual.md`.

| Job | Commands |
|-----|----------|
| Identity | `hello`, `version`, `capabilities`, `ping` |
| Run control | `run`, `pause`, `reset`, `step-cycle`, `step-instruction`, `step-over`, `step-out`, `step-frame`, `run-cycles`, `run-instructions`, `run-to`, `run-to-raster` |
| Speed | `set-turbo 1\|2\|3` |
| Sync | `wait-paused`, `wait-running`, `wait-frame`, `wait-event` |
| State | `get-state`, `get-cpu`, `get-vic`, `get-cia`, `get-memory`, `set-memory`, `get-debug-memory`, `get-call-stack`, `get-drive-cpu` |
| Screen | `get-frame`, `frame-ring-info`, `frame-ring-record`, `get-frame-at`, `vic-ring-*` |
| Input | `key-down`, `key-up`, `restore`, `joystick`, `paste-text`, `paste-events`, `paste-*-data` |
| Files | `load-prg`, `load-bin`, `save-bin`, `load-state`, `save-state`, `mount-d64`, `unmount-disk`, `power-drive`, `get-disk-status` |
| Breakpoints | `break-exec`, `break-create`, `break-update`, `break-list`, `break-clear`, `break-clear-all`, `break-enable`, `rearm-oneshots` |
| History | `history-info`, `history-record`, `history-clear`, `history-find`, `history-next`, `history-read`, `history-close` |
| Build | `assemble`, `find-symbol` |
| Session | `quit-client` |

Key names for `key-down` / `key-up` are lower-case C64 names: `a`–`z`, `0`–`9`,
`space`, `return`, `delete`, `left-shift`, `run-stop`, `f1`, … (full list in the
manual).

Joystick mask: bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire.

---

## 6. Co-op use (human plays, agent debugs)

**Constraint:** one TCP client at a time. You cannot attach two independent
control clients in parallel. Co-op means **one** automated client owns the port
while a human uses the **windowed** emulator (keyboard/pause UI).

### 6.1 Pattern A — agent-only automation

Headless + your script or `tools/c64_control_client.py`. Best for batch tests,
frame dumps, and CI-style loops. No human input on the emulated keyboard unless
you paste it.

### 6.2 Pattern B — shoulder debugger (`coop_watch.py`)

In-tree workflow for “I saw a glitch while playing; freeze and investigate”:

1. Launch **windowed** c64m with a control port (real-time turbo so play feels
   normal and pixels are live):

   ```sh
   ./build/c64m --control-port 6510
   ```

2. Run the watcher (it is the sole control client):

   ```sh
   tools/coop_watch.py --port 6510
   ```

3. You play. When you see a glitch, hit the **frontend pause** key.
4. The daemon wakes on `wait-paused`, writes an evidence pack under
   `build/debug/snap-NNN.txt` (CPU, VIC, configured regions, recent writes), and
   leaves the machine frozen.
5. You describe the glitch in chat. The agent steers via a **file inbox**
   (`build/debug/coop_inbox`, one command per line), e.g.:

   | Inbox line | Effect |
   |------------|--------|
   | `resume` | Run and watch for the next pause |
   | `arm write $00C3 when=raster>=250` | Guaranteed freeze on matching write |
   | `count write $D015` | Count-only watch (`actions=none`) |
   | `clear` | Clear breakpoints |
   | `dump <addr> <len> [mode]` | Extra memory into the snap file |
   | `hist <addr> [access] [limit]` | Flight-recorder context into the snap |
   | `scrub [count]` | Dump last N ring frames (late pause recovery) |
   | `frame <n>` | One ring frame + cycle context |
   | `vic <frame> [first-last]` | Per-line VIC state (latched sprite X, etc.) |
   | `note <text>` | Annotate the snap |
   | `quit` | Exit the daemon |

6. For intermittent bugs: arm a guarded watchpoint that matches the description,
   `resume`, reproduce — next snap should be `stop=breakpoint` at the
   offending instruction, not the aftermath.

Configure named regions and write traces in the `CONFIG` dict at the top of
`coop_watch.py`. Full inbox semantics are in that script’s module docstring.

### 6.3 Pattern C — shared “session owner”

If another tool (IDE plugin, test harness) must own the port, the human either:

- plays only through that tool’s input passthrough, or  
- uses windowed c64m **without** a second control client (UI only), and hands
  the port back to the agent when investigation starts.

Do not expect multi-client round-robin; the server serves one connection until
disconnect.

### 6.4 Aligning the three black boxes

After a freeze (co-op or pure agent):

| Tool | Question |
|------|----------|
| Frame ring (`scrub` / `get-frame-at`) | What did the screen show *N* frames ago? |
| VIC ring (`vic-ring-find` / coop `vic`) | What did the VIC latch on each line? |
| Flight recorder (`history-find` / `hist`) | What did the CPU execute around that cycle? |

They share a **machine cycle** axis — pull cycle from the frame/VIC record,
then search history at that time.

---

## 7. Minimal client sketch

```python
import socket

class C64M:
    def __init__(self, host="127.0.0.1", port=6511):
        self.sock = socket.create_connection((host, port))
        self.file = self.sock.makefile("rb")
        self.next_id = 1

    def command(self, command: str):
        rid = self.next_id
        self.next_id += 1
        self.sock.sendall(f"{rid} {command}\n".encode("utf-8"))
        header = self.file.readline()
        if not header:
            raise EOFError("c64m closed the connection")
        fields = header.rstrip(b"\r\n").split(b" ", 3)
        if int(fields[0]) != rid:
            raise RuntimeError(f"id mismatch: {header!r}")
        kind = fields[1].decode("ascii")
        if kind == "ok":
            return {"kind": "ok", "text": fields[2].decode() if len(fields) > 2 else ""}
        if kind == "error":
            return {
                "kind": "error",
                "code": fields[2].decode() if len(fields) > 2 else "",
                "message": fields[3].decode() if len(fields) > 3 else "",
            }
        # data
        rest = fields[3].split(b" ", 1)
        size = int(rest[0])
        meta = rest[1].decode() if len(rest) > 1 else ""
        payload = self.file.read(size)
        if self.file.read(1) != b"\n":
            raise EOFError("truncated data trailer")
        return {
            "kind": "data",
            "type": fields[2].decode(),
            "metadata": meta,
            "payload": payload,
        }

    def close(self):
        self.file.close()
        self.sock.close()
```

For production scripting prefer `tools/c64_control_client.py` (history decode,
pipelining helpers, etc.).

---

## 8. Maintenance

- **Audience:** remote agents and humans driving c64m; not implementers of the
  port.
- **When behavior scripts depend on changes** (gotchas, load semantics, turbo,
  waits, co-op tools), update **this file in the same change**.
- Full command grammar, deferred concurrency, and source pointers remain in
  `control-port.md`. If this guide and the wire disagree, **trust the source and
  fix the docs**.
- Portable copy: drop this file into another project’s agent brief; point
  “deep reference” at the c64m tree’s `agents/control-port.md` or a pinned
  protocol version from `hello`.
