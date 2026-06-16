




''' main premise: there are two columns (two arrays that are mutable) of states. First go as many cops as we are given in program
and one of the parameters for the program will be cops. So first we take the graph, make it into a matrix (2d array) (again format is 
that the matrix is a 2d array file with '-' end), then we make states based on how many cops we got; IMPORTANT note THOUGH! - IF THERE 
IS COPS POSITIONS LIKE: cops: (a,b,c) robber: (a) and there is cops: (b,a,c) robber: (a) it is THE SAME THING because cops are 
interchangable (for now at least). So we can either lexicographicallly sort positions, but with numbers of nodes I guess.  Now we go itterrate 
and mark the states where one of the cops is the same state as the robber as winning. NOW the problem is - we need to make sure the on right side (robbers turn)
the right side gets marked if all the reachable states ARE marked as winning on the left side, meanwhile on left side to be marked as winning - you just need one state 
on the right to be marked as winning. I am not too sure if algorithmically it will work out automatically WITHOUT checking all the states on the right side, but if it does not -
then right side array (robbers turn) should be not the same memory as the states on the left side - since they will be marked for different reasons. The reason why 
the array on the right needs all the states on the left - is because for a state to be winning on robbers turn he can NOT go ANYWHERE because all the states he could go are winning
for cops, so it becomes a winning state. We built all the states, we marked all the winning states where one of the cops is on top of the robber, and also same we do for right side
since the game is practically over. Then the game is won '''
import os
import sys
import itertools

# Citation: The algorithm logic follows the "backward induction" method
# [cite_start]described in Berarducci & Intrigila (1993)[cite: 101, 103, 118].

