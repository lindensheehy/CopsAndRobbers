import os
import sys

class CopsAndRobbersSolver:
    def __init__(self, filepath):
        self.adj = {}
        self.nodes = [] 
        self.load_graph_matrix(filepath)
        
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
            
            # Add self-loops (Staying put is a valid move)
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)

            print(f"Graph loaded: {num_nodes} nodes.")
            
        except FileNotFoundError:
            print(f"Error: File {filepath} not found.")
            sys.exit(1)

    def solve(self):
        n = len(self.nodes)
        
        # --- STEP 1: Base Case (W0) ---
        # Immediate wins: Cop and Robber on same node
        winning_states = set()
        for i in self.nodes:
            winning_states.add((i, i))
            
        changed = True
        iteration_count = 0
        
        print("Running backward induction...")
        
        while changed:
            changed = False
            iteration_count += 1
            
            # Check all states NOT yet known to be wins
            for c in self.nodes:
                for r in self.nodes:
                    if (c, r) in winning_states:
                        continue
                    
                    # --- STEP 2 & 3: Expansion (W_n -> W_n+1) ---
                    # Can Cop move to a 'c_next' such that ALL robber responses 
                    # lead to a state that is ALREADY in winning_states?
                    
                    can_force_win = False
                    cop_moves = self.adj[c] 
                    
                    for c_next in cop_moves:
                        # If Cop moves onto Robber, it's an instant win.
                        if c_next == r:
                            can_force_win = True
                            break

                        robber_escaped = False
                        robber_moves = self.adj[r]
                        
                        # Check Robber's possible responses
                        for r_next in robber_moves:
                            # If the resulting state is NOT a known win, Robber survives this branch
                            if (c_next, r_next) not in winning_states:
                                robber_escaped = True
                                break
                        
                        # If Robber had NO escape for this specific c_next move
                        if not robber_escaped:
                            can_force_win = True
                            break
                    
                    if can_force_win:
                        winning_states.add((c, r))
                        changed = True
                        
        # --- STEP 4: Final Verdict ---
        # Find a Cop Start 'c' that wins against ALL Robber Starts 'r'
        winning_cop_start = None
        
        for c_start in self.nodes:
            is_universal_win = True
            for r_start in self.nodes:
                if (c_start, r_start) not in winning_states:
                    is_universal_win = False
                    break
            
            if is_universal_win:
                winning_cop_start = c_start
                break
                
        if winning_cop_start is not None:
            print(f"\nResult: YES. 1 Cop CAN win this graph.")
            print(f"Optimal starting position: Node {winning_cop_start}")
            return True
        else:
            print(f"\nResult: NO. 1 Cop CANNOT win this graph.")
            return False

# --- Main Execution ---
if __name__ == "__main__":
    if len(sys.argv) > 1:
        target_filename = sys.argv[1]
    else:
        print("Usage: python script.py <filename>")
        target_filename = 'cycle5.txt' 

    assets_path = os.path.join('assets', target_filename)

    if not os.path.exists(assets_path):
        # Auto-create the Line Graph for testing if missing
        if target_filename == 'line3.txt':
            os.makedirs('assets', exist_ok=True)
            with open(assets_path, 'w') as f:
                f.write("010\n101\n010\n-")
            print("Created line3.txt for testing.")
        else:
            print(f"Error: {assets_path} not found.")
            sys.exit(1)

    print(f"Processing: {assets_path}")
    solver = CopsAndRobbersSolver(assets_path)
    solver.solve()