#!/usr/bin/env python3
"""
Test harness for build/bin/k_cops_visibility.exe

Runs the solver across a sweep of graphs (assets/matrices/*.txt) and parameters
(k = number of cops, p = visibility fraction 1/p), parses the solver's stdout,
and prints / saves a results table.

Performance is a real concern: the DP state space is ~ n^(k+1) * 2p bytes, and
runtime grows with it. So each combination is cost-estimated first and skipped
if it exceeds a memory budget, and every run is wrapped in a hard wall-clock
timeout. Cheap runs are scheduled first so you get results fast and expensive
runs only fire if they fit the budget.

Usage (from anywhere; paths are resolved relative to the repo root):
    python util/test_k_cops_visibility.py
    python util/test_k_cops_visibility.py --timeout 60 --max-k 2 --max-p 2
    python util/test_k_cops_visibility.py --graphs cycle,grid,peterson
    python util/test_k_cops_visibility.py --include-big       # allow scotlandyard etc.
    python util/test_k_cops_visibility.py --state-budget 2e9  # bytes
"""

import argparse
import csv
import os
import re
import subprocess
import sys
import time
from pathlib import Path

# The exe is built with MSYS2 UCRT64 g++ and dynamically links libstdc++-6.dll /
# libgcc_s_seh-1.dll. Those live in the toolchain bin dir, which is on PATH in a
# normal PowerShell session but NOT in e.g. git-bash. Inject it so the harness
# runs from any shell. Override with K_COPS_DLL_DIR if your toolchain differs.
DLL_DIRS = [os.environ.get("K_COPS_DLL_DIR", r"C:\msys64\ucrt64\bin")]


def ensure_runtime_on_path():
    # Force the toolchain dir to the FRONT. It may already be on PATH but sitting
    # behind another mingw/git bin dir that ships an incompatible libstdc++-6.dll;
    # the loader takes the first match, so position matters, not mere presence.
    parts = os.environ["PATH"].split(os.pathsep)
    for d in reversed(DLL_DIRS):
        if d and os.path.isdir(d):
            parts = [p for p in parts if p.lower().rstrip("\\/") != d.lower().rstrip("\\/")]
            parts.insert(0, d)
    os.environ["PATH"] = os.pathsep.join(parts)

# --- Repo layout -------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
EXE = REPO_ROOT / "build" / "bin" / "k_cops_visibility.exe"
MATRIX_DIR = REPO_ROOT / "assets" / "matrices"
OUT_CSV = REPO_ROOT / "console_outputs" / "test_results.csv"

# Graphs that are large enough to be slow/memory-heavy; gated behind --include-big.
BIG_GRAPHS = {"scotlandyard-all", "scotlandyard-green", "scotlandyard-red",
              "scotlandyard-yellow", "cycle100", "line100", "tree127"}


def node_count(path: Path) -> int:
    """Node count = width of the (square) adjacency matrix = length of first row."""
    with open(path, "r") as f:
        for line in f:
            row = line.strip()
            if row:
                return len(row)
    return 0


def est_state_bytes(n: int, k: int, p: int) -> int:
    """Bytes for the DP `states` array: configs (n^k) * n nodes * 2p columns * 1B."""
    return (n ** k) * n * (2 * p)


# --- Output parsing ----------------------------------------------------------
RE_RESULT = re.compile(r"RESULT:\s+(WIN|LOSS)")
RE_POSITIONS = re.compile(r"Optimal Cop Start Positions:\s*\(([^)]*)\)")
RE_CAPTURE = re.compile(r"Capture Time:\s*(\d+)\s*plys")
RE_FINISHED = re.compile(r"finished in\s+([\d.]+)\s+seconds")
RE_MAINLOOP = re.compile(r"Main Loop\s*-*=>\s*([\d.]+)\s*s")
RE_FOOTPRINT = re.compile(r"Total Footprint\s*-*=>\s*(\d+)\s*B")
RE_PASS = re.compile(r"Pass\s+(\d+):")
RE_CACHE_HIT = re.compile(r"Loaded AuxGraph from cache")
RE_BASECAPS = re.compile(r"Initialized\s+(\d+)\s+base captures")


