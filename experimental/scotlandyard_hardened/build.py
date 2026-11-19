"""Build only the hardened solver; optional CUDA DLL is independent of the GNU host ABI."""
import argparse, os, shutil, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--cuda', action='store_true')
p.add_argument('--arch', default='86', help='CUDA SM; 86 is RTX 3060 Ti')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--output', type=Path)
a = p.parse_args()
out = ROOT / 'build/bin'; out.mkdir(parents=True, exist_ok=True)
cmd = [a.cxx, '-std=c++17', '-pthread', '-O3', '-Wall', '-Wextra', '-Wpedantic',
       '-include', 'cstddef', '-I'+str(ROOT/'lib/include'),
       str(Path(__file__).with_name('main.cpp')),
       *map(str, sorted((ROOT/'lib/src').glob('*.cpp'))), '-o', str(a.output or out/'scotlandyard_hardened.exe')]
if os.name != 'nt': cmd += ['-ldl']
if a.sanitize:
    cmd += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if os.name != 'nt': cmd += ['-fno-pie', '-no-pie']
print(subprocess.list2cmdline(cmd), flush=True); subprocess.run(cmd, check=True)
if a.cuda:
    nvcc = shutil.which('nvcc')
    if not nvcc and os.environ.get('CUDA_PATH'):
        candidate = Path(os.environ['CUDA_PATH'])/'bin/nvcc.exe'
        if candidate.exists(): nvcc = str(candidate)
    if not nvcc: raise SystemExit('CPU build succeeded. CUDA toolkit nvcc was not found.')
    if os.name == 'nt' and not shutil.which('cl'):
        raise SystemExit('CPU build succeeded. Run --cuda in an x64 Native Tools Command Prompt for Visual Studio, with g++ on PATH.')
    lib = out / ('sy_gpu.dll' if os.name == 'nt' else 'libsy_gpu.so')
    cmd = [nvcc, '-std=c++17', '-O3', '--shared', '--cudart=static', '-arch=sm_'+a.arch,
           str(Path(__file__).with_name('gpu.cu')), '-o', str(lib)]
    if os.name != 'nt': cmd += ['-Xcompiler', '-fPIC']
    print(subprocess.list2cmdline(cmd), flush=True); subprocess.run(cmd, check=True)
print(a.output or out/'scotlandyard_hardened.exe')
