#!/usr/bin/env python3
"""
gauntlet.py
cc65 Chess - test support

Plays the four skill levels against a ladder of reference opponents and turns
the scores into Elo differences with error bars.  Drives either fastchess or
c-chess-cli, whichever it finds; the argument shapes differ and the rest does
not.

Two things this is careful about, because both were got wrong in Phase 4 by
trusting a short match:

  - Nothing is reported without a confidence interval.  At a 50% score the
    standard error is about 250/sqrt(N) Elo, so 100 games is +-70 Elo at 95%
    and 16 games is worth nothing at all.
  - Both sides are run at fixed nodes by default, never a clock.  The engine's
    strength is a node budget, so a node-limited match is exactly reproducible
    on any host - the same property the 8-bit ports depend on.  A time control
    would throw that away on one side of the board.

    The exception is --anchor, which is the one measurement that needs a rated
    opponent: Stockfish's UCI_Elo is calibrated against a clock, so pinning it
    to a node count would be quoting a number for a configuration nobody
    calibrated.  That run is not reproducible, and says so.

The engine's own budget comes from option.Skill, so it plays exactly the four
levels the game's menu offers rather than a configuration invented here.
fastchess refuses to start without some limit declared, so it is handed a
nominal movetime that the engine ignores on purpose - see the header of
tests/uci.c.  The margin is enormous (seconds against milliseconds), and any
game actually lost on time is reported rather than quietly averaged in.

The match runner and Stockfish are found automatically - in tools/, then on
PATH, then in the usual package-manager prefixes.  Nothing here needs an
absolute path.  --which reports what was found, with versions, which is worth
recording alongside any figure: a different major version of Stockfish moves
every number in doc/strength.md.

  ./gauntlet.py --which
  ./gauntlet.py --games 512
"""

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

TESTS = Path(__file__).resolve().parent
REPO = TESTS.parent
TOOLS = REPO / "tools"

# in preference order.  a local copy under tools/ wins over one on PATH, so a
# pinned build can be used without disturbing whatever is installed system wide
RUNNERS = ["fastchess", "c-chess-cli"]
BREW_PREFIXES = ["/opt/homebrew/opt", "/usr/local/opt"]

INSTALL_HELP = """
  fastchess     https://github.com/Disservin/fastchess
  c-chess-cli   https://github.com/lucasart/c-chess-cli
  stockfish     brew install stockfish | apt install stockfish

Put a build under tools/<name>/ or anywhere on PATH.  See tools/README.md."""


def find_tool(name, explicit=None):
    """tools/ first, then PATH, then the usual package prefixes."""
    if explicit:
        if Path(explicit).is_file() or shutil.which(explicit):
            return explicit
        sys.exit(f"{name}: not found at {explicit!r}")

    local = TOOLS / name / name
    if local.is_file() and os.access(local, os.X_OK):
        return str(local)

    found = shutil.which(name)
    if found:
        return found

    for prefix in BREW_PREFIXES:
        candidate = Path(prefix) / name / "bin" / name
        if candidate.is_file():
            return str(candidate)
    return None


def find_runner(explicit=None):
    if explicit:
        return find_tool(os.path.basename(explicit), explicit)
    for name in RUNNERS:
        found = find_tool(name)
        if found:
            return found
    return None


def tool_version(path, is_engine=False):
    """A UCI engine answers "uci"; a match runner answers --version."""
    try:
        if is_engine:
            out = subprocess.run([path], input="uci\nquit\n", capture_output=True,
                                 text=True, timeout=10).stdout
            for line in out.splitlines():
                if line.startswith("id name "):
                    return line[8:].strip()
            return "unknown"
        out = subprocess.run([path, "--version"], capture_output=True,
                             text=True, timeout=10)
        return (out.stdout + out.stderr).strip().splitlines()[0]
    except Exception:
        return "unknown"


# c-chess-cli prints a running score line; fastchess prints a block at the end
CCC_SCORE = re.compile(r"^Score of (.+?) vs (.+?): (\d+) - (\d+) - (\d+)")
FC_GAMES = re.compile(r"Games:\s*(\d+),\s*Wins:\s*(\d+),\s*Losses:\s*(\d+),\s*Draws:\s*(\d+)")
# the word boundary matters: fastchess prints "Elo: X +/- Y, nElo: Z +/- W" on
# one line, and nElo is a normalized scale for SPRT, not an Elo difference.
# Without the \b this quietly reports nElo and every number is wrong
FC_ELO = re.compile(r"\bElo:\s*([-+]?[\d.]+)\s*\+/-\s*([\d.]+)")
FC_PTNML = re.compile(r"Ptnml\(0-2\):\s*\[([^\]]+)\]")

# the engine takes milliseconds a move natively; this is only here because
# fastchess insists on a limit existing
NOMINAL_MOVETIME = 30


