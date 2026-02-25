import os
import sys
import json
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

def parse_matrix(filepath):
    """Reads a text file containing adjacency matrix rows and returns a numpy array."""
    matrix_rows = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('-'): 
                    continue
                row = [int(char) for char in line if char in '01']
                if row:
                    matrix_rows.append(row)
        return np.array(matrix_rows)
    except FileNotFoundError:
        print(f"Error: Matrix file '{filepath}' not found.")
        sys.exit(1)

def parse_positions(filepath):
    """Reads a text file containing coordinates 'x,y'."""
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
        
        pos_dict = {}
        if coords:
            # Flip Y-axis so image coordinates map correctly to Matplotlib
            max_y = max(c[1] for c in coords)
            for i, (x, y) in enumerate(coords):
                pos_dict[i] = (x, max_y - y)
        return pos_dict
    except FileNotFoundError:
        print(f"Warning: Position file '{filepath}' not found. Using auto-layout.")
        return None

def visualize_interactive(matrix, history, pos_dict=None):
    """Self-contained interactive Matplotlib UI for the game replay."""
    G = nx.from_numpy_array(matrix)
    
    # 1. Enforce Layout
    if pos_dict is None:
        print("No position file provided. Using auto-generated Spring Layout.")
        pos = nx.spring_layout(G, seed=42)
    else:
        if len(pos_dict) != len(G.nodes):
            print(f"Warning: Position count ({len(pos_dict)}) does not match Node count ({len(G.nodes)}).")
        pos = pos_dict

    # 2. Setup Figure and Axes
    fig, ax = plt.subplots(figsize=(12, 9))
    plt.subplots_adjust(bottom=0.2) # Leave room for buttons
    
    current_step = [0]
    
    # 3. Drawing Logic
    def draw_step(step_idx):
        ax.clear()
        state = history[step_idx]
        cops = state['cops']
        robber = state['robber']
        turn_text = state['turn']
        
        # Draw Base Graph
        nx.draw_networkx_edges(G, pos, ax=ax, edge_color='gray')
        nx.draw_networkx_nodes(G, pos, ax=ax, node_color='lightgray', node_size=200)
        nx.draw_networkx_labels(G, pos, ax=ax, font_size=7, font_weight='bold')
        
        # Highlight Players
        nx.draw_networkx_nodes(G, pos, nodelist=[robber], ax=ax, node_color='red', node_size=350, label='Robber')
        nx.draw_networkx_nodes(G, pos, nodelist=cops, ax=ax, node_color='blue', node_size=350, label='Cop')
        
        ax.set_title(f"Step {step_idx + 1}/{len(history)}: {turn_text}", fontsize=14, fontweight='bold')
        ax.legend(loc="upper right")
        ax.axis('off')
        fig.canvas.draw_idle()

    # Draw the initial state
    draw_step(0)

    # 4. Interactive Buttons
    axprev = plt.axes([0.35, 0.05, 0.1, 0.075])
    axnext = plt.axes([0.55, 0.05, 0.1, 0.075])
    bnext = Button(axnext, 'Next Turn')
    bprev = Button(axprev, 'Previous')

    def next_step(event):
        if current_step[0] < len(history) - 1:
            current_step[0] += 1
            draw_step(current_step[0])

    def prev_step(event):
        if current_step[0] > 0:
            current_step[0] -= 1
            draw_step(current_step[0])

    bnext.on_clicked(next_step)
    bprev.on_clicked(prev_step)

    plt.show()

def replay(matrix_filepath, json_filepath, pos_filepath=None):
    print(f"Loading board from: {matrix_filepath}")
    adj_matrix = parse_matrix(matrix_filepath)
    
    positions = None
    if pos_filepath:
        print(f"Loading positions from: {pos_filepath}")
        positions = parse_positions(pos_filepath)
        
        rows, _ = adj_matrix.shape
        if positions and len(positions) != rows:
            print(f"Warning: Position count ({len(positions)}) does not match Node count ({rows}).")
            
    print(f"Loading cached game from: {json_filepath}")
    try:
        with open(json_filepath, 'r') as f:
            cached_history = json.load(f)
    except FileNotFoundError:
        print("Error: JSON file not found. Did you run the solver first?")
        sys.exit(1)

    print("Launching unified interactive visualizer...")
    visualize_interactive(adj_matrix, cached_history, pos_dict=positions)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python replay_game.py <matrix_file> <json_file> [positions_file]")
        sys.exit(1)
        
    matrix_file = sys.argv[1]
    json_file = sys.argv[2]
    pos_file = sys.argv[3] if len(sys.argv) > 3 else None
        
    replay(matrix_file, json_file, pos_file)