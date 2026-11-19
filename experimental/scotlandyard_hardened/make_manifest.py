"""Bind an existing completed V5 artifact to its authentic input, without changing the oracle.
This records operator attestation, not a new mathematical validation of 13 billion states.
"""
import argparse, hashlib, math, struct
from pathlib import Path

def canonical_matrix(path):
    text=Path(path).read_text(encoding='utf-8').split('-',1)[0]
    rows=[line.rstrip('\r') for line in text.splitlines() if line]
    if not rows or any(len(r)!=len(rows) or set(r)-{'0','1'} for r in rows):
        raise ValueError('Expected a square 0/1 adjacency matrix, without spaces, optionally followed by a - trailer')
    if any(rows[i][j]!=rows[j][i] for i in range(len(rows)) for j in range(len(rows))):
        raise ValueError('Graph must be undirected')
    return rows, ('\n'.join(rows)+'\n').encode()

def make(graph, oracle, result, attest=False):
    if not attest: raise ValueError('Explicit --attest-completed-same-rules is required')
    rows, canonical=canonical_matrix(graph)
    metadata=dict(line.split('=',1) for line in Path(result).read_text().splitlines() if '=' in line)
    if metadata.get('status')!='oracle_saved_and_validated' or metadata.get('cop_stay')!='0' or metadata.get('robber_stay')!='0':
        raise ValueError('Expected completed V5 result metadata with no added stay moves')
    n=len(rows); k=int(metadata['cops']); degree=max(r.count('1') for r in rows)
    with Path(oracle).open('rb') as f:
        header=f.read(64)
        fields=struct.unpack('<4I3Q6I',header)
        configs=math.comb(n+k-1,k)
        if fields[:4]!=(0x35564643,3,n,k) or fields[4:6]!=(configs,configs*n) or fields[8:]!=(2,degree,degree+1,125-degree,127):
            raise ValueError('Oracle header does not match matrix/rules')
        if int(metadata['vertices'])!=n or int(metadata['config_count'])!=configs or int(metadata['state_count'])!=configs*n:
            raise ValueError('Result metadata dimensions mismatch')
        if Path(oracle).stat().st_size!=64+configs*n: raise ValueError('Oracle size mismatch')
        h=hashlib.sha256(header)
        while block:=f.read(32<<20): h.update(block)
    target=Path(str(oracle)+'.manifest')
    body='SY_ORACLE_MANIFEST_V1\n'+hashlib.sha256(canonical).hexdigest()+'\n'+h.hexdigest()+'\noperator_attests_completed_fixed_point_same_rules\n'
    temporary=Path(str(target)+'.tmp');temporary.write_text(body,encoding='ascii');temporary.replace(target)
    return target

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--graph',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path)
    p.add_argument('--result',required=True,type=Path)
    p.add_argument('--attest-completed-same-rules',action='store_true')
    a=p.parse_args(); print('Hashing the complete oracle; this can take several minutes.',flush=True)
    print(make(a.graph,a.oracle,a.result,a.attest_completed_same_rules))
