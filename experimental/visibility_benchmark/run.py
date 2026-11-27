"""Original v3 whole-table versus hardened fixed-deadline queries. See README.md."""
import argparse,csv,hashlib,itertools,json,math,os,random,struct,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sizes',default='4,6');p.add_argument('--periods',default='1,2,3');p.add_argument('--horizons',default='2,4');p.add_argument('--cops',default='1,2')
p.add_argument('--families',default='path,cycle,grid,random');p.add_argument('--threads',type=int,default=min(16,os.cpu_count() or 1));p.add_argument('--seconds',type=int,default=20);p.add_argument('--oracle-seconds',type=int,default=180)
p.add_argument('--repeats',type=int,default=1);p.add_argument('--max-states',type=int,default=500000);p.add_argument('--max-transition-mib',type=int,default=256);p.add_argument('--out',type=Path);p.add_argument('--gpu',action='store_true');p.add_argument('--resume',action='store_true')
p.add_argument('--graph-file',type=Path);p.add_argument('--total-seconds',type=float,default=0)
a=p.parse_args();deadline=time.monotonic()+a.total_seconds if a.total_seconds else float('inf');nums=lambda s:[int(v) for v in s.split(',')]
sizes,periods,horizons,cops=map(nums,(a.sizes,a.periods,a.horizons,a.cops))
assert all(3<=n<=200 for n in sizes) and all(1<=k<=4 for k in cops) and all(1<=x<=250 for x in periods+horizons)
assert a.threads>0 and a.seconds>0 and a.repeats>0
out=(a.out or ROOT/'console_outputs'/time.strftime('v3_race_%Y%m%d_%H%M%S')).resolve();out.mkdir(parents=True,exist_ok=a.resume)
exes={name:ROOT/'build/bin'/file for name,file in [('v3','visibility_bench_v3.exe'),('v5','visibility_bench_v5.exe'),('new','scotlandyard_hardened.exe')]}
for exe in exes.values():
    if not exe.exists():raise SystemExit('Build first; missing '+str(exe))
sha=lambda b:hashlib.sha256(b).hexdigest()
settings={**vars(a),'out':str(out),'graph_file':str(a.graph_file.resolve()) if a.graph_file else None,'binaries':{k:sha(v.read_bytes()) for k,v in exes.items()}}
settings.pop('resume');meta=out/'settings.json'
if a.resume and json.loads(meta.read_text())!=settings:raise SystemExit('Resume settings/binaries differ; choose a new output directory')
meta.write_text(json.dumps(settings,indent=2))
base={k:v for k,v in os.environ.items() if not k.startswith(('SY_','COPS_'))}
records=[];journal=out/'runs.jsonl'
if a.resume and journal.exists():
    for line in journal.read_text().splitlines():
        try:records.append(json.loads(line))
        except json.JSONDecodeError:raise SystemExit('Incomplete journal line; retain a copy and remove the final incomplete line before resume')
done={r['key'] for r in records}
def record(row):
    records.append(row);done.add(row['key'])
    with journal.open('a') as f:f.write(json.dumps(row)+'\n');f.flush()
    print(row['key'],row['status'],flush=True)
def execute(command,folder,label,env,limit):
    remaining=deadline-time.monotonic()
    if remaining<=0:raise KeyboardInterrupt
    limit=min(limit,remaining)
    started=time.perf_counter()
    with (folder/(label+'.log')).open('w') as log:
        try:
            r=subprocess.run(command,cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=limit)
            status='OK' if r.returncode==0 else 'ERROR'
        except subprocess.TimeoutExpired:status='TIMEOUT'
    return status,time.perf_counter()-started
