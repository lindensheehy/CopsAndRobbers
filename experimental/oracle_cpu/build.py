"""Standalone CPU build. Does not rebuild or change existing solvers."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--debug', action='store_true')
a = p.parse_args()
out = root / 'build' / 'bin' / 'scotlandyard_oracle_cpu.exe'
out.parent.mkdir(parents=True, exist_ok=True)
cmd = [a.cxx, '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Wpedantic',
       '-O0' if a.debug else '-O3', str(root/'experimental/oracle_cpu/main.cpp'), '-o', str(out)]
print(subprocess.list2cmdline(cmd), flush=True)
subprocess.run(cmd, check=True)
print(out)
