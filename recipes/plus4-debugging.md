# Debugging the plus4 build

Written 2026-08-07, after "there is a plus4 build but it goes sideways right
away." Companion to
`apple2-debugging.md`; the two machines need different instruments but the same
discipline.

The headline finding is that "goes sideways" was **two independent faults**
stacked on top of each other, and each one hid the other. Getting that wrong in
either direction — blaming the emulator for both, or the program for both — was
available at every step.

---

## 1. The instrument

`xplus4` ships alongside `x64sc` in `/Applications/vice-arm64-gtk3-3.10/bin/`.
VICE's **binary** monitor is the usable interface; the text monitor is fragile
about reconnects and cannot do half of this.

Read `../c64m/agents/vice-oracle.md` first. It is written for c64m-vs-VICE
comparison, but its protocol section and its list of traps apply unchanged. VICE
source, for reading what the Kernal actually does, is at
`/Users/swessels/Develop/svm/vice-emu-code/vice`.

```bash
xplus4 -TEDdsize -autostart build/plus4/cc65-Chess \
       -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502
```

There is no ready-made Python client — `../c64m/tools` has one for the *text*
monitor only. `vice/vicebin.py` next to this file is a minimal one written from
the wire format in the oracle doc; `vice/shot.py` renders `DISPLAY_GET` to PNG,
and `vice/plus4run.py` is the deterministic launcher described in §4.

Two corrections to the oracle doc's command table, found the hard way:
`REGISTERS_SET` is `0x32` and takes a leading `<u8 memspace>` exactly like
`REGISTERS_GET` — without it you get error `128`, "command length not correct" —
and `MEM_SET` is `0x02`.

Addresses worth having: keyboard buffer `$0527` (8 bytes), `KEY_COUNT` (NDX)
`$00EF`, `FKEY_COUNT` `$055D`, function-key strings `$055F`, `KBDREAD` `$D8C1`,
screen RAM `$0C00`, cc65 BASIC stub entry `$100D`. Palette for rendering frames:
`.../share/vice/PLUS4/yape-pal.vpl`.

---

## 2. Two rules that are not optional

**Closing the socket resumes emulation.** This is in the oracle doc and it still
cost an hour. Anything measured across two script invocations has a gap in the
middle where the machine ran unobserved, and the reading you get back describes a
machine that has moved on. It produced two confident wrong conclusions here: a
"crash" that was really the machine having run for ten more seconds, and a
screenshot of a failure that had already been superseded. **One connection per
experiment**, from setup to final capture.

**Never believe a negative without a control.** "The checkpoint at `$100D` never
fires" is worth nothing until a checkpoint on an address the machine
demonstrably executes *does* fire. The control here was: break in, read PC, arm a
checkpoint at that same PC, resume, confirm the hit. Thirty seconds, and it is
what turned "I couldn't get it to stop" into evidence.

---

## 3. Fault one — VICE's autostart never starts the program

The program loads perfectly: `$1001`–`$7182`, byte-identical to the file. Only
the RUN fails. An exec checkpoint on `$100D` never fires under `-autostart`, and
that holds for all four `-autostartprgmode` values.

A store watchpoint on the keyboard buffer shows why:

```
0 buf=|RUN.....| NDX=  3     VICE feeds "RUN"
1 buf=|UUN.....| NDX=  3     kernal shifts the buffer down
3 buf=|........| NDX=  1
4 buf=|........| NDX=  1     the CR arrives as a separate async write
6 NDX=  0
7 NDX=255                    underflow
```

VICE writes the buffer and the count directly from its vsync hook, racing the
Kernal's read-modify-write of `$EF`. The count wraps, and the Kernal then "types"
~255 bytes of the RAM that follows the 8-byte buffer. That RAM is the
function-key macro table, which is why the screen fills with `DSAVE"`, `LOAD"`
and `DIRECTORY`, and why the machine ends up hung in the Kernal SAVE routine at
`$ED08` waiting for a device that does not exist.

**Control that proves it is VICE and not the machine:** with no VICE injection at
all, hand-writing `RUN\r` and a count of 4 into the buffer *while stopped* drains
`4→3→2→1→0` and stops. No underflow. The Kernal is fine.

`-autostart-delay 40` reaches the entry point reliably (3/3). It is a workaround
for the launch, not a fix for the race.

---

## 4. Starting it deterministically

Since the launch is what is broken, do not use it. `vice/plus4run.py`:

1. Boot with no autostart at all.
2. Write the PRG into RAM with `MEM_SET` (2 KB chunks) and verify it back.
3. Set the BASIC pointers at `$2D`/`$2F`/`$31` to the end address.
4. Arm a checkpoint at `$100D`.
5. Write `RUN\r` and count 4 into the keyboard buffer **while stopped**, then
   resume.

No VICE keyboard injection anywhere, no race, and the checkpoint confirms the
entry was genuinely reached rather than assumed.

