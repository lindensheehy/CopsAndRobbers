import os
import sys
import itertools

# Citation: The algorithm logic follows the "backward induction" method
# described in Berarducci & Intrigila (1993), implemented here with 
# strict Minimax dynamic programming to find optimal capture time.

class CopsAndRobbersSolver:
    def __init__(self, filepath, num_cops):
        self.adj = {}
        self.nodes = []
        self.k = num_cops
        self.load_graph_matrix(filepath)
        
        # --- MINIMAX DATA STRUCTURES ---
        # State: represented as a tuple (cop_positions_tuple, robber_position)
        # cop_positions_tuple is SORTED to handle symmetry (e.g., (0,1) == (1,0))
        self.states = [] 
        
        # Dictionaries now store Integers representing exact Distance to Capture.
        # float('inf') represents that the Cops cannot force a win from this state yet.
        self.cop_win_steps = {} 
        self.robber_win_steps = {}

    def load_graph_matrix(self, filepath):
        """
        Parses adjacency matrix ending with a '-'.
        """
        try:
            with open(filepath, 'r') as f:
                lines = [line.strip() for line in f.readlines()]
            
            matrix_rows = []
            for line in lines:
                if line == '-': break
                if line: matrix_rows.append(line)
            
            num_nodes = len(matrix_rows)
            self.nodes = list(range(num_nodes))
            
            for i in self.nodes:
                self.adj[i] = []

            for r, row_str in enumerate(matrix_rows):
                for c, char in enumerate(row_str):
                    if char == '1':
                        self.adj[r].append(c)
            
            # IMPLICIT RULE: Players can choose to stay put (pass turn).
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)

            print(f"Graph loaded: {num_nodes} nodes.")
            
        except FileNotFoundError:
            print(f"Error: File '{filepath}' not found.")
            sys.exit(1)

    def generate_all_states(self):
        """
        Generates all possible configurations.
        """
        cop_configs = list(itertools.combinations_with_replacement(self.nodes, self.k))
        
        for cops in cop_configs:
            for r in self.nodes:
                state = (cops, r)
                self.states.append(state)
                
                # Initialize all distances to Infinity
                self.cop_win_steps[state] = float('inf')
                self.robber_win_steps[state] = float('inf')

    def solve(self):
        print(f"Generating states for {self.k} cops...")
        self.generate_all_states()
        print(f"Total States: {len(self.states)}")
        
        # --- STEP 1: INITIALIZATION (Base Cases) ---
        initial_wins = 0
        for state in self.states:
            cops_pos, r_pos = state
            
            # If robber is on the same node as ANY cop, distance is 0.
            if r_pos in cops_pos:
                self.cop_win_steps[state] = 0
                self.robber_win_steps[state] = 0
                initial_wins += 1
                
        print(f"Initialized {initial_wins} winning states at Distance 0.")
        print("Starting Strict Minimax Backward Induction...")

        # --- STEP 2: MAIN LOOP (Dynamic Programming Relaxation) ---
        changed = True
        iteration = 0
        
        while changed:
            changed = False
            iteration += 1
            updates_this_pass = 0
            
            for state in self.states:
                cops_pos, r_pos = state
                
                # --- UPDATE RIGHT SIDE (ROBBER'S TURN) ---
                # Goal: MAXIMIZE the number of steps before capture.
                is_safe = False
                max_cop_steps = -1
                
                for r_next in self.adj[r_pos]:
                    next_state = (cops_pos, r_next)
                    
                    # If even one path leads to infinity, the Robber is completely safe.
                    if self.cop_win_steps[next_state] == float('inf'):
                        is_safe = True
                        break
                    
                    # Track the path that delays the cops the longest
                    if self.cop_win_steps[next_state] > max_cop_steps:
                        max_cop_steps = self.cop_win_steps[next_state]
                
                # If Robber has NO safe moves left, update with the maximum survival time
                if not is_safe:
                    if max_cop_steps < self.robber_win_steps[state]:
                        self.robber_win_steps[state] = max_cop_steps
                        changed = True
                        updates_this_pass += 1


                # --- UPDATE LEFT SIDE (COP'S TURN) ---
                # Goal: MINIMIZE the number of steps to capture.
                min_steps_to_win = float('inf')
                
                possible_moves_lists = [self.adj[c] for c in cops_pos]
                
                for next_cops_unsorted in itertools.product(*possible_moves_lists):
                    next_cops = tuple(sorted(next_cops_unsorted))
                    next_state = (next_cops, r_pos)
                    
                    # The Cop wants to find the absolute shortest path
                    if self.robber_win_steps[next_state] < min_steps_to_win:
                        min_steps_to_win = self.robber_win_steps[next_state]
                
                # If the Cop found a guaranteed path to victory
                if min_steps_to_win != float('inf'):
                    # The Cop takes 1 step/round to execute this move
                    candidate_steps = 1 + min_steps_to_win
                    
                    # Update if this new path is strictly faster than previous knowledge
                    if candidate_steps < self.cop_win_steps[state]:
                        self.cop_win_steps[state] = candidate_steps
                        changed = True
                        updates_this_pass += 1

            if updates_this_pass > 0:
                print(f"Iteration {iteration}: Updated optimal distances for {updates_this_pass} states.")
            
        # --- STEP 3: FINAL CHECK (Min-Max Game Verdict) ---
        print("\n--- FINAL VERDICT ---")
        
        overall_best_cop_start = None
        overall_min_worst_case_steps = float('inf')
        
        unique_cop_configs = set(s[0] for s in self.states)
        
        for c_start in unique_cop_configs:
            is_universal_win = True
            worst_case_steps_for_this_start = 0 
            
            for r_start in self.nodes:
                state = (c_start, r_start)
                
                # If any Robber start leads to infinity, this Cop start is a failure
                if self.cop_win_steps[state] == float('inf'):
                    is_universal_win = False
                    break
                
                # The Robber chooses the start that MAXIMIZES survival time
                if self.cop_win_steps[state] > worst_case_steps_for_this_start:
                    worst_case_steps_for_this_start = self.cop_win_steps[state]
            
            # The Cops choose the starting configuration that MINIMIZES that worst-case time
            if is_universal_win:
                if worst_case_steps_for_this_start < overall_min_worst_case_steps:
                    overall_min_worst_case_steps = worst_case_steps_for_this_start
                    overall_best_cop_start = c_start
        
        if overall_best_cop_start:
            print(f"RESULT: WIN. {self.k} Cop(s) CAN win this graph.")
            print(f"Optimal Cop Start Positions: {overall_best_cop_start}")
            print(f"Exact optimal rounds to win: {overall_min_worst_case_steps}")
        else:
            print(f"RESULT: LOSS. {self.k} Cop(s) CANNOT guarantee a win.")
            print("(The Robber has a strategy to survive indefinitely against any start).")


# --- ENTRY POINT ---
if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python k_cops.py <graph_file.txt> <num_cops>")
        print("Example: python k_cops.py graph3.txt 4")
        sys.exit(1)
        
    filename = sys.argv[1]
    try:
        k = int(sys.argv[2])
    except ValueError:
        print("Error: Number of cops must be an integer.")
        sys.exit(1)
        
    # Fixed the crash here: Removed the undefined 'assets_folder' variable
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found.")
        print("Please check the path and try again.")
        sys.exit(1)
        
    solver = CopsAndRobbersSolver(filename, k)
    solver.solve()