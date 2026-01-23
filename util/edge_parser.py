import sys

def parse_edges_to_matrix(input_path, output_path):
    edges = []
    max_id = 0  # Changed start to 0 since we expect 1-based positive integers

    # Read file and parse edges
    try:
        with open(input_path, 'r') as f:
            for line in f:
                parts = list(map(int, line.strip().split()))
                if len(parts) == 2:
                    u, v = parts
                    edges.append((u, v))
                    max_id = max(max_id, u, v)
    except FileNotFoundError:
        print(f"Error: The file '{input_path}' was not found.")
        return

    # Size is max_id exactly (because Node 20 -> Index 19, so we need 20 slots)
    size = max_id
    matrix = [[0] * size for _ in range(size)]

    # Populate matrix (undirected)
    for u, v in edges:
        # Subtract 1 to convert 1-based input to 0-based matrix index
        matrix[u-1][v-1] = 1
        matrix[v-1][u-1] = 1

    # Write to output file
    with open(output_path, 'w') as f:
        for row in matrix:
            f.write("".join(map(str, row)) + "\n")
            
    print(f"Successfully wrote matrix to {output_path}")

if __name__ == "__main__":
    # Usage: python script.py input.txt [output.txt]
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
        # Use second argument as output file if provided, otherwise default
        output_file = "../out/matrix_output.txt"
        
        parse_edges_to_matrix(input_file, output_file)
    else:
        print("Usage: python script.py <input_file> [output_file]")