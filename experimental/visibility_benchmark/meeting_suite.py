"""Time-budgeted repository-wide benchmark. Run from the installed repository."""
import argparse,json,math,os,subprocess,sys,time,zipfile
from pathlib import Path
from meeting_report import generate
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--hours',type=float,default=8);p.add_argument('--threads',type=int,default=min(16,os.cpu_count() or 1));p.add_argument('--out',type=Path,default=ROOT/'console_outputs'/'meeting_suite');p.add_argument('--resume',action='store_true');p.add_argument('--plan-only',action='store_true');p.add_argument('--max-states',type=int,default=2000000);p.add_argument('--max-transition-mib',type=int,default=256)
a=p.parse_args()
if a.hours<=0 or a.threads<1:p.error('hours and threads must be positive')
out=a.out.resolve()
if out.exists() and not a.resume:p.error('Output exists. Use --resume or choose a new --out.')
out.mkdir(parents=True,exist_ok=True)
inv=[];eligible=[]
for path in sorted((ROOT/'assets/matrices').glob('*.txt')):
    rows=path.read_text().split('-',1)[0].strip().splitlines();n=len(rows)
    valid=bool(n) and all(len(r)==n and not(set(r)-set('01')) for r in rows)
    if not valid:
        inv.append(dict(graph=path.name,n=n,action='EXCLUDED',reason='Not a square binary matrix'));continue
    degree=max(r.count('1') for r in rows)
    if any(rows[i][j]!=rows[j][i] for i in range(n) for j in range(n)):
        inv.append(dict(graph=path.name,n=n,degree=degree,action='EXCLUDED',reason='Asymmetric matrix; undirected solver assumptions'));continue
    if path.stem.startswith('scotlandyard'):
        inv.append(dict(graph=path.name,n=n,degree=degree,action='EXCLUDED',reason='Requested exclusion: Scotland Yard scale'));continue
    ks=[1] if path.stem.startswith(('line','tree')) else ([1,2] if path.stem.startswith('cycle') else [1,2,3,4])
    for k in ks:
        if n<2*k:continue
        states=n*math.comb(n+k-1,k);mem=math.comb(n+k-1,k)*degree**k*16/2**20
        gated=states>a.max_states or mem>a.max_transition_mib or n>200
        item=dict(graph=path.name,n=n,degree=degree,k=k,states=states,transition_mib=mem,action='RESOURCE_GATE' if gated else 'PENDING',reason=f'{states:,} states; conservative transition estimate {mem:.1f} MiB')
        inv.append(item)
        if not gated:eligible.append((item,path))
# Cover the graph collection with low cop counts first, then add harder combinations.
eligible.sort(key=lambda pair:(pair[0]['k'],pair[0]['states'],pair[0]['graph']))
(out/'inventory.json').write_text(json.dumps(inv,indent=2));generate(out)
print(f'{len(eligible)} eligible graph/cop combinations. Plan and report: {out}',flush=True)
if a.plan_only:sys.exit(0)
for filename in ('visibility_bench_v3.exe','visibility_bench_v5.exe','scotlandyard_hardened.exe'):
    if not (ROOT/'build/bin'/filename).exists():raise SystemExit('Missing build/bin/'+filename+'; build first.')
started=time.monotonic();deadline=started+a.hours*3600;child=None
try:
    for stage,seconds,oracle_seconds,case_seconds,horizons,repeats in [('coverage',3,45,120,'4,8',1),('depth',20,180,480,'4,8,12,20',2)]:
        for item,graph in eligible:
            if time.monotonic()+case_seconds>deadline:
                print('Time budget reserved: stopping before another case. Report retained.',flush=True);raise KeyboardInterrupt
            folder=out/stage/(graph.stem+'_'+str(item['k'])+'c')
            marker=folder/'suite_finished.json'
            if marker.exists():
                item['action']='ATTEMPTED';item['reason']='Previous stage attempt retained; inspect ledger for results'
                (out/'inventory.json').write_text(json.dumps(inv,indent=2))
                continue
            command=[sys.executable,str(HERE/'run.py'),'--graph-file',str(graph),'--cops',str(item['k']),'--periods','2,3,5,1','--horizons',horizons,'--threads',str(a.threads),'--seconds',str(seconds),'--oracle-seconds',str(oracle_seconds),'--total-seconds',str(case_seconds),'--repeats',str(repeats),'--max-states',str(a.max_states),'--max-transition-mib',str(a.max_transition_mib),'--out',str(folder)]
            if (folder/'settings.json').exists():command.append('--resume')
            folder.parent.mkdir(parents=True,exist_ok=True)
            print(f'\n[{stage}] {graph.name}, {item["k"]} cops; at most {case_seconds}s',flush=True)
            child=subprocess.Popen(command,cwd=ROOT)
            while child.poll() is None:
                try:child.wait(timeout=30)
                except subprocess.TimeoutExpired:generate(out)
            rc=child.returncode;child=None
            folder.mkdir(parents=True,exist_ok=True)
            marker.write_text(json.dumps(dict(exit_code=rc,stage=stage,budget_seconds=case_seconds)))
            item['action']='ATTEMPTED';item['reason']=f'{stage} attempted; inspect ledger for completed, unknown, timeout or error outcomes'
            (out/'inventory.json').write_text(json.dumps(inv,indent=2));generate(out)
except KeyboardInterrupt:
    print('Stopping; retaining recorded results.',flush=True)
    if child is not None and child.poll() is None:
        # Windows console Ctrl+C is normally delivered to the child too. Give it time to flush.
        try:child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            if os.name=='nt':subprocess.run([str(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32/taskkill.exe'),'/PID',str(child.pid),'/T','/F'],check=False)
            else:
                # Interactive SIGINT reaches the foreground process group, including solver.
                child.terminate();child.wait()
finally:
    count=generate(out)
    # Small shareable evidence bundle; excludes binary oracle payloads and large state dumps.
    bundle=out/'MEETING_RESULTS.zip'
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as z:
        for path in out.rglob('*'):
            if path.is_file() and (path.name in ('MEETING_REPORT.html','MEETING_REPORT.md','ALL_RUNS.csv','inventory.json','runs.jsonl','runs.csv','settings.json','suite_finished.json','REPORT.md') or path.suffix=='.log'):
                z.write(path,path.relative_to(out))
    print(f'\nSaved {count} records. Open: {out / "MEETING_REPORT.html"}\nUpload: {bundle}',flush=True)
