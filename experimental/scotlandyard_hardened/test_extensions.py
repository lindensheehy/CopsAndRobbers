"""Regression checks for failure recovery, periodic visibility, AVX scoring and provenance."""
import argparse, hashlib, importlib.util, json, os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('reference',ROOT/'experimental/oracle_cpu/test.py')
refmod=importlib.util.module_from_spec(spec);spec.loader.exec_module(refmod)
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,default=ROOT/'build/bin/scotlandyard_hardened.exe');p.add_argument('--skip-score-build',action='store_true');a=p.parse_args()
base={k:v for k,v in os.environ.items() if not k.startswith('SY_')}
base.update(SY_TT_ENTRIES='4096',SY_WARM='0',SY_THREADS='4',SY_FORCE_SPLIT='1',SY_TRUST_LEGACY_ORACLE='1')
checks=0
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp)
    def run(graph,k,settings,period=2,codes=(0,)):
        global checks
        output=folder/'result.json';output.unlink(missing_ok=True)
        proc=subprocess.run([str(a.exe.resolve()),str(graph),str(k),str(period)],env={**base,'SY_RESULTS':str(output),**{k:str(v) for k,v in settings.items()}},capture_output=True,text=True,timeout=30)
        assert proc.returncode in codes,(settings,proc.returncode,proc.stdout[-1500:],proc.stderr)
        checks+=1
        return json.loads(output.read_text()) if output.exists() else None
    rows=[''.join('1' if (i-j)%9 in (1,8) else '0' for j in range(9)) for i in range(9)]
    graph=folder/'cycle9.txt';graph.write_text('\n'.join(rows)+'\n')
    oracle=folder/'cycle9.bin';reference=refmod.Reference(rows,2);reference.oracle(oracle)
    settings={'SY_ORACLE':oracle,'SY_START':'0,4','SY_ROUNDS':5}
    periodic=run(graph,2,settings)
    explicit=run(graph,2,{**settings,'SY_REVEALS':'2,4'})
    assert periodic['roots']==explicit['roots'] and periodic['reveals']==[2,4]
    byperiod=run(graph,2,{**settings,'SY_PERIOD':3})
    assert byperiod['reveals']==[3]
    run(graph,2,{**settings,'SY_PERIOD':3,'SY_REVEALS':'2,4'},codes=(2,))
    # No unchecked oracle accepted by default; manifest binds graph and every payload byte.
    run(graph,2,{**settings,'SY_TRUST_LEGACY_ORACLE':0},codes=(2,))
    manifest=Path(str(oracle)+'.manifest')
    manifest.write_text('SY_ORACLE_MANIFEST_V1\n'+hashlib.sha256(('\n'.join(rows)+'\n').encode()).hexdigest()+'\n'+hashlib.sha256(oracle.read_bytes()).hexdigest()+'\noperator_attests_completed_fixed_point_same_rules\n')
    assert run(graph,2,{**settings,'SY_TRUST_LEGACY_ORACLE':0})['roots']==periodic['roots']
    original=oracle.read_bytes();changed=bytearray(original);changed[-1]^=2;oracle.write_bytes(changed)
    run(graph,2,settings,codes=(2,));oracle.write_bytes(original)
    # Exceptions in nested/root work must terminate as UNKNOWN, not hang or turn into LOSS.
    hard={**settings,'SY_ORACLE_MODE':'off','SY_REVEALS':'none','SY_ROUNDS':8,'SY_HIDDEN_START':','.join(map(str,range(9))),'SY_SPLIT_ROUNDS':250}
    for node in (1,7,25):
        result=run(graph,2,{**hard,'SY_TEST_FAIL_NODE':node},codes=(3,))
        assert 'worker error' in result['stop_reason'],result
        if result['status']!='UNKNOWN':
            assert (result['status']=='WIN')==reference.game(8,())[0]((0,4),tuple(range(9)),1),result
    result=run(graph,2,{**hard,'SY_ARENA_MB':0},codes=(3,))
    assert result['status']=='UNKNOWN'
    for option,value in [('SY_TEST_FAIL_QUEUE',1),('SY_TEST_FAIL_QUEUE',2),('SY_TEST_FAIL_THREAD',2)]:
        result=run(graph,2,{**hard,option:value},codes=(3,))
        assert 'worker error' in result['stop_reason']
    # Snapshot completed subtrees while leaving the root unfinished, then resume and compare.
    cp=folder/'resume.chk'
    result=run(graph,2,{**hard,'SY_CHECKPOINT':cp,'SY_MAX_NODES':10,'SY_THREADS':1})
    assert result['status']=='UNKNOWN',result
    memo=Path(str(cp)+'.memo');assert memo.exists()
    resumed=run(graph,2,{**hard,'SY_CHECKPOINT':cp,'SY_RESUME':1})
    fresh=run(graph,2,hard)
    assert [r['status'] for r in resumed['roots']]==[r['status'] for r in fresh['roots']] and resumed['memo_loaded']>0
    data=memo.read_bytes();memo.write_bytes(data[:-1]+bytes([data[-1]^1]))
    run(graph,2,{**hard,'SY_CHECKPOINT':cp,'SY_RESUME':1},codes=(2,));memo.write_bytes(data)
    rootdata=cp.read_bytes();cp.write_bytes(rootdata.replace(b'SHA256 ',b'SHA257 ',1))
    run(graph,2,{**hard,'SY_CHECKPOINT':cp,'SY_RESUME':1},codes=(2,))
    # A missing GPU must be transparent in auto mode, explicit in required/verify mode.
    assert run(graph,2,{**settings,'SY_GPU':'auto','SY_GPU_LIBRARY':folder/'missing.dll'})['roots']==periodic['roots']
    run(graph,2,{**settings,'SY_GPU':'verify','SY_GPU_LIBRARY':folder/'missing.dll'},codes=(2,))
    if not a.skip_score_build:
        score=folder/'score_test.exe'
        cmd=[os.environ.get('CXX','g++'),'-std=c++17','-O2','-pthread','-include','cstddef','-I'+str(ROOT/'lib/include'),str(Path(__file__).with_name('score_test.cpp')),*map(str,(ROOT/'lib/src').glob('*.cpp')),'-o',str(score)]
        if os.name!='nt':cmd+=['-ldl']
        subprocess.run(cmd,check=True);subprocess.run([str(score)],check=True,timeout=30)
print(f'PASS: {checks} periodic/provenance/failure/resume/GPU-fallback process checks')