try:
 for n in ([len(a.graph_file.read_text().split('-',1)[0].strip().splitlines())] if a.graph_file else sizes):
  for family in (['repository'] if a.graph_file else a.families.split(',')):
   rng=random.Random(710+n);edges=set()
   def edge(u,v):edges.add(tuple(sorted((u,v))))
   if family=='repository':pass
   elif family=='path':
    for i in range(n-1):edge(i,i+1)
   elif family in ('cycle','random'):
    for i in range(n):edge(i,(i+1)%n)
    if family=='random':
     for i in range(n):
      for j in range(i+2,n):
       if rng.random()<min(.25,2/n):edge(i,j)
   elif family=='grid':
    width=max(2,math.isqrt(n))
    for i in range(n):
     if i+1<n and i//width==(i+1)//width:edge(i,i+1)
     if i+width<n:edge(i,i+width)
   else:raise ValueError('Unknown family '+family)
   rows=[''.join('1' if tuple(sorted((i,j))) in edges else '0' for j in range(n)) for i in range(n)];matrix=('\n'.join(rows)+'\n').encode();degree=max(r.count('1') for r in rows)
   if a.graph_file:
    rows=a.graph_file.read_text().split('-',1)[0].strip().splitlines();matrix=('\n'.join(rows)+'\n').encode();degree=max(r.count('1') for r in rows)
    if not rows or any(len(r)!=n or set(r)-set('01') for r in rows):raise ValueError('Expected square binary adjacency matrix')
   for k in cops:
    if family=='path' and k!=1 or family=='cycle' and k>2 or n<2*k:continue
    count=math.comb(n+k-1,k);states=count*n;transition_mib=count*degree**k*16/2**20
    case=f'{family}{n}_{k}c';folder=out/case;folder.mkdir(exist_ok=True);graph=folder/'graph.txt';graph.write_bytes(matrix)
    if states>a.max_states or transition_mib>a.max_transition_mib:
     key=case+'/resource_gate'
     if key not in done:record(dict(key=key,status='SKIPPED_ESTIMATE',states=states,transition_mib=transition_mib))
     continue
    oracle=folder/'cache'/f'fullvis_v5_strategy__graph__{k}-cops.bin';receipt=folder/'oracle_receipt.json';oraclekey=case+'/oracle'
    if oraclekey not in done:
     status,wall=execute([str(exes['v5']),str(graph),str(k),str(receipt)],folder,'oracle',{**base,'COPS_SAVE_FULLVIS_V5':'1'},a.oracle_seconds)
     if status=='OK':
      assert json.loads(receipt.read_text())['completed_fixed_point']
      raw=oracle.read_bytes();header=struct.unpack('<4I3Q6I',raw[:64]);assert len(raw)==64+states and header[2:6]==(n,k,count,states)
      Path(str(oracle)+'.manifest').write_text('SY_ORACLE_MANIFEST_V1\n'+sha(matrix)+'\n'+sha(raw)+'\noperator_attests_completed_fixed_point_same_rules\n')
     record(dict(key=oraclekey,status=status,wall_seconds=wall,states=states,phase='oracle_build'))
    if next(r for r in records if r['key']==oraclekey)['status']!='OK':continue
    configs=list(itertools.combinations_with_replacement(range(n),k));header=struct.unpack('<4I3Q6I',oracle.read_bytes()[:64]);start=configs[header[6]] if header[6]<count else tuple(i*n//k for i in range(k))
    for period in periods:
     for repeat in range(a.repeats):
      prefix=f'{case}/p{period}/r{repeat}';key=prefix+'/v3';dump=folder/f'v3_p{period}_r{repeat}.csv';summary=folder/f'v3_p{period}_r{repeat}.json'
      if key not in done:
       status,wall=execute([str(exes['v3']),str(graph),str(k),str(period),str(dump),str(summary)],folder,f'v3_p{period}_r{repeat}',base,a.seconds)
       data=json.loads(summary.read_text()) if status=='OK' else {}
       record(dict(key=key,status=status,phase='v3_whole_table',wall_seconds=wall,**data))
      v3ok=next(r for r in records if r['key']==key)['status']=='OK';data=json.loads(summary.read_text()) if v3ok else {}
      starts=list(dict.fromkeys([start,tuple(data.get('best_start') or tuple(i*n//k for i in range(k)))]))
      costs={}
      if v3ok:
       with dump.open() as f:
        for row in csv.DictReader(f):
         c=tuple(map(int,row['cops'].split(',')))
         if c in starts:costs[c,int(row['robber'])]=int(row['reported_ply']) if row['marked']=='1' else None
      for si,c in enumerate(starts):
       for horizon in horizons:
        query=f'{prefix}/s{si}/H{horizon}';ref=None
        if n<=8 and states<=4000:
         inp=folder/'reference_input.json';refout=folder/'reference_output.json';refout.unlink(missing_ok=True)
         inp.write_text(json.dumps(dict(rows=rows,k=k,start=c,horizon=horizon,period=period)))
         status,_=execute([sys.executable,str(HERE/'reference_worker.py'),str(inp),str(refout)],folder,'reference',base,a.seconds)
         if status=='OK':ref=json.loads(refout.read_text())
        variants=[('cpu1',1,'off'),('cpu_all',a.threads,'off')]+([('gpu_auto',a.threads,'auto')] if a.gpu else [])
        for variant,threads,gpu in variants:
         key=query+'/'+variant
         if key in done:continue
         label=f'p{period}_r{repeat}_s{si}_H{horizon}_{variant}';result=folder/(label+'.json');result.unlink(missing_ok=True)
         env={**base,'SY_START':','.join(map(str,c)),'SY_ROUNDS':str(horizon),'SY_ORACLE':str(oracle),'SY_THREADS':str(threads),'SY_TT_MB':'64','SY_MAX_SECONDS':str(a.seconds),'SY_RESULTS':str(result),'SY_GPU':gpu,'SY_GPU_LIBRARY':str(ROOT/'build/bin'/('sy_gpu.dll' if os.name=='nt' else 'libsy_gpu.so'))}
         status,wall=execute([str(exes['new']),str(graph),str(k),str(period)],folder,label,env,a.seconds+10)
         d=json.loads(result.read_text()) if status=='OK' and result.exists() else {}
         assessment={'v3_bound_contradictions':0,'improved_v3_bound':0,'v3_unmarked_new_win':0,'reference_mismatches':0}
         for r,root in enumerate(d.get('roots',[])):
          answer=root['status'];cost=costs.get((c,r))
          if answer=='UNKNOWN':continue
          if cost is not None and cost<=2*horizon and answer=='LOSS':assessment['v3_bound_contradictions']+=1
          if cost is not None and cost>2*horizon and answer=='WIN':assessment['improved_v3_bound']+=1
          if v3ok and cost is None and answer=='WIN':assessment['v3_unmarked_new_win']+=1
          if ref:
           bad=(answer=='WIN')!=ref['wins'][r]
           if answer=='WIN' and r not in c:bad|=root['first_cop_move'] not in ref['winning_actions'].get(str(r),[])
           assessment['reference_mismatches']+=int(bad)
         record(dict(key=key,status=d.get('status',status),phase='hardened_fixed_start',wall_seconds=wall,search_seconds=d.get('seconds'),start=c,n=n,k=k,period=period,horizon=horizon,threads=threads,reference_checked=ref is not None,v3_completed=v3ok,gpu_rows=d.get('gpu_rows',0),nodes=d.get('nodes'),pruned=d.get('pruned'),**assessment))
except KeyboardInterrupt:print('Interrupted. Completed records retained; use --resume with identical settings.')
finally:
 fields=sorted({k for r in records for k in r})
 with (out/'runs.csv').open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(records)
 report=['# Visibility benchmark report',f'Records: {len(records)}','',
 'Original v3 computes a whole table; hardened computes selected fixed starts and deadlines. Their raw times are different workloads, NOT a fair speedup ratio.',
 'Oracle generation is reported separately and reused across visibility periods, horizons and repeats. Hardened process wall time includes oracle integrity checks; search time excludes setup.',
 'v3 costs are recorded upper bounds, not guaranteed minima. Unmarked states are unproven here, not certified infinite survival.',
 'Improved v3 bound is not automatically a correctness failure. Bound contradictions and reference mismatches require investigation.',
 'Timeout/UNKNOWN results are censored; do not treat them as completed runtimes. Tiny timings mostly measure setup.',
 'Transition-memory gates are estimates, not hard process RAM limits. No Scotland Yard runtime extrapolation is claimed.',
 'GPU auto may fall back or perform zero batches; inspect gpu_rows.',
 '',f"Independent-reference mismatches: {sum(r.get('reference_mismatches',0) for r in records)}",f"v3 bound contradictions: {sum(r.get('v3_bound_contradictions',0) for r in records)}",f"Improved recorded v3 bounds: {sum(r.get('improved_v3_bound',0) for r in records)}"]
 (out/'REPORT.md').write_text('\n\n'.join(report));print('Results:',out)
