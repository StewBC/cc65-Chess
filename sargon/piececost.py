#!/usr/bin/env python3
"""What does moving each kind of piece cost cc65, per move, by game phase?

This is the statistic doc/strength.md §5.2a arrived at after two wrong ones,
and the sequence is the lesson rather than the table:

  - **queen moves per game** correlated beautifully with losing and meant
    almost nothing, because a losing position invites queen moves as much as
    queen moves invite a losing position;
  - **cost per move** reversed it and said rooks were twice as expensive;
  - **cost per move split by phase** reversed it back, because rooks barely
    move before move 15.

Only the third controls for anything, so only the third is computed here.

Every cc65 move is scored by the change in Stockfish's evaluation from the
mover's point of view, and attributed to the kind of piece that moved.  The
usual caution applies twice over: Stockfish judges by a standard neither
program can play to, and this is a correlation over moves that were chosen for
reasons the number cannot see.

    python3 sargon/piececost.py <pgn> [--depth 12] [--threads 2] [--engine cc65]
"""
import argparse
import json
import os
import shutil
import statistics
from collections import defaultdict

import chess
import chess.engine
import chess.pgn

SF = os.environ.get("STOCKFISH") or shutil.which("stockfish") or "stockfish"

ap = argparse.ArgumentParser()
ap.add_argument("pgn")
ap.add_argument("--depth", type=int, default=12)
ap.add_argument("--threads", type=int, default=2)
ap.add_argument("--engine", default="cc65", help="substring naming our side")
ap.add_argument("--split", type=int, default=15, help="last move number of the opening bucket")
a = ap.parse_args()

sf = chess.engine.SimpleEngine.popen_uci(SF)
sf.configure({"Threads": a.threads})

NAMES = {chess.PAWN: "pawn", chess.KNIGHT: "knight", chess.BISHOP: "bishop",
         chess.ROOK: "rook", chess.QUEEN: "queen", chess.KING: "king"}
buckets = defaultdict(lambda: defaultdict(list))
games = 0

handle = open(a.pgn)
while True:
    game = chess.pgn.read_game(handle)
    if game is None:
        break
    games += 1
    ours = chess.WHITE if a.engine in game.headers.get("White", "") else chess.BLACK
    board = game.board()
    prev = None
    for i, mv in enumerate(game.mainline_moves()):
        piece = board.piece_at(mv.from_square)
        mover = board.turn
        if prev is None:
            info = sf.analyse(board, chess.engine.Limit(depth=a.depth))
            prev = info["score"].white().score(mate_score=10000)
        board.push(mv)
        info = sf.analyse(board, chess.engine.Limit(depth=a.depth))
        score = info["score"].white().score(mate_score=10000)
        delta = (score - prev) if mover == chess.WHITE else (prev - score)
        prev = score
        if mover == ours and piece is not None:
            phase = "opening" if (i // 2) + 1 <= a.split else "middlegame"
            buckets[phase][NAMES[piece.piece_type]].append(delta)

sf.quit()

# Mate scores clamp at +-10000, so one move that walks into a forced mate is
# worth two hundred ordinary moves in a mean.  §5.2a's table was fourteen
# opening queen moves over six games computed as a plain mean, which is few
# enough for a single clamped move to *be* the result.  Report the mean, the
# mean with those excluded, and the median, and let the three disagree in
# public if they are going to
MATE_ISH = 1000

print(f"\n{games} games, {a.pgn}, Stockfish depth {a.depth}, "
      f"moves 1-{a.split} against {a.split + 1}+")
for phase in ("opening", "middlegame"):
    rows = sorted(buckets[phase].items(), key=lambda kv: statistics.median(kv[1]))
    label = "1-%d" % a.split if phase == "opening" else "%d+" % (a.split + 1)
    print(f"\n  moves {label}, ordered by median")
    print(f"    {'piece':<8}{'n':>5}{'mean':>9}{'no-mates':>10}{'median':>9}"
          f"{'|d|>1000':>10}")
    for name, xs in rows:
        keep = [x for x in xs if abs(x) <= MATE_ISH]
        clamped = len(xs) - len(keep)
        trimmed = statistics.mean(keep) if keep else float("nan")
        print(f"    {name:<8}{len(xs):>5}{statistics.mean(xs):>9.1f}"
              f"{trimmed:>10.1f}{statistics.median(xs):>9.1f}{clamped:>10}")

with open(a.pgn + ".piececost.json", "w") as fh:
    json.dump({p: dict(d) for p, d in buckets.items()}, fh)
