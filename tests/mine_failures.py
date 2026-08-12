#!/usr/bin/env python3
"""Mine first durable Stockfish evaluation swings from a PGN of losses.

Phase E6: group losses by mechanism before inventing evaluation terms.
Uses Stockfish multi-PV off, depth-limited analysis of each position after
our move. Mate-clamped scores are dropped from the average.

  ./mine_failures.py /tmp/e6-pgn/L3-sf30.pgn --our-name cc65
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
	import chess
	import chess.pgn
	import chess.engine
except ImportError:
	sys.exit("needs python-chess")


def find_stockfish():
	for p in (
		Path(__file__).resolve().parent.parent / "tools" / "stockfish" / "stockfish",
		Path("/opt/homebrew/bin/stockfish"),
		Path("/usr/local/bin/stockfish"),
	):
		if p.is_file():
			return str(p)
	import shutil
	return shutil.which("stockfish")


def clamp_cp(info):
	score = info["score"].white()
	if score.is_mate():
		return None
	return score.score()


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("pgn")
	ap.add_argument("--our-name", default="cc65")
	ap.add_argument("--depth", type=int, default=12)
	ap.add_argument("--min-swing", type=int, default=150)
	args = ap.parse_args()

	sf = find_stockfish()
	if not sf:
		sys.exit("stockfish not found")

	engine = chess.engine.SimpleEngine.popen_uci(sf)
	engine.configure({"Threads": 1, "Hash": 64})

	swings = []
	mechanisms = Counter()
	games = 0
	losses = 0

	with open(args.pgn) as f:
		while True:
			game = chess.pgn.read_game(f)
			if game is None:
				break
			games += 1
			white = game.headers.get("White", "")
			black = game.headers.get("Black", "")
			result = game.headers.get("Result", "*")
			we_white = args.our_name.lower() in white.lower()
			we_black = args.our_name.lower() in black.lower()
			if not (we_white or we_black):
				continue
			if we_white and result != "0-1":
				continue
			if we_black and result != "1-0":
				continue
			losses += 1

			board = game.board()
			prev = None
			first = None
			for ply, node in enumerate(game.mainline()):
				move = node.move
				our_turn = (board.turn == chess.WHITE and we_white) or (
					board.turn == chess.BLACK and we_black
				)
				board.push(move)
				if not our_turn:
					continue
				info = engine.analyse(board, chess.engine.Limit(depth=args.depth))
				cp = clamp_cp(info)
				if cp is None or prev is None:
					prev = cp
					continue
				# white-positive; our swing is negative for us
				if we_black:
					delta = prev - cp  # improvement for black is rising black score = falling white
				else:
					delta = cp - prev
				# we care when position got worse for us after our move: delta < -min_swing
				if first is None and delta <= -args.min_swing:
					san = board.peek()
					piece = board.piece_at(move.to_square)
					kind = piece.symbol().upper() if piece else "?"
					first = {
						"ply": ply,
						"move": move.uci(),
						"delta": delta,
						"piece": kind,
						"cap": board.is_capture(move) if False else (prev is not None),
					}
					# crude mechanism tags
					tags = []
					if board.is_check():
						tags.append("gives-check")
					if move.uci()[0:2] != move.uci()[2:4]:
						pass
					mover = board.piece_at(move.to_square)
					if mover:
						if mover.piece_type == chess.QUEEN:
							tags.append("queen-move")
						elif mover.piece_type == chess.PAWN:
							tags.append("pawn-move")
						elif mover.piece_type == chess.KING:
							tags.append("king-move")
						elif mover.piece_type == chess.BISHOP:
							tags.append("bishop-move")
						elif mover.piece_type == chess.KNIGHT:
							tags.append("knight-move")
						elif mover.piece_type == chess.ROOK:
							tags.append("rook-move")
					if not tags:
						tags.append("other")
					for t in tags:
						mechanisms[t] += 1
					swings.append(first)
					break
				prev = cp

	engine.quit()

	print(f"games {games}, our losses {losses}, first swings {len(swings)}")
	print("mechanism counts (first durable swing only):")
	for k, v in mechanisms.most_common():
		print(f"  {k:16} {v}")
	if swings:
		ds = sorted(s["delta"] for s in swings)
		print(f"swing cp: median {ds[len(ds)//2]}, worst {ds[0]}, n={len(ds)}")
		print("sample first swings:")
		for s in swings[:12]:
			print(f"  ply {s['ply']:3} {s['move']} {s['piece']} Δ={s['delta']}")


if __name__ == "__main__":
	main()
