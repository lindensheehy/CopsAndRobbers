import os
import sys
import subprocess
from pathlib import Path

# --- CONFIGURATION ---
COMPILER = "g++"
FLAGS = ["-Wall", "-std=c++17", "-Ilib/include", "-Ofast"]

# Directories (These will now be evaluated relative to this script's location)
LIB_SRC_DIR = Path("lib/src")
SOLVERS_DIR = Path("solvers")
BUILD_DIR = Path("build")
OBJ_DIR = BUILD_DIR / "obj"
BIN_DIR = BUILD_DIR / "bin"

def main():
    # 0. Ensure the script runs relative to its own location, not the terminal's CWD
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    # 1. Hard fail if the required build directories do not exist
    if not OBJ_DIR.is_dir() or not BIN_DIR.is_dir():
        print("[FATAL] Required build directories are missing.", file=sys.stderr)
        print(f"Ensure both '{OBJ_DIR}' and '{BIN_DIR}' exist in the cpp folder before building.", file=sys.stderr)
        sys.exit(1)

    # 2. Gather .cpp files purely by directory scanning
    lib_files = list(LIB_SRC_DIR.glob("*.cpp"))
    solver_files = list(SOLVERS_DIR.glob("*.cpp"))

    if not lib_files and not solver_files:
        print("Error: No .cpp files found in lib/src/ or solvers/.")
        sys.exit(1)

    obj_files = []
    libs_compiled = 0

    # 3. Compile backend library files to .o
    if lib_files:
        print(f"--- Compiling Backend Logic ({len(lib_files)} files) ---")
        for src in lib_files:
            obj_path = OBJ_DIR / f"{src.stem}.o"
            obj_files.append(obj_path)

            # Skip compilation if the object file is already up to date
            if obj_path.exists() and obj_path.stat().st_mtime >= src.stat().st_mtime:
                continue

            cmd = [COMPILER] + FLAGS + ["-c", str(src), "-o", str(obj_path)]
            print(f"Compiling: {src.name}")
            
            result = subprocess.run(cmd)
            if result.returncode != 0:
                print(f"\n[FATAL] Build failed on {src.name}")
                sys.exit(1)
            libs_compiled += 1
            
        if libs_compiled == 0:
            print("All backend objects are up to date.")

    # 4. Compile and link the solvers (with advanced timestamp caching)
    if solver_files:
        print(f"\n--- Building Executables ({len(solver_files)} targets) ---")
        obj_args = [str(o) for o in obj_files]
        exes_built = 0
        
        for solver_src in solver_files:
            exe_path = BIN_DIR / f"{solver_src.stem}.exe"
            
            # Rebuild if: Exe doesn't exist, OR solver source is newer, OR any object file is newer.
            needs_rebuild = True
            if exe_path.exists():
                exe_mtime = exe_path.stat().st_mtime
                src_newer = solver_src.stat().st_mtime > exe_mtime
                objs_newer = any(o.stat().st_mtime > exe_mtime for o in obj_files)
                
                if not src_newer and not objs_newer:
                    needs_rebuild = False
            
            if not needs_rebuild:
                continue

            cmd = [COMPILER] + FLAGS + [str(solver_src)] + obj_args + ["-o", str(exe_path)]
            print(f"Linking:   {exe_path.name}")
            
            result = subprocess.run(cmd)
            if result.returncode != 0:
                print(f"\n[FATAL] Build failed on {solver_src.name}")
                sys.exit(1)
            exes_built += 1
            
        if exes_built == 0:
            print("All executables are up to date.")

    print("\n[SUCCESS] Build process completed.")

if __name__ == "__main__":
    main()