---

## 5. Fault two — the program's own keyboard path

This one survives a perfectly clean start, which is what makes it real. With
zero VICE involvement, `KEY_COUNT` goes `0 → 255` immediately after entry and
keeps decrementing. A store watchpoint names the writer: `$D8E6`, which is inside
the ROM's `KBDREAD` at `$D8C1`.

Sampling the state either side of it gives the whole story:

```
at entry $100D    KEY_COUNT=  0  FKEY_COUNT=0  FKEY_SPACE=12060a0706040505
at 1st $EF write  KEY_COUNT=255  FKEY_COUNT=0  FKEY_SPACE=0101010101010185
at 2nd $EF write  KEY_COUNT=254  FKEY_COUNT=1  FKEY_SPACE=0101010101018589
```

The first reading of this blamed `initkbd` — it rewrites the function-key table
(that is the `12060a…` → `0101…` change above) with interrupts enabled, so the
theory was that the Kernal's scanner saw the table mid-rewrite and left
`FKEY_COUNT` at 1, which `cgetc` would then act on. It is a tidy story and it is
**wrong**. Its own evidence contradicts it: at the first underflow `FKEY_COUNT`
was 0, so the `KEY_COUNT | FKEY_COUNT` gate could not have passed.

The real defect is four lines further down `libsrc/plus4/cgetc.s`:

```
_cgetc: lda     KEY_COUNT       ; $EF
        ora     FKEY_COUNT      ; $055D
        bne     L2              ; something waiting -> go read it

        lda     #%00100000
        bit     $FF06
        bne     L2              ; always disable cursor if in bitmap mode
        ...                     ; cursor on, then L1 waits for a key
L2:     jsr     KBDREAD         ; decrements KEY_COUNT
```

The second `bne L2` means to skip the **cursor** code in bitmap mode, since there
is no text cursor to draw. What it actually skips is the `L1` wait loop as well,
landing on `KBDREAD` with nothing in the buffer. So in bitmap mode `cgetc()`
never blocks, always returns a character that does not exist, and takes
`KEY_COUNT` from 0 to 255 on the first call. From then on every key read returns
garbage, the menus navigate themselves, the game reaches Quit and returns to
BASIC. That is the "goes sideways", and the plus4 chess build is in bitmap mode
throughout.

**This is a cc65 bug, not ours.** Two four-line programs, identical apart from
one setting TED bitmap mode, isolate it with no chess code present:

```c
int main(void) { for(;;) cgetc(); }                    /* KEY_COUNT stays 0 */
int main(void) { *(unsigned char*)0xFF06 |= 0x20;      /* bitmap mode      */
                 for(;;) cgetc(); }                    /* KEY_COUNT trashed */
```

`kbhit()` is a narrower version of the same hazard: it only *tests*, so it never
underflows anything itself, but it reports true on `FKEY_COUNT` alone, and the
`cgetc()` that follows then decrements `KEY_COUNT`.

**The workaround** (in `plat_ReadKeys`) is to check `KEY_COUNT` directly and call
`cgetc()` only when a character is genuinely in the buffer — which keeps the
bitmap-mode shortcut harmless, because the first gate now passes legitimately.
Verified: `KEY_COUNT` stays 0, the About screen waits for a key instead of
running away, and one injected keypress brings up the board and menu. 3/3 over a
normal `-autostart-delay 40` launch, where before the fix it exited to BASIC
every time.

It is a workaround, not a fix. cc65 still has the bug, and it will bite any
plus4 program that calls `cgetc()` in bitmap mode.

**The fix** (in `plat_ReadKeys`) is to stop trusting cc65's gate and check the
real count, so `cgetc()` is only ever called with a character actually in the
buffer. Verified: `KEY_COUNT` stays 0, the About screen waits for a key instead
of running away, and one injected keypress brings up the board and menu. 3/3 over
a normal `-autostart-delay 40` launch, where before the fix it exited to BASIC
every time.

---

## 6. Order of operations for the next one

1. **Separate the launch from the program before anything else.** Check whether
   the entry point is reached at all, with a control. Half of what looked like a
   program bug here was the emulator never starting it, and the other half was
   only visible once that was out of the way.
2. Dump memory and compare against the file. "The program loaded correctly" is
   two seconds of work and rules out an enormous amount.
3. Hold one connection. Capture state and screen in the same script run as the
   thing you are measuring.
4. When a count or index behaves impossibly, put a **store watchpoint on the
   single address** and read the PC. Naming the writer ended both halves of this
   investigation.

---

## 7. Noticed in passing, not acted on

The plus4 About screen reads "Commodore 64 graphics version, 2020."
(`platPlus4.c:113`), copied verbatim from `platC64.c:102`. Probably deliberate,
since the plus4 uses the c64 piece graphics, but it does say Commodore 64 on a
Plus/4.
