# Debugging the apple2 build

Written 2026-08-07, after chasing a menu that rendered `1 Human player` as
`1 H5-!. 0,!9%2`. This is a note to whoever hits the next Apple II rendering bug.

`apple2` can be built, booted, driven and measured on this machine, and this
document is how. `atari` builds here too (`make atari atr`, `dir2atr` is
installed) and now runs here under AltirraSDL's JSON bridge. `cx16` builds here
but is not run here; that remains the one target requiring the Windows machine.

---

## 1. The instrument

`../a2m-v2` is Stefan's own Apple II emulator. It has a scriptable control port,
which is the part that matters: you can read the machine's memory and its
rendered frames from Python while it runs.

```bash
make apple2 po                        # needs cadius; it is at /usr/local/bin
cd ../a2m-v2
./build-release/a2m-v2 --noini --hd s7d0=<absolute-path>/build/apple2/cc65-Chess.po --control-port 6510
```

**Run it windowed. Do not pass `--headless`.** Stefan watches the window while
you work, and a headless run silently removes him from the loop. `--model plus`
gives a ][+; the default is an Enhanced //e. System ROMs are compiled into the
binary, so `roms/` being empty is fine. The path to the `.po` must be absolute.

`build-release/a2m-v2` is the fast binary used for profiling.  `build/a2m-v2` also works for
interactive debugging, but is slower.  For the retained exact-cycle profile, build the image
and print its current handshake addresses with `recipes/build-a2m-profile.sh`, then run
the controller command it prints:

```sh
recipes/build-a2m-profile.sh /tmp/cc65-chess-profile.po
# Example only; use the marker and ack printed for this build:
python3 -u tests/profile-run-a2m.py /tmp/cc65-chess-profile.po \
    --marker 0x15A5 --ack 0x15A6
```

The addresses move whenever BSS layout moves; never copy the example without reading the new
map.  `profile-run-a2m.py` launches windowed, uses `--noini`, mounts the absolute image on
SmartPort `s7d0`, verifies protocol `A2M/6`, runs `--turbo=max`, reads exact emulated cycles at
the two-byte marker/ack handshake, and terminates the emulator.  It deliberately sets no
breakpoints while the search runs because instruction breakpoint checks destroy max-mode
throughput.

The title screen waits for a keypress before the game starts. Send it yourself
rather than asking for it:

```python
import sys; sys.path.insert(0, '<...>/a2m-v2/tools')
from a2m_control_client import Ctl
c = Ctl(port=6510)
c.cmd('key 13')                       # ENTER past the title
c.mem(0x400, 0x400)                   # text page 1
c.get_frame()                         # dict: 560x192 ARGB + frame number
c.wait_frame(6, 5000)
```

Text page 1 row bases are `0x400 + (r % 8) * 0x80 + (r // 8) * 0x28`. The rows
are interleaved; do not assume `0x400 + r * 40`.

---

## 2. What the instrument will lie to you about

**`get-memory` does not go through the softswitch handler.** It is
`apple2_debug_read`, which indexes the page table directly. Reading `$C01E` to
ask "is ALTCHARSET on?" returned `$A0` — high bit set, which reads as *yes* and
is the opposite of the truth. Any `$C0xx` read through the control port is
whatever happened to be in the backing store.

This nearly ended the investigation with the wrong answer. **Video state has to
be inferred from what got painted, not from the switch you would read on real
hardware.**

---

## 3. The technique that actually worked

Frames are addressable and comparable, so **diffing one character cell across
time is a real measurement.**

Grab the same 14x8 pixel cell from ten frames spaced a few frames apart, and
count the distinct bitmaps. One means the cell is steady. Two means it is
flashing. Run it across a row and you get a per-column map of which characters
flash:

```
col  4  distinct=1
col  5  distinct=2 FLASHING
col  6  distinct=2 FLASHING
```

That is what settled the bug. Flashing columns were 5–8 and 10–15, which is
exactly `u m a n` and `p l a y e r` — every lowercase letter and nothing else.
On the Apple II only one thing flashes: screen codes `$40-$7F` **when the
primary character set is selected**. So the character set was wrong, and the
byte values were not.

Cells are 14 host pixels wide in 40-column mode, 8 scanlines tall, frame stride
560. Flash toggles roughly every 15 frames, so sample over 50+ frames.

---

## 4. Apple II text encoding, which you will need

conio writes screen codes, not ASCII, and `revers()` changes the mapping. The
decode table for the //e:

| Screen code | Alternate charset | Primary charset |
|---|---|---|
| `$00-$3F` | inverse uppercase + symbols | same |
| `$40-$5F` | MouseText | **flashing** |
| `$60-$7F` | **inverse lowercase** | **flashing** |
| `$80-$FF` | normal, lowercase at `$E0-$FF` | same |

cc65's `revers(1)` sets `INVFLG = $3F`, so `putchar` masks every character to
`$00-$3F`; for lowercase on a //e it then does `ora #$40` to put it back at
`$60-$7F`. That is correct **only if the alternate charset is selected**.

Normal text is unaffected by all of this because it lives at `$80-$FF`, which
means lowercase in either set. **A bug that corrupts inverse text while leaving
normal text perfect is a character-set bug, essentially by definition.** That
asymmetry is the strongest single clue and it was visible in the first
screenshot.

Worked example, `u` = `$75`:

```
$75 eor #$80 -> $F5   cmp #$E0 -> lowercase, and uppercasemask ($FF on //e)
    and INVFLG ($3F)  -> $35
    ora #$40          -> $75      correct inverse 'u' in the alternate charset
                                  flashing '5' in the primary one
```

---

## 5. The bug, and the shape of the lesson

`hires_Init` in `src/apple2/hires.s` turned off the 80-column firmware by hand
with `lda #$15 / jsr $C300`. That firmware shutdown restores the primary
character set. cc65's own `videomode()` makes the identical call and then adds
`sta SETALTCHAR` — the hand-rolled copy had dropped that one line.

The fix was to stop hand-rolling it: call `videomode()`, then
`allow_lowercase(0)` so the menu is uppercase and independent of the character
set on both ][+ and //e. conio is used in exactly one place, `plat_Menu`, so
nothing else changed.

Two things generalise:

**Prefer cc65's library entry point to an open-coded firmware poke.** The
library version knows about state the hand-rolled one forgets. `videomode()`
also gates on `aux80col`, which verifies the Pascal 1.1 firmware signature at
`$C300`, instead of trusting the ROM version byte at `$FBB3`.

**Check the fastcall argument width.** `videomode()` takes an `unsigned`, so it
is passed in A/X, and `lda #$15 / jsr _videomode` leaves X undefined. It happens
to work because the current implementation never reads X. `ldx #$00` is free.

---

## 6. Order of operations for the next one

1. Read the text page and print the raw bytes. Do not theorise from a
   screenshot; the bytes are three seconds away and they are the ground truth
   about what the code *stored*.
2. If the bytes are right, the problem is in how they are *painted* — character
   set, video mode, softswitch. Diff frames over time to find out which.
3. Verify on both `--model plus` and the default //e. Any change touching video
   or firmware entry points has two behaviours, and the //e one is not the
   conservative case.
4. Do not trust `$C0xx` through `get-memory`. Ever.
