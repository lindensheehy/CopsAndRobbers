"""Build all sources with one toolchain; never link MSYS2 objects with MSVC."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cpu-only', action='store_true')
p.add_argument('--arch', default='sm_86')
a = p.parse_args()
os.chdir(root)
out = root / 'build' / 'bin'
out.mkdir(parents=True, exist_ok=True)
sources = ['experimental/v3_2/main.cu'] + [str(s) for s in sorted(Path('lib/src').glob('*.cpp'))]
if a.cpu_only:
    cmd = ['g++', '-O3', '-std=c++17', '-pthread', '-include', 'cstddef', '-Ilib/include', '-x', 'c++', *sources,
           '-o', str(out / 'k_cops_visibility_v3_2_cpu.exe')]
else:
    nvcc_path = r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\nvcc.exe"
    cmd = [nvcc_path, '-O3', '-std=c++17', '-arch=' + a.arch, '-Ilib/include', '-include', 'cstddef']
    cmd += ['-Xcompiler', '/O2,/EHsc,/DNOMINMAX'] if os.name == 'nt' else ['-Xcompiler', '-pthread']
    cmd += sources + ['-o', str(out / 'k_cops_visibility_v3_2.exe')]
print(subprocess.list2cmdline(cmd), flush=True)
subprocess.run(cmd, check=True)