def elo(score):
    """Score in 0..1 to an Elo difference.  Unbounded at the extremes."""
    if score <= 0.0:
        return float("-inf")
    if score >= 1.0:
        return float("inf")
    return -400.0 * math.log10(1.0 / score - 1.0)


def elo_interval(wins, losses, draws):
    """
    95% interval on the Elo difference.  The per game result is 1, 0.5 or 0, so
    the variance comes straight from those three counts - which is what makes a
    high draw rate narrow the interval rather than widen it.

    This is the binomial version, used for c-chess-cli.  fastchess reports its
    own pentanomial interval, which pairs the two colours of each opening and
    is tighter for the same games; that one is preferred when available.
    """
    n = wins + losses + draws
    if n == 0:
        return 0.0, 0.0, 0.0

    s = (wins + 0.5 * draws) / n
    var = (wins * (1 - s) ** 2 + draws * (0.5 - s) ** 2 + losses * s ** 2) / n
    se = math.sqrt(var / n)

    lo = max(1e-9, min(1 - 1e-9, s - 1.96 * se))
    hi = max(1e-9, min(1 - 1e-9, s + 1.96 * se))
    return elo(s), elo(lo), elo(hi)


class Runner:
    """Argument shapes and output parsing differ; everything else does not."""

    def __init__(self, cli):
        self.cli = cli
        self.fastchess = "fastchess" in os.path.basename(cli).lower()

    def build(self, a_cmd, a_opts, b_cmd, b_opts, games, book, concurrency, pgn):
        # the nominal movetime goes on our engine alone, never through -each:
        # the opponent brings its own limit and the two must not interact
        if self.fastchess:
            a_opts = a_opts + [f"st={NOMINAL_MOVETIME}"]

        cmd = [self.cli, "-engine", f"cmd={a_cmd}"] + a_opts
        cmd += ["-engine", f"cmd={b_cmd}"] + b_opts

        if self.fastchess:
            # a round is one opening; -repeat plays it twice with the colours
            # swapped, so rounds is half the games
            cmd += [
                "-rounds", str(max(1, games // 2)), "-repeat",
                "-openings", f"file={book}", "format=epd", "order=sequential",
                "-concurrency", str(concurrency),
                "-autosaveinterval", "0",
            ]
            if pgn:
                cmd += ["-pgnout", f"file={pgn}", "notation=san"]
        else:
            cmd += [
                "-games", str(games),
                "-openings", f"file={book}", "order=sequential",
                "-repeat",
                "-concurrency", str(concurrency),
            ]
            if pgn:
                cmd += ["-pgn", pgn, "1"]
        return cmd

    def parse(self, out):
        """Returns (wins, losses, draws, reported, ptnml, timelosses)."""
        timelosses = out.count("loses on time")

        if self.fastchess:
            m = None
            for m in FC_GAMES.finditer(out):
                pass
            if not m:
                sys.exit(f"no result parsed from:\n{out[-2000:]}")
            w, l, d = int(m.group(2)), int(m.group(3)), int(m.group(4))

            e = None
            for e in FC_ELO.finditer(out):
                pass
            reported = None
            if e and e.group(1) not in ("nan", "-nan"):
                reported = (float(e.group(1)), float(e.group(2)))

            p = None
            for p in FC_PTNML.finditer(out):
                pass
            ptnml = p.group(1).replace(" ", "") if p else None
            return w, l, d, reported, ptnml, timelosses

        last = None
        for line in out.splitlines():
            m = CCC_SCORE.match(line)
            if m:
                last = m
        if not last:
            sys.exit(f"no result parsed from:\n{out[-2000:]}")
        return (int(last.group(3)), int(last.group(4)), int(last.group(5)),
                None, None, timelosses)


def run_match(runner, *args):
    cmd = runner.build(*args)
    # run from tests/ rather than from wherever the user is standing.  fastchess
    # drops a resume file into its working directory whatever you tell it, and
    # -config is a *load* directive that will happily clobber the engine setup
    # if you try to use it to redirect the write.  every path handed to the
    # runner is absolute, so the working directory is free to be chosen
    res = subprocess.run(cmd, capture_output=True, text=True, cwd=TESTS)
    return runner.parse(res.stdout + res.stderr)


HEADER = (f"{'level':>6} {'opponent':>16} {'W-L-D':>14} {'score':>7} "
          f"{'elo diff':>10} {'95% interval':>18}  {'pairs (0-2)'}")


def report(level, label, w, l, d, reported, ptnml, timelosses):
    n = max(1, w + l + d)
    s = (w + 0.5 * d) / n
    if reported:
        e, half = reported
        lo, hi = e - half, e + half
    else:
        e, lo, hi = elo_interval(w, l, d)
    print(f"{level:>6} {label:>16} {f'{w}-{l}-{d}':>14} {s:>7.3f} {e:>+10.0f} "
          f"{f'[{lo:+.0f}, {hi:+.0f}]':>18}  {ptnml or ''}")
    if timelosses:
        print(f"       !! {timelosses} game(s) lost on time - the result is not "
              f"a strength measurement")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=os.environ.get("MATCH_CLI"),
                    help="fastchess or c-chess-cli; found automatically if omitted")
    ap.add_argument("--sf", default=os.environ.get("SF"))
    ap.add_argument("--which", action="store_true",
                    help="report the tools found, with versions, and exit")
    # relative to this script rather than to the shell's cwd, so the whole
    # thing runs from anywhere.  the absolute path also tells both runners
    # which directory to start the engine in
    ap.add_argument("--uci", default=str(TESTS / "uci"))
    ap.add_argument("--book", default=str(TESTS / "book.epd"))
    ap.add_argument("--games", type=int, default=256)
    ap.add_argument("--concurrency", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--levels", default="1,2,3,4")
    ap.add_argument("--nodes", default="1,3,10,30,100",
                    help="stockfish node budgets to play against")
    ap.add_argument("--anchor", action="store_true",
                    help="also play Stockfish at its rated UCI_Elo settings")
    ap.add_argument("--anchor-games", type=int, default=128)
    ap.add_argument("--anchor-tc", default="4+0.04")
    ap.add_argument("--anchor-ratings", default="1320,1500")
    ap.add_argument("--pgn-dir", default=None)
    args = ap.parse_args()

    def ints(csv):
        return [int(x) for x in csv.split(",") if x.strip()]

    cli = find_runner(args.cli)
    sf = find_tool("stockfish", args.sf)

    if args.which:
        for label, path, engine in (("match runner", cli, False),
                                    ("stockfish", sf, True)):
            if path:
                print(f"{label:>13}: {path}\n{'':>13}  {tool_version(path, engine)}")
            else:
                print(f"{label:>13}: NOT FOUND")
        print(f"{'engine':>13}: {args.uci} "
              f"{'(built)' if Path(args.uci).exists() else '(run: make uci)'}")
        print(f"{'book':>13}: {args.book} "
              f"{'(present)' if Path(args.book).exists() else '(MISSING)'}")
        return

    if not cli or not sf:
        missing = [n for n, p in (("a match runner", cli), ("stockfish", sf)) if not p]
        sys.exit(f"could not find {' and '.join(missing)}.{INSTALL_HELP}")

    runner = Runner(cli)
    sf_common = ["option.Threads=1", "option.Hash=16"]
    levels = ints(args.levels)
    nodes = ints(args.nodes)

    # the versions belong with the numbers: a figure is only reproducible
    # against the same opponent that produced it
    print(f"runner: {os.path.basename(cli)} - {tool_version(cli)}"
          f"{'  (pentanomial intervals)' if runner.fastchess else ''}")
    print(f"opponent: {tool_version(sf, True)}")

    if nodes:
        print(f"{args.games} games a pairing, {args.book}, "
              f"both sides node limited\n")
        print(HEADER)
        print("-" * 96)

        for level in levels:
            for n in nodes:
                pgn = None
                if args.pgn_dir:
                    # absolute, because the runner is started in tests/
                    pgn_dir = Path(args.pgn_dir).resolve()
                    pgn_dir.mkdir(parents=True, exist_ok=True)
                    pgn = str(pgn_dir / f"L{level}-sf{n}.pgn")

                report(level, f"SF nodes={n}", *run_match(
                    runner,
                    args.uci, [f"name=cc65-L{level}", f"option.Skill={level}"],
                    sf, [f"name=SF-n{n}", f"nodes={n}"] + sf_common,
                    args.games, args.book, args.concurrency, pgn))
            print()

    if args.anchor:
        # Stockfish's UCI_Elo floor is 1320, and its calibration assumes a
        # clock, so this rung uses a time control and is NOT reproducible.
        # It is the only thing here that turns a ladder into a number
        print(f"anchor: Stockfish at UCI_LimitStrength, tc={args.anchor_tc}, "
              f"{args.anchor_games} games "
              "(not reproducible - see the note in the source)\n")
        print(HEADER)
        print("-" * 96)
        for level in levels:
            for rating in ints(args.anchor_ratings):
                report(level, f"SF Elo {rating}", *run_match(
                    runner,
                    args.uci, [f"name=cc65-L{level}", f"option.Skill={level}"],
                    sf, [f"name=SF-{rating}", f"tc={args.anchor_tc}",
                              "option.UCI_LimitStrength=true",
                              f"option.UCI_Elo={rating}"] + sf_common,
                    args.anchor_games, args.book, args.concurrency, None))
            print()


if __name__ == "__main__":
    main()
