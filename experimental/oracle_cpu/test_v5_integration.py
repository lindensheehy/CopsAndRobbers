"""Build existing V5 producer, generate tiny caches, compare reader to Python reference."""
import os
from pathlib import Path
import subprocess
import tempfile
import json
from test import Reference, ROOT, INF

with tempfile.TemporaryDirectory(prefix='sy_v5_integration_') as temp:
    tmp=Path(temp)
    writer=tmp/'v5_writer.exe'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O2','-pthread','-include','cstddef',
                    '-I'+str(ROOT/'lib/include'),str(ROOT/'app/main.cpp'),
                    *map(str,(ROOT/'lib/src').glob('*.cpp')),
                    '-DALGORITHM_INCLUDE="'+str(ROOT/'solvers/k_cops_fullvis_v5.cpp')+'"',
                    '-o',str(writer)],check=True)
    checks=0
    fixtures=[(['010','101','010'],1),(['0101','1010','0101','1010'],2),(['110','111','011'],1)]
    for i,(rows,k) in enumerate(fixtures):
        graph=tmp/f'fixture{i}.txt';graph.write_text('\n'.join(rows)+'\n')
        env=dict(os.environ,COPS_SAVE_FULLVIS_V5='1')
        result=subprocess.run([str(writer),str(graph),str(k),'1'],cwd=tmp,env=env,capture_output=True,text=True,timeout=60)
        assert result.returncode==0 and 'Saved and validated' in result.stdout,(result.stdout,result.stderr)
        oracle=tmp/'cache'/f'fullvis_v5_strategy__fixture{i}__{k}-cops.bin'
        assert oracle.exists()
        ref=Reference(rows,k);ref.fullvis()
        for c in ref.configs:
            for r in range(ref.n):
                q=subprocess.run([str(ROOT/'build/bin/scotlandyard_oracle_cpu.exe'),str(graph),'--cops',str(k),
                                  '--start',','.join(map(str,c)),'--oracle',str(oracle),'--oracle-graph',str(graph),
                                  '--query',str(r)],capture_output=True,text=True,check=True)
                value=json.loads(q.stdout)
                assert value['exact'] and value['ply_lower_bound']==(ref.C[c,r] if ref.C[c,r]<INF else None),(c,r,value)
                checks+=1
    print(f'PASS: {checks} cop-turn queries from three actual V5-generated binaries match independent reference.')
