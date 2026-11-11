import networkx as nx
import matplotlib.pyplot as plt
import numpy as np
import sys
import os

def parse_matrix(filepath):
    """
    Reads a text file containing adjacency matrix rows (e.g., "0101...").
    Returns a numpy array.
    """
    matrix_rows = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                # Skip empty lines or separator lines if they exist in the file
                if not line or line.startswith('-'): 
                    continue
                
                # Parse row: Convert "0101" string to list [0, 1, 0, 1]
                # Filters to ensure we only grab valid bits
                row = [int(char) for char in line if char in '01']
                
                # Only append if we actually found data
                if row:
                    matrix_rows.append(row)
                    
        return np.array(matrix_rows)
    except FileNotFoundError:
        print(f"Error: Matrix file '{filepath}' not found.")
        sys.exit(1)

def parse_positions(filepath):
    """
    Reads a text file containing coordinates "x,y".
    Returns a dict {node_id: (x, y)} suitable for NetworkX.
    """
    coords = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('-'): 
                    continue
                
                parts = line.split(',')
                if len(parts) >= 2:
                    coords.append((float(parts[0]), float(parts[1])))
        
        # Create dictionary
        # We invert the Y-axis because image coordinates (Top-Left 0,0)
        # differ from Matplotlib coordinates (Bottom-Left 0,0)
        pos_dict = {}
        if coords:
            max_y = max(c[1] for c in coords)
            for i, (x, y) in enumerate(coords):
                pos_dict[i] = (x, max_y - y)
                
        return pos_dict
    except FileNotFoundError:
        print(f"Error: Position file '{filepath}' not found.")
        return None

def visualize_graph(matrix, pos_dict=None):
    """
    Plots the graph using NetworkX.
    """
    # Create the graph from the numpy matrix
    G = nx.from_numpy_array(matrix)
    
    # Determine Layout
    if pos_dict is None:
        print("No position file provided. Using auto-generated Spring Layout.")
        pos = nx.spring_layout(G, seed=42)
    else:
        # Validate count
        if len(pos_dict) != len(G.nodes):
            print(f"Warning: Position count ({len(pos_dict)}) does not match Node count ({len(G.nodes)}).")
        pos = pos_dict

    # Set up the visual plot
    plt.figure(figsize=(10, 8))
    
    nx.draw(
        G, 
        pos, 
        with_labels=True, 
        node_color='skyblue', 
        node_size=300, 
        edge_color='gray', 
        font_size=8, 
        font_weight='bold'
    )
    
    title = "Graph Visualization"
    if pos_dict:
        title += " (Custom Positions)"
    else:
        title += " (Auto Layout)"
        
    plt.title(title)
    plt.axis('off')
    plt.show()

if __name__ == "__main__":
    # Strict Argument Checking
    if len(sys.argv) < 2:
        print("Usage: python visualizer.py <matrix_file.txt> [positions_file.txt]")
        sys.exit(1)

    # 1. Parse Matrix (Mandatory)
    matrix_path = sys.argv[1]
    print(f"Loading matrix from: {matrix_path}")
    adj_matrix = parse_matrix(matrix_path)
    
    # Validation: Matrix must be square
    rows, cols = adj_matrix.shape
    if rows != cols:
        print(f"Error: Matrix is not square. Found {rows} rows and {cols} columns.")
        sys.exit(1)

    # 2. Parse Positions (Optional)
    positions = None
    if len(sys.argv) > 2:
        pos_path = sys.argv[2]
        print(f"Loading positions from: {pos_path}")
        positions = parse_positions(pos_path)

    # 3. Visualize
    visualize_graph(adj_matrix, positions)