def parse_output(text: str) -> dict:
    out = {}
    m = RE_RESULT.search(text);      out["result"] = m.group(1) if m else "?"
    m = RE_POSITIONS.search(text);   out["positions"] = m.group(1).strip() if m else ""
    m = RE_CAPTURE.search(text);     out["capture_plys"] = int(m.group(1)) if m else ""
    m = RE_FINISHED.search(text);    out["solver_s"] = float(m.group(1)) if m else ""
    m = RE_MAINLOOP.search(text);    out["mainloop_s"] = float(m.group(1)) if m else ""
    m = RE_FOOTPRINT.search(text);   out["mem_mb"] = round(int(m.group(1)) / 1e6, 1) if m else ""
    m = RE_BASECAPS.search(text);    out["base_caps"] = int(m.group(1)) if m else ""
    out["cache_hit"] = bool(RE_CACHE_HIT.search(text))
    passes = RE_PASS.findall(text);  out["passes"] = int(passes[-1]) if passes else ""
    return out


# Windows NTSTATUS crash codes the exe may exit with (raw unsigned form).
CRASH_CODES = {
    0xC0000374: "CRASH(heap-corruption)",
    0xC0000005: "CRASH(access-violation)",
    0xC0000409: "CRASH(stack-overrun)",
    0xC0000139: "DLL-not-found",
    0xC000041D: "CRASH(unhandled-exc)",
}


def classify_exit(code: int) -> str:
    # subprocess may report the status as signed; normalize to unsigned 32-bit.
    u = code & 0xFFFFFFFF
    if u in CRASH_CODES:
        return CRASH_CODES[u]
    return f"exit{code}"


