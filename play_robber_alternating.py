import sys
import os
import itertools
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt

class PlayRobberGame:
    def __init__(self):
        print("Loading AI Brain into memory...")
        self.data = np.load("assets/dp_tables/alternating_dp.npz")
        self.N = int(self.data['N'])
        self.k = int(self.data['k'])
        self.configs = self.data['configs']
        self.col1 = self.data['col1']
        self.col2 = self.data['col2']
        self.col3 = self.data['col3']
        self.col4 = self.data['col4']
        self.cid_map = {tuple(c): i for i, c in enumerate(self.configs)}

        self.adj_matrix = self.parse_matrix("assets/matrices/scotlandyard-all.txt")
        self.adj = {i: [i] for i in range(self.N)}
        for r in range(self.N):
            for c in range(self.N):
                if self.adj_matrix[r][c] == 1: self.adj[r].append(c)

        self.pos_dict = self.parse_positions("assets/positions/scotlandyard.txt")
        self.G = nx.from_numpy_array(self.adj_matrix)
        if not self.pos_dict or len(self.pos_dict) != self.N:
            self.pos_dict = nx.spring_layout(self.G, seed=42)

        self.setup_game()

    def parse_matrix(self, filepath):
        rows = []
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('-'): continue
                row = [int(char) for char in line if char in '01']
                if row: rows.append(row)
        return np.array(rows)

    def parse_positions(self, filepath):
        coords = []
        try:
            with open(filepath, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('-'): continue
                    parts = line.split(',')
                    if len(parts) >= 2: coords.append((float(parts[0]), float(parts[1])))
            pos_dict = {}
            if coords:
                max_y = max(c[1] for c in coords)
                for i, (x, y) in enumerate(coords): pos_dict[i] = (x, max_y - y)
            return pos_dict
        except: return None

    def score_val(self, val): return 999999 if val == -1 else val
    def get_cop_moves(self, cops):
        valid = [self.adj[c] for c in cops]
        return list(set(tuple(sorted(p)) for p in itertools.product(*valid)))

    def setup_game(self):
        print("Finding optimal AI Cop starting positions...")
        start_id, best_val = 0, 999999
        for i in range(len(self.configs)):
            r_best = max([self.score_val(self.col1[i][r]) for r in range(self.N)])
            if r_best < best_val:
                best_val = r_best
                start_id = i
        
        self.cops = tuple(int(c) for c in self.configs[start_id])
        self.robber = None
        self.last_known_robber = None
        self.phase = "PLACE_ROBBER"

        self.fig, self.ax = plt.subplots(figsize=(12, 9))
        self.fig.canvas.manager.set_window_title('Play as Robber')
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.draw_board()
        plt.show()

    def draw_board(self):
        self.ax.clear()
        
        node_colors = []
        for node in self.G.nodes():
            if node in self.cops: node_colors.append('blue')
            elif self.phase in ["PLACE_ROBBER", "ROBBER_INV_MOVE", "GAME_OVER"] and node == self.robber: 
                node_colors.append('red')
            elif self.phase == "ROBBER_VIS_MOVE" and self.last_known_robber is not None:
                if node == self.last_known_robber or node in self.adj[self.last_known_robber]:
                    node_colors.append('yellow')
                else: node_colors.append('lightgray')
            else: node_colors.append('lightgray')

        nx.draw_networkx_edges(self.G, self.pos_dict, ax=self.ax, edge_color='gray')
        nx.draw_networkx_nodes(self.G, self.pos_dict, ax=self.ax, node_color=node_colors, node_size=250)
        nx.draw_networkx_labels(self.G, self.pos_dict, ax=self.ax, font_size=7, font_weight='bold')

        title = ""
        if self.phase == "PLACE_ROBBER": title = "Game Start: Click to place Robber!"
        elif self.phase == "ROBBER_INV_MOVE": title = "Visible Phase: Click adjacent node to HIDE"
        elif self.phase == "ROBBER_VIS_MOVE": title = "Hidden Phase: Click adjacent node to REAPPEAR"
        elif self.phase == "GAME_OVER": title = "GAME OVER: The AI Cops caught you."

        self.ax.set_title(title, fontsize=14, fontweight='bold')
        self.ax.axis('off')
        self.fig.canvas.draw_idle()

    def get_closest_node(self, x, y):
        best_node, min_dist = None, float('inf')
        for node, (nx, ny) in self.pos_dict.items():
            dist = (x - nx)**2 + (y - ny)**2
            if dist < min_dist:
                min_dist = dist
                best_node = node
        return best_node if min_dist < 2000 else None

    def on_click(self, event):
        if self.phase == "GAME_OVER" or event.xdata is None: return
        node = self.get_closest_node(event.xdata, event.ydata)
        if node is None: return

        if self.phase == "PLACE_ROBBER":
            self.robber = node
            self.check_win()
            if self.phase != "GAME_OVER":
                # AI Cop takes turn 1
                self.cops = min(self.get_cop_moves(self.cops), key=lambda m: self.score_val(self.col2[self.cid_map[m]][self.robber]))
                self.check_win()
                if self.phase != "GAME_OVER": self.phase = "ROBBER_INV_MOVE"
            self.draw_board()

        elif self.phase == "ROBBER_INV_MOVE":
            if node in self.adj[self.robber]:
                self.last_known_robber = self.robber
                self.robber = node
                # AI Cop takes blind turn
                self.cops = min(self.get_cop_moves(self.cops), key=lambda m: self.score_val(self.col4[self.cid_map[m]][self.last_known_robber]))
                self.check_win()
                if self.phase != "GAME_OVER": self.phase = "ROBBER_VIS_MOVE"
                self.draw_board()
            else: print("Invalid move! Must be adjacent.")

        elif self.phase == "ROBBER_VIS_MOVE":
            if node in self.adj[self.robber]:
                self.robber = node
                self.check_win()
                if self.phase != "GAME_OVER":
                    # AI Cop takes visible turn
                    self.cops = min(self.get_cop_moves(self.cops), key=lambda m: self.score_val(self.col2[self.cid_map[m]][self.robber]))
                    self.check_win()
                    if self.phase != "GAME_OVER": self.phase = "ROBBER_INV_MOVE"
                self.draw_board()
            else: print("Invalid move! Must be adjacent.")

    def check_win(self):
        if self.robber in self.cops:
            self.phase = "GAME_OVER"
            print("COPS WIN!")

if __name__ == "__main__":
    PlayRobberGame()