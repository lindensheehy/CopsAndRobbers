import numpy as np
import os

def build_npz():
    # Adjusted paths to include the assets/ folder
    bin_path = "assets/dp_tables/alternating_raw.bin"
    out_path = "assets/dp_tables/alternating_dp.npz"
    
    if not os.path.exists(bin_path):
        print(f"Error: Binary dump not found at {bin_path}")
        return

    print("Reading C++ binary dump...")
    with open(bin_path, "rb") as f:
        # Read header and explicitly cast to native Python int
        N = int(np.frombuffer(f.read(4), dtype=np.int32)[0])
        k = int(np.frombuffer(f.read(4), dtype=np.int32)[0])
        config_count = int(np.frombuffer(f.read(8), dtype=np.uint64)[0])

        print(f"Graph Data: {N} Nodes, {k} Cops, {config_count} Configurations")

        # Read Cop Configurations
        configs = np.frombuffer(f.read(config_count * k), dtype=np.uint8).reshape((config_count, k))

        # Read DP Tables
        num_states = config_count * N
        col1 = np.frombuffer(f.read(num_states * 4), dtype=np.int32).reshape((config_count, N))
        col2 = np.frombuffer(f.read(num_states * 4), dtype=np.int32).reshape((config_count, N))
        col3 = np.frombuffer(f.read(num_states * 4), dtype=np.int32).reshape((config_count, N))
        col4 = np.frombuffer(f.read(num_states * 4), dtype=np.int32).reshape((config_count, N))

    print("Compressing into NPZ archive...")
    np.savez_compressed(
        out_path,
        N=N,
        k=k,
        configs=configs,
        col1=col1,
        col2=col2,
        col3=col3,
        col4=col4
    )
    
    print(f"Successfully generated {out_path}!")
    
    # Clean up the raw binary to save space
    os.remove(bin_path)

if __name__ == "__main__":
    build_npz()