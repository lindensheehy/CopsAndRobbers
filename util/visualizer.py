import networkx as nx
import matplotlib.pyplot as plt
import numpy as np
import sys

def parse_graph_file(filepath):
    """
    Reads a text file containing:
    1. Coordinate list (x,y)
    2. Separator '-'
    3. Adjacency matrix rows (0101...)
    4. Optional trailing '-'
    
    Returns: (numpy_matrix, positions_dict)
    """
    coords = []
    matrix_rows = []
    reading_mode = "coords" # Switch to "matrix" after hitting first '-'
    
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
            
        for line in lines:
            line = line.strip()
            
            # Skip empty lines
            if not line:
                continue
            
            if line == '-':
                if reading_mode == "coords":
                    reading_mode = "matrix"
                    continue
                else:
                    break # Stop if we hit a second '-'
            
            if reading_mode == "coords":
                # Parse "123,456" into (123, 456)
                parts = line.split(',')
                if len(parts) == 2:
                    coords.append((int(parts[0]), int(parts[1])))
                    
            elif reading_mode == "matrix":
                # Convert string "011" into list of integers [0, 1, 1]
                # Filter out any non-digit characters just in case
                row = [int(char) for char in line if char in '01']
                matrix_rows.append(row)
        
        # Convert coords list to a dict {node_id: (x, y)}
        # We need to invert Y because matplotlib plot origin is bottom-left, 
        # but image coords are top-left.
        pos_dict = {}
        if coords:
            max_y = max(c[1] for c in coords)
            for i, (x, y) in enumerate(coords):
                pos_dict[i] = (x, max_y - y) # Flip Y axis
                
        return np.array(matrix_rows), pos_dict

    except FileNotFoundError:
        print(f"Error: The file '{filepath}' was not found.")
        sys.exit(1)

def visualize_graph(matrix, pos):
    """
    Takes a numpy adjacency matrix and a position dict, converts to NetworkX graph, and plots it.
    """
    # Create the graph from the numpy matrix
    G = nx.from_numpy_array(matrix)
    
    # Validation: Ensure we have positions for all nodes
    if len(pos) != len(G.nodes):
        print(f"Warning: Position count ({len(pos)}) does not match Node count ({len(G.nodes)}).")
        # Fallback to spring layout if counts don't match
        pos = nx.spring_layout(G, seed=42)

    # Set up the visual plot
    plt.figure(figsize=(8, 6))
    
    # Draw the graph nodes, edges, and labels using the imported positions
    nx.draw(
        G, 
        pos, 
        with_labels=True, 
        node_color='skyblue', 
        node_size=180, # Increased size slightly for visibility
        edge_color='gray', 
        font_size=6, 
        font_weight='bold'
    )
    
    plt.title("Scotland Yard Graph Visualization")
    plt.axis('off') # Hide the axis ticks
    plt.show()

if __name__ == "__main__":
    import sys

    # The directory where your files are stored
    base_path = "../assets/matrices/"
    
    # Default to robertson.txt if no argument is provided
    input_filename = base_path + "robertson.txt"
    
    # If a command line argument is provided, use it
    if len(sys.argv) > 1:
        # Takes "my_graph.txt" and turns it into "../assets/matrices/my_graph.txt"
        input_filename = base_path + sys.argv[1]
    
    print(f"Reading from {input_filename}...")
    adj_matrix, positions = parse_graph_file(input_filename)
    
    # Specific check to ensure matrix is square (N x N)
    rows, cols = adj_matrix.shape
    if rows != cols:
        print(f"Error: Matrix is not square. Found {rows} rows and {cols} columns.")
    else:
        visualize_graph(adj_matrix, positions)