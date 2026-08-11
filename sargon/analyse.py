"""Where did each game turn, and who turned it?

Runs Stockfish over every position of every game in a PGN and reports, per
game, the moves that changed the evaluation most - which is the difference
between "cc65 lost" and "cc65 was fine until move 31".

Three cautions the cc65-Chess docs earned the hard way:

  - Stockfish at depth N judges by a standard neither program can play to.  A
    position it calls equal can be lost for a 60,000-node engine, and the
    1.e4 Nc6 game in doc/strength.md 5.1.7 is the proof: Sargon's Nxf7 there
    is worth -139 to Sargon and won the game anyway.
  - A big swing is where the evaluation moved, not necessarily where the game
    was decided.  Read the swings with the board, not instead of it.
  - Mate scores clamp at +/-10000, so walking into a forced mate and failing to
    deliver one both print as ~9000cp swings.  They are not the same error and
    the blunder counts at the foot of each game do not distinguish them.

    python3 sargon/analyse.py <pgn> [--depth 18] [--threshold 150]

Stockfish is found on PATH, or set STOCKFISH to point at it.
"""
import argparse, collections, os, shutil
import chess, chess.engine, chess.pgn

SF = os.environ.get("STOCKFISH") or shutil.which("stockfish") or "stockfish"

ap = argparse.ArgumentParser()
ap.add_argument("pgn")
ap.add_argument("--depth", type=int, default=18)
ap.add_argument("--threshold", type=int, default=150, help="centipawns")
ap.add_argument("--engine", default="cc65", help="substring naming our side")
a = ap.parse_args()

sf = chess.engine.SimpleEngine.popen_uci(SF)


def cp(board):
    info = sf.analyse(board, chess.engine.Limit(depth=a.depth))
    return info["score"].white().score(mate_score=10000)


handle = open(a.pgn)
n = 0
while True:
    game = chess.pgn.read_game(handle)
    if game is None:
        break
    n += 1
    white, black = game.headers.get("White", "?"), game.headers.get("Black", "?")
    result = game.headers.get("Result", "*")
    ours = chess.WHITE if a.engine in white else chess.BLACK
    board = game.board()
    prev, swings, moves = 0, [], list(game.mainline_moves())

    print(f"\n=== game {n}: {white} vs {black} — {result}, {len(moves)} plies")
    for i, mv in enumerate(moves):
        san = board.san(mv)
        board.push(mv)
        score = cp(board)
        # from the mover's point of view: positive delta = the mover gained
        delta = (score - prev) if (i % 2 == 0) else (prev - score)
        mover_is_ours = (i % 2 == 0) == (ours == chess.WHITE)
        if delta < -a.threshold:
            swings.append((i, san, prev, score, delta, mover_is_ours))
        prev = score

    if not swings:
        print("   no single move moved the evaluation past the threshold")
    for i, san, before, after, delta, mine in swings:
        who = "cc65 " if mine else "opp  "
        num = f"{i//2 + 1}{'.' if i % 2 == 0 else '...'}"
        print(f"   {who} {num:>6} {san:<8} {before:+6d} -> {after:+6d}   {delta:+5d}")

    mine = [s for s in swings if s[5]]
    theirs = [s for s in swings if not s[5]]
    print(f"   blunders past {a.threshold}cp: cc65 {len(mine)}, opponent {len(theirs)}"
          f"{'   <- cc65 gave away more' if len(mine) > len(theirs) else ''}")

sf.quit()
print(f"\n{n} games analysed at depth {a.depth}")
