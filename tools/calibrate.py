#!/usr/bin/env python3
"""calibrate.py: fits the probability of a win that the engine shows to
the values of positions of games, which the solver finds.

    calibrate.py POSITIONS [--solve PATH] [--jobs N] [--timeout S] [--cache FILE] [--cached]

POSITIONS is tools/calibrate.c's output. Each position is solved by
build/host/solve, N at a time, each in at most S seconds; those not
solved in time are left out. The values are kept in the cache, so
that a run again solves only the positions it has not; --cached solves
none, and uses those in the cache.

It prints, for the scores of the search at each depth, the log loss of
the probability as the score is (1 / (1 + e^-(score / 100))), as the
engine shows it (its table, percent[] of src/engine/search.c), and as
the best of 1 / (1 + e^-(a * score / 100 + b)) has it, with a and b;
and for depth 6 how often a position is a WIN for each tenth of the
probability, as the score is and as the engine shows it.

Then a new table for the engine: the probability that only rises with
the score and fits best the values (isotonic regression, by pooling
adjacent violators), of the scores at depths 4 to 8, smoothed over 5
entries.

Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
"""

import argparse
import concurrent.futures
import math
import os
import subprocess
import sys

import re

DEPTHS = (1, 2, 3, 4, 5, 6, 7, 8)
# The depths the table is fitted to.
TABLE_DEPTHS = (4, 5, 6, 7, 8)


def engine_table(path="src/engine/search.c"):
    """The engine's table: the score of its first entry, the step, and
    the entries, in percent."""
    text = open(path).read()
    low = int(re.search(r"#define PERCENT_MIN \((-?\d+)\)", text).group(1))
    step = int(re.search(r"#define PERCENT_STEP (\d+)", text).group(1))
    body = re.search(r"percent\[\d+\] = \{([^}]*)\}", text).group(1)
    return low, step, [int(x) for x in re.findall(r"\d+", body)]


def table_prob(table, score):
    low, step, entries = table
    i = (score - low + step // 2) // step
    return entries[min(len(entries) - 1, max(0, i))] / 100


def isotonic(data):
    """The pooled blocks of (score, win), sorted by score, as (centre,
    rate)."""
    blocks = []  # wins, n, lowest score, highest
    for s, w in sorted(data):
        blocks.append([w, 1, s, s])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] >= blocks[-1][0] / blocks[-1][1]:
            b = blocks.pop()
            a = blocks[-1]
            a[0] += b[0]
            a[1] += b[1]
            a[3] = b[3]
    return [((b[2] + b[3]) / 2, b[0] / b[1]) for b in blocks]


def new_table(data, low, step, n):
    pts = isotonic(data)

    def at(s):
        if s <= pts[0][0]:
            return pts[0][1]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if s <= x1:
                return y1 if x1 == x0 else y0 + (y1 - y0) * (s - x0) / (x1 - x0)
        return pts[-1][1]

    raw = [at(low + step * i) for i in range(n)]
    smooth = [sum(raw[max(0, i - 2):i + 3]) / len(raw[max(0, i - 2):i + 3]) for i in range(n)]
    out, last = [], 1
    for p in smooth:
        last = max(last, min(99, max(1, round(100 * p))))
        out.append(last)
    return out


def solve(solver, position, timeout):
    try:
        out = subprocess.run([solver, "--hash", "512", position], capture_output=True, text=True,
                             timeout=timeout).stdout
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    for word in ("WIN", "LOSS"):
        if ": " + word in out:
            return word
    return "TIMEOUT"


def sig(x):
    return 1 / (1 + math.exp(-x)) if x > -50 else 0.0


def loss(data, a, b):
    total = 0.0
    for s, w in data:
        p = sig(a * s + b)
        total -= math.log(max(1e-12, p if w else 1 - p))
    return total / len(data)


def fit(data):
    """a and b of the least log loss, by Newton's method."""
    a, b = 1.0, 0.0
    for _ in range(100):
        ga = gb = haa = hab = hbb = 0.0
        for s, w in data:
            p = sig(a * s + b)
            e, v = p - w, p * (1 - p)
            ga += e * s
            gb += e
            haa += v * s * s
            hab += v * s
            hbb += v
        det = haa * hbb - hab * hab
        a -= (hbb * ga - hab * gb) / det
        b -= (haa * gb - hab * ga) / det
    return a, b