def run_one(exe: Path, matrix: Path, k: int, p: int, timeout: float) -> dict:
    cmd = [str(exe), str(matrix.relative_to(REPO_ROOT)), str(k), str(p)]
    t0 = time.perf_counter()
    try:
        proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True,
                              timeout=timeout)
        wall = time.perf_counter() - t0
        parsed = parse_output(proc.stdout)
        parsed["wall_s"] = round(wall, 2)
        parsed["exit_code"] = proc.returncode
        got_verdict = parsed["result"] in ("WIN", "LOSS")
        if proc.returncode == 0:
            parsed["status"] = "ok" if got_verdict else "no-result"
        elif got_verdict:
            # Solver printed a complete verdict but the process still died nonzero
            # (observed: heap corruption during static/global teardown). The result
            # is computed but the run is NOT clean -- flag it distinctly.
            parsed["status"] = "ok-but-" + classify_exit(proc.returncode)
        else:
            parsed["status"] = classify_exit(proc.returncode)
        return parsed
    except subprocess.TimeoutExpired:
        return {"status": "TIMEOUT", "wall_s": round(time.perf_counter() - t0, 2)}
    except MemoryError:
        return {"status": "OOM", "wall_s": round(time.perf_counter() - t0, 2)}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--timeout", type=float, default=90.0,
                    help="Hard per-run wall-clock timeout in seconds (default 90).")
    ap.add_argument("--max-k", type=int, default=3, help="Max number of cops (default 3).")
    ap.add_argument("--max-p", type=int, default=3, help="Max visibility p (default 3).")
    ap.add_argument("--state-budget", type=float, default=1.5e9,
                    help="Skip a combo if estimated DP state bytes exceed this (default 1.5e9).")
    ap.add_argument("--graphs", type=str, default="",
                    help="Comma-separated substrings; only matrices matching one are tested.")
    ap.add_argument("--include-big", action="store_true",
                    help="Include large graphs (scotlandyard*, cycle100, line100, tree127).")
    ap.add_argument("--csv", type=str, default=str(OUT_CSV),
                    help="Where to write the results CSV.")
    ap.add_argument("--exe", type=str, default=str(EXE),
                    help="Solver executable to test (default: k_cops_visibility.exe). "
                         "When testing an alternative solver, also pass --csv so the "
                         "baseline results file is not overwritten.")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.is_absolute():
        exe = REPO_ROOT / exe
    if not exe.exists():
        sys.exit(f"Executable not found: {exe}")
    ensure_runtime_on_path()

    filters = [s.strip() for s in args.graphs.split(",") if s.strip()]
    matrices = sorted(MATRIX_DIR.glob("*.txt"))

    # Build the job list: (est_bytes, n, matrix, k, p)
    jobs = []
    for mx in matrices:
        stem = mx.stem
        if filters and not any(f in stem for f in filters):
            continue
        if stem in BIG_GRAPHS and not args.include_big:
            continue
        n = node_count(mx)
        if n < 2:
            continue
        for k in range(1, args.max_k + 1):
            if k >= n:  # more cops than vertices is degenerate
                continue
            for p in range(1, args.max_p + 1):
                est = est_state_bytes(n, k, p)
                if est > args.state_budget:
                    continue
                jobs.append((est, n, mx, k, p))

    jobs.sort(key=lambda j: j[0])  # cheapest first

    print(f"Executable : {exe}")
    print(f"Matrices   : {MATRIX_DIR}  ({len(matrices)} files)")
    print(f"Jobs       : {len(jobs)} (timeout {args.timeout:.0f}s, "
          f"state budget {args.state_budget:.1e} B)\n")

    hdr = f"{'graph':<20} {'n':>4} {'k':>2} {'p':>2} {'result':<6} {'plys':>4} " \
          f"{'passes':>6} {'mem MB':>8} {'solver s':>9} {'wall s':>8}  status/positions"
    print(hdr)
    print("-" * len(hdr))

    rows = []
    suite_t0 = time.perf_counter()
    for est, n, mx, k, p in jobs:
        res = run_one(exe, mx, k, p, args.timeout)
        row = {"graph": mx.stem, "n": n, "k": k, "p": p,
               "est_state_mb": round(est / 1e6, 1)}
        row.update(res)
        rows.append(row)

        note = res.get("status", "?")
        if res.get("status") == "ok":
            note = res.get("positions", "") or "(loss)"
        print(f"{mx.stem:<20} {n:>4} {k:>2} {p:>2} "
              f"{str(res.get('result','')):<6} {str(res.get('capture_plys','')):>4} "
              f"{str(res.get('passes','')):>6} {str(res.get('mem_mb','')):>8} "
              f"{str(res.get('solver_s','')):>9} {str(res.get('wall_s','')):>8}  {note}")

    suite_wall = time.perf_counter() - suite_t0

    # Write CSV
    fields = ["graph", "n", "k", "p", "est_state_mb", "status", "result",
              "capture_plys", "passes", "base_caps", "cache_hit", "mem_mb",
              "mainloop_s", "solver_s", "wall_s", "exit_code", "positions"]
    Path(args.csv).parent.mkdir(parents=True, exist_ok=True)
    with open(args.csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    # Summary
    ok = sum(1 for r in rows if r.get("status") == "ok")
    dirty = sum(1 for r in rows if str(r.get("status", "")).startswith("ok-but-"))
    wins = sum(1 for r in rows if r.get("result") == "WIN")
    losses = sum(1 for r in rows if r.get("result") == "LOSS")
    timeouts = sum(1 for r in rows if r.get("status") == "TIMEOUT")
    crashes = sum(1 for r in rows if str(r.get("status", "")).startswith("CRASH"))
    other = len(rows) - ok - dirty - timeouts - crashes
    print("\n" + "=" * 60)
    print(f"Ran {len(rows)} combos in {suite_wall:.1f}s  |  ok={ok} "
          f"(win={wins} loss={losses})  ok-but-crashed-at-exit={dirty}  "
          f"crash={crashes}  timeout={timeouts}  other={other}")
    print(f"CSV written to {args.csv}")


if __name__ == "__main__":
    main()
