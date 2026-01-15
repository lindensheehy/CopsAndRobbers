import networkx as nx
import matplotlib.pyplot as plt
import numpy as np
import sys

def parse_graph_file(filepath):
    """
    Reads a text file containing an adjacency matrix and returns a numpy array.
    Stops reading when it encounters a '-'.
    """
    matrix_rows = []
    
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
            
        for line in lines:
            line = line.strip()
            
            # Stop if we hit the terminator
            if line == '-':
                break
            
            # Skip empty lines
            if not line:
                continue
                
            # Convert string "011" into list of integers [0, 1, 1]
            row = [int(char) for char in line]
            matrix_rows.append(row)
            
        return np.array(matrix_rows)

    except FileNotFoundError:
        print(f"Error: The file '{filepath}' was not found.")
        sys.exit(1)

def visualize_graph(matrix):
    """
    Takes a numpy adjacency matrix, converts to NetworkX graph, and plots it.
    """
    # Create the graph from the numpy matrix
    # G represents the graph object
    G = nx.from_numpy_array(matrix)
    
    # Set up the visual plot
    plt.figure(figsize=(8, 6))
    
    # Choose a layout (Spring layout is usually best for general graphs)
    # You can also use nx.circular_layout(G) if you know it's a ring
    pos = nx.spring_layout(G, seed=42) 
    
    # Draw the graph nodes, edges, and labels
    nx.draw(
        G, 
        pos, 
        with_labels=True, 
        node_color='skyblue', 
        node_size=1, 
        edge_color='gray', 
        font_size=5, 
        font_weight='bold'
    )
    
    plt.title("Undirected Graph Visualization")
    plt.show()

if __name__ == "__main__":
    # You can change this filename to match your input file
    input_filename = "tree127.txt"
    
    print(f"Reading from {input_filename}...")
    adj_matrix = parse_graph_file(input_filename)
    
    # specific check to ensure matrix is square (N x N)
    rows, cols = adj_matrix.shape
    if rows != cols:
        print(f"Error: Matrix is not square. Found {rows} rows and {cols} columns.")
    else:
        visualize_graph(adj_matrix)