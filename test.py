#!/usr/bin/env python3
"""
Test runner for every solver in build/bin.

Runs the standard sweep -- 9 cases per graph (k = 1..3 cops x p = 1..3
visibility) over assets/matrices/*.txt -- against BOTH the v1 and v2 solvers,
and writes two files per solver to console_outputs/:

    <exe name>_test_results.csv            full raw results, one row per run
    <exe name>_test_results_formatted.csv  presentation mirror of the same data

so the two solvers never clobber each other (or the hand-kept baselines in
console_outputs/test_results.csv / test_results_v2.csv).

The formatted mirror matches console_outputs/reference.csv: tab-separated,
sorted by graph then k then p, filtered to the columns worth reading, with
styled headers. Runtime is wall-clock seconds.

The per-run mechanics -- PATH fixup for the MSYS2 runtime DLLs, stdout
parsing, crash-code classification, wall-clock timeout -- are reused from
util/test_k_cops_visibility.py rather than duplicated here.

After both solvers finish, a comparison summary diffs them against the
correctness invariants: at p=1 the two describe the identical game and must
agree exactly, and v1's intermediate-ghosting bug is one-directional (it can
report false LOSSes but never false WINs), so a v1 WIN that v2 calls a LOSS is
a genuine regression.

Usage (from anywhere; paths resolve relative to the repo root):
    python test.py
    python test.py --timeout 300
    python test.py --graphs cycle,line,tree
    python test.py --exe k_cops_visibility_v2      # just one solver
    python test.py --include-big                   # allow scotlandyard etc.
"""

import argparse
import csv
import sys
import time
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO_ROOT / "util"))

# Reuse the existing harness internals (module-level code is import-safe;
# its own sweep is guarded behind __main__).
from test_k_cops_visibility import (  # noqa: E402
    BIG_GRAPHS,
    ensure_runtime_on_path,
    est_state_bytes,
    node_count,
    run_one,
)

BIN_DIR = REPO_ROOT / "build" / "bin"
MATRIX_DIR = REPO_ROOT / "assets" / "matrices"
OUT_DIR = REPO_ROOT / "console_outputs"

# The solvers under test, in comparison order: element 0 is the baseline the
# rest are diffed against.
DEFAULT_SOLVERS = ["k_cops_visibility", "k_cops_visibility_v2"]

K_VALUES = (1, 2, 3)
P_VALUES = (1, 2, 3)

CSV_FIELDS = ["graph", "n", "k", "p", "est_state_mb", "status", "result",
              "capture_plys", "passes", "base_caps", "cache_hit", "mem_mb",
              "mainloop_s", "solver_s", "wall_s", "exit_code", "positions"]

# The presentation mirror: (styled header, source column). n/k/p keep their
# lowercase math-symbol names. Matches console_outputs/reference.csv.
MIRROR_COLUMNS = [
    ("Graph",                        "graph"),
    ("n",                            "n"),
    ("k",                            "k"),
    ("p",                            "p"),
    ("Result",                       "result"),
    ("Capture Plys",                 "capture_plys"),
    ("Runtime (Seconds)",            "wall_s"),
    ("Memory Footprint (Megabytes)", "mem_mb"),
]


def collect_jobs(args):
    """Build the (est_bytes, n, matrix, k, p) job list -- 9 combos per graph."""
    filters = [s.strip() for s in args.graphs.split(",") if s.strip()]
    jobs = []

    for mx in sorted(MATRIX_DIR.glob("*.txt")):
        stem = mx.stem
        if filters and not any(f in stem for f in filters):
            continue
        if stem in BIG_GRAPHS and not args.include_big:
            continue

        n = node_count(mx)
        if n < 2:
            continue

        for k in K_VALUES:
            for p in P_VALUES:
                est = est_state_bytes(n, k, p)
                if est > args.state_budget:
                    continue
                jobs.append((est, n, mx, k, p))

    jobs.sort(key=lambda j: j[0])  # cheapest first, so results arrive fast
    return jobs


def run_suite(exe: Path, jobs, timeout: float):
    """Run every job against one solver; returns the list of result rows."""
    hdr = (f"{'graph':<20} {'n':>4} {'k':>2} {'p':>2} {'result':<6} {'plys':>4} "
           f"{'passes':>6} {'mem MB':>8} {'wall s':>8}  status/positions")
    print(f"\n=== {exe.name} ===")
    print(hdr)
    print("-" * len(hdr))

    rows = []
    for est, n, mx, k, p in jobs:
        res = run_one(exe, mx, k, p, timeout)
        row = {"graph": mx.stem, "n": n, "k": k, "p": p,
               "est_state_mb": round(est / 1e6, 1)}
        row.update(res)
        rows.append(row)

        note = res.get("status", "?")
        if note == "ok":
            note = res.get("positions", "") or "(loss)"
        print(f"{mx.stem:<20} {n:>4} {k:>2} {p:>2} "
              f"{str(res.get('result','')):<6} {str(res.get('capture_plys','')):>4} "
              f"{str(res.get('passes','')):>6} {str(res.get('mem_mb','')):>8} "
              f"{str(res.get('wall_s','')):>8}  {note}")

    return rows


