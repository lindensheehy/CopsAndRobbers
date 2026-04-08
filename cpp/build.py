import os
import sys
import subprocess
from pathlib import Path

# --- CONFIGURATION ---
COMPILER = "g++"
FLAGS = ["-Wall", "-std=c++17", "-Ilib/include", "-Ofast"]

# Directories
LIB_SRC_DIR = Path("lib/src")
SOLVERS_DIR = Path("solvers")
BUILD_DIR = Path("build")
OBJ_DIR = BUILD_DIR / "obj"
BIN_DIR = BUILD_DIR / "bin"

def main():
    # 0. Ensure the script runs relative to its own location
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    # 1. Create the required build directories if they do not exist
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    BIN_DIR.mkdir(parents=True, exist_ok=True)

    # 2. Gather .cpp files
    lib_files = list(LIB_SRC_DIR.glob("*.cpp"))
    solver_files = list(SOLVERS_DIR.glob("*.cpp"))

    if not lib_files and not solver_files:
        print("Error: No .cpp files found in lib/src/ or solvers/.")
        sys.exit(1)

    obj_files = []
    libs_compiled = 0

    # 3. Compile backend library files to .o (Still hard-fails on error)
    if lib_files:
        print(f"--- Compiling Backend Logic ({len(lib_files)} files) ---")
        for src in lib_files:
            obj_path = OBJ_DIR / f"{src.stem}.o"
            obj_files.append(obj_path)

            if obj_path.exists() and obj_path.stat().st_mtime >= src.stat().st_mtime:
                continue

            cmd = [COMPILER] + FLAGS + ["-c", str(src), "-o", str(obj_path)]
            print(f"Compiling: {src.name}")
            
            result = subprocess.run(cmd)
            if result.returncode != 0:
                print(f"\n[FATAL] Core library build failed on {src.name}. Aborting.")
                sys.exit(1)
            libs_compiled += 1
            
        if libs_compiled == 0:
            print("All backend objects are up to date.")

    # 4. Compile and link the solvers (Continues on error)
    failed_solvers = []
    
    if solver_files:
        print(f"\n--- Building Executables ({len(solver_files)} targets) ---")
        obj_args = [str(o) for o in obj_files]
        exes_built = 0
        
        for solver_src in solver_files:
            exe_path = BIN_DIR / f"{solver_src.stem}.exe"
            
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
            
            # If it fails, record it and move on instead of crashing
            if result.returncode != 0:
                print(f"[ERROR] Failed to compile {solver_src.name}")
                failed_solvers.append(solver_src.name)
            else:
                exes_built += 1
            
        if exes_built == 0 and not failed_solvers:
            print("All executables are up to date.")

    # 5. Final Build Summary
    if failed_solvers:
        print(f"\n[WARNING] Build finished with errors. {len(failed_solvers)} solver(s) failed:")
        for failed_file in failed_solvers:
            print(f"  - {failed_file}")
        # Exit with a non-zero code so CI/CD pipelines know the build wasn't fully successful
        sys.exit(1) 
    else:
        print("\n[SUCCESS] Build process completed successfully.")

if __name__ == "__main__":
    main()