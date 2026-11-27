"""Independent Python set-based exhaustive game and full-visibility oracle reference.
No packages required. Runs tiny fixtures only; never reads the user's large cache.
"""
import argparse
from functools import lru_cache
from itertools import combinations_with_replacement, product
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INF = 10**9

class Reference:
    def __init__(self, rows, k):
        self.rows = rows
        self.n = len(rows)
        self.adj = [tuple(i for i, x in enumerate(row) if x == '1') for row in rows]
        self.configs = list(combinations_with_replacement(range(self.n), k))
        self.ids = {c: i for i, c in enumerate(self.configs)}
        self.moves = {c: sorted(set(tuple(sorted(x)) for x in product(*(self.adj[v] for v in c)))) for c in self.configs}

    def fullvis(self):
        C = {(c,r): (0 if r in c else INF) for c in self.configs for r in range(self.n)}
        R = dict(C)
        while True:
            newC, newR = dict(C), dict(R)
            for c, r in C:
                if r in c:
                    continue
                newC[c,r] = min(INF, 1 + min(R[d,r] for d in self.moves[c]))
                newR[c,r] = min(INF, 1 + max(C[c,s] for s in self.adj[r]))
            if newC == C and newR == R:
                self.C, self.R = C, R
                return
            C, R = newC, newR

    def oracle(self, path):
        self.fullvis()
        degree = max(map(len, self.adj))
        base, cap = degree + 1, 125 - degree
        best = min(self.configs, key=lambda c: max(self.C[c,r] for r in range(self.n)))
        bestply = max(self.C[best,r] for r in range(self.n))
        header = struct.pack('<4I3Q6I', 0x35564643, 3, self.n, len(best), len(self.configs),
                             len(self.configs)*self.n, self.ids[best] if bestply < INF else 2**64-1,
                             bestply, 2, degree, base, cap, 127)
        data = bytearray()
        for c in self.configs:
            for r in range(self.n):
                value = self.R[c,r]
                payload = degree if value == INF else 127 if value > cap else base + value
                data.append((payload << 1) | (self.C[c,r] < INF))
        path.write_bytes(header + data)

    def game(self, horizon, reveals):
        @lru_cache(None)
        def solve(c, cloud, turn):
            cloud = frozenset(cloud) - set(c)
            if not cloud:
                return True
            if turn > horizon:
                return False
            return any(action(c, cloud, turn, d) for d in self.moves[c])

        def action(c, cloud, turn, dest):
            surviving = set(cloud) - set(c) - set(dest)
            after = set().union(*(set(self.adj[r]) for r in surviving)) - set(dest)
            if not after:
                return True
            if turn == horizon:
                return False
            if turn in reveals:
                return all(solve(dest, (r,), turn+1) for r in after)
            return solve(dest, tuple(sorted(after)), turn+1)
        return solve, action


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--exe', type=Path, default=ROOT/'build/bin/scotlandyard_oracle_cpu.exe')
    a = p.parse_args()
    exe = str(a.exe.resolve())
    checks = 0
    prunes = 0
    differences = 0
    def run(args, codes=(0,)):
        result = subprocess.run([exe, *map(str,args)], capture_output=True, text=True, timeout=20)
        assert result.returncode in codes, (args, result.returncode, result.stdout, result.stderr)
        return json.loads(result.stdout) if result.stdout else result.stderr

    with tempfile.TemporaryDirectory(prefix='sy_cpu_test_') as tmp:
        folder = Path(tmp)
        rank_exe=folder/'rank_test.exe'
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O2',
                        str(Path(__file__).with_name('rank_test.cpp')),'-o',str(rank_exe)],check=True)
        subprocess.run([str(rank_exe)],check=True)
        fixtures = [(['010','101','010'],1), (['0101','1010','0101','1010'],1),
                    (['1100','1110','0111','0011'],1), (['0101','1010','0101','1010'],2),
                    (['0100','1011','0100','0100'],2), (['011','101','110'],4)]
        rng = random.Random(710)
        for _ in range(4):
            n=5
            edges = {(i,i+1) for i in range(n-1)}
            edges |= {(i,j) for i in range(n) for j in range(i,n) if rng.random()<.2}
            fixtures.append(([''.join('1' if (min(i,j),max(i,j)) in edges else '0' for j in range(n)) for i in range(n)],rng.choice([1,2])))
        for fi,(rows,k) in enumerate(fixtures):
            graph, oracle = folder/f'g{fi}.txt', folder/f'g{fi}.bin'
            graph.write_text('\n'.join(rows)+'\n')
            ref=Reference(rows,k);ref.oracle(oracle)
            starts = list(dict.fromkeys([ref.configs[0],ref.configs[len(ref.configs)//2],ref.configs[-1]]))
            for c in starts:
                base=[graph,'--cops',k,'--start',','.join(map(str,c)),'--max-seconds',0]
                for r in range(ref.n):
                    q=run(base+['--oracle',oracle,'--oracle-graph',graph,'--query',r])
                    expected=ref.C[c,r]
                    assert q['winning']==(expected<INF)
                    assert q['exact']
                    assert q['ply_lower_bound']==(expected if expected<INF else None),(fi,c,r,q,expected)
                    checks+=1
                for horizon,reveals in [(1,()),(2,()),(3,()),(4,(1,3)),(4,(1,2,3,4))]:
                    solve,action=ref.game(horizon,reveals)
                    common=base+['--rounds',horizon,'--reveals',','.join(map(str,reveals)) or 'none']
                    for cloud in [None,tuple(range(ref.n)),tuple(range(0,ref.n,2))]:
                        expected_roots=[(r,) for r in range(ref.n)] if cloud is None else [cloud]
                        expected=[solve(c,b,1) for b in expected_roots]
                        if cloud is not None and all(solve(c,(r,),1) for r in cloud) and not expected[0]:differences+=1
                        rootargs=[] if cloud is None else ['--hidden-start',','.join(map(str,cloud))]
                        for mode in ['off','order','bound']:
                            result=run(common+rootargs+['--oracle',oracle,'--oracle-graph',graph,'--oracle-mode',mode,
                                                       '--threads',3,'--memo-entries',rng.choice([0,3,1000])])
                            assert [r['status']=='WIN' for r in result['roots']]==expected,(fi,c,horizon,reveals,cloud,mode,result,expected)
                            assert result['status']==('WIN' if all(expected) else 'LOSS')
                            for root,b,win in zip(result['roots'],expected_roots,expected):
                                if win and set(b)-set(c):
                                    d=tuple(root['first_cop_move'])
                                    assert d in ref.moves[c] and action(c,b,1,d)
                            prunes+=result['pruned'];checks+=1
        assert prunes>0 and differences>0,(prunes,differences)
        # Checkpoint restart and honest resource exhaustion.
        graph=folder/'g0.txt';oracle=folder/'g0.bin';cp=folder/'roots.checkpoint'
        base=[graph,'--cops',1,'--start','0','--rounds',4,'--max-seconds',0,'--reveals','none',
              '--oracle',oracle,'--oracle-graph',graph]
        limited=run(base+['--checkpoint',cp,'--max-nodes',1],(2,))
        assert limited['status']=='UNKNOWN'
        resumed=run(base+['--checkpoint',cp,'--resume'])
        fresh=run(base)
        assert resumed['roots']==fresh['roots']
        run(base+['--checkpoint',cp],(1,))
        run(base+['--checkpoint',cp,'--resume','--rounds',3],(1,))
        assert run(base+['--preflight'])['status']=='PREFLIGHT_OK'
        # Reject invalid/truncated/foreign headers and graph provenance.
        data=oracle.read_bytes()
        for changed in [data[:-1], b'NOPE'+data[4:],data[:4]+struct.pack('<I',2)+data[8:]]:
            bad=folder/'bad.bin';bad.write_bytes(changed)
            run(base+['--oracle',bad,'--preflight'],(1,))
        run(base+['--oracle-graph',folder/'g1.txt','--preflight'],(1,))
        run(base+['--start','255'],(1,));run(base+['--reveals','0'],(1,))
        # Saturation is a finite optimistic bound, not a loss. Complete graph degree 125 => cap 0.
        rows=[''.join('0' if i==j else '1' for j in range(126)) for i in range(126)]
        graph=folder/'saturated.txt';graph.write_text('\n'.join(rows)+'\n')
        oracle=folder/'saturated.bin';ref=Reference(rows,1);ref.oracle(oracle)
        result=run([graph,'--cops',1,'--start',0,'--oracle',oracle,'--oracle-graph',graph,
                    '--rounds',2,'--hidden-start','1,2','--max-seconds',0])
        assert result['status']==('WIN' if ref.game(2,())[0]((0,),(1,2),1) else 'LOSS')
        checks+=13
    print(f'PASS: {checks} solver/reference and boundary checks; {prunes} oracle prunes; '
          f'{differences} cases where individual wins do not solve the hidden cloud.')

if __name__=='__main__':main()