def write_csv(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)


def write_outputs(stem: str, rows):
    """Write both the raw CSV and its formatted mirror. Returns their paths."""
    raw = OUT_DIR / f"{stem}_test_results.csv"
    mirror = OUT_DIR / f"{stem}_test_results_formatted.csv"
    write_csv(raw, rows)
    write_mirror(mirror, rows)
    return raw, mirror


def fmt_cell(value):
    """Render one mirror cell: blank for missing, and no trailing '.0' on
    whole numbers (mem_mb 3.0 prints as '3', matching the reference)."""
    if value is None:
        return ""
    text = str(value).strip()
    if not text:
        return ""
    try:
        number = float(text)
    except ValueError:
        return text
    return str(int(number)) if number == int(number) else str(number)


def write_mirror(path: Path, rows):
    """Write the presentation mirror: tab-separated, sorted, column-filtered,
    styled headers. See console_outputs/reference.csv."""
    ordered = sorted(rows, key=lambda r: (str(r.get("graph", "")),
                                          int(r.get("k", 0)),
                                          int(r.get("p", 0))))

    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        f.write("\t".join(header for header, _ in MIRROR_COLUMNS) + "\n")
        for r in ordered:
            cells = [fmt_cell(r.get(key)) for _, key in MIRROR_COLUMNS]
            # A run that never produced a verdict has no Result to show, so
            # surface why (TIMEOUT / CRASH(...)) instead of an empty cell.
            status = str(r.get("status", ""))
            if not status.startswith("ok"):
                cells[4] = status
            f.write("\t".join(cells) + "\n")


def summarize(name, rows):
    ok = sum(1 for r in rows if r.get("status") == "ok")
    dirty = sum(1 for r in rows if str(r.get("status", "")).startswith("ok-but-"))
    wins = sum(1 for r in rows if r.get("result") == "WIN")
    losses = sum(1 for r in rows if r.get("result") == "LOSS")
    timeouts = sum(1 for r in rows if r.get("status") == "TIMEOUT")
    crashes = sum(1 for r in rows if str(r.get("status", "")).startswith("CRASH"))
    other = len(rows) - ok - dirty - timeouts - crashes
    print(f"{name:<26} {len(rows):>4} runs | ok={ok} (win={wins} loss={losses}) "
          f"ok-but-crashed={dirty} crash={crashes} timeout={timeouts} other={other}")


def finished(row):
    return str(row.get("status", "")).startswith("ok")


def compare(base_name, base_rows, other_name, other_rows):
    """Diff two solvers against the game-theoretic invariants."""
    base = {(r["graph"], r["k"], r["p"]): r for r in base_rows}
    other = {(r["graph"], r["k"], r["p"]): r for r in other_rows}

    failures, flips, plys_changed, incomplete = [], [], [], []

    for key in sorted(base.keys() & other.keys()):
        g, k, p = key
        b, o = base[key], other[key]
        if not (finished(b) and finished(o)):
            if not finished(b):
                incomplete.append(f"{base_name} {g} k={k} p={p}: {b.get('status')}")
            if not finished(o):
                incomplete.append(f"{other_name} {g} k={k} p={p}: {o.get('status')}")
            continue

        rb, ro = b.get("result"), o.get("result")
        pb, po = str(b.get("capture_plys", "")), str(o.get("capture_plys", ""))

        if p == 1:
            # Full visibility: identical game, so any divergence is a bug.
            if rb != ro:
                failures.append(f"p=1 VERDICT MISMATCH {g} k={k}: "
                                f"{base_name}={rb} {other_name}={ro}")
            elif rb == "WIN" and pb != po:
                failures.append(f"p=1 PLY MISMATCH {g} k={k}: "
                                f"{base_name}={pb} {other_name}={po}")
        else:
            if rb == "WIN" and ro == "LOSS":
                failures.append(f"WIN->LOSS REGRESSION {g} k={k} p={p}: "
                                f"{base_name} won in {pb} plys, {other_name} says LOSS")
            elif rb == "LOSS" and ro == "WIN":
                flips.append(f"{g} k={k} p={p}: {other_name} wins in {po} plys")
            elif rb == "WIN" and ro == "WIN" and pb != po:
                plys_changed.append(f"{g} k={k} p={p}: {pb} -> {po} plys")

    # Monotonicity, checked within each solver: more visibility (lower p) must
    # never be worse for the cops, and more cops must never be worse either.
    for name, rows in ((base_name, base_rows), (other_name, other_rows)):
        by_gk, by_gp = defaultdict(dict), defaultdict(dict)
        for r in rows:
            if finished(r) and r.get("result") in ("WIN", "LOSS"):
                by_gk[(r["graph"], r["k"])][r["p"]] = r["result"]
                by_gp[(r["graph"], r["p"])][r["k"]] = r["result"]
        for (g, k), m in sorted(by_gk.items()):
            ps = sorted(m)
            for a, b_ in zip(ps, ps[1:]):
                if m[a] == "LOSS" and m[b_] == "WIN":
                    failures.append(f"p-MONOTONICITY {name} {g} k={k}: "
                                    f"LOSS at p={a} but WIN at p={b_}")
        for (g, p), m in sorted(by_gp.items()):
            ks = sorted(m)
            for a, b_ in zip(ks, ks[1:]):
                if m[a] == "WIN" and m[b_] == "LOSS":
                    failures.append(f"k-MONOTONICITY {name} {g} p={p}: "
                                    f"WIN at k={a} but LOSS at k={b_}")

    print("\n" + "=" * 72)
    print(f"COMPARISON: {base_name} (baseline) vs {other_name}")
    print("=" * 72)

    print(f"\nFAILURES ({len(failures)}):")
    for x in failures:
        print("  " + x)
    if not failures:
        print("  none -- all invariants hold")

    print(f"\nLOSS -> WIN flips ({len(flips)}), expected at p>=2 from the ghosting fix:")
    for x in flips:
        print("  " + x)

    print(f"\nWIN ply-count changes at p>=2 ({len(plys_changed)}):")
    for x in plys_changed:
        print("  " + x)

    if incomplete:
        print(f"\nNot compared -- run did not finish ({len(incomplete)}):")
        for x in incomplete:
            print("  " + x)

    return len(failures)


