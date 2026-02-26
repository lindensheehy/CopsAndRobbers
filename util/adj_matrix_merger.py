# --- PATHS ---
path1 = "../assets/matrices/scotlandyard-all.txt"
path2 = "../assets/matrices/scotlandyard-yellow.txt"
out_path = "../out/merged_matrix.txt"
# -------------

def merge_matrices():
    with open(path1, 'r') as f1, open(path2, 'r') as f2, open(out_path, 'w') as out:
        while True:
            c1 = f1.read(1)
            c2 = f2.read(1)

            # Hit the EOF marker or actual end of file
            if not c1 or not c2 or c1 == '-' or c2 == '-':
                out.write('-')
                break

            # Handle the node delimiter
            if c1 == '\n' or c2 == '\n':
                out.write('\n')
            # Logical OR for edges
            elif c1 == '1' or c2 == '1':
                out.write('1')
            # Non-edge
            elif c1 == '0' and c2 == '0':
                out.write('0')
            # Any carriage returns (\r) or spaces are implicitly ignored

if __name__ == "__main__":
    merge_matrices()
    print(f"Matrices merged successfully to {out_path}")
