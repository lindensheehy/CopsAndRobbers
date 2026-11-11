"""Correctness tests for build/bin/k_cops_scotlandyard.exe.

Compares the solver against the independent, exhaustive Python reference from
experimental/oracle_cpu/test.py (set-based finite-horizon game + Bellman full-visibility
oracle written in the real V5 file format). Small fixtures only.

Covers: oracle modes off/order/bound, visible and hidden roots, several horizons and reveal
schedules, repeated cop positions, self-edges, 1 thread vs. many threads with forced
splitting, transposition table off / tiny (eviction) / normal, validity of every returned first
cop move, checkpoint/resume, UNKNOWN on budget exhaustion, bad oracle headers, saturation.

    python util/test_k_cops_scotlandyard.py [--exe build/bin/k_cops_scotlandyard.exe]
"""
import argparse
import concurrent.futures as cf
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sy_reference', ROOT / 'experimental/oracle_cpu/test.py')
ref_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref_module)
Reference, INF = ref_module.Reference, ref_module.INF


def runtime_env():
    env = dict(os.environ)
    dll = os.environ.get('K_COPS_DLL_DIR', r'C:\msys64\ucrt64\bin')
    if os.name == 'nt' and Path(dll).exists():
        env['PATH'] = dll + os.pathsep + env.get('PATH', '')
    for key in list(env):
        if key.startswith('SY_'):
            del env[key]
    return env


BASE_ENV = runtime_env()


