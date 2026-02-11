import sys

def parse_matrix_from_file(filepath):
    """
    Skips the coordinate section and reads the adjacency matrix 
    located after the first '-' separator.
    """
    matrix = []
    reading_matrix = False
    
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line: continue
                
                if line == '-':
                    if not reading_matrix:
                        reading_matrix = True # Found the first separator
                        continue
                    else:
                        break # Found the trailing separator (if any)
                
                if reading_matrix:
                    # Convert string "0101" to list of ints [0, 1, 0, 1]
                    # Filter ensures we only grab digits, ignoring potential whitespace
                    row = [int(c) for c in line if c in '01']
                    if row:
                        matrix.append(row)
        return matrix
    except FileNotFoundError:
        print(f"Error: Graph file '{filepath}' not found.")
        sys.exit(1)

def load_expected_degrees(filepath):
    degrees = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if line.isdigit():
                    degrees.append(int(line))
        return degrees
    except FileNotFoundError:
        print(f"Error: Degree file '{filepath}' not found.")
        sys.exit(1)

def verify_graph(graph_path, degrees_path):
    print(f"Verifying graph: {graph_path}")
    print(f"Against reference: {degrees_path}")
    print("-" * 40)

    matrix = parse_matrix_from_file(graph_path)
    expected_degrees = load_expected_degrees(degrees_path)

    # 1. Check Node Count
    num_nodes_matrix = len(matrix)
    num_nodes_expected = len(expected_degrees)

    if num_nodes_matrix != num_nodes_expected:
        print(f"CRITICAL ERROR: Node count mismatch!")
        print(f"Matrix has {num_nodes_matrix} nodes.")
        print(f"Expected file has {num_nodes_expected} values.")
        print("Cannot proceed with verification.")
        return

    # 2. Check Degrees
    error_count = 0
    
    for i in range(num_nodes_matrix):
        # Calculate actual degree (sum of 1s in the row)
        actual_degree = sum(matrix[i])
        expected_degree = expected_degrees[i]
        
        if actual_degree != expected_degree:
            error_count += 1
            # Using i+1 for 1-based indexing (Scotland Yard style)
            print(f"[Node {i+1}] MISMATCH: Expected {expected_degree}, Found {actual_degree}")

    print("-" * 40)
    if error_count == 0:
        print("SUCCESS: Graph matches expected topology perfectly!")
    else:
        print(f"FAILURE: Found {error_count} discrepancies.")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        graph_file = sys.argv[1]
        degrees_file = sys.argv[2]
        verify_graph(graph_file, degrees_file)
    else:
        print("Usage: python verify_graph.py <graph_file.txt> <expected_degrees.txt>")