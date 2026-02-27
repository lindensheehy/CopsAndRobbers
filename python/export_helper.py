import sys
import os
import json
import numpy as np

def main():
    if len(sys.argv) < 3:
        print("Usage: python export_helper.py <graph_name> <num_cops>")
        sys.exit(1)

    graph_name = sys.argv[1]
    num_cops = int(sys.argv[2])

    # --- 1. Parse the Path ---
    game_history = []
    try:
        with open("temp_path.txt", "r") as f:
            for line in f:
                parts = line.strip().split('|')
                if len(parts) == 3:
                    cops = [int(x) for x in parts[0].split(',')]
                    robber = int(parts[1])
                    turn_str = parts[2]
                    game_history.append({'cops': cops, 'robber': robber, 'turn': turn_str})
    except FileNotFoundError:
        print("Warning: temp_path.txt not found. No path exported.")

    # --- 2. Parse the DP Table ---
    matrix_data = []
    try:
        with open("temp_dp.txt", "r") as f:
            for line in f:
                parts = line.strip().split('|')
                if len(parts) == 3:
                    cops = [int(x) for x in parts[0].split(',')]
                    robber = int(parts[1])
                    steps = int(parts[2])
                    matrix_data.append(cops + [robber, steps])
    except FileNotFoundError:
        print("Warning: temp_dp.txt not found. No DP table exported.")

    # --- 3. Export JSON ---
    cache_dir = "assets/cached_solutions"
    os.makedirs(cache_dir, exist_ok=True)
    base_name = os.path.basename(graph_name).split('.')[0]
    json_filename = os.path.join(cache_dir, f"{base_name}_{num_cops}cops_perfect_game.json")
    
    with open(json_filename, 'w') as f:
        json.dump(game_history, f, indent=4)
    print(f"Success! Perfect game cached to: {json_filename}")

    # --- 4. Export NPZ ---
    export_dir = "assets/dp_tables"
    os.makedirs(export_dir, exist_ok=True)
    npz_filename = os.path.join(export_dir, f"{base_name}_{num_cops}cops_dp_table.npz")
    
    if matrix_data:
        np_matrix = np.array(matrix_data, dtype=np.int16)
        np.savez_compressed(npz_filename, dp_table=np_matrix)
        print(f"Success! Highly compressed DP Table saved to: {npz_filename}")

    # --- 5. Clean up temp files ---
    if os.path.exists("temp_path.txt"): os.remove("temp_path.txt")
    if os.path.exists("temp_dp.txt"): os.remove("temp_dp.txt")

if __name__ == "__main__":
    main()