# tools/

A drop zone for the external programs the strength measurements need. Everything in here
except this file is gitignored — the tools are fetched, never vendored. They are large,
platform-specific, and under their own licences.

`tests/gauntlet.py` looks here first, then falls back to whatever is on your `PATH`, so
putting a tool in this directory is a convenience rather than a requirement. Run
`tests/gauntlet.py --which` to see what it found.

## What goes here

| Directory | What it is |
|---|---|
| `fastchess/` | Match runner. The one Stockfish's own testing framework uses. |
| `c-chess-cli/` | Match runner. Smaller, plain C, no dependencies. |
| `stockfish/` | The reference opponent. |

Either match runner is enough; having both is how a result gets cross-checked against a
second implementation, which is worth doing at least once.

## Getting them

**Stockfish** is packaged nearly everywhere — `brew install stockfish`,
`apt install stockfish`, or a download from stockfishchess.org on Windows. **Record the
version**: it is printed in the gauntlet output, and a different major version will move
every number in `doc/strength.md`.

**fastchess** — <https://github.com/Disservin/fastchess>, `make` in the clone, or grab a
release binary. Windows binaries are published.

**c-chess-cli** — <https://github.com/lucasart/c-chess-cli>. Its `make.py` passes
`-mpopcnt`, which is x86-only and fails on Apple Silicon; on ARM build it directly instead:

```bash
cc -I./src -std=gnu11 -DNDEBUG -O2 -DVERSION=\"local\" src/*.c -o c-chess-cli -lpthread -lm
```

`doc/measuring.md` covers what to do with them.
