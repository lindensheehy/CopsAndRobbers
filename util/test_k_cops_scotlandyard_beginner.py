"""Correctness tests for build/bin/k_cops_scotlandyard_beginner.exe.

Compares the solver's verdict with an independent brute-force reference written straight from the
rules in docs/beginner_rules/beginner_rules_brief.md. The reference shares no code or method with
the solver: it plays the game forward by plain recursion, and builds cop moves by literally trying
every order in which the cops can move one at a time.

    python util/test_k_cops_scotlandyard_beginner.py [--exe PATH] [--random N] [--board N]
"""

import argparse
import functools
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


# ------------------------------------------------------------------------------------------------
# Reference solver
# ------------------------------------------------------------------------------------------------

def cop_moves(cops, nbrs):
    """All station sets the cops can end on, moving one at a time in some order.

    A cop may move to any neighbour not occupied by another cop at that moment (so a station
    vacated earlier in the same turn is fine, but a swap is not). Nobody may stay put."""
    results = set()
    k = len(cops)

    def move(order, idx, pos):
        if idx == k:
            results.add(frozenset(pos))
            return
        i = order[idx]
        for d in nbrs[pos[i]]:
            if d in pos:
                continue
            nxt = list(pos)
            nxt[i] = d
            move(order, idx + 1, nxt)

    for order in itertools.permutations(range(k)):
        move(order, 0, list(cops))
    return results


def reference_verdict(nbrs, cops, robber, rounds, hidden):
    """True iff the cops can force a capture. Stations are 0-based here."""

    @functools.lru_cache(maxsize=None)
    def moves(c):
        return tuple(cop_moves(tuple(sorted(c)), nbrs))

    @functools.lru_cache(maxsize=None)
    def win(t, c, r):
        if t > rounds:
            return False                       # all rounds played, no capture
        options = [x for x in nbrs[r] if x not in c]
        if not options:
            return True                        # robber has no free station
        ms = moves(c)                          # empty -> the cops lose (any() of nothing)
        if t in hidden:
            # The cops pick one move without knowing where the robber went
            return any(all(x in m or win(t + 1, m, x) for x in options) for m in ms)
        return all(any(x in m or win(t + 1, m, x) for m in ms) for x in options)

    return win(1, frozenset(cops), robber)


# ------------------------------------------------------------------------------------------------
# Running the solver
# ------------------------------------------------------------------------------------------------

def solver_env(threads):
    env = {k: v for k, v in os.environ.items() if not k.startswith('SYB_')}
    if os.name == 'nt':
        env['PATH'] = r'C:\msys64\ucrt64\bin' + os.pathsep + env.get('PATH', '')
    env['SYB_THREADS'] = str(threads)
    return env


def run_solver(exe, graph_file, cops, robber, rounds, hidden, threads, workdir):
    env = solver_env(threads)
    env['SYB_COPS'] = ','.join(str(c + 1) for c in cops)          # solver uses 1-based stations
    env['SYB_ROBBER'] = str(robber + 1)
    env['SYB_ROUNDS'] = str(rounds)
    env['SYB_HIDDEN'] = ','.join(str(h) for h in sorted(hidden)) or 'none'
    proc = subprocess.run([str(exe), str(graph_file), str(len(cops))], capture_output=True, text=True,
                          env=env, cwd=workdir, timeout=600)
    m = re.search(r'RESULT: (WIN|LOSS)', proc.stdout)
    if not m:
        raise RuntimeError(f'solver failed:\n{proc.stdout}\n{proc.stderr}')
    return m.group(1) == 'WIN'


def write_graph(path, n, edges):
    rows = [['0'] * n for _ in range(n)]
    for u, v in edges:
        rows[u][v] = rows[v][u] = '1'
    path.write_text('\n'.join(''.join(r) for r in rows) + '\n-')


def load_graph(path):
    rows = [line.strip() for line in path.read_text().splitlines() if line.strip() and line.strip() != '-']
    return [[j for j, ch in enumerate(r) if ch == '1'] for r in rows]


def random_graph(rng, n):
    """Connected graph: a random spanning tree plus some extra edges (trees cause blocked cops)."""
    edges = set()
    for v in range(1, n):
        u = rng.randrange(v)
        edges.add((u, v))
    extra = rng.choice([0, 0, 1, 2, 4, 8])
    for _ in range(extra):
        u, v = rng.sample(range(n), 2)
        edges.add((min(u, v), max(u, v)))
    return sorted(edges)


# ------------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/bin/k_cops_scotlandyard_beginner.exe')
    parser.add_argument('--random', type=int, default=400, help='random small-graph games')
    parser.add_argument('--board', type=int, default=40, help='short games on the real board')
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()

    exe = args.exe.resolve()
    assert exe.exists(), f'build first: {exe}'
    rng = random.Random(args.seed)
    failures, counts = 0, {True: 0, False: 0}

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        def check(label, graph_file, nbrs, cops, robber, rounds, hidden):
            nonlocal failures
            expected = reference_verdict(nbrs, tuple(cops), robber, rounds, frozenset(hidden))
            got = run_solver(exe, graph_file, cops, robber, rounds, hidden, rng.randint(1, 4), tmp)
            counts[expected] += 1
            if got != expected:
                failures += 1
                print(f'MISMATCH {label}: cops={[c + 1 for c in cops]} robber={robber + 1} rounds={rounds} '
                      f'hidden={sorted(hidden)} expected={"WIN" if expected else "LOSS"} got={"WIN" if got else "LOSS"}')

        # 1. Random small graphs, random rules
        for i in range(args.random):
            n = rng.randint(4, 10)
            k = rng.randint(1, min(4, n - 2))
            graph_file = tmp / f'g{i}.txt'
            write_graph(graph_file, n, random_graph(rng, n))
            nbrs = load_graph(graph_file)
            stations = rng.sample(range(n), k + 1)
            rounds = rng.randint(1, 7)
            hidden = {t for t in range(1, rounds + 1) if rng.random() < 0.3}
            check(f'random #{i} (n={n}, k={k})', graph_file, nbrs, stations[:k], stations[k], rounds, hidden)

        # 2. Short games on the real beginner board
        board = ROOT / 'assets/matrices/scotlandyard-beginner.txt'
        nbrs = load_graph(board)
        for i in range(args.board):
            k = rng.randint(1, 3)
            stations = rng.sample(range(len(nbrs)), k + 1)
            rounds = rng.randint(1, 3 if k == 3 else 4)
            hidden = {t for t in range(1, rounds + 1) if rng.random() < 0.4}
            check(f'board #{i} (k={k})', board, nbrs, stations[:k], stations[k], rounds, hidden)

    total = counts[True] + counts[False]
    print(f'{total - failures}/{total} agree ({counts[True]} cop wins, {counts[False]} robber wins in the reference)')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
