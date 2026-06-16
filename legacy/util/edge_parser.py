import sys
import os

def parse_edges_to_matrix(input_path):
    edges = []
    max_id = 0
    
    print(f"Parsing edge list: {input_path}")

    # 1. Read file and parse edges
    try:
        with open(input_path, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                # Skip comments or empty lines
                if not line or line.startswith('#'): continue 
                
                # Robust parsing: handle commas or spaces
                # "1, 2" -> "1 2" -> ["1", "2"]
                parts = line.replace(',', ' ').split()
                
                try:
                    # Filter for numbers only
                    nums = [int(p) for p in parts if p.isdigit()]
                    
                    if len(nums) >= 2:
                        u, v = nums[0], nums[1]
                        
                        # Sanity check for 0-indexed inputs
                        if u == 0 or v == 0:
                            print(f"Warning (Line {line_num}): Found node '0'. This script assumes 1-based indexing.")
                        
                        edges.append((u, v))
                        max_id = max(max_id, u, v)
                except ValueError:
                    continue # Skip malformed lines

    except FileNotFoundError:
        print(f"Error: The file '{input_path}' was not found.")
        return

    if not edges:
        print("Error: No valid edges found in file.")
        return

    # 2. Create Matrix
    # Size is max_id (Node 5 -> Index 4, so size must be 5)
    size = max_id
    matrix = [[0] * size for _ in range(size)]

    # 3. Populate matrix (undirected)
    for u, v in edges:
        # Subtract 1 to convert 1-based input to 0-based matrix index
        idx_u, idx_v = u - 1, v - 1
        
        # Ensure we stay within bounds (though max_id logic should handle this)
        if idx_u < size and idx_v < size:
            matrix[idx_u][idx_v] = 1
            matrix[idx_v][idx_u] = 1

    # 4. Generate Output Filename in 'out/' directory
    output_dir = "out"
    
    # Create the directory if it doesn't exist
    if not os.path.exists(output_dir):
        try:
            os.makedirs(output_dir)
            print(f"Created directory: {output_dir}")
        except OSError as e:
            print(f"Error creating directory {output_dir}: {e}")
            return

    # Get just the filename (e.g., "my_edges.txt") from the path
    filename = os.path.basename(input_path)
    base, ext = os.path.splitext(filename)
    
    # Construct new path: out/my_edges_matrix.txt
    output_path = os.path.join(output_dir, f"{base}_matrix{ext}")

    # 5. Write to output file
    try:
        with open(output_path, 'w') as f:
            for row in matrix:
                f.write("".join(map(str, row)) + "\n")
        print(f"Success! Matrix saved to: {output_path}")
    except IOError as e:
        print(f"Error writing output: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
        parse_edges_to_matrix(input_file)
    else:
        print("Usage: python edge_parser.py <edge_list.txt>")