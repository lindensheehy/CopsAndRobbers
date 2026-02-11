import matplotlib.pyplot as plt
import numpy as np
import sys

# CONFIGURATION
PICK_TOLERANCE = 15  # Pixel distance to consider a "hover" or "click"
NODE_SIZE_NORMAL = 50
NODE_SIZE_HOVER = 150
COLOR_NORMAL = 'skyblue'
COLOR_HOVER = 'orange'
COLOR_SELECTED = 'limegreen'
COLOR_EDGE = 'black'

class GraphBuilder:
    def __init__(self, input_file, output_file=None):
        # If no output file specified, overwrite the input file
        self.output_file = output_file if output_file else input_file
        
        # Load data
        self.coords, initial_matrix = self.load_graph_data(input_file)
        self.n_nodes = len(self.coords)
        
        # Initialize Adjacency matrix
        if initial_matrix is not None:
            self.adj_matrix = initial_matrix
        else:
            self.adj_matrix = np.zeros((self.n_nodes, self.n_nodes), dtype=int)
        
        # State
        self.selected_node = None
        self.hovered_node = None
        self.edge_lines = {}  # Store plot objects for edges: (u, v) -> line_obj

        # Setup Plot
        self.fig, self.ax = plt.subplots(figsize=(12, 10))
        self.ax.set_title(f"Graph Editor ({self.n_nodes} nodes)\nClick two nodes to connect/disconnect. Press 'E' to Save.")
        
        # Scatter plot for nodes
        x_vals = [c[0] for c in self.coords]
        y_vals = [c[1] for c in self.coords]
        self.scat = self.ax.scatter(x_vals, y_vals, s=NODE_SIZE_NORMAL, c=COLOR_NORMAL, zorder=3)
        
        # Annotate nodes with IDs (Starting at 1)
        for i, (x, y) in enumerate(self.coords):
            self.ax.text(x, y, str(i + 1), fontsize=9, ha='right', va='bottom', zorder=4, fontweight='bold')

        # Invert Y axis to match image coordinate system
        self.ax.invert_yaxis()
        self.ax.set_aspect('equal')

        # Draw Initial Edges
        self.draw_existing_edges()

        # Events
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.fig.canvas.mpl_connect('motion_notify_event', self.on_hover)
        self.fig.canvas.mpl_connect('key_press_event', self.on_key)

        print(f"Graph Editor Started. Loaded {self.n_nodes} nodes.")
        print("Close window or press 'E' to save changes.")
        plt.show()

    def load_graph_data(self, filepath):
        coords = []
        matrix_rows = []
        reading_matrix = False
        
        try:
            with open(filepath, 'r') as f:
                lines = f.readlines()
                
            for line in lines:
                line = line.strip()
                if not line: continue
                
                if line == '-':
                    reading_matrix = True
                    continue
                
                if not reading_matrix:
                    # Parse Coords "123,456"
                    parts = line.split(',')
                    if len(parts) >= 2:
                        coords.append((float(parts[0]), float(parts[1])))
                else:
                    # Parse Matrix Row "01001..."
                    row = [int(c) for c in line if c in '01']
                    if row:
                        matrix_rows.append(row)
            
            matrix = np.array(matrix_rows) if matrix_rows else None
            
            # Validation
            if matrix is not None:
                if matrix.shape[0] != len(coords):
                    print(f"Warning: Matrix size ({matrix.shape[0]}) does not match coord count ({len(coords)}).")
            
            return coords, matrix

        except FileNotFoundError:
            print(f"Error: File '{filepath}' not found.")
            sys.exit(1)

    def draw_existing_edges(self):
        # Iterate upper triangle of matrix to avoid duplicates
        rows, cols = self.adj_matrix.shape
        count = 0
        for i in range(rows):
            for j in range(i + 1, cols):
                if self.adj_matrix[i][j] == 1:
                    self.add_visual_edge(i, j)
                    count += 1
        print(f"Visualized {count} existing edges.")

    def add_visual_edge(self, u, v):
        # Ensure u < v
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
        # Ensure u < v
        u, v = (u, v) if u < v else (v, u)
        
        if self.adj_matrix[u][v] == 0:
            # Add
            self.adj_matrix[u][v] = 1
            self.adj_matrix[v][u] = 1
            self.add_visual_edge(u, v)
            print(f"Edge added: {u + 1} - {v + 1}")
        else:
            # Remove
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
            self.export_file()

    def export_file(self):
        print(f"Saving full graph to {self.output_file}...")
        try:
            with open(self.output_file, 'w') as f:
                # 1. Write Coordinates
                for x, y in self.coords:
                    f.write(f"{int(x)},{int(y)}\n")
                
                # 2. Write Separator
                f.write("-\n")
                
                # 3. Write Matrix
                for row in self.adj_matrix:
                    f.write("".join(map(str, row)) + "\n")
                    
            print("Done! File saved successfully.")
        except IOError as e:
            print(f"Error saving file: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        GraphBuilder(sys.argv[1])
    else:
        print("Usage: python graph_editor.py <graph_file.txt>")