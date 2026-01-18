import os
import sys
"""
THIS FILE IS TO BE USED THE FOLLOWING WAY: python your_script_name.py <GRAPH_FILE_NAME.TXT>
"""
class CopsAndRobbersSolver:
    def __init__(self, filepath):
        self.adj = {}
        self.nodes = [] # Using list to keep index alignment with matrix rows
        self.load_graph_matrix(filepath)
        
    def load_graph_matrix(self, filepath):
        """
        Parses an adjacency matrix file ending with a '-'.
        Example format:
        01001
        10100
        ...
        -
        """
        try:
            with open(filepath, 'r') as f:
                lines = [line.strip() for line in f.readlines()]
            
            matrix_rows = []
            for line in lines:
                if line == '-':
                    break
                if line: # Skip empty lines if any
                    matrix_rows.append(line)
            
            num_nodes = len(matrix_rows)
            # Create nodes 0 to n-1
            self.nodes = list(range(num_nodes))
            
            # Initialize adjacency dict
            for i in self.nodes:
                self.adj[i] = []

            # Parse Matrix
            for r, row_str in enumerate(matrix_rows):
                # Validation: ensure row length matches num_nodes
                if len(row_str) != num_nodes:
                    print(f"Warning: Row {r} has length {len(row_str)}, expected {num_nodes}.")
                
                for c, char in enumerate(row_str):
                    if char == '1':
                        self.adj[r].append(c)
            
            # IMPLICIT RULE: Cop can choose NOT to move.
            # "cop can decide to not to move" 
            # We add self-loops to allow the algorithm to simulate 'staying put'.
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)

            print(f"Graph loaded from matrix: {num_nodes} nodes.")
            
        except FileNotFoundError:
            print(f"Error: File {filepath} not found.")
            sys.exit(1)

    def solve(self):
        """
        Determines if 1 Cop can win.
        """
        n = len(self.nodes)
        
        # 1. Initialize Winning States (Base Case)
        # Any state where cop and robber are on the same node is a win.
        winning_states = set()
        for i in self.nodes:
            winning_states.add((i, i))
            
        changed = True
        print("Running propagation algorithm...")
        
        # 2. Backpropagation Loop
        while changed:
            changed = False
            # Iterate over all possible states (c, r)
            for c in self.nodes:
                for r in self.nodes:
                    if (c, r) in winning_states:
                        continue
                    
                    # LOGIC:
                    # The Cop (at c) can force a win if there exists a move to c_next
                    # such that for ALL possible moves the Robber (at r) makes to r_next,
                    # the state (c_next, r_next) is ALREADY a winning state.
                    
                    can_force_win = False
                    
                    cop_moves = self.adj[c] # Includes current node (stay)
                    for c_next in cop_moves:
                        
                        robber_escaped = False
                        
                        robber_moves = self.adj[r] # Includes current node (stay)
                        for r_next in robber_moves:
                            if (c_next, r_next) not in winning_states:
                                robber_escaped = True
                                break
                        
                        # If robber couldn't find a safe move for this specific c_next
                        if not robber_escaped:
                            can_force_win = True
                            break
                    
                    if can_force_win:
                        winning_states.add((c, r))
                        changed = True
                        
        # 3. Final Check
        # Can the Cop pick a start node c_start such that (c_start, r_start)
        # is a winning state for ALL possible r_starts?
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
    # 1. Check if the user provided an argument
    if len(sys.argv) > 1:
        target_filename = sys.argv[1]
    else:
        # Default behavior if no argument is given
        print("Usage: python script.py <filename>")
        print("No file specified. Defaulting to 'cycle5.txt'...")
        target_filename = 'cycle5.txt'

    # 2. Construct the full path
    # This assumes the file is inside the 'assets' folder as per your requirements
    assets_path = os.path.join('assets', target_filename)

    # 3. Check if the file actually exists
    if not os.path.exists(assets_path):
        print(f"Error: The file '{assets_path}' was not found.")
        print("Please make sure the file is in the 'assets' folder.")
        sys.exit(1)

    # 4. Run the solver
    print(f"Processing: {assets_path}")
    solver = CopsAndRobbersSolver(assets_path)
    solver.solve()