def reformat_existing(names):
    """Rebuild the mirrors from CSVs already on disk (no solver runs)."""
    written = 0
    for name in names:
        stem = Path(name).stem
        raw = OUT_DIR / f"{stem}_test_results.csv"
        if not raw.exists():
            print(f"Skipped {stem}: {raw.relative_to(REPO_ROOT)} not found")
            continue
        with open(raw, newline="") as f:
            rows = list(csv.DictReader(f))
        mirror = OUT_DIR / f"{stem}_test_results_formatted.csv"
        write_mirror(mirror, rows)
        print(f"Written: {mirror.relative_to(REPO_ROOT)}  ({len(rows)} rows)")
        written += 1
    return 0 if written else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--timeout", type=float, default=120.0,
                    help="Hard per-run wall-clock timeout in seconds (default 120).")
    ap.add_argument("--state-budget", type=float, default=1.5e9,
                    help="Skip a combo if estimated DP state bytes exceed this (default 1.5e9).")
    ap.add_argument("--graphs", type=str, default="",
                    help="Comma-separated substrings; only matrices matching one are tested.")
    ap.add_argument("--include-big", action="store_true",
                    help="Include large graphs (scotlandyard*, cycle100, line100, tree127).")
    ap.add_argument("--exe", action="append", default=None,
                    help="Solver to test (exe name or path); repeatable. "
                         f"Default: {', '.join(DEFAULT_SOLVERS)}.")
    ap.add_argument("--reformat", action="store_true",
                    help="Rebuild the formatted mirrors from the CSVs already in "
                         "console_outputs/ and exit, without re-running any solver.")
    args = ap.parse_args()

    if args.reformat:
        sys.exit(reformat_existing(args.exe or DEFAULT_SOLVERS))

    ensure_runtime_on_path()

    solvers = []
    for name in (args.exe or DEFAULT_SOLVERS):
        exe = Path(name)
        if not exe.is_absolute():
            exe = exe if exe.exists() else BIN_DIR / exe
        if exe.suffix != ".exe":
            exe = exe.with_suffix(".exe")
        if not exe.exists():
            sys.exit(f"Executable not found: {exe}\n(Run `python build.py` first.)")
        solvers.append(exe)

    jobs = collect_jobs(args)
    if not jobs:
        sys.exit("No jobs matched -- check --graphs / --state-budget.")

    graphs = len({j[2] for j in jobs})
    print(f"Solvers  : {', '.join(s.name for s in solvers)}")
    print(f"Matrices : {MATRIX_DIR}  ({graphs} graphs)")
    print(f"Jobs     : {len(jobs)} per solver ({len(jobs) * len(solvers)} total), "
          f"k={list(K_VALUES)} x p={list(P_VALUES)}, timeout {args.timeout:.0f}s")

    results = {}
    suite_t0 = time.perf_counter()
    for exe in solvers:
        rows = run_suite(exe, jobs, args.timeout)
        results[exe.stem] = rows
        for path in write_outputs(exe.stem, rows):
            print(f"\nWritten: {path.relative_to(REPO_ROOT)}")
    suite_wall = time.perf_counter() - suite_t0

    print("\n" + "=" * 72)
    print(f"Ran {len(jobs) * len(solvers)} runs in {suite_wall:.1f}s")
    print("=" * 72)
    for name, rows in results.items():
        summarize(name, rows)

    failures = 0
    names = list(results)
    for other in names[1:]:
        failures += compare(names[0], results[names[0]], other, results[other])

    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
