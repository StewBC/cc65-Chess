#!/usr/bin/env python3
"""How slow is each Sargon II level, in seconds per Sargon move?

sargon/README.md has one operational number for the deep levels - about 45
seconds a move at level 6, from six games - and nothing at all for levels 2 to
5.  That gap is what decides whether a 64-game screen at some level is an
afternoon or a fortnight, so it is worth a few moves per level rather than a
match.

This plays the opening of one throwaway game per level and times Sargon's
replies.  Two things it deliberately does not claim:

  * **An opening move is the expensive case.**  Sargon's cost tracks the piece
    count, so these numbers are a ceiling on the per-move cost of a whole game,
    not its mean.  README.md records extrapolating a level-6 move-four time
    across a game and predicting sixty hours for what took eight.  Level 6 is
    probed here anyway, precisely because its whole-game mean is known: it is
    the calibration that turns the other levels' opening times into game
    estimates.

  * **Sargon's first reply may be book**, not a search, and a book move is
    instant.  Reply 1 is timed and reported separately for that reason.

cc65 plays White with its opening table off, so its own moves are the same
every level and cost well under a tenth of a second at 400 nodes; the timing
starts once its move has been typed and ends when Sargon's move is on the
screen with the input cursor back.

One harness on the control port at a time, and never --headless: this shares
both rules, and the emulator, with match.py.

  python3 sargon/levelcost.py --output scratch/sargon-levelcost-YYYYMMDD
"""

import argparse
import json
import time
from pathlib import Path

import chess

import match as m


# Per level: how many Sargon replies to time, how long one of them may take,
# and how long the whole level may take.  The budget is the thing that keeps
# this bounded - a level that runs out of it reports the moves it got.
PLAN = {
	0: {"moves": 4, "move_timeout": 180.0, "budget": 150.0},
	1: {"moves": 4, "move_timeout": 240.0, "budget": 200.0},
	2: {"moves": 4, "move_timeout": 300.0, "budget": 260.0},
	3: {"moves": 4, "move_timeout": 400.0, "budget": 330.0},
	4: {"moves": 3, "move_timeout": 500.0, "budget": 450.0},
	5: {"moves": 3, "move_timeout": 700.0, "budget": 620.0},
	6: {"moves": 3, "move_timeout": 900.0, "budget": 900.0},
}


def probe_level(emulator, engine, level, plan, seed):
	board = chess.Board()
	moves = []
	engine.set_book_seed(seed)
	engine.new_game()
	emulator.begin_game(chess.WHITE, level, seed)

	samples = []
	started = time.monotonic()
	deadline = started + plan["budget"]
	while len(samples) < plan["moves"]:
		if board.is_game_over(claim_draw=True):
			m.log(f"level {level}: game ended after {len(moves)} plies")
			break
		remaining = deadline - time.monotonic()
		if remaining <= 0 and samples:
			m.log(f"level {level}: out of budget after {len(samples)} timed moves")
			break
		# Snapshot Sargon's move cells before typing, never after: at max turbo
		# it can answer before the next Python statement runs.
		before = m.SargonEmulator._move_entries(emulator.screen(), chess.BLACK)
		uci = engine.best_move(moves)
		move = chess.Move.from_uci(uci)
		if move not in board.legal_moves:
			raise m.BridgeError(f"cc65 produced illegal move {uci} in {board.fen()}")
		emulator.player_move(move)
		board.push(move)
		moves.append(uci)

		# Give one move whatever is left of the level's budget, so a slow level
		# cannot run past it, and record a censored sample if it does.
		m.MOVE_TIMEOUT = max(60.0, min(plan["move_timeout"], deadline - time.monotonic()))
		pieces = len(board.piece_map())
		t0 = time.monotonic()
		try:
			reply = emulator.sargon_move(board, before)
		except m.BridgeError as exc:
			elapsed = time.monotonic() - t0
			samples.append({
				"reply": len(samples) + 1,
				"seconds": round(elapsed, 2),
				"censored": True,
				"pieces": pieces,
				"cc65": uci,
				"sargon": None,
			})
			m.log(f"level {level} reply {len(samples)}: >{elapsed:.1f}s (timed out: {exc})")
			break
		elapsed = time.monotonic() - t0
		board.push(reply)
		moves.append(reply.uci())
		samples.append({
			"reply": len(samples) + 1,
			"seconds": round(elapsed, 2),
			"censored": False,
			"pieces": pieces,
			"cc65": uci,
			"sargon": reply.uci(),
		})
		m.log(f"level {level} reply {len(samples)}: {elapsed:.1f}s  ({uci} {reply.uci()})")

	return {
		"level": level,
		"seed": seed,
		"wall_seconds": round(time.monotonic() - started, 2),
		"moves": moves,
		"samples": samples,
	}


def summarize(record):
	searched = [s["seconds"] for s in record["samples"] if s["reply"] > 1]
	book = [s["seconds"] for s in record["samples"] if s["reply"] == 1]
	return {
		"level": record["level"],
		"timed": len(record["samples"]),
		"reply1_seconds": round(book[0], 1) if book else None,
		"mean_after_reply1": round(sum(searched) / len(searched), 1) if searched else None,
		"max_seconds": round(max(s["seconds"] for s in record["samples"]), 1)
		if record["samples"] else None,
		"censored": any(s["censored"] for s in record["samples"]),
	}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--levels", default="0,1,2,3,4,5,6")
	parser.add_argument("--port", type=int, default=m.DEFAULT_PORT)
	parser.add_argument("--cc-skill", type=int, default=1, choices=sorted(m.CC65_SKILLS))
	parser.add_argument("--seed", type=int, default=97)
	parser.add_argument("--output", required=True)
	args = parser.parse_args()

	levels = [int(part) for part in args.levels.split(",") if part.strip() != ""]
	output = Path(args.output)
	output.mkdir(parents=True, exist_ok=True)
	results_path = output / "levelcost.json"

	m.prepare_disk()
	emulator = m.SargonEmulator(args.port)
	engine = m.UCIEngine(skill=args.cc_skill, own_book=False)
	records = []
	try:
		emulator.start()
		for level in levels:
			plan = PLAN[level]
			m.log(f"--- level {level}: up to {plan['moves']} replies, {plan['budget']:.0f}s budget")
			record = probe_level(emulator, engine, level, plan, args.seed)
			records.append(record)
			results_path.write_text(
				json.dumps(
					{
						"cc_skill": args.cc_skill,
						"levels": records,
						"summary": [summarize(r) for r in records],
					},
					indent=2,
				),
				encoding="utf-8",
			)
	finally:
		engine.close()
		emulator.close()

	print()
	print("level  timed  reply1(book?)  mean after reply1  max")
	for record in records:
		row = summarize(record)
		print(
			f"{row['level']:>5}  {row['timed']:>5}  "
			f"{str(row['reply1_seconds']) + 's':>13}  "
			f"{str(row['mean_after_reply1']) + 's':>17}  "
			f"{str(row['max_seconds']) + 's':>6}"
			+ ("  (censored)" if row["censored"] else "")
		)
	print(f"\nwrote {results_path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
