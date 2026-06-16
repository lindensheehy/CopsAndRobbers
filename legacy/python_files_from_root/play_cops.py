import sys
import os
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

class CopsVsPerfectRobber:
    def __init__(self, matrix_file, npz_file, pos_file=None):
        self.adj_matrix = parse_matrix(matrix_file)
        self.num_nodes = len(self.adj_matrix)
        self.adj = {i: [] for i in range(self.num_nodes)}
        for r in range(self.num_nodes):
            for c in range(self.num_nodes):
                if self.adj_matrix[r][c] == 1:
                    self.adj[r].append(c)
            if r not in self.adj[r]:
                self.adj[r].append(r)

        self.pos_dict = parse_positions(pos_file) if pos_file else None
        
        print("Loading AI Brain into memory...")
        data = np.load(npz_file)
        dp_table = data['dp_table']
        self.k = len(dp_table[0]) - 2
        
        self.dp_dict = {}
        for row in dp_table:
            cops = tuple(row[:self.k])
            robber = row[-2]
            steps = row[-1]
            self.dp_dict[(cops, robber)] = steps
            
        print("Brain loaded. Setting up the board...")
        self.cops = []
        self.robber = None
        self.phase = "PLACE_COPS"
        self.active_cop_idx = 0
        
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

        # Draw Cops
        if len(self.cops) > 0:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=self.cops, ax=self.ax, node_color='blue', node_size=350, label='Cops')
            
            # Highlight the cop that needs to move right now
            if self.phase == "MOVE_COPS" and self.active_cop_idx < len(self.cops):
                active_cop = self.cops[self.active_cop_idx]
                nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=[active_cop], ax=self.ax, node_color='cyan', node_size=400, edgecolors='black', linewidths=2)

        # Draw Robber
        if self.robber is not None:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=[self.robber], ax=self.ax, node_color='red', node_size=350, label='Robber')

        # Dynamic Titles
        title = ""
        if self.phase == "PLACE_COPS":
            title = f"Setup: Click anywhere to place Cop {len(self.cops) + 1} of {self.k}"
        elif self.phase == "MOVE_COPS":
            title = f"Your Turn: Click an adjacent node to move the highlighted Cop (Cyan)"
        elif self.phase == "GAME_OVER_WIN":
            title = "GAME OVER: You caught the Robber!"
        elif self.phase == "GAME_OVER_LOSS":
            title = "GAME OVER: The Robber evaded you forever."

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
        return best_node if min_dist < 2000 else None

    def on_click(self, event):
        if "GAME_OVER" in self.phase or event.xdata is None or event.ydata is None:
            return
            
        clicked_node = self.get_closest_node(event.xdata, event.ydata)
        if clicked_node is None: return

        if self.phase == "PLACE_COPS":
            self.cops.append(clicked_node)
            if len(self.cops) == self.k:
                self.trigger_ai_spawn()
            self.draw_board()
            
        elif self.phase == "MOVE_COPS":
            active_cop = self.cops[self.active_cop_idx]
            if clicked_node in self.adj[active_cop]:
                self.cops[self.active_cop_idx] = clicked_node
                self.active_cop_idx += 1
                
                # If all cops have moved, it's the Robber's turn
                if self.active_cop_idx >= self.k:
                    self.trigger_ai_robber()
                    self.active_cop_idx = 0
                self.draw_board()
            else:
                print("Invalid Move: You must click an adjacent node (or the same node to stay).")

    def trigger_ai_spawn(self):
        print("AI is calculating the most annoying starting position...")
        cops_tuple = tuple(sorted(self.cops))
        best_r = 0
        max_steps = -1
        
        for r in range(self.num_nodes):
            steps = self.dp_dict.get((cops_tuple, r), -1)
            if steps > max_steps:
                max_steps = steps
                best_r = r
                
        self.robber = best_r
        self.phase = "MOVE_COPS"
        print(f"Robber spawned at {self.robber}. The hunt begins!")

    def trigger_ai_robber(self):
        cops_tuple = tuple(sorted(self.cops))
        if self.robber in cops_tuple:
            self.phase = "GAME_OVER_WIN"
            return
            
        print("AI Robber is looking for the best escape route...")
        best_r_next = self.robber
        max_steps = -1
        
        for r_next in self.adj[self.robber]:
            state = (cops_tuple, r_next)
            steps = self.dp_dict.get(state, -1)
            
            # If the AI finds an escape route (-1), take it immediately
            if steps == -1:
                best_r_next = r_next
                self.phase = "GAME_OVER_LOSS"
                break
                
            if steps > max_steps:
                max_steps = steps
                best_r_next = r_next
                
        self.robber = best_r_next
        
        # Check catch again after robber moves (if they had no safe moves)
        if self.robber in cops_tuple and self.phase != "GAME_OVER_LOSS":
            self.phase = "GAME_OVER_WIN"

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python play_cops.py <matrix_file> <npz_file> [positions_file]")
        sys.exit(1)
        
    m_file = sys.argv[1]
    n_file = sys.argv[2]
    p_file = sys.argv[3] if len(sys.argv) > 3 else None
    
    CopsVsPerfectRobber(m_file, n_file, p_file)