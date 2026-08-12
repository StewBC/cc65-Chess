#!/usr/bin/env python3
"""
One constrained fit of the existing evaluation numbers.

Pawn stays 100.  Tables keep their shape: the fit is piece values plus a
scale per existing PST / endgame table, not a free cell and not another d1
dose.  Train and locked val are disjoint by book.epd opening index (even /
odd).  The landing gauntlet is not this script.

  ./tune_eval.py --check scratch/e2/eval-book.txt
  ./tune_eval.py --train scratch/e2/train.tsv --val scratch/e2/val.tsv
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

# Shipping numbers.  Copied from src/eval.c; --check proves they still match.
PIECE = {"P": 100, "N": 320, "B": 330, "R": 500, "Q": 900, "K": 0}
PHASE_ENDGAME = 3200
DRIVE_GATE = 400
DRIVE_PHASE = 1100
CENTRE = (3, 2, 1, 0, 0, 1, 2, 3)

PST = {
    "P": [
        0, 0, 0, 0, 0, 0, 0, 0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
        5, 5, 10, 25, 25, 10, 5, 5,
        0, 0, 0, 20, 20, 0, 0, 0,
        5, -5, -10, 0, 0, -10, -5, 5,
        5, 10, 10, -20, -20, 10, 10, 5,
        0, 0, 0, 0, 0, 0, 0, 0,
    ],
    "N": [
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20, 0, 0, 0, 0, -20, -40,
        -30, 0, 10, 15, 15, 10, 0, -30,
        -30, 5, 15, 20, 20, 15, 5, -30,
        -30, 0, 15, 20, 20, 15, 0, -30,
        -30, 5, 10, 15, 15, 10, 5, -30,
        -40, -20, 0, 5, 5, 0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50,
    ],
    "B": [
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10, 0, 0, 0, 0, 0, 0, -10,
        -10, 0, 5, 10, 10, 5, 0, -10,
        -10, 5, 5, 10, 10, 5, 5, -10,
        -10, 0, 10, 10, 10, 10, 0, -10,
        -10, 10, 10, 10, 10, 10, 10, -10,
        -10, 5, 0, 0, 0, 0, 5, -10,
        -20, -10, -10, -10, -10, -10, -10, -20,
    ],
    "R": [
        0, 0, 0, 0, 0, 0, 0, 0,
        5, 10, 10, 10, 10, 10, 10, 5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5,
        0, 0, 0, 5, 5, 0, 0, 0,
    ],
    "Q": [
        -20, -10, -10, -5, -5, -10, -10, -20,
        -10, 0, 0, 0, 0, 0, 0, -10,
        -10, 0, 5, 5, 5, 5, 0, -10,
        -5, 0, 5, 5, 5, 5, 0, -5,
        0, 0, 5, 5, 5, 5, 0, -5,
        -10, 5, 5, 5, 5, 5, 0, -10,
        -10, 0, 5, 0, 0, 0, 0, -10,
        -20, -10, -10, -5, -5, -10, -10, -20,
    ],
    "K": [
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
        20, 20, 0, 0, 0, 0, 20, 20,
        20, 30, 10, 0, 0, 10, 30, 20,
    ],
}

PST_END = {
    "P": [
        0, 0, 0, 0, 0, 0, 0, 0,
        127, 127, 127, 127, 127, 127, 127, 127,
        85, 85, 85, 85, 85, 85, 85, 85,
        50, 50, 50, 50, 50, 50, 50, 50,
        28, 28, 28, 28, 28, 28, 28, 28,
        14, 14, 14, 14, 14, 14, 14, 14,
        5, 5, 5, 5, 5, 5, 5, 5,
        0, 0, 0, 0, 0, 0, 0, 0,
    ],
    "K": [
        -50, -40, -30, -20, -20, -30, -40, -50,
        -30, -20, -10, 0, 0, -10, -20, -30,
        -30, -10, 20, 30, 30, 20, -10, -30,
        -30, -10, 30, 40, 40, 30, -10, -30,
        -30, -10, 30, 40, 40, 30, -10, -30,
        -30, -10, 20, 30, 30, 20, -10, -30,
        -30, -30, 0, 0, 0, 0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50,
    ],
}

FEN_KIND = {
    "P": "P", "N": "N", "B": "B", "R": "R", "Q": "Q", "K": "K",
    "p": "P", "n": "N", "b": "B", "r": "R", "q": "Q", "k": "K",
}


def clamp_char(n: int) -> int:
    return max(-128, min(127, n))


def blend_adj(adj: int, phase: int, phase_end: int = PHASE_ENDGAME) -> int:
    # the four-step shift in eval_Position, including toward-floor >> on negatives
    if phase >= phase_end:
        return 0
    step = (phase_end - phase) >> 10
    if step == 0:
        return adj >> 2
    if step == 1:
        return adj >> 1
    if step == 2:
        return (adj >> 1) + (adj >> 2)
    return adj


def mate_drive(winner: int, loser: int) -> int:
    lf, lr = loser & 7, loser >> 4
    wf, wr = winner & 7, winner >> 4
    corner = CENTRE[lf] + CENTRE[lr]
    apart = abs(wf - lf) + abs(wr - lr)
    return corner * 10 + (14 - apart) * 8


@dataclass
class Features:
    nP: int = 0
    nN: int = 0
    nB: int = 0
    nR: int = 0
    nQ: int = 0
    cN: int = 0
    cB: int = 0
    cR: int = 0
    cQ: int = 0
    pstP: int = 0
    pstN: int = 0
    pstB: int = 0
    pstR: int = 0
    pstQ: int = 0
    pstK: int = 0
    endP: int = 0
    endK: int = 0
    wk: int = 0
    bk: int = 0
    result: float = 0.5
    engine_eval: int | None = None
    fen: str = ""


def parse_board(fen: str) -> Features:
    feat = Features(fen=fen)
    board = fen.split()[0]
    row = file = 0
    for ch in board:
        if ch == "/":
            row += 1
            file = 0
            continue
        if ch.isdigit():
            file += int(ch)
            continue
        kind = FEN_KIND[ch]
        white = ch.isupper()
        sq = (row << 4) | file
        tile = ((sq >> 4) << 3) | (sq & 7)
        index = tile if white else tile ^ 56
        sign = 1 if white else -1
        feat.nP += sign if kind == "P" else 0
        feat.nN += sign if kind == "N" else 0
        feat.nB += sign if kind == "B" else 0
        feat.nR += sign if kind == "R" else 0
        feat.nQ += sign if kind == "Q" else 0
        if kind != "P" and kind != "K":
            if kind == "N":
                feat.cN += 1
            elif kind == "B":
                feat.cB += 1
            elif kind == "R":
                feat.cR += 1
            elif kind == "Q":
                feat.cQ += 1
        pst = PST[kind][index]
        if kind == "P":
            feat.pstP += sign * pst
            feat.endP += sign * PST_END["P"][index]
        elif kind == "N":
            feat.pstN += sign * pst
        elif kind == "B":
            feat.pstB += sign * pst
        elif kind == "R":
            feat.pstR += sign * pst
        elif kind == "Q":
            feat.pstQ += sign * pst
        elif kind == "K":
            feat.pstK += sign * pst
            feat.endK += sign * PST_END["K"][index]
            if white:
                feat.wk = sq
            else:
                feat.bk = sq
        file += 1
    return feat


def shipping_eval(feat: Features) -> int:
    return int(round(eval_with(feat, shipping_params())))


def eval_with(feat: Features, p: dict) -> float:
    mid = (
        100 * feat.nP
        + p["N"] * feat.nN
        + p["B"] * feat.nB
        + p["R"] * feat.nR
        + p["Q"] * feat.nQ
        + p["sP"] * feat.pstP
        + p["sN"] * feat.pstN
        + p["sB"] * feat.pstB
        + p["sR"] * feat.pstR
        + p["sQ"] * feat.pstQ
        + p["sK"] * feat.pstK
    )
    end_adj = (
        p["sEndP"] * feat.endP
        + p["sEndK"] * feat.endK
        - p["sP"] * feat.pstP
        - p["sK"] * feat.pstK
    )
    phase = p["N"] * feat.cN + p["B"] * feat.cB + p["R"] * feat.cR + p["Q"] * feat.cQ
    # keep the existing four-step shape: threshold tracks starting material
    start = 4 * p["N"] + 4 * p["B"] + 4 * p["R"] + 2 * p["Q"]
    phase_end = int(round(start / 2.0))
    phase_i = int(round(phase))
    score = int(round(mid)) + blend_adj(int(round(end_adj)), phase_i, phase_end)
    if phase_i <= DRIVE_PHASE:
        if score > DRIVE_GATE:
            score += mate_drive(feat.wk, feat.bk)
        elif score < -DRIVE_GATE:
            score -= mate_drive(feat.bk, feat.wk)
    return float(score)


def shipping_params() -> dict:
    return {
        "N": 320.0, "B": 330.0, "R": 500.0, "Q": 900.0,
        "sP": 1.0, "sN": 1.0, "sB": 1.0, "sR": 1.0, "sQ": 1.0, "sK": 1.0,
        "sEndP": 1.0, "sEndK": 1.0,
    }


PARAM_ORDER = ["N", "B", "R", "Q", "sP", "sN", "sB", "sR", "sQ", "sK", "sEndP", "sEndK"]

# Stay near the existing family.  Piece values in centipawns; scales around 1.
BOUNDS = {
    "N": (260, 400),
    "B": (260, 400),
    "R": (420, 600),
    "Q": (800, 1100),
    "sP": (0.5, 1.5),
    "sN": (0.5, 1.5),
    "sB": (0.5, 1.5),
    "sR": (0.5, 1.5),
    "sQ": (0.5, 1.5),
    "sK": (0.5, 1.5),
    "sEndP": (0.5, 1.5),
    "sEndK": (0.5, 1.5),
}


def load_tsv(path: Path) -> list[Features]:
    rows: list[Features] = []
    with path.open() as fp:
        header = None
        for line in fp:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if header is None:
                header = parts
                continue
            rec = dict(zip(header, parts))
            feat = parse_board(rec["fen"])
            feat.result = float(rec["result"])
            feat.engine_eval = int(rec["eval"])
            rows.append(feat)
    return rows


def mse_and_k(rows: list[Features], params: dict, k: float) -> tuple[float, float]:
    loss = 0.0
    for feat in rows:
        ev = eval_with(feat, params)
        pred = 1.0 / (1.0 + math.pow(10.0, -k * ev / 400.0))
        d = feat.result - pred
        loss += d * d
    return loss / max(1, len(rows)), k


def fit_k(rows: list[Features], params: dict) -> float:
    best_k, best = 1.0, 1e9
    k = 0.2
    while k <= 3.01:
        loss, _ = mse_and_k(rows, params, k)
        if loss < best:
            best, best_k = loss, k
        k += 0.05
    return best_k


def pack(params: dict) -> list[float]:
    return [params[name] for name in PARAM_ORDER]


def unpack(vec: list[float]) -> dict:
    p = {}
    for name, raw in zip(PARAM_ORDER, vec):
        lo, hi = BOUNDS[name]
        p[name] = min(hi, max(lo, raw))
    return p


def regularizer(params: dict) -> float:
    base = shipping_params()
    acc = 0.0
    for name in PARAM_ORDER:
        scale = 100.0 if name in "NBRQ" else 1.0
        d = (params[name] - base[name]) / scale
        acc += d * d
    return acc


def total_loss(rows: list[Features], params: dict, k: float, lam: float) -> float:
    mse, _ = mse_and_k(rows, params, k)
    return mse + lam * regularizer(params)


def fit_once(rows: list[Features], k: float, lam: float) -> dict:
    """One Adam run from the shipping point.  Not a search over candidates."""
    params = shipping_params()
    vec = pack(params)
    m = [0.0] * len(vec)
    v = [0.0] * len(vec)
    lr = 0.08
    beta1, beta2, eps = 0.9, 0.999, 1e-8
    steps = 80
    delta = [2.0, 2.0, 2.0, 4.0, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02]

    for t in range(1, steps + 1):
        base_loss = total_loss(rows, unpack(vec), k, lam)
        grad = []
        for i in range(len(vec)):
            trial = vec[:]
            trial[i] += delta[i]
            up = total_loss(rows, unpack(trial), k, lam)
            trial[i] -= 2 * delta[i]
            down = total_loss(rows, unpack(trial), k, lam)
            grad.append((up - down) / (2 * delta[i]))
        for i in range(len(vec)):
            m[i] = beta1 * m[i] + (1 - beta1) * grad[i]
            v[i] = beta2 * v[i] + (1 - beta2) * grad[i] * grad[i]
            mhat = m[i] / (1 - beta1 ** t)
            vhat = v[i] / (1 - beta2 ** t)
            vec[i] -= lr * mhat / (math.sqrt(vhat) + eps)
        # keep piece values on a pawn=100 scale; do not let a scale explode
        vec = pack(unpack(vec))
        if t % 20 == 0 or t == 1:
            print(f"  step {t:3d}  train-obj {base_loss:.6f}", flush=True)
    return unpack(vec)


def max_scale(table: list[int]) -> float:
    peak = max(abs(v) for v in table)
    if peak == 0:
        return 1.5
    return 127.0 / peak


def quantize(params: dict) -> dict:
    """The shipping form: integer piece values, tables still signed char."""
    q = dict(params)
    for name in ("N", "B", "R", "Q"):
        q[name] = float(int(round(params[name] / 5.0) * 5))
        lo, hi = BOUNDS[name]
        q[name] = min(hi, max(lo, q[name]))
    scale_tables = {
        "sP": PST["P"], "sN": PST["N"], "sB": PST["B"], "sR": PST["R"],
        "sQ": PST["Q"], "sK": PST["K"], "sEndP": PST_END["P"], "sEndK": PST_END["K"],
    }
    for name, table in scale_tables.items():
        q[name] = min(round(params[name], 2), round(max_scale(table), 2))
        q[name] = max(0.5, q[name])
    return q


def scaled_table(table: list[int], scale: float) -> list[int]:
    return [clamp_char(int(round(v * scale))) for v in table]


def tables_fit(params: dict) -> bool:
    """True if every scaled cell still fits a signed char without saturation."""
    for name, scale_key in (("P", "sP"), ("N", "sN"), ("B", "sB"),
                            ("R", "sR"), ("Q", "sQ"), ("K", "sK")):
        for v in PST[name]:
            n = int(round(v * params[scale_key]))
            if n < -128 or n > 127:
                return False
    for name, scale_key in (("P", "sEndP"), ("K", "sEndK")):
        for v in PST_END[name]:
            n = int(round(v * params[scale_key]))
            if n < -128 or n > 127:
                return False
    return True


def is_near_shipping(params: dict) -> bool:
    base = shipping_params()
    for name in ("N", "B", "R", "Q"):
        if abs(params[name] - base[name]) > 10:
            return False
    for name in ("sP", "sN", "sB", "sR", "sQ", "sK", "sEndP", "sEndK"):
        if abs(params[name] - base[name]) > 0.05:
            return False
    return True


def fmt_params(params: dict) -> str:
    return (
        f"N={params['N']:.1f} B={params['B']:.1f} R={params['R']:.1f} Q={params['Q']:.1f}  "
        f"sP={params['sP']:.2f} sN={params['sN']:.2f} sB={params['sB']:.2f} "
        f"sR={params['sR']:.2f} sQ={params['sQ']:.2f} sK={params['sK']:.2f}  "
        f"sEndP={params['sEndP']:.2f} sEndK={params['sEndK']:.2f}"
    )


def check_evals(path: Path) -> int:
    bad = 0
    n = 0
    with path.open() as fp:
        for line in fp:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(" ", 2)
            if len(parts) < 3:
                continue
            want = int(parts[0])
            fen = parts[2]
            got = shipping_eval(parse_board(fen))
            n += 1
            if got != want:
                print(f"mismatch {want} vs {got}: {fen}")
                bad += 1
                if bad >= 10:
                    break
    print(f"eval check: {n - bad}/{n} match")
    return 0 if bad == 0 else 1


def report_split(name: str, rows: list[Features], params: dict, k: float) -> float:
    mse, _ = mse_and_k(rows, params, k)
    # also a mean |eval| and result correlation, without using it to pick
    wins = sum(1 for r in rows if r.result == 1.0)
    losses = sum(1 for r in rows if r.result == 0.0)
    draws = len(rows) - wins - losses
    print(f"  {name}: {len(rows)} positions  W/L/D pos {wins}/{losses}/{draws}  "
          f"mse {mse:.6f}  K={k:.2f}")
    return mse


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", type=Path, help="file of `eval stm fen` from collectpos --eval")
    ap.add_argument("--train", type=Path)
    ap.add_argument("--val", type=Path)
    ap.add_argument("--lambda", dest="lam", type=float, default=0.02,
                    help="L2 toward the shipping numbers (one predeclared prior)")
    args = ap.parse_args()

    if args.check:
        return check_evals(args.check)
    if not args.train or not args.val:
        ap.error("need --train and --val, or --check")

    train = load_tsv(args.train)
    val = load_tsv(args.val)
    print(f"loaded train={len(train)} val={len(val)}")

    base = shipping_params()
    print("verifying dumped engine eval against the Python shipping eval")
    bad = 0
    for feat in train[:400] + val[:400]:
        if feat.engine_eval is None:
            continue
        got = shipping_eval(feat)
        if got != feat.engine_eval:
            bad += 1
            if bad <= 5:
                print(f"  mismatch engine={feat.engine_eval} py={got} {feat.fen}")
    if bad:
        print(f"  FAIL: {bad} eval mismatches — not fitting a broken model")
        return 1
    print("  ok")

    k = fit_k(train, base)
    print("baseline (shipping numbers)")
    tr0 = report_split("train", train, base, k)
    va0 = report_split("val  ", val, base, k)

    print(f"fit: one Adam run, lambda={args.lam}, K frozen at {k:.2f}")
    fitted = fit_once(train, k, args.lam)
    print("continuous fit")
    print("  " + fmt_params(fitted))
    report_split("train", train, fitted, k)
    report_split("val  ", val, fitted, k)

    quant = quantize(fitted)
    print("quantized candidate (the freeze)")
    print("  " + fmt_params(quant))
    if not tables_fit(quant):
        print("  note: a scale was clamped so every cell still fits a signed char")
    tr1 = report_split("train", train, quant, k)
    va1 = report_split("val  ", val, quant, k)

    print("delta vs shipping")
    print(f"  train mse {tr0:.6f} -> {tr1:.6f}  ({tr1 - tr0:+.6f})")
    print(f"  val   mse {va0:.6f} -> {va1:.6f}  ({va1 - va0:+.6f})")

    if is_near_shipping(quant):
        print("NEAR_SHIPPING: the constrained minimum is the table that already ships.")
        print("A null is done — do not spend a gauntlet on a no-op.")
        return 0

    print("CANDIDATE")
    print("  piece values: "
          f"P=100 N={int(quant['N'])} B={int(quant['B'])} "
          f"R={int(quant['R'])} Q={int(quant['Q'])}")
    print("  PST scales stay a shape-preserving multiply of the existing tables.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
