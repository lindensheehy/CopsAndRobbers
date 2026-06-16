import numpy as np
import itertools
import os
import sys
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

class AlternatingSimulator:
    def __init__(self):
        print("Loading NPZ DP Tables and Graph Matrix...")
        
        self.data = np.load("assets/dp_tables/alternating_dp.npz")
        self.N = int(self.data['N'])
        self.k = int(self.data['k'])
        self.configs = self.data['configs']
        self.col1 = self.data['col1']
        self.col2 = self.data['col2']
        self.col3 = self.data['col3']
        self.col4 = self.data['col4']

        self.cid_map = {tuple(c): i for i, c in enumerate(self.configs)}

        matrix_path = "assets/matrices/scotlandyard-all.txt"
        pos_path = "assets/positions/scotlandyard.txt" 

        self.matrix = self.parse_matrix(matrix_path)
        self.G = nx.from_numpy_array(self.matrix)
        
        self.pos = self.parse_positions(pos_path)
        if self.pos is None or len(self.pos) != self.N:
            print("Using auto-generated Spring Layout for visualization...")
            self.pos = nx.spring_layout(self.G, seed=42)

        self.adj = {i: [i] for i in range(self.N)} 
        for r in range(self.N):
            for c in range(self.N):
                if self.matrix[r][c] == 1:
                    self.adj[r].append(c)

    def parse_matrix(self, filepath):
        matrix_rows = []
        try:
            with open(filepath, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('-'): continue
                    row = [int(char) for char in line if char in '01']
                    if row: matrix_rows.append(row)
            return np.array(matrix_rows)
        except FileNotFoundError:
            print(f"Error: Matrix file '{filepath}' not found.")
            sys.exit(1)

    def parse_positions(self, filepath):
        coords = []
        try:
            with open(filepath, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('-'): continue
                    parts = line.split(',')
                    if len(parts) >= 2:
                        coords.append((float(parts[0]), float(parts[1])))
            pos_dict = {}
            if coords:
                max_y = max(c[1] for c in coords)
                for i, (x, y) in enumerate(coords):
                    pos_dict[i] = (x, max_y - y)
            return pos_dict
        except FileNotFoundError:
            return None

    # --- AI LOGIC ---
    def get_cop_moves(self, current_cops):
        valid_moves_per_cop = [self.adj[c] for c in current_cops]
        return list(set(tuple(sorted(p)) for p in itertools.product(*valid_moves_per_cop)))

    def score_val(self, val):
        return 999999 if val == -1 else val

    def ai_cop_visible_turn(self, cops, r_curr):
        moves = self.get_cop_moves(cops)
        return min(moves, key=lambda m: self.score_val(self.col2[self.cid_map[m]][r_curr]))

    def ai_cop_invisible_turn(self, cops, r_last_known):
        moves = self.get_cop_moves(cops)
        return min(moves, key=lambda m: self.score_val(self.col4[self.cid_map[m]][r_last_known]))

    def ai_robber_pick_r1(self, cops, r_curr):
        predicted_c_next = min(self.get_cop_moves(cops), key=lambda m: self.score_val(self.col4[self.cid_map[m]][r_curr]))
        c_next_id = self.cid_map[predicted_c_next]
        best_r1, best_score = r_curr, -2
        
        for r1 in self.adj[r_curr]:
            if r1 in predicted_c_next: continue 
            r2_scores = [self.score_val(self.col1[c_next_id][r2]) for r2 in self.adj[r1]]
            max_r2_score = max(r2_scores) if r2_scores else -1
            if max_r2_score > best_score:
                best_score = max_r2_score
                best_r1 = r1
        return best_r1

    def ai_robber_pick_r2(self, cops_after_move, r1):
        c_id = self.cid_map[cops_after_move]
        return max(self.adj[r1], key=lambda r2: self.score_val(self.col1[c_id][r2]))

    # --- GAME EXTRACTION ---
    def extract_perfect_game(self):
        print("Calculating perfect game sequence...")
        history = []
        
        # 1. Find optimal Start (Cop minimizes col1)
        start_id = 0
        best_start_val = 999999
        for i in range(len(self.configs)):
            robber_best = max([self.score_val(self.col1[i][r]) for r in range(self.N)])
            if robber_best < best_start_val:
                best_start_val = robber_best
                start_id = i
        
        cops = tuple(int(c) for c in self.configs[start_id])
        robber = max(range(self.N), key=lambda r: self.score_val(self.col1[start_id][r]))

        round_num = 1
        while round_num <= 50:
            # 1. Cop Visible Turn
            history.append({'cops': cops, 'robber': robber, 'is_visible': True, 'possible_r': None, 'turn_text': f"Round {round_num}: Cop Visible Move"})
            if robber in cops: 
                history[-1]['turn_text'] = f"COPS WIN! Caught visible robber at {robber}!"
                break

            cops = tuple(int(c) for c in self.ai_cop_visible_turn(cops, robber))

            # 2. Robber Invisible Turn
            history.append({'cops': cops, 'robber': robber, 'is_visible': True, 'possible_r': None, 'turn_text': f"Round {round_num}: Robber Hides"})
            if robber in cops: 
                history[-1]['turn_text'] = f"COPS WIN! Caught visible robber at {robber}!"
                break

            r1 = int(self.ai_robber_pick_r1(cops, robber))
            r_last_known = robber
            robber = r1 

            # 3. Cop Invisible Turn
            poss = self.adj[r_last_known]
            history.append({'cops': cops, 'robber': r_last_known, 'is_visible': False, 'possible_r': poss, 'turn_text': f"Round {round_num}: Cop Invisible Move"})
            
            cops = tuple(int(c) for c in self.ai_cop_invisible_turn(cops, r_last_known))
            
            # 4. Robber Reappears
            history.append({'cops': cops, 'robber': r_last_known, 'is_visible': False, 'possible_r': poss, 'turn_text': f"Round {round_num}: Robber Reappears"})
            if robber in cops: 
                history[-1]['turn_text'] = f"COPS WIN! Caught invisible robber at {robber}!"
                history[-1]['is_visible'] = True
                history[-1]['robber'] = robber
                break

            robber = int(self.ai_robber_pick_r2(cops, robber))

            if robber in cops: 
                history.append({'cops': cops, 'robber': robber, 'is_visible': True, 'possible_r': None, 'turn_text': f"COPS WIN! Robber reappeared directly onto a cop at {robber}!"})
                break

            round_num += 1
            
        if round_num > 50:
            history[-1]['turn_text'] += " [LIMIT REACHED - GAME IS A LOSS]"
            
        return history

    # --- INTERACTIVE VISUALIZER ---
    def visualize_interactive(self, history):
        print("Launching Interactive Replay Window...")
        fig, ax = plt.subplots(figsize=(12, 9))
        plt.subplots_adjust(bottom=0.2)
        fig.canvas.manager.set_window_title('Perfect Game Replay')

        current_step = [0]

        def draw_step(step_idx):
            ax.clear()
            state = history[step_idx]
            cops = state['cops']
            robber = state['robber']
            is_visible = state['is_visible']
            possible_r = state['possible_r']
            turn_text = state['turn_text']

            node_colors = []
            for node in self.G.nodes():
                if node in cops: node_colors.append('blue')
                elif is_visible and node == robber: node_colors.append('red')
                elif not is_visible and possible_r and node in possible_r: node_colors.append('yellow')
                else: node_colors.append('lightgray')

            nx.draw_networkx_edges(self.G, self.pos, ax=ax, edge_color='gray')
            nx.draw_networkx_nodes(self.G, self.pos, ax=ax, node_color=node_colors, node_size=200)
            nx.draw_networkx_labels(self.G, self.pos, ax=ax, font_size=7, font_weight='bold')
            
            ax.set_title(f"Step {step_idx + 1}/{len(history)} | {turn_text}", fontsize=14, fontweight='bold')
            ax.axis('off')
            fig.canvas.draw_idle()

        draw_step(0)

        # Buttons
        axprev = plt.axes([0.35, 0.05, 0.1, 0.075])
        axnext = plt.axes([0.55, 0.05, 0.1, 0.075])
        self.bnext = Button(axnext, 'Next Turn')
        self.bprev = Button(axprev, 'Previous')

        def next_step(event):
            if current_step[0] < len(history) - 1:
                current_step[0] += 1
                draw_step(current_step[0])

        def prev_step(event):
            if current_step[0] > 0:
                current_step[0] -= 1
                draw_step(current_step[0])

        self.bnext.on_clicked(next_step)
        self.bprev.on_clicked(prev_step)

        plt.show()

if __name__ == "__main__":
    sim = AlternatingSimulator()
    history = sim.extract_perfect_game()
    sim.visualize_interactive(history)