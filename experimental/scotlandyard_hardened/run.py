"""Logged bounded overnight run; resumable completed roots and subproblem snapshots."""
import argparse, json, os, platform, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--period',type=int,default=2);p.add_argument('--rounds',type=int,default=24)
p.add_argument('--hours',type=float,default=8);p.add_argument('--threads',type=int,default=os.cpu_count() or 1)
p.add_argument('--gpu',choices=['off','auto','verify','required'],default='off')
p.add_argument('--resume',action='store_true');p.add_argument('--run-dir',type=Path)
p.add_argument('--tt-mb',type=int,default=1024);p.add_argument('--arena-mb',type=int,default=2048)
p.add_argument('--rss-mb',type=int,default=24576);p.add_argument('--checkpoint-seconds',type=int,default=600)
p.add_argument('--graph',type=Path,default=ROOT/'assets/matrices/scotlandyard-all.txt')
p.add_argument('--oracle',type=Path,default=ROOT/'cache/fullvis_v5_strategy__scotlandyard-all__4-cops.bin')
p.add_argument('--cops',type=int,default=4);p.add_argument('--start',help='zero-based comma-separated cops; default oracle best start')
a=p.parse_args()
if a.hours<=0 or not 1<=a.period<=250 or not 1<=a.rounds<=250 or a.threads<1:p.error('positive hours/threads and period/rounds in 1..250 required')
exe=ROOT/'build/bin/scotlandyard_hardened.exe'
if not exe.exists():p.error('Build first: python experimental/scotlandyard_hardened/build.py')
if not Path(str(a.oracle)+'.manifest').exists():p.error('Create the oracle manifest first; see README.md. The oracle itself is never modified.')
folder=(a.run_dir or ROOT/f'cache/hardened_{a.graph.stem}_{a.cops}c_p{a.period}_H{a.rounds}').resolve()
folder.mkdir(parents=True,exist_ok=True);checkpoint=folder/'roots.chk'
if checkpoint.exists() and not a.resume:p.error('Run checkpoint already exists; use --resume or choose --run-dir')
if a.resume and not checkpoint.exists():p.error('No checkpoint in this run directory')
stamp=time.strftime('%Y%m%d_%H%M%S');log=folder/f'console_{stamp}.txt'
env={k:v for k,v in os.environ.items() if not k.startswith('SY_')}
env.update(SY_ROUNDS=str(a.rounds),SY_THREADS=str(a.threads),SY_ORACLE=str(a.oracle.resolve()),SY_ORACLE_MODE='bound',SY_TT_MB=str(a.tt_mb),SY_ARENA_MB=str(a.arena_mb),SY_MAX_RSS_MB=str(a.rss_mb),SY_MAX_SECONDS=str(max(1,int(a.hours*3600))),SY_CHECKPOINT=str(checkpoint),SY_CHECKPOINT_SECONDS=str(a.checkpoint_seconds),SY_RESUME='1' if a.resume else '0',SY_RESULTS=str(folder/f'result_{stamp}.json'),SY_GPU=a.gpu,SY_GPU_LIBRARY=str(ROOT/'build/bin'/('sy_gpu.dll' if os.name=='nt' else 'libsy_gpu.so')))
if a.start:env['SY_START']=a.start
command=[str(exe),str(a.graph.resolve()),str(a.cops),str(a.period)]
(folder/f'launch_{stamp}.json').write_text(json.dumps({'command':command,'settings':{k:v for k,v in env.items() if k.startswith('SY_')},'platform':platform.platform(),'logical_cpus':os.cpu_count(),'note':'Search budget excludes initial oracle hash/GPU upload. Resume reuses proved subproblems; active stacks restart.'},indent=2))
print(f'Log: {log}\nCheckpoint: {checkpoint}\nGPU: {a.gpu}\nSearch budget: {a.hours} hours after setup',flush=True)
with log.open('w',encoding='utf-8') as f:
    process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',bufsize=1)
    try:
        for line in process.stdout:sys.stdout.write(line);sys.stdout.flush();f.write(line);f.flush()
        code=process.wait()
    except KeyboardInterrupt:
        print('\nWaiting for solver to checkpoint after Ctrl+C...',flush=True)
        # It shares this console/process group, so it received the same Ctrl+C.
        tail,_=process.communicate();f.write(tail or '');f.flush();print(tail or '',end='');code=process.returncode
print(f'Exit code: {code}; diagnostics: {folder}')
raise SystemExit(code)
