import matplotlib.pyplot as plt
import numpy as np
import sys
import os
import time

# CONFIGURATION
PICK_TOLERANCE = 15
NODE_SIZE = 100
COLOR_NODE = 'skyblue'
COLOR_SELECTED = 'limegreen'
COLOR_EDGE = 'black'

# Modes
MODE_NODE = "NODE_MODE"
MODE_EDGE = "EDGE_MODE"

class GraphBuilder:
    def __init__(self, matrix_file=None, pos_file=None):
        self.matrix_file = matrix_file
        self.pos_file = pos_file

        self.had_input = not (self.matrix_file is None or pos_file is None)
        
        # Load Data or Start Empty
        self.coords = []     # List of (x, y) tuples
        self.adj_matrix = [] # List of lists (easier to resize dynamically than numpy)
        
        if matrix_file and pos_file:
            self.load_data()
        else:
            print("Starting with empty graph.")

        # State
        self.mode = MODE_NODE
        self.selected_node = None # For edge creation
        
        # Setup Plot
        self.fig, self.ax = plt.subplots(figsize=(12, 10))
        self.setup_plot()
        
        # Events
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.fig.canvas.mpl_connect('key_press_event', self.on_key)
        
        self.redraw()
        print("Graph Builder Started.")
        print("Controls: 'm' to switch modes, 'e' to save.")
        plt.show()

    def load_data(self):
        # 1. Load Positions
        try:
            with open(self.pos_file, 'r') as f:
                for line in f:
                    parts = line.strip().split(',')
                    if len(parts) >= 2:
                        self.coords.append((float(parts[0]), float(parts[1])))
        except FileNotFoundError:
            print("Position file not found. Starting empty.")

        # 2. Load Matrix
        try:
            with open(self.matrix_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line: continue
                    row = [int(c) for c in line if c in '01']
                    if row:
                        self.adj_matrix.append(row)
            
            # Validation
            if len(self.coords) != len(self.adj_matrix):
                print("Warning: Coordinate/Matrix size mismatch. Graph may behave oddly.")
        except FileNotFoundError:
            print("Matrix file not found. Starting empty.")

    def setup_plot(self):
        # Always set aspect to equal so circles look like circles
        self.ax.set_aspect('equal')
        
        # If we have NO data, force a 1000x1000 canvas
        if not self.had_input:
            self.ax.set_xlim(0, 1000)
            self.ax.set_ylim(1000, 0) # 0 at top, 1000 at bottom
        else:
            # If we DO have data, let matplotlib scale to fit it, 
            # but ensure Y is inverted to match image coordinates
            self.ax.invert_yaxis()
            
        self.update_title()

    def update_title(self):
        mode_str = "NODE MODE (Click empty to Add, Click node to Delete)" if self.mode == MODE_NODE else "EDGE MODE (Click 2 nodes to connect)"
        self.ax.set_title(f"Graph Builder - {len(self.coords)} Nodes\n{mode_str}\nPress 'm' to switch mode | 'e' to save")

    def redraw(self):
        self.ax.clear()
        self.setup_plot()
        
        # Draw Edges
        # Since adj_matrix is symmetric, just iterate upper triangle
        n = len(self.coords)
        for i in range(n):
            for j in range(i + 1, n):
                if self.adj_matrix[i][j] == 1:
                    p1, p2 = self.coords[i], self.coords[j]
                    self.ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=COLOR_EDGE, alpha=0.6, zorder=1)

        # Draw Nodes
        if self.coords:
            x_vals = [c[0] for c in self.coords]
            y_vals = [c[1] for c in self.coords]
            
            # Color logic
            colors = [COLOR_NODE] * n
            if self.selected_node is not None:
                colors[self.selected_node] = COLOR_SELECTED
            
            self.ax.scatter(x_vals, y_vals, s=NODE_SIZE, c=colors, zorder=2, edgecolors='black')
            
            # Draw Labels (Indices)
            for i, (x, y) in enumerate(self.coords):
                self.ax.text(x, y, str(i + 1), fontsize=9, ha='right', va='bottom', zorder=3, fontweight='bold')

        self.fig.canvas.draw()

    def get_node_under_cursor(self, event):
        if not self.coords: return None
        if event.xdata is None or event.ydata is None: return None
        
        cursor_pos = np.array([event.xdata, event.ydata])
        # Calculate distances
        distances = np.linalg.norm(np.array(self.coords) - cursor_pos, axis=1)
        min_idx = np.argmin(distances)
        
        if distances[min_idx] < PICK_TOLERANCE:
            return min_idx
        return None

    def on_click(self, event):
        if event.button != 1: return # Left click only
        if event.inaxes != self.ax: return

        clicked_node = self.get_node_under_cursor(event)

        if self.mode == MODE_NODE:
            if clicked_node is not None:
                self.delete_node(clicked_node)
            else:
                self.add_node(event.xdata, event.ydata)
        
        elif self.mode == MODE_EDGE:
            if clicked_node is None:
                self.selected_node = None # Deselect if clicking space
            else:
                if self.selected_node is None:
                    self.selected_node = clicked_node # Select first
                elif self.selected_node == clicked_node:
                    self.selected_node = None # Deselect self
                else:
                    self.toggle_edge(self.selected_node, clicked_node)
                    self.selected_node = None # Reset after action
        
        self.redraw()

    def add_node(self, x, y):
        # 1. Add Coordinate
        self.coords.append((int(x), int(y)))
        
        # 2. Add Row/Col to Matrix
        n = len(self.adj_matrix)
        # Add 0 to end of existing rows
        for row in self.adj_matrix:
            row.append(0)
        # Add new row of 0s
        self.adj_matrix.append([0] * (n + 1))
        
        print(f"Added Node {len(self.coords)}")

    def delete_node(self, idx):
        print(f"Deleting Node {idx + 1}")
        
        # 1. Remove Coordinate
        self.coords.pop(idx)
        
        # 2. Remove Row from Matrix
        self.adj_matrix.pop(idx)
        
        # 3. Remove Column from Matrix
        for row in self.adj_matrix:
            row.pop(idx)
            
        # Reset selection to avoid crashing
        self.selected_node = None

    def toggle_edge(self, u, v):
        current = self.adj_matrix[u][v]
        new_val = 1 if current == 0 else 0
        
        self.adj_matrix[u][v] = new_val
        self.adj_matrix[v][u] = new_val
        
        action = "Connected" if new_val == 1 else "Disconnected"
        print(f"{action} {u+1} <-> {v+1}")

    def on_key(self, event):
        if event.key == 'm':
            self.mode = MODE_EDGE if self.mode == MODE_NODE else MODE_NODE
            self.selected_node = None
            self.update_title()
            self.redraw()
        elif event.key == 'e':
            self.save_files()

    def save_files(self):
        # Determine Filenames
        if self.matrix_file and self.pos_file:
            mat_out = self.matrix_file
            pos_out = self.pos_file
        else:
            ts = int(time.time())
            if not os.path.exists("out"): os.makedirs("out")
            mat_out = f"out/graph_{ts}_matrix.txt"
            pos_out = f"out/graph_{ts}_positions.txt"

        print(f"Saving to:\n  Matrix: {mat_out}\n  Positions: {pos_out}")

        # Save Matrix
        with open(mat_out, 'w') as f:
            for row in self.adj_matrix:
                f.write("".join(map(str, row)) + "\n")
                
        # Save Positions
        with open(pos_out, 'w') as f:
            for x, y in self.coords:
                f.write(f"{int(x)},{int(y)}\n")
        
        print("Save Complete!")

if __name__ == "__main__":
    # Handle Args
    matrix_arg = None
    pos_arg = None
    
    if len(sys.argv) > 1:
        matrix_arg = sys.argv[1]
    if len(sys.argv) > 2:
        pos_arg = sys.argv[2]
        
    GraphBuilder(matrix_arg, pos_arg)