class CopsAndRobbersSolver:
    def __init__(self, filepath, num_cops):
        self.adj = {}
        self.nodes = []
        self.k = num_cops
        self.load_graph_matrix(filepath)
        self.steps_to_win = {} # Maps state -> integer
        # --- DATA STRUCTURES ---
        # State: represented as a tuple (cop_positions_tuple, robber_position)
        # cop_positions_tuple is SORTED to handle symmetry (e.g., (0,1) == (1,0))
        self.states = [] 
        
        # The Two Mutable Arrays (Dictionaries for easy Python lookup)
        # Left Side: Is this state a WIN for Cop when it is COP'S TURN?
        self.cop_turn_wins = {} 
        
        # Right Side: Is this state a WIN for Cop when it is ROBBER'S TURN?
        self.robber_turn_wins = {}
        
        # Optimization: Tracks how many "safe" moves the robber has left from this state.
        # When this hits 0, the state becomes a WIN for the Cop (Robber is trapped).
        self.robber_safe_moves_count = {} 

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
            # [cite_start]We add self-loops to represent this. [cite: 42, 79]
            for i in self.nodes:
                if i not in self.adj[i]:
                    self.adj[i].append(i)

            print(f"Graph loaded: {num_nodes} nodes.")
            
        except FileNotFoundError:
            print(f"Error: File {filepath} not found.")
            sys.exit(1)

    def generate_all_states(self):
        """
        Generates all possible configurations.
        Uses combinations_with_replacement because multiple cops can be on the same node.
        [cite_start][cite: 80] "it is allowed that two or more cops are on the same vertex"
        """
        # Generate all sorted tuples of k cops positions
        # e.g., for 2 cops on 3 nodes: (0,0), (0,1), (0,2), (1,1), (1,2), (2,2)
        cop_configs = list(itertools.combinations_with_replacement(self.nodes, self.k))
        
        for cops in cop_configs:
            for r in self.nodes:
                state = (cops, r)
                self.states.append(state)
                
                # Initialize arrays
                self.cop_turn_wins[state] = False
                self.robber_turn_wins[state] = False
                
                # Initialize optimization counter
                # The robber initially has 'degree' safe moves (number of neighbors)
                self.robber_safe_moves_count[state] = len(self.adj[r])

    def solve(self):
        print(f"Generating states for {self.k} cops...")
        self.generate_all_states()
        print(f"Total States: {len(self.states)}")
        
        # --- STEP 1: INITIALIZATION (Base Case W0) ---
        # [cite_start]Mark all states where a cop is ALREADY catching the robber [cite: 118]
        # We must mark BOTH arrays because physically the game is over.
        
        initial_wins = 0
        for state in self.states:
            cops_pos, r_pos = state
            
            # If robber is on the same node as ANY cop
            if r_pos in cops_pos:
                self.cop_turn_wins[state] = True
                self.robber_turn_wins[state] = True
                self.robber_safe_moves_count[state] = 0 # No moves needed, he's caught
                self.steps_to_win[state] = 0  # <--- WE JUST ADD THIS LINE
                initial_wins += 1
                
        print(f"Initialized {initial_wins} winning states (Captures).")
        print("Starting Backward Induction Loop...")

        # --- STEP 2: MAIN LOOP (The "Fixed Point" Iteration) ---
        changed = True
        passes = 0
        
        while changed:
            changed = False
            passes += 1
            new_wins_this_pass = 0
            

            # 1. ADD THESE TWO LINES (The Buffers)
            cop_wins_buffer = []
            robber_wins_buffer = []
            # We iterate through all states that are NOT yet won
            for state in self.states:
                cops_pos, r_pos = state
                
                # If already solved, skip
                if self.cop_turn_wins[state] and self.robber_turn_wins[state]:
                    continue
                
                # --- UPDATE RIGHT SIDE (ROBBER'S TURN) ---
                # Logic: Robber is trapped if ALL his moves lead to a state 
                # [cite_start]where CopTurnWins is TRUE. [cite: 126]
                if not self.robber_turn_wins[state]:
                    
                    # Optimization: Instead of checking all neighbors every time,
                    # we recount safe moves based on the updated Left Array.
                    
                    current_safe_moves = self.robber_safe_moves_count[state]
                    
                    # Check Robber's neighbors to see if any NEW ones became unsafe
                    # Note: In a highly optimized C++ version, we would decrease this count
                    # immediately when the neighbor flips to True. Here we re-scan for clarity.
                    safe_count = 0
                    for r_next in self.adj[r_pos]:
                        next_state = (cops_pos, r_next)
                        # If the destination is NOT a win for Cop, it's safe for Robber
                        if not self.cop_turn_wins[next_state]:
                            safe_count += 1
                    
                    self.robber_safe_moves_count[state] = safe_count
                    
                    if safe_count == 0:
                        robber_wins_buffer.append(state) # <--- ADD TO BUFFER
                        changed = True
                        new_wins_this_pass += 1


                # --- UPDATE LEFT SIDE (COP'S TURN) ---
                # Logic: Cop wins if there EXISTS a move to a state 
                # [cite_start]where RobberTurnWins is TRUE. [cite: 124]
                if not self.cop_turn_wins[state]:
                    
                    can_reach_winning_state = False
                    
                    # We need to check all possible moves for ALL k cops.
                    # This is complex: we generate all successor configurations.
                    # e.g., if Cops=(0, 5), Cop 1 can move or Cop 2 can move.
                    
                    # Get all possible next positions for the cop team
                    # itertools.product generates all combos of moves for the team
                    # (This handles the rule "subset of cops move" implicitly by allowing 'stay')
                    possible_moves_lists = [self.adj[c] for c in cops_pos]
                    
                    # IMPORTANT: For k > 1, this loop can be large.
                    # Ideally, Cops move one at a time or subset? 
                    # Paper says "choosing a subset... and moving each".
                    # For simplicity/standard play, we assume the team transitions to any 
                    # combination of neighbor nodes.
                    for next_cops_unsorted in itertools.product(*possible_moves_lists):
                        # Sort to match our state key format
                        next_cops = tuple(sorted(next_cops_unsorted))
                        next_state = (next_cops, r_pos)
                        
                        # Check the Right Array (Robber's Turn)
                        if self.robber_turn_wins.get(next_state, False):
                            can_reach_winning_state = True
                            break
                    
                    if can_reach_winning_state:
                        cop_wins_buffer.append(state) # <--- ADD TO BUFFER
                        self.steps_to_win[state] = passes
                        changed = True
                        new_wins_this_pass += 1
            # 3. APPLY ALL BUFFERED WINS AT ONCE
            for s in robber_wins_buffer:
                self.robber_turn_wins[s] = True
                
            for s in cop_wins_buffer:
                self.cop_turn_wins[s] = True
                self.steps_to_win[s] = passes
                
            print(f"Pass {passes}: Found {new_wins_this_pass} new winning states.")
            
        # --- STEP 3: FINAL CHECK (Game Verdict) ---
        print("\n--- FINAL VERDICT ---")

        overall_best_cop_start = None
        overall_min_worst_case_steps = float('inf')
        
        # Get all unique cop configurations
        unique_cop_configs = set(s[0] for s in self.states)
        
        for c_start in unique_cop_configs:
            is_universal_win = True
            worst_case_steps_for_this_start = 0
            
            # Check against EVERY possible Robber start node
            for r_start in self.nodes:
                state = (c_start, r_start)

                # We check LEFT ARRAY because Cops move first
                if not self.cop_turn_wins[state]:
                    is_universal_win = False
                    break
                
                # The Robber wants to MAXIMIZE the number of steps it takes to get caught
                steps = self.steps_to_win[state]
                if steps > worst_case_steps_for_this_start:
                    worst_case_steps_for_this_start = steps

            # If the Cops win from this starting setup against ALL robber starts...
            if is_universal_win:
                # ...The Cops want to pick the setup that MINIMIZES that worst-case time
                if worst_case_steps_for_this_start < overall_min_worst_case_steps:
                    overall_min_worst_case_steps = worst_case_steps_for_this_start
                    overall_best_cop_start = c_start
        
        if overall_best_cop_start:
            print(f"RESULT: WIN. {self.k} Cop(s) CAN win this graph.")
            print(f"Optimal Cop Start Positions: {overall_best_cop_start}")
            print(f"Estimated worst-case rounds to win: {overall_min_worst_case_steps}")
        else:
            print(f"RESULT: LOSS. {self.k} Cop(s) CANNOT guarantee a win.")
            print("(The Robber has a strategy to survive indefinitely against any start).")

# --- ENTRY POINT ---
if __name__ == "__main__":
    # Check arguments: python script.py <filename> <num_cops>
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
        
    # Check for file existence
    if not os.path.exists(filename):
        # print(f"Error: File '{filename}' not found inside '{assets_folder}'.")
        print("Please create the file or check the name.")
        sys.exit(1)
        
    # Run Solver
    solver = CopsAndRobbersSolver(filename, k)
    solver.solve()

#-----------------------------------------------------------------------------------------------------
