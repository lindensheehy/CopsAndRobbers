import os
import sys
import itertools
from collections import deque

# Citation: The algorithm logic follows the "backward induction" method
# described in Berarducci & Intrigila (1993), optimized with Minimax BFS.

class CopsAndRobbersSolver:
    def __init__(self, filepath, num_cops):
        self.adj = {}
        self.reverse_adj = {}
        self.nodes = []
        self.k = num_cops
        self.load_graph_matrix(filepath)
        
        # --- DATA STRUCTURES ---
        # State: (cop_positions_tuple, robber_position, turn)
        # turn: 0 for Cop's turn to move, 1 for Robber's turn to move
        self.states = set()
        
        # Maps state -> Minimum rounds to capture
        self.winning_states = {} 
        
        # Optimization: Tracks how many safe moves the robber has left
        self.robber_safe_moves = {}

    def load_graph_matrix(self, filepath):
        """Parses adjacency matrix and builds BOTH forward and reverse adjacency lists."""
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
                self.reverse_adj[i] = [] # Crucial for reverse propagation

            for r, row_str in enumerate(matrix_rows):
                for c, char in enumerate(row_str):
                    if char == '1':
                        self.adj[r].append(c)
                        self.reverse_adj[c].append(r) # Track incoming edges
            
            # IMPLICIT RULE: Players can choose to stay put (pass turn)
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)
                    self.reverse_adj[i].append(i)

            print(f"Graph loaded: {num_nodes} nodes.")
        except FileNotFoundError:
            print(f"Error: File {filepath} not found.")
            sys.exit(1)

    def solve(self):
        print(f"Generating states for {self.k} cops...")
        
        queue = deque()
        cop_configs = list(itertools.combinations_with_replacement(self.nodes, self.k))
        
        # 1. INITIALIZATION
        for cops in cop_configs:
            for r in self.nodes:
                state_cop_turn = (cops, r, 0)
                state_rob_turn = (cops, r, 1)
                
                self.states.add(state_cop_turn)
                self.states.add(state_rob_turn)
                
                # Robber initially assumes all outward neighbors are safe
                self.robber_safe_moves[state_rob_turn] = len(self.adj[r])

                # BASE CASE: Cop is already on the Robber (0 rounds to win)
                if r in cops:
                    self.winning_states[state_cop_turn] = 0
                    self.winning_states[state_rob_turn] = 0
                    queue.append(state_cop_turn)
                    queue.append(state_rob_turn)

        print(f"Total States: {len(self.states)}")
        print(f"Initialized {len(queue)} capturing states.")
        print("Starting Optimized Backward Induction Queue...")

        # 2. MAIN BFS QUEUE (The Minimax Fixed Point)
        while queue:
            current_state = queue.popleft()
            current_rounds = self.winning_states[current_state]
            cops_pos, r_pos, turn = current_state
            
            # If current state is Cop's turn, the PREVIOUS move was the Robber's
            if turn == 0:
                for r_prev in self.reverse_adj[r_pos]:
                    prev_state = (cops_pos, r_prev, 1)
                    
                    if prev_state in self.winning_states:
                        continue # Already solved
                        
                    # Robber lost an escape route
                    self.robber_safe_moves[prev_state] -= 1
                    
                    # If all routes are blocked, the Robber loses
                    if self.robber_safe_moves[prev_state] == 0:
                        self.winning_states[prev_state] = current_rounds + 1
                        queue.append(prev_state)

            # If current state is Robber's turn, the PREVIOUS move was the Cop's
            else:
                prev_cop_lists = [self.reverse_adj[c] for c in cops_pos]
                
                for prev_cops_unsorted in itertools.product(*prev_cop_lists):
                    prev_cops = tuple(sorted(prev_cops_unsorted))
                    prev_state = (prev_cops, r_pos, 0)
                    
                    if prev_state in self.winning_states:
                        continue # Cop already has a faster route
                        
                    # Cop takes the fastest route to a win
                    self.winning_states[prev_state] = current_rounds + 1
                    queue.append(prev_state)

        # 3. VERDICT
        self.print_verdict()

    def print_verdict(self):
        print("\n--- FINAL VERDICT ---")
        winning_start_config = None
        min_worst_case_rounds = float('inf')
        
        unique_cop_configs = set(s[0] for s in self.states)
        
        for c_start in unique_cop_configs:
            is_universal_win = True
            worst_case_for_this_start = -1
            
            for r_start in self.nodes:
                state = (c_start, r_start, 0) # Cop goes first
                if state not in self.winning_states:
                    is_universal_win = False
                    break
                else:
                    if self.winning_states[state] > worst_case_for_this_start:
                        worst_case_for_this_start = self.winning_states[state]
            
            if is_universal_win:
                if worst_case_for_this_start < min_worst_case_rounds:
                    min_worst_case_rounds = worst_case_for_this_start
                    winning_start_config = c_start
        
        if winning_start_config:
            print(f"RESULT: WIN. {self.k} Cop(s) CAN win this graph.")
            print(f"Optimal Cop Start Positions: {winning_start_config}")
            print(f"Max Rounds to Capture: {min_worst_case_rounds}")
        else:
            print(f"RESULT: LOSS. {self.k} Cop(s) CANNOT guarantee a win.")

# --- ENTRY POINT ---
if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python cops_k_and_robber_algo.py <graph_file.txt> <num_cops>")
        sys.exit(1)
        
    filename = sys.argv[1]
    k = int(sys.argv[2])
    
    solver = CopsAndRobbersSolver(filename, k)
    solver.solve()