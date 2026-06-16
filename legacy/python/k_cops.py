import os
import sys
import itertools
import numpy as np
import json #for export

class CopsAndRobbersSolver:
    def __init__(self, filepath, num_cops, pos_filepath=None): # <-- Added argument
        self.adj = {}
        self.nodes = []
        self.k = num_cops
        self.load_graph_matrix(filepath)
        self.steps_to_win = {} 
        self.states = [] 
        self.cop_turn_wins = {} 
        self.robber_turn_wins = {}
        self.robber_safe_moves_count = {} 
        self.filepath = filepath 
        self.pos_filepath = pos_filepath

    def load_graph_matrix(self, filepath):
        try:
            with open(filepath, 'r') as f:
                lines = [line.strip() for line in f.readlines()]
            matrix_rows = [line for line in lines if line and line != '-']
            num_nodes = len(matrix_rows)
            self.nodes = list(range(num_nodes))
            for i in self.nodes:
                self.adj[i] = []
            for r, row_str in enumerate(matrix_rows):
                for c, char in enumerate(row_str):
                    if char == '1':
                        self.adj[r].append(c)
            # Self-loops allow players to "stay put" [cite: 45]
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)
            print(f"Graph loaded: {num_nodes} nodes.")
        except FileNotFoundError:
            print(f"Error: File {filepath} not found."); sys.exit(1)

    def generate_all_states(self):
        # Lexicographical sorting handles interchangeable cops
        cop_configs = list(itertools.combinations_with_replacement(self.nodes, self.k))
        for cops in cop_configs:
            for r in self.nodes:
                state = (cops, r)
                self.states.append(state)
                self.cop_turn_wins[state] = False
                self.robber_turn_wins[state] = False
                # Robber starts with N safe moves (neighbors)
                self.robber_safe_moves_count[state] = len(self.adj[r])

    def solve(self):
        print(f"Generating states for {self.k} cops...")
        self.generate_all_states()
        print(f"Total States: {len(self.states)}")
        
        # --- STEP 1: INITIALIZATION (Capture at Round 0) ---
        initial_wins = 0
        for state in self.states:
            cops_pos, r_pos = state
            if r_pos in cops_pos:
                self.cop_turn_wins[state] = True
                self.robber_turn_wins[state] = True
                self.robber_safe_moves_count[state] = 0
                self.steps_to_win[state] = 0 
                initial_wins += 1
        print(f"Initialized {initial_wins} winning states (Captures).")

        # --- STEP 2: SYNCHRONOUS INDUCTION LOOP ---
        changed = True
        passes = 0
        while changed:
            changed = False
            passes += 1
            
            # Temporary buffers to prevent the "Domino Effect"
            cop_wins_to_apply = []
            robber_wins_to_apply = []

            for state in self.states:
                cops_pos, r_pos = state
                
                # Skip already locked win states
                if self.cop_turn_wins[state] and self.robber_turn_wins[state]:
                    continue
                
                # --- UPDATE ROBBER SIDE (Right Column) ---
                if not self.robber_turn_wins[state]:
                    # Recalculate safe moves based ONLY on previous pass wins
                    safe_count = 0
                    for r_next in self.adj[r_pos]:
                        next_state = (cops_pos, r_next)
                        if not self.cop_turn_wins[next_state]:
                            safe_count += 1
                    
                    self.robber_safe_moves_count[state] = safe_count
                    if safe_count == 0:
                        robber_wins_to_apply.append(state)

                # --- UPDATE COP SIDE (Left Column) ---
                if not self.cop_turn_wins[state]:
                    can_reach_win = False
                    possible_moves = [self.adj[c] for c in cops_pos]
                    for next_c_unsorted in itertools.product(*possible_moves):
                        next_c = tuple(sorted(next_c_unsorted))
                        if self.robber_turn_wins.get((next_c, r_pos), False):
                            can_reach_win = True
                            break
                    
                    if can_reach_win:
                        cop_wins_to_apply.append(state)

            # --- STEP 3: CONVERSIVE UPDATE ---
            # Now we commit the buffers to the main dictionaries simultaneously
            for s in robber_wins_to_apply:
                if not self.robber_turn_wins[s]:
                    self.robber_turn_wins[s] = True
                    changed = True
            
            new_wins_this_pass = 0
            for s in cop_wins_to_apply:
                if not self.cop_turn_wins[s]:
                    self.cop_turn_wins[s] = True
                    # Round logic: 2 passes = 1 game round
                    self.steps_to_win[s] = (passes + 1) // 2
                    changed = True
                    new_wins_this_pass += 1

            if new_wins_this_pass > 0:
                print(f"Pass {passes} (Round {(passes+1)//2}): Found {new_wins_this_pass} new states.")

        # --- STEP 4: FINAL VERDICT ---
        print("\n--- FINAL VERDICT ---")
        overall_best_cop_start = None
        overall_min_worst_case = float('inf')
        unique_cop_configs = set(s[0] for s in self.states)
        
        for c_start in unique_cop_configs:
            is_universal_win = True
            worst_case_steps = 0
            for r_start in self.nodes:
                state = (c_start, r_start)
                if not self.cop_turn_wins[state]:
                    is_universal_win = False; break
                if self.steps_to_win[state] > worst_case_steps:
                    worst_case_steps = self.steps_to_win[state]
            
            if is_universal_win and worst_case_steps < overall_min_worst_case:
                overall_min_worst_case = worst_case_steps
                overall_best_cop_start = c_start
        
        if overall_best_cop_start:
            print(f"RESULT: WIN. Best Cop Position: {overall_best_cop_start}")
            print(f"Capture Time: {overall_min_worst_case} rounds.")
            
            print("Extracting perfect game path...")
            game_history = self.extract_perfect_game(overall_best_cop_start)
            
            # --- NEW: CACHE THE SOLUTION ---
            # Pass the original filename so it names the JSON properly
            json_file = self.export_game_to_json(game_history, self.filepath)
            self.export_dp_table(self.filepath)
            print("Launching interactive visualizer...")
            cmd = f'python replay_game.py "{self.filepath}" "{json_file}"'
            if self.pos_filepath:
                cmd += f' "{self.pos_filepath}"'
            os.system(cmd)
            
        else:
            print("RESULT: LOSS. Robber can evade forever.")

    def extract_perfect_game(self, best_c_start):
        """Walks the DP table to extract the Minimax perfect game."""
        best_r_start = None
        max_steps = -1
        for r in self.nodes:
            state = (best_c_start, r)
            if self.cop_turn_wins.get(state, False):
                if self.steps_to_win[state] > max_steps:
                    max_steps = self.steps_to_win[state]
                    best_r_start = r
                    
        path = []
        curr_cops = best_c_start
        curr_robber = best_r_start
        
        loop_count = 0
        hard_limit = len(self.nodes) * 5

        while curr_robber not in curr_cops:
            loop_count += 1
            if loop_count > hard_limit:
                print("Extraction hit cycle limit. Breaking to prevent infinite loop.")
                break

            # --- COP TURN (Minimize the Maximum Robber Response) ---
            path.append({'cops': curr_cops, 'robber': curr_robber, 'turn': "Cop's Turn"})
            best_next_cops = curr_cops
            min_worst_case_steps = float('inf')
            
            possible_moves = [self.adj[c] for c in curr_cops]
            for next_c_unsorted in itertools.product(*possible_moves):
                next_c = tuple(sorted(next_c_unsorted))
                
                worst_case_robber_response = -1
                is_valid_cop_move = True
                
                if curr_robber in next_c:
                    worst_case_robber_response = 0 # Instant catch!
                else:
                    for r_next in self.adj[curr_robber]:
                        next_state = (next_c, r_next)
                        if not self.cop_turn_wins.get(next_state, False):
                            is_valid_cop_move = False 
                            break 
                            
                        steps = self.steps_to_win.get(next_state, float('inf'))
                        if steps > worst_case_robber_response:
                            worst_case_robber_response = steps
                
                if is_valid_cop_move and worst_case_robber_response < min_worst_case_steps:
                    min_worst_case_steps = worst_case_robber_response
                    best_next_cops = next_c
                        
            curr_cops = best_next_cops
            
            if curr_robber in curr_cops:
                path.append({'cops': curr_cops, 'robber': curr_robber, 'turn': "Game Over - Captured!"})
                break
                
            # --- ROBBER TURN (Maximize Steps) ---
            path.append({'cops': curr_cops, 'robber': curr_robber, 'turn': "Robber's Turn"})
            best_next_robber = curr_robber
            max_steps_r = -1
            
            for r_next in self.adj[curr_robber]:
                next_state = (curr_cops, r_next)
                if self.cop_turn_wins.get(next_state, False):
                    steps = self.steps_to_win.get(next_state, -1)
                    if steps > max_steps_r:
                        max_steps_r = steps
                        best_next_robber = r_next
                        
            curr_robber = best_next_robber
            
        return path

    def export_game_to_json(self, game_history, graph_name="graph"):
        """Saves the perfect game sequence to a JSON file for instant replay."""
        # Create the cache directory if it doesn't exist
        cache_dir = "cached_solutions"
        if not os.path.exists(cache_dir):
            os.makedirs(cache_dir)
            
        # Format the filename: e.g., line10_2cops_perfect_game.json
        base_name = os.path.basename(graph_name).split('.')[0]
        filename = f"{base_name}_{self.k}cops_perfect_game.json"
        filepath = os.path.join(cache_dir, filename)
        
        # Save the data
        with open(filepath, 'w') as f:
            json.dump(game_history, f, indent=4)
            
        print(f"Success! Perfect game cached to: {filepath}")
        return filepath

    def export_dp_table(self, graph_name="graph"):
        """Exports the DP table as a highly compressed Numpy Binary (.npz) file."""
        export_dir = "dp_tables"
        if not os.path.exists(export_dir):
            os.makedirs(export_dir)
            
        base_name = os.path.basename(graph_name).split('.')[0]
        filename = f"{base_name}_{self.k}cops_dp_table.npz" # .npz extension!
        filepath = os.path.join(export_dir, filename)
        
        print(f"Compressing {len(self.states)} states into binary DP table...")
        
        # Pack everything into a flat list of integers: [cop1, cop2, ..., robber, steps_to_win]
        # Note: We don't need to save "cop_wins" because if steps_to_win == -1, the cop lost!
        matrix_data = []
        for state in self.states:
            cops, robber = state
            steps = self.steps_to_win.get(state, -1)
            
            # Combine the cop tuple, robber int, and steps int into one flat row
            row = list(cops) + [robber, steps]
            matrix_data.append(row)
            
        # Convert to a Numpy array using 16-bit integers to save massive RAM
        # (int16 goes up to 32,767, which is plenty for node IDs and step counts)
        np_matrix = np.array(matrix_data, dtype=np.int16)
        
        # Save it highly compressed
        np.savez_compressed(filepath, dp_table=np_matrix)
            
        print(f"Success! Highly compressed DP Table saved to: {filepath}")
        return filepath

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python k_cops.py <matrix_file> <num_cops> [positions_file]")
        sys.exit(1)
        
    matrix_file = sys.argv[1]
    num_cops = int(sys.argv[2])
    pos_file = sys.argv[3] if len(sys.argv) > 3 else None
    
    solver = CopsAndRobbersSolver(matrix_file, num_cops, pos_filepath=pos_file)
    solver.solve()