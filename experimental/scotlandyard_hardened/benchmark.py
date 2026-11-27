"""Paired finite-horizon correctness/performance runs. No large-map extrapolation claims.
Run after both builds. Logs and JSONL are flushed after every process for crash diagnostics.
"""
import argparse, csv, importlib.util, json, math, os, random, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('reference',ROOT/'experimental/oracle_cpu/test.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def matrix(n,edges,loops=False):
    e={tuple(sorted(pair)) for pair in edges}
    return [''.join('1' if tuple(sorted((i,j))) in e or (loops and i==j) else '0' for j in range(n)) for i in range(n)]

def fixtures(full):
    cases=[('path7',matrix(7,[(i,i+1) for i in range(6)]),1),
           ('cycle7',matrix(7,[(i,(i+1)%7) for i in range(7)]),1),
           ('cycle8',matrix(8,[(i,(i+1)%8) for i in range(8)]),2),
           ('grid9',matrix(9,[(i,j) for i in range(9) for j in range(i+1,9) if (j==i+1 and i//3==j//3) or j==i+3]),2)]
    if full:
        rng=random.Random(910)
        edges=[(i,(i+1)%8) for i in range(8)]+[(i,j) for i in range(8) for j in range(i+2,8) if rng.random()<.25]
        cases += [('random8',matrix(8,edges),2),('loops8',matrix(8,edges,True),2),
                  ('cube8',matrix(8,[(i,i^(1<<b)) for i in range(8) for b in range(3)]),3),
                  ('cube8_4c',matrix(8,[(i,i^(1<<b)) for i in range(8) for b in range(3)]),4)]
    return cases

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--suite',choices=['smoke','full'],default='smoke');p.add_argument('--threads',type=int,default=min(16,os.cpu_count() or 1))
    p.add_argument('--seconds',type=int,default=15);p.add_argument('--repeats',type=int,default=1)
    p.add_argument('--oracle-modes',default='bound');p.add_argument('--gpu',action='store_true')
    p.add_argument('--out',type=Path);p.add_argument('--baseline',type=Path,default=ROOT/'build/bin/scotlandyard_oracle_cpu.exe')
    p.add_argument('--solver',type=Path,default=ROOT/'build/bin/scotlandyard_hardened.exe')
    a=p.parse_args();assert a.threads>=1 and a.seconds>=1 and a.repeats>=1
    for exe in (a.baseline,a.solver):
        if not exe.exists():raise SystemExit('Build first: '+str(exe))
    modes=a.oracle_modes.split(',');assert set(modes)<={'off','order','bound'}
    out=a.out or ROOT/'console_outputs'/time.strftime('benchmark_%Y%m%d_%H%M%S')
    out.mkdir(parents=True,exist_ok=False);graphs=out/'fixtures';graphs.mkdir()
    (out/'metadata.json').write_text(json.dumps({'suite':a.suite,'threads':a.threads,'seconds':a.seconds,'repeats':a.repeats,'oracle_modes':modes,'gpu':a.gpu,'note':'Process wall time includes setup/hash/upload; solver seconds exclude setup. OS cache is not forcibly cold. Timeouts are censored, not completed timings.'},indent=2))
    variants=[('baseline_1',False,1,False),('hardened_1',True,1,False)]
    if a.threads>1:variants += [('baseline_multi',False,a.threads,False),('hardened_multi',True,a.threads,False)]
    if a.gpu:variants += [('hardened_gpu',True,a.threads,True)]
    rows=[];index=0;rng=random.Random(42)
    envbase={k:v for k,v in os.environ.items() if not k.startswith('SY_')}
    with (out/'runs.jsonl').open('w',encoding='utf-8') as journal:
        for name,graphrows,k in fixtures(a.suite=='full'):
            graph=graphs/(name+'.txt');graph.write_text('\n'.join(graphrows)+'\n')
            oracle=graphs/(name+'.bin');ref=module.Reference(graphrows,k)
            started=time.perf_counter();ref.oracle(oracle);oracle_seconds=time.perf_counter()-started
            n=len(graphrows);start=tuple(i*n//k for i in range(k));starttext=','.join(map(str,start))
            schedules=[(4,2,False),(5,3,True)] if a.suite=='smoke' else [(h,period,hidden) for h in (4,6) for period in (1,2,3,5) for hidden in (False,True)]
            for horizon,period,hidden in schedules:
                reveals=tuple(range(period,horizon+1,period));revealtext=','.join(map(str,reveals)) or 'none'
                roots=[tuple(range(n))] if hidden else [(r,) for r in range(n)]
                solve,action=ref.game(horizon,reveals);expected=[solve(start,b,1) for b in roots]
                for mode in modes:
                    for repeat in range(a.repeats):
                        order=list(variants);rng.shuffle(order)
                        for label,new,threads,gpu in order:
                            index+=1;prefix=out/f'{index:05d}_{name}_{label}'
                            resultpath=Path(str(prefix)+'.json');env=dict(envbase)
                            if new:
                                env.update(SY_START=starttext,SY_ROUNDS=str(horizon),SY_REVEALS=revealtext,SY_ORACLE=str(oracle.resolve()),SY_ORACLE_MODE=mode,SY_TRUST_LEGACY_ORACLE='1',SY_THREADS=str(threads),SY_TT_MB='16',SY_ARENA_MB='256',SY_WARM='0',SY_MAX_SECONDS=str(a.seconds),SY_RESULTS=str(resultpath.resolve()),SY_GPU='required' if gpu and mode!='off' else 'off')
                                if hidden:env['SY_HIDDEN_START']=','.join(map(str,range(n)))
                                command=[str(a.solver.resolve()),str(graph.resolve()),str(k),str(period)]
                            else:
                                command=[str(a.baseline.resolve()),str(graph.resolve()),'--cops',str(k),'--start',starttext,'--rounds',str(horizon),'--reveals',revealtext,'--threads',str(threads),'--oracle',str(oracle.resolve()),'--oracle-graph',str(graph.resolve()),'--oracle-mode',mode,'--max-seconds',str(a.seconds)]
                                if hidden:command+=['--hidden-start','all']
                            began=time.perf_counter();timeout=False
                            try:
                                proc=subprocess.run(command,env=env,capture_output=True,text=True,timeout=a.seconds+30)
                                stdout,stderr,code=proc.stdout,proc.stderr,proc.returncode
                            except subprocess.TimeoutExpired as e:
                                timeout=True;code=None
                                stdout=e.stdout or '';stderr=e.stderr or ''
                                if isinstance(stdout,bytes):stdout=stdout.decode(errors='replace')
                                if isinstance(stderr,bytes):stderr=stderr.decode(errors='replace')
                            wall=time.perf_counter()-began
                            Path(str(prefix)+'.stdout.txt').write_text(stdout,encoding='utf-8');Path(str(prefix)+'.stderr.txt').write_text(stderr,encoding='utf-8')
                            data=None;error=None
                            if not timeout:
                                try:
                                    data=json.loads(resultpath.read_text() if new else stdout)
                                    if code not in ((0,) if new else (0,2)):raise ValueError(f'process exit {code}')
                                    assert len(data['roots'])==len(roots)
                                    for root,b,win in zip(data['roots'],roots,expected):
                                        status=root['status'];assert root['robbers']==list(b)
                                        if status=='UNKNOWN':continue
                                        assert (status=='WIN')==win
                                        if status=='WIN' and set(b)-set(start):
                                            move=tuple(root['first_cop_move']);assert move in ref.moves[start] and action(start,b,1,move)
                                    if data['status']!='UNKNOWN':assert (data['status']=='WIN')==all(expected)
                                except Exception as e:error=f'{type(e).__name__}: {e}'
                            record={'case':name,'n':n,'k':k,'period':period,'horizon':horizon,'hidden':hidden,'mode':mode,'repeat':repeat,'variant':label,'threads':threads,'wall_seconds':wall,'oracle_build_seconds':oracle_seconds,'max_degree':max(row.count('1') for row in graphrows),'config_count':math.comb(n+k-1,k),'status':'TIMEOUT' if timeout else 'ERROR' if error else data['status'],'error':error,'search_seconds':None if data is None else data.get('seconds'),'nodes':None if data is None else data.get('nodes'),'gpu_rows':0 if data is None else data.get('gpu_rows',0),'returncode':code}
                            rows.append(record);journal.write(json.dumps(record)+'\n');journal.flush()
                            print(f'{index}: {name} k={k} p={period} H={horizon} {label} {record["status"]} wall={wall:.3f}s',flush=True)
                            if error:raise SystemExit(f'Correctness/process failure; inspect {prefix}: {error}')
    with (out/'runs.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
    print(f'Completed {len(rows)} paired runs: {out}')
    print('Use completed paired runs for speed comparisons; tiny process times are mostly startup. No Scotland Yard runtime upper bound is inferred.')
if __name__=='__main__':main()
