size = 20
filename = "dodecahedron_20.txt"

# Initialize 20x20 matrix with "0"s
matrix = [["0"] * size for _ in range(size)]

def add_edge(u, v):
    matrix[u][v] = "1"
    matrix[v][u] = "1"

# The Dodecahedron is Generalized Petersen Graph GP(10, 2)
n = 10
k = 2

for i in range(n):
    # 1. Outer Cycle (0 to 9) -> Connect i to (i+1)%10
    u1, u2 = i, (i + 1) % n
    add_edge(u1, u2)
    
    # 2. Spokes -> Connect Outer i to Inner i+10
    v1 = i + n
    add_edge(i, v1)
    
    # 3. Inner Cycle (10 to 19) -> Connect i+10 to (i+k)%10 + 10
    # Note: The inner nodes wrap around with step k=2
    v2 = (i + k) % n + n
    add_edge(v1, v2)

# Write to file
with open(filename, "w") as f:
    for row in matrix:
        f.write("".join(row) + "\n")

print(f"Successfully generated {filename}")