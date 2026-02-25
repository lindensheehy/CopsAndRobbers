import os
import subprocess
from pathlib import Path

# --- CONFIGURATION ---
COMPILER = "g++"
FLAGS = ["-Wall", "-std=c++17", "-Iinclude", "-Ofast"]

# Directories
SRC_DIR = Path("src")
OBJ_DIR = Path("obj")
OUT_DIR = Path("out")

# Whitelist of files containing a main() function
# (Just use the filenames, the script will find them)
WHITELIST = ["k_cops.cpp", "k_cops_2.cpp", "k_cops_3.cpp", "k_cops_4.cpp"]

def main():
    # 1. Setup directories
    OBJ_DIR.mkdir(exist_ok=True)
    OUT_DIR.mkdir(exist_ok=True)

    # 2. Gather all .cpp files from src/ and the root directory
    # Using a set to avoid duplicates if your structure changes
    all_cpp_files = list(Path(".").glob("*.cpp")) + list(SRC_DIR.glob("*.cpp"))
    all_cpp_files = list(set(all_cpp_files))

    if not all_cpp_files:
        print("Error: No .cpp files found.")
        return

    # 3. Separate files into libraries (needs .o) and mains (needs .exe)
    lib_files = [f for f in all_cpp_files if f.name not in WHITELIST]
    main_files = [f for f in all_cpp_files if f.name in WHITELIST]

    obj_files = []

    # 4. Compile library files to .o (with timestamp caching)
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
            return

    # 5. Compile and link the whitelisted main files
    print(f"\n--- Building Executables ({len(main_files)} targets) ---")
    obj_args = [str(o) for o in obj_files]
    
    for main_src in main_files:
        exe_name = OUT_DIR / f"{main_src.stem}.exe"
        
        # We always relink the executable just to be safe if backend logic changed
        cmd = [COMPILER] + FLAGS + [str(main_src)] + obj_args + ["-o", str(exe_name)]
        print(f"Linking:   {exe_name.name}")
        
        result = subprocess.run(cmd)
        if result.returncode != 0:
            print(f"\n[FATAL] Build failed on {main_src.name}")
            return

    print("\n[SUCCESS] All targets built successfully.")

if __name__ == "__main__":
    main()