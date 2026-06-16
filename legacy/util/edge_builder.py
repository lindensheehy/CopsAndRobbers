import matplotlib.pyplot as plt
import numpy as np
import networkx as nx
import sys
import os

# CONFIGURATION
PICK_TOLERANCE = 15  # Pixel distance to consider a "hover" or "click"
NODE_SIZE_NORMAL = 50
NODE_SIZE_HOVER = 150
COLOR_NORMAL = 'skyblue'
COLOR_HOVER = 'orange'
COLOR_SELECTED = 'limegreen'
COLOR_EDGE = 'black'

class GraphBuilder:
    def __init__(self, matrix_file, positions_file=None):
        self.matrix_file = matrix_file
        self.positions_file = positions_file
        
        # Load Data
        self.adj_matrix = self.load_matrix(matrix_file)
        self.n_nodes = self.adj_matrix.shape[0]
        self.coords = self.load_positions(positions_file, self.n_nodes)
        
        # State
        self.selected_node = None
        self.hovered_node = None
        self.edge_lines = {}  # Store plot objects for edges: (u, v) -> line_obj

        # Setup Plot
        self.fig, self.ax = plt.subplots(figsize=(12, 10))
        title_text = f"Edge Builder ({self.n_nodes} nodes)\nClick two nodes to connect/disconnect. Press 'E' to Save Matrix."
        if not positions_file:
            title_text += "\n(No positions provided - Using auto-layout)"
        self.ax.set_title(title_text)
        
        # Scatter plot for nodes
        x_vals = [c[0] for c in self.coords]
        y_vals = [c[1] for c in self.coords]
        self.scat = self.ax.scatter(x_vals, y_vals, s=NODE_SIZE_NORMAL, c=COLOR_NORMAL, zorder=3)
        
        # Annotate nodes with IDs (Starting at 1)
        for i, (x, y) in enumerate(self.coords):
            self.ax.text(x, y, str(i + 1), fontsize=9, ha='right', va='bottom', zorder=4, fontweight='bold')

        # Invert Y axis to match image coordinate system (0,0 at top-left)
        # This keeps it consistent with your previous tools, even if using auto-layout
        self.ax.invert_yaxis()
        self.ax.set_aspect('equal')

        # Draw Initial Edges
        self.draw_existing_edges()

        # Events
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.fig.canvas.mpl_connect('motion_notify_event', self.on_hover)
        self.fig.canvas.mpl_connect('key_press_event', self.on_key)

        print(f"Graph Editor Started. Loaded {self.n_nodes} nodes.")
        print(f"Editing file: {self.matrix_file}")
        print("Close window or press 'E' to save changes.")
        plt.show()

    def load_matrix(self, filepath):
        """Loads adjacency matrix from a text file."""
        matrix_rows = []
        try:
            with open(filepath, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line: continue
                    
                    # Parse row: Convert "0101" string to list
                    row = [int(c) for c in line if c in '01']
                    if row:
                        matrix_rows.append(row)
            
            if not matrix_rows:
                print("Error: Matrix file is empty or invalid.")
                sys.exit(1)
                
            return np.array(matrix_rows)
            
        except FileNotFoundError:
            print(f"Error: Matrix file '{filepath}' not found.")
            sys.exit(1)

    def load_positions(self, filepath, n_nodes):
        """Loads positions from file or generates them if missing."""
        coords = []
        
        if filepath:
            try:
                with open(filepath, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith('-'): continue
                        
                        parts = line.split(',')
                        if len(parts) >= 2:
                            coords.append((float(parts[0]), float(parts[1])))
                
                # Validation
                if len(coords) != n_nodes:
                    print(f"Warning: Position count ({len(coords)}) does not match Matrix size ({n_nodes}).")
                    # If mismatch, we might crash later, but let's try to proceed or pad/truncate
                    if len(coords) < n_nodes:
                        print("Padding missing nodes with (0,0)...")
                        coords.extend([(0,0)] * (n_nodes - len(coords)))
                    else:
                        coords = coords[:n_nodes]
                        
                return coords
            except FileNotFoundError:
                print(f"Warning: Position file '{filepath}' not found. Switching to auto-layout.")
        
        # Fallback: Generate Circular Layout
        # We scale it up to 800x800 so the mouse tolerance works nicely
        print("Generating auto-layout coordinates...")
        G = nx.complete_graph(n_nodes)
        pos_dict = nx.circular_layout(G, scale=400, center=(400, 400))
        return [pos_dict[i] for i in range(n_nodes)]

    def draw_existing_edges(self):
        rows, cols = self.adj_matrix.shape
        count = 0
        for i in range(rows):
            for j in range(i + 1, cols):
                if self.adj_matrix[i][j] == 1:
                    self.add_visual_edge(i, j)
                    count += 1
        print(f"Visualized {count} existing edges.")

    def add_visual_edge(self, u, v):
        u, v = (u, v) if u < v else (v, u)
        p1, p2 = self.coords[u], self.coords[v]
        line, = self.ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=COLOR_EDGE, linestyle='-', alpha=0.6, zorder=1)
        self.edge_lines[(u, v)] = line

    def remove_visual_edge(self, u, v):
        u, v = (u, v) if u < v else (v, u)
        if (u, v) in self.edge_lines:
            self.edge_lines[(u, v)].remove()
            del self.edge_lines[(u, v)]

    def get_node_under_cursor(self, event):
        if event.xdata is None or event.ydata is None: return None
        cursor_pos = np.array([event.xdata, event.ydata])
        distances = np.linalg.norm(np.array(self.coords) - cursor_pos, axis=1)
        min_idx = np.argmin(distances)
        if distances[min_idx] < PICK_TOLERANCE:
            return min_idx
        return None

    def on_hover(self, event):
        if event.inaxes != self.ax: return
        node_idx = self.get_node_under_cursor(event)
        if node_idx != self.hovered_node:
            self.hovered_node = node_idx
            self.update_visuals()

    def on_click(self, event):
        if event.button != 1: return
        if event.inaxes != self.ax: return 

        clicked_node = self.get_node_under_cursor(event)
        
        if clicked_node is None:
            self.selected_node = None
        else:
            if self.selected_node is None:
                self.selected_node = clicked_node
            elif self.selected_node == clicked_node:
                self.selected_node = None
            else:
                u, v = self.selected_node, clicked_node
                self.toggle_edge(u, v)
                self.selected_node = None 
        
        self.update_visuals()

    def toggle_edge(self, u, v):
        u, v = (u, v) if u < v else (v, u)
        if self.adj_matrix[u][v] == 0:
            self.adj_matrix[u][v] = 1
            self.adj_matrix[v][u] = 1
            self.add_visual_edge(u, v)
            print(f"Edge added: {u + 1} - {v + 1}")
        else:
            self.adj_matrix[u][v] = 0
            self.adj_matrix[v][u] = 0
            self.remove_visual_edge(u, v)
            print(f"Edge removed: {u + 1} - {v + 1}")

    def update_visuals(self):
        colors = np.array([COLOR_NORMAL] * self.n_nodes, dtype=object)
        sizes = np.array([NODE_SIZE_NORMAL] * self.n_nodes)
        
        if self.hovered_node is not None:
            colors[self.hovered_node] = COLOR_HOVER
            sizes[self.hovered_node] = NODE_SIZE_HOVER
            
        if self.selected_node is not None:
            colors[self.selected_node] = COLOR_SELECTED
            sizes[self.selected_node] = NODE_SIZE_HOVER
            
        self.scat.set_facecolors(colors)
        self.scat.set_sizes(sizes)
        self.fig.canvas.draw_idle()

    def on_key(self, event):
        if event.key == 'e':
            self.export_matrix()

    def export_matrix(self):
        print(f"Saving adjacency matrix to {self.matrix_file}...")
        try:
            with open(self.matrix_file, 'w') as f:
                for row in self.adj_matrix:
                    f.write("".join(map(str, row)) + "\n")
            print("Done! File saved successfully.")
        except IOError as e:
            print(f"Error saving file: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python edge_builder.py <matrix_file.txt> [positions_file.txt]")
    else:
        matrix_path = sys.argv[1]
        pos_path = sys.argv[2] if len(sys.argv) > 2 else None
        GraphBuilder(matrix_path, pos_path)