class Runner:
    def __init__(self, exe, folder):
        self.exe, self.folder, self.counter = str(exe), folder, 0

    def run(self, graph, k, settings, expect_ok=True):
        self.counter += 1
        results = self.folder / f'res_{os.getpid()}_{self.counter}_{random.random():.9f}.json'
        env = dict(BASE_ENV)
        env.setdefault('SY_TT_ENTRIES', '4096')
        env['SY_RESULTS'] = str(results)
        env['SY_WARM'] = '0'
        env.update({key: str(value) for key, value in settings.items()})
        proc = subprocess.run([self.exe, str(graph), str(k), '1'], capture_output=True, text=True,
                              timeout=120, env=env)
        failed = '[FAILED STEP]' in proc.stdout or proc.returncode != 0
        if expect_ok:
            assert not failed, (settings, proc.stdout[-2000:], proc.stderr[-2000:])
            data = json.loads(results.read_text())
            results.unlink()
            return data
        assert failed, (settings, proc.stdout[-2000:])
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/bin/k_cops_scotlandyard.exe')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    exe = args.exe.resolve()
    assert exe.exists(), f'build first: {exe}'

    rng = random.Random(710)
    with tempfile.TemporaryDirectory(prefix='sy_solver_test_') as tmp:
        folder = Path(tmp)
        runner = Runner(exe, folder)

        fixtures = [(['010', '101', '010'], 1), (['0101', '1010', '0101', '1010'], 1),
                    (['1100', '1110', '0111', '0011'], 1), (['0101', '1010', '0101', '1010'], 2),
                    (['0100', '1011', '0100', '0100'], 2), (['011', '101', '110'], 4)]
        for _ in range(4):
            n = 5
            edges = {(i, i + 1) for i in range(n - 1)}
            edges |= {(i, j) for i in range(n) for j in range(i, n) if rng.random() < .2}
            fixtures.append(([''.join('1' if (min(i, j), max(i, j)) in edges else '0'
                                      for j in range(n)) for i in range(n)], rng.choice([1, 2])))
        # A slightly larger fixture so parallel splitting has real work (6-cycle + chord, 2 cops).
        n = 7
        edges = {(i, (i + 1) % n) for i in range(n)} | {(0, 3)}
        fixtures.append(([''.join('1' if (i, j) in edges or (j, i) in edges else '0'
                                  for j in range(n)) for i in range(n)], 2))

        parallel_configs = [
            {'SY_THREADS': 1},
            {'SY_THREADS': 4, 'SY_FORCE_SPLIT': 1, 'SY_SPLIT_ROUNDS': 250},
            {'SY_THREADS': 3, 'SY_FORCE_SPLIT': 1, 'SY_SPLIT_ROUNDS': 2},
            {'SY_THREADS': 8},
        ]
        tt_configs = [0, 2, 64, 4096]

        jobs = []   # (description, graph, k, settings, checker)
        differences = 0
        for fi, (rows, k) in enumerate(fixtures):
            graph, oracle = folder / f'g{fi}.txt', folder / f'g{fi}.bin'
            graph.write_text('\n'.join(rows) + '\n')
            ref = Reference(rows, k)
            ref.oracle(oracle)
            starts = list(dict.fromkeys([ref.configs[0], ref.configs[len(ref.configs) // 2],
                                         ref.configs[-1]]))
            for c in starts:
                for horizon, reveals in [(1, ()), (2, ()), (3, ()), (4, (1, 3)), (4, (1, 2, 3, 4)),
                                         (5, (2, 4))]:
                    solve, action = ref.game(horizon, reveals)
                    for cloud in [None, tuple(range(ref.n)), tuple(range(0, ref.n, 2))]:
                        roots = [(r,) for r in range(ref.n)] if cloud is None else [cloud]
                        expected = [solve(c, b, 1) for b in roots]
                        if cloud is not None and all(solve(c, (r,), 1) for r in cloud) and not expected[0]:
                            differences += 1
                        for mode in ['off', 'order', 'bound']:
                            settings = {'SY_START': ','.join(map(str, c)), 'SY_ROUNDS': horizon,
                                        'SY_REVEALS': ','.join(map(str, reveals)) or 'none',
                                        'SY_ORACLE_MODE': mode, 'SY_ORACLE': oracle,
                                        'SY_TT_ENTRIES': rng.choice(tt_configs)}
                            settings.update(rng.choice(parallel_configs))
                            if cloud is not None:
                                settings['SY_HIDDEN_START'] = ','.join(map(str, cloud))

                            def check(result, c=c, roots=roots, expected=expected, action=action,
                                      ref=ref, ctx=(fi, c, horizon, reveals, cloud, mode)):
                                got = [r['status'] == 'WIN' for r in result['roots']]
                                assert got == expected, (ctx, result, expected)
                                assert result['status'] == ('WIN' if all(expected) else 'LOSS'), ctx
                                for root, b, win in zip(result['roots'], roots, expected):
                                    if win and set(b) - set(c):
                                        d = tuple(root['first_cop_move'])
                                        assert d in ref.moves[c] and action(c, b, 1, d), (ctx, d)
                                return result['pruned']
                            jobs.append((graph, k, settings, check))

        checks = prunes = 0
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            futures = [pool.submit(runner.run, g, k, s) for g, k, s, _ in jobs]
            for future, (_, _, _, check) in zip(futures, jobs):
                prunes += check(future.result())
                checks += 1
        assert prunes > 0 and differences > 0, (prunes, differences)

        # --- checkpoint / budget behaviour ---
        graph, oracle, cp = folder / 'g0.txt', folder / 'g0.bin', folder / 'roots.checkpoint'
        base = {'SY_START': '0', 'SY_ROUNDS': 4, 'SY_REVEALS': 'none', 'SY_ORACLE': oracle,
                'SY_THREADS': 1}
        limited = runner.run(graph, 1, {**base, 'SY_CHECKPOINT': cp, 'SY_MAX_NODES': 1})
        assert limited['status'] == 'UNKNOWN', limited
        assert any(r['status'] == 'UNKNOWN' for r in limited['roots'])
        resumed = runner.run(graph, 1, {**base, 'SY_CHECKPOINT': cp, 'SY_RESUME': 1})
        fresh = runner.run(graph, 1, base)
        assert resumed['roots'] == fresh['roots'], (resumed, fresh)
        runner.run(graph, 1, {**base, 'SY_CHECKPOINT': cp}, expect_ok=False)            # no overwrite
        runner.run(graph, 1, {**base, 'SY_CHECKPOINT': cp, 'SY_RESUME': 1, 'SY_ROUNDS': 3},
                   expect_ok=False)                                                   # changed input
        checks += 5

        # --- input validation ---
        data = oracle.read_bytes()
        import struct
        for changed in [data[:-1], b'NOPE' + data[4:], data[:4] + struct.pack('<I', 2) + data[8:]]:
            bad = folder / 'bad.bin'
            bad.write_bytes(changed)
            runner.run(graph, 1, {**base, 'SY_ORACLE': bad}, expect_ok=False)
        runner.run(graph, 1, {**base, 'SY_ORACLE': folder / 'g1.bin'}, expect_ok=False)  # other graph
        runner.run(graph, 1, {**base, 'SY_START': '255'}, expect_ok=False)
        runner.run(graph, 1, {**base, 'SY_REVEALS': '0'}, expect_ok=False)
        runner.run(graph, 1, {**base, 'SY_ORACLE_MODE': 'off', 'SY_START': ''}, expect_ok=False)
        checks += 7

        # --- saturation: payload 127 is a finite optimistic bound, not a loss (cap 0) ---
        rows = [''.join('0' if i == j else '1' for j in range(126)) for i in range(126)]
        graph = folder / 'saturated.txt'
        graph.write_text('\n'.join(rows) + '\n')
        oracle = folder / 'saturated.bin'
        ref = Reference(rows, 1)
        ref.oracle(oracle)
        result = runner.run(graph, 1, {'SY_START': 0, 'SY_ORACLE': oracle, 'SY_ROUNDS': 2,
                                       'SY_HIDDEN_START': '1,2', 'SY_REVEALS': 'none'})
        assert result['status'] == ('WIN' if ref.game(2, ())[0]((0,), (1, 2), 1) else 'LOSS')
        checks += 1

    print(f'PASS: {checks} solver/reference and boundary checks; {prunes} oracle prunes; '
          f'{differences} cases where individual wins do not solve the hidden cloud.')


if __name__ == '__main__':
    main()
