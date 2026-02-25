import sys
import os
import itertools
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt

def parse_matrix(filepath):
    matrix_rows = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('-'): continue
            row = [int(char) for char in line if char in '01']
            if row: matrix_rows.append(row)
    return np.array(matrix_rows)

def parse_positions(filepath):
    coords = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('-'): continue
            parts = line.split(',')
            if len(parts) >= 2: coords.append((float(parts[0]), float(parts[1])))
    pos_dict = {}
    if coords:
        max_y = max(c[1] for c in coords)
        for i, (x, y) in enumerate(coords):
            pos_dict[i] = (x, max_y - y)
    return pos_dict

class CopsAndRobbersGame:
    def __init__(self, matrix_file, npz_file, pos_file=None):
        self.adj_matrix = parse_matrix(matrix_file)
        self.num_nodes = len(self.adj_matrix)
        self.adj = {i: [] for i in range(self.num_nodes)}
        for r in range(self.num_nodes):
            for c in range(self.num_nodes):
                if self.adj_matrix[r][c] == 1:
                    self.adj[r].append(c)
            if r not in self.adj[r]:
                self.adj[r].append(r) # Self loops

        self.pos_dict = parse_positions(pos_file) if pos_file else None
        
        print("Loading AI Brain into memory...")
        data = np.load(npz_file)
        dp_table = data['dp_table']
        self.k = len(dp_table[0]) - 2 # Extract k cops
        
        # Convert binary matrix back to O(1) lookup dictionary
        self.dp_dict = {}
        for row in dp_table:
            cops = tuple(row[:self.k])
            robber = row[-2]
            steps = row[-1]
            self.dp_dict[(cops, robber)] = steps
            
        print("Brain loaded. Setting up the board...")
        self.setup_board()

    def setup_board(self):
        # Find absolute best starting position for Cops
        best_cops = None
        min_worst_case = float('inf')
        
        unique_cops = set(k[0] for k in self.dp_dict.keys())
        for c in unique_cops:
            worst_case = 0
            universal_win = True
            for r in range(self.num_nodes):
                steps = self.dp_dict.get((c, r), -1)
                if steps == -1:
                    universal_win = False; break
                if steps > worst_case: worst_case = steps
            if universal_win and worst_case < min_worst_case:
                min_worst_case = worst_case
                best_cops = c

        self.cops = best_cops
        self.robber = None
        self.phase = "PLACE_ROBBER" # Next is ROBBER_TURN
        
        self.G = nx.from_numpy_array(self.adj_matrix)
        if not self.pos_dict:
            self.pos_dict = nx.spring_layout(self.G, seed=42)

        self.fig, self.ax = plt.subplots(figsize=(12, 9))
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.draw_board()
        plt.show()

    def draw_board(self):
        self.ax.clear()
        nx.draw_networkx_edges(self.G, self.pos_dict, ax=self.ax, edge_color='gray')
        nx.draw_networkx_nodes(self.G, self.pos_dict, ax=self.ax, node_color='lightgray', node_size=200)
        nx.draw_networkx_labels(self.G, self.pos_dict, ax=self.ax, font_size=7, font_weight='bold')

        if self.cops:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=list(self.cops), ax=self.ax, node_color='blue', node_size=350, label='Cops')
        if self.robber is not None:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=[self.robber], ax=self.ax, node_color='red', node_size=350, label='Robber')

        title = ""
        if self.phase == "PLACE_ROBBER":
            title = "Game Start: Click ANY node to place your Robber!"
        elif self.phase == "ROBBER_TURN":
            title = f"Your Turn: Click an adjacent node to move."
        elif self.phase == "GAME_OVER":
            title = "GAME OVER: The AI Caught You."

        self.ax.set_title(title, fontsize=14, fontweight='bold')
        self.ax.axis('off')
        self.fig.canvas.draw_idle()

    def get_closest_node(self, x, y):
        best_node = None
        min_dist = float('inf')
        for node, (nx, ny) in self.pos_dict.items():
            dist = (x - nx)**2 + (y - ny)**2
            if dist < min_dist:
                min_dist = dist
                best_node = node
        # Threshold so clicking empty space does nothing
        return best_node if min_dist < 2000 else None

    def on_click(self, event):
        if self.phase == "GAME_OVER" or event.xdata is None or event.ydata is None:
            return
            
        clicked_node = self.get_closest_node(event.xdata, event.ydata)
        if clicked_node is None: return

        if self.phase == "PLACE_ROBBER":
            self.robber = clicked_node
            self.phase = "ROBBER_TURN"
            self.trigger_ai_cops()
            
        elif self.phase == "ROBBER_TURN":
            if clicked_node in self.adj[self.robber]:
                self.robber = clicked_node
                self.trigger_ai_cops()
            else:
                print("Invalid Move: You must click an adjacent node (or your own node to stay put).")

    def trigger_ai_cops(self):
        if self.robber in self.cops:
            self.phase = "GAME_OVER"
            self.draw_board()
            return
            
        print("AI is thinking...")
        best_next_cops = self.cops
        min_worst_case = float('inf')
        
        possible_moves = [self.adj[c] for c in self.cops]
        for next_c_unsorted in itertools.product(*possible_moves):
            next_c = tuple(sorted(next_c_unsorted))
            worst_case = -1
            is_valid = True
            
            if self.robber in next_c:
                worst_case = 0
            else:
                for r_next in self.adj[self.robber]:
                    state = (next_c, r_next)
                    steps = self.dp_dict.get(state, -1)
                    if steps == -1:
                        is_valid = False; break
                    if steps > worst_case: worst_case = steps
            
            if is_valid and worst_case < min_worst_case:
                min_worst_case = worst_case
                best_next_cops = next_c
                
        self.cops = best_next_cops
        
        if self.robber in self.cops:
            self.phase = "GAME_OVER"
        else:
            self.phase = "ROBBER_TURN"
            
        self.draw_board()

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python play_game.py <matrix_file> <npz_file> [positions_file]")
        sys.exit(1)
        
    m_file = sys.argv[1]
    n_file = sys.argv[2]
    p_file = sys.argv[3] if len(sys.argv) > 3 else None
    
    CopsAndRobbersGame(m_file, n_file, p_file)