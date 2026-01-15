size = 127
filename = "binary_tree_127.txt"

# Initialize a grid of "0"s
# We use a list of lists for easy modification
matrix = [["0"] * size for _ in range(size)]

# Iterate through the parents
# We only need to go halfway because the second half are all leaf nodes
for i in range(size):
    left_child = 2 * i + 1
    right_child = 2 * i + 2
    
    # If the left child index is within bounds, link it
    if left_child < size:
        matrix[i][left_child] = "1"
        matrix[left_child][i] = "1"  # Symmetric (undirected) connection
    
    # If the right child index is within bounds, link it
    if right_child < size:
        matrix[i][right_child] = "1"
        matrix[right_child][i] = "1" # Symmetric (undirected) connection

# Write to file
with open(filename, "w") as f:
    for row in matrix:
        f.write("".join(row) + "\n")

print(f"Successfully generated {filename}")