def table_loss(data, table):
    total = 0.0
    for s, w in data:
        p = table_prob(table, round(s * 100))
        total -= math.log(p if w else 1 - p)
    return total / len(data)


def bins(data, prob, what):
    print(f"depth 6, {what}:")
    count = [[0, 0, 0.0] for _ in range(10)]
    for s, w in data:
        p = prob(s)
        c = count[min(9, int(p * 10))]
        c[0] += 1
        c[1] += w
        c[2] += p
    for i, (n, wins, ps) in enumerate(count):
        if n:
            print(f"  {10 * i:3}-{10 * i + 10:3}% {n:6}: shown {100 * ps / n:5.1f}%, WIN {100 * wins / n:5.1f}%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("positions")
    ap.add_argument("--solve", default="build/host/solve")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--cache", default="build/calibrate/values.txt")
    ap.add_argument("--cached", action="store_true", help="solve nothing; fit to the cache")
    args = ap.parse_args()

    rows = [line.rstrip("\n").split("\t") for line in open(args.positions) if line.strip()]
    values = {}
    if os.path.exists(args.cache):
        for line in open(args.cache):
            fields = line.rstrip("\n").split("\t")
            if len(fields) == 2:
                values[fields[0]] = fields[1]
    todo = [] if args.cached else sorted({r[1] for r in rows} - values.keys())
    print(f"{len(rows)} positions, {len(todo)} to solve", file=sys.stderr)
    os.makedirs(os.path.dirname(args.cache) or ".", exist_ok=True)
    with open(args.cache, "a") as cache, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(solve, args.solve, p, args.timeout): p for p in todo}
        for i, f in enumerate(concurrent.futures.as_completed(futures), 1):
            values[futures[f]] = f.result()
            cache.write(f"{futures[f]}\t{f.result()}\n")
            if i % 500 == 0:
                cache.flush()
                print(f"  {i} solved", file=sys.stderr)

    table = engine_table()
    solved = [r for r in rows if values.get(r[1]) in ("WIN", "LOSS")]
    sizes = sorted({r[0] for r in rows}, key=lambda s: tuple(map(int, s.split("x"))))
    print(f"{len(solved)} of {len(rows)} positions solved in {args.timeout:g} s:",
          ", ".join(f"{sz} {sum(r[0] == sz for r in solved)}/{sum(r[0] == sz for r in rows)}" for sz in sizes))
    for k, d in enumerate(DEPTHS):
        print(f"depth {d}:")
        for sz in sizes + ["all"]:
            data = [(int(r[3 + k]) / 100, values[r[1]] == "WIN") for r in solved
                    if (sz == "all" or r[0] == sz) and r[3 + k] != "P"]
            if len(data) < 30:
                continue
            a, b = fit(data)
            print(f"  {sz:5} {len(data):6}: log loss as it is {loss(data, 1, 0):.4f},"
                  f" the engine's {table_loss(data, table):.4f}, a {a:4.2f} b {b:+.2f} {loss(data, a, b):.4f}")
    data = [(int(r[8]) / 100, values[r[1]] == "WIN") for r in solved if r[8] != "P"]
    bins(data, lambda s: sig(s), "the score as it is")
    bins(data, lambda s: table_prob(table, round(s * 100)), "the engine's")

    pooled = [(int(r[2 + d]), values[r[1]] == "WIN") for r in solved for d in TABLE_DEPTHS if r[2 + d] != "P"]
    low, step, entries = table
    new = new_table(pooled, low, step, len(entries))
    print(f"a new table, of {len(pooled)} scores at depths {TABLE_DEPTHS[0]} to {TABLE_DEPTHS[-1]}"
          f" (from {low}, by {step}):")
    for i in range(0, len(new), 17):
        print("    " + ", ".join(str(x) for x in new[i:i + 17]) + ",")


if __name__ == "__main__":
    main()
