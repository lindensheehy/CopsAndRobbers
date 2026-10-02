"""Independent set/recursive minimax oracle. No C++ bitmask or DFS reuse."""
import argparse
import csv
import itertools
from pathlib import Path
import random
import subprocess
import tempfile

INF = 10**30

def reference(adj, k, p):
    n = len(adj)
    configs = list(itertools.combinations_with_replacement(range(n), k))
    index = {c:i for i,c in enumerate(configs)}
    moves = [sorted({index[tuple(sorted(dest))] for dest in itertools.product(*(adj[u] for u in c))}) for c in configs]
    values = [[0 if r in c else INF for r in range(n)] for c in configs]
    while True:
        def tree(c, cloud, depth):
            best = INF
            for dest in moves[c]:
                survivors = cloud - set(configs[dest])
                if not survivors:
                    best = min(best, 2*depth-1)
                    continue
                survivors = set().union(*(adj[r] for r in survivors)) - set(configs[dest])
                if not survivors:
                    best = min(best, 2*depth)
                elif depth == p:
                    worst = max(values[dest][r] for r in survivors)
                    if worst != INF:
                        best = min(best, 2*p+worst)
                else:
                    best = min(best, tree(dest, survivors, depth+1))
            return best
        following = [[min(values[c][r],tree(c,{r},1)) if r not in configs[c] else 0
                      for r in range(n)] for c in range(len(configs))]
        if following == values:
            return [v for row in values for v in row]
        values = following

def check(exe, path, k, p, backend, folder, expected=None):
    output = folder/'states.csv'
    run = subprocess.run([str(exe),str(path),str(k),str(p),'--backend',backend,'--threads','2',
                          '--batch','17','--budget','1','--dump',str(output)],
                         capture_output=True,text=True,timeout=90)
    if run.returncode:
        raise AssertionError(run.stdout+run.stderr)
    with output.open() as f:
        actual = [INF if row['cost']=='INF' else int(row['cost']) for row in csv.DictReader(f)]
    if expected is not None and actual != expected:
        i = next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)
        raise AssertionError(f'{path.name}, k={k}, p={p}, state={i}: {actual[i]} != {expected[i]}')
    return actual

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',default='build/bin/k_cops_visibility_v3_2_cpu.exe')
    parser.add_argument('--backend',choices=['cpu','compare'],default='cpu')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    exe=(root/args.exe).resolve()
    count=0
    with tempfile.TemporaryDirectory() as tmp:
        folder=Path(tmp)
        # Every labeled undirected 4-vertex graph without isolates, with/without loops.
        pairs=list(itertools.combinations(range(4),2))
        for bits in range(1<<len(pairs)):
            adj=[set() for _ in range(4)]
            for i,(u,v) in enumerate(pairs):
                if bits>>i&1:
                    adj[u].add(v); adj[v].add(u)
            if any(not row for row in adj): continue
            for loops in [False,True]:
                graph=[row|({u} if loops else set()) for u,row in enumerate(adj)]
                path=folder/'graph.txt'
                path.write_text('\n'.join(''.join('1' if v in row else '0' for v in range(4)) for row in graph)+'\n')
                for k,p in [(1,1),(1,2),(2,1),(2,2)]:
                    check(exe,path,k,p,args.backend,folder,reference(graph,k,p)); count+=1
        # Deeper blind windows, irregular graph with partial self-loops.
        rng=random.Random(732)
        for sample in range(6):
            graph=[{(u-1)%5,(u+1)%5} for u in range(5)]
            for u in range(5):
                for v in range(u,5):
                    if rng.random()<0.3: graph[u].add(v); graph[v].add(u)
            path=folder/'graph.txt'
            path.write_text('\n'.join(''.join('1' if v in row else '0' for v in range(5)) for row in graph)+'\n')
            check(exe,path,1,3,args.backend,folder,reference(graph,1,3)); count+=1
        path=folder/'long_path.txt'
        path.write_text('\n'.join(''.join('1' if abs(u-v)<=1 else '0' for v in range(100)) for u in range(100))+'\n')
        actual=check(exe,path,1,1,args.backend,folder)
        assert max(v for v in actual if v!=INF)>127, 'Missing long-capture regression'
        # The path has explicit self loops: a cop can chase the robber to the far end.
        assert actual[99]==197, actual[99]
        count+=1
        actual=check(exe,root/'assets/matrices/dismantleable48.txt',1,1,args.backend,folder)
        assert min(max(actual[c*48:(c+1)*48]) for c in range(48))==13
        count+=1
    print(f'PASS: {count} graph/parameter cases; complete tables checked against independent reference for {count-2} cases; 2 capture-time regressions.')

if __name__=='__main__': main()
