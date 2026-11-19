"""Build instrumented v3 and V5 without editing their source or replacing old executables."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--cxx',default=os.environ.get('CXX','g++'));a=p.parse_args()
out=ROOT/'build/bin';out.mkdir(parents=True,exist_ok=True)
for name in ('v3','v5'):
    command=[a.cxx,'-std=c++17','-O3','-pthread','-Wall','-Wextra','-include','cstddef','-I'+str(ROOT/'lib/include'),str(HERE/f'{name}_driver.cpp'),*map(str,sorted((ROOT/'lib/src').glob('*.cpp'))),'-o',str(out/f'visibility_bench_{name}.exe')]
    print(subprocess.list2cmdline(command),flush=True);subprocess.run(command,check=True)
paths=[ROOT/'solvers/k_cops_visibility_v3.cpp',ROOT/'solvers/k_cops_fullvis_v5.cpp',*sorted((ROOT/'lib').rglob('*.h')),*sorted((ROOT/'lib').rglob('*.cpp'))]
(HERE/'build_sources.json').write_text(json.dumps({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2))
print('Original v3 and V5 built. Hardened executable must also be built with experimental/scotlandyard_hardened/build.py.')
