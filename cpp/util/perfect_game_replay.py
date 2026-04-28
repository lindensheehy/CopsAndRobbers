import struct
import sys
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

# --- 1. BINARY UNPACKING & PATH EXTRACTION ---

def unpack_binary(filepath):
    print(f"Loading binary data from: {filepath}...")
    with open(filepath, 'rb') as f:
        header_bytes = f.read(80)
        header = struct.unpack('10Q', header_bytes)
        
        k, N, configCount, numStates, dataItemStride = [int(x) for x in header[:5]]
        offsets = [int(x) for x in header[5:]]

        f.seek(0)
        blob = f.read()

    configs = np.frombuffer(blob[offsets[0]:offsets[1]], dtype=np.uint8).reshape((configCount, k))
    trans_heads = np.frombuffer(blob[offsets[1]:offsets[2]], dtype=np.uint64)
    trans_data = np.frombuffer(blob[offsets[2]:offsets[3]], dtype=np.uint64)
    raw_states = np.frombuffer(blob[offsets[3]:offsets[4]], dtype=np.uint8)
    
    maxDegree = (len(blob) - offsets[4]) // N
    edges = np.frombuffer(blob[offsets[4]:], dtype=np.uint8).reshape((N, maxDegree))

    return k, N, configCount, configs, trans_heads, trans_data, raw_states, edges, dataItemStride

def get_state_data(raw_states, cId, r, turn, N, stride):
    idx = (cId * N + r) * 2 + turn
    byte_val = raw_states[idx * stride] 
    marked = bool(byte_val & 0x01)
    markedRound = byte_val >> 1 
    return marked, markedRound

def generate_history_from_bin(filepath):
    k, N, configCount, configs, trans_heads, trans_data, raw_states, edges, stride = unpack_binary(filepath)
    
    COPS_TURN, ROBBERS_TURN = 0, 1
    history = []
    
    # Find Best Start
    bestCId, overallMinWorstCase = -1, 255
    for cId in range(configCount):
        universalWin, worstCasePlys = True, 0
        for r in range(N):
            marked, markedRound = get_state_data(raw_states, cId, r, COPS_TURN, N, stride)
            if not marked:
                universalWin = False
                break
            if markedRound > worstCasePlys:
                worstCasePlys = markedRound
        if universalWin and worstCasePlys < overallMinWorstCase:
            overallMinWorstCase, bestCId = worstCasePlys, cId

    if bestCId == -1:
        print("RESULT: LOSS. No universal win found.")
        sys.exit(0)

    # Find Robber's Best Start
    bestRStart, maxSteps = -1, -1
    for r in range(N):
        marked, markedRound = get_state_data(raw_states, bestCId, r, COPS_TURN, N, stride)
        if markedRound > maxSteps:
            maxSteps, bestRStart = markedRound, r

    currCId, currRobber = bestCId, bestRStart
    print(f"Optimal Path Generated: {overallMinWorstCase} Plys to Capture.")

    # Walk the Path
    while True:
        # Check instant catch
        if currRobber in configs[currCId]:
            history.append({'cops': configs[currCId].tolist(), 'robber': int(currRobber), 'turn': 'Game Over - Captured!'})
            break
            
        history.append({'cops': configs[currCId].tolist(), 'robber': int(currRobber), 'turn': 'Cop Turn'})
        
        # Cop Move
        bestNextCId, minWorstCaseSteps = currCId, 999
        for i in range(trans_heads[currCId], trans_heads[currCId + 1]):
            nextCId = trans_data[i] // N
            if currRobber in configs[nextCId]:
                bestNextCId = nextCId
                break
            marked, markedRound = get_state_data(raw_states, nextCId, currRobber, ROBBERS_TURN, N, stride)
            if marked and markedRound < minWorstCaseSteps:
                minWorstCaseSteps, bestNextCId = markedRound, nextCId
                
        currCId = bestNextCId
        
        if currRobber in configs[currCId]:
            history.append({'cops': configs[currCId].tolist(), 'robber': int(currRobber), 'turn': 'Game Over - Captured!'})
            break

        history.append({'cops': configs[currCId].tolist(), 'robber': int(currRobber), 'turn': 'Robber Turn'})

        # Robber Move
        bestNextRobber, maxStepsR = currRobber, -1
        for nextR in edges[currRobber]:
            if nextR == 255: break
            marked, markedRound = get_state_data(raw_states, currCId, nextR, COPS_TURN, N, stride)
            if marked and markedRound > maxStepsR:
                maxStepsR, bestNextRobber = markedRound, nextR
                
        currRobber = bestNextRobber

    # Build NetworkX Graph directly from the binary edges
    G = nx.Graph()
    G.add_nodes_from(range(N))
    for u in range(N):
        for v in edges[u]:
            if v == 255: break
            G.add_edge(u, v)

    return G, history

# --- 2. VISUALIZATION ENGINE ---

def parse_positions(filepath):
    """Reads a text file containing coordinates 'x,y'."""
    coords = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('-'): continue
                parts = line.split(',')
                if len(parts) >= 2:
                    coords.append((float(parts[0]), float(parts[1])))
        
        if coords:
            max_y = max(c[1] for c in coords)
            return {i: (x, max_y - y) for i, (x, y) in enumerate(coords)}
        return None
    except FileNotFoundError:
        print(f"Warning: Position file '{filepath}' not found. Using auto-layout.")
        return None

def visualize_interactive(G, history, pos_dict=None):
    if pos_dict is None:
        print("No position file provided. Using auto-generated Spring Layout.")
        pos = nx.spring_layout(G, seed=42)
    else:
        if len(pos_dict) != len(G.nodes):
            print(f"Warning: Position count ({len(pos_dict)}) does not match Node count ({len(G.nodes)}).")
        pos = pos_dict

    fig, ax = plt.subplots(figsize=(12, 9))
    plt.subplots_adjust(bottom=0.2)
    current_step = [0]
    
    def draw_step(step_idx):
        ax.clear()
        state = history[step_idx]
        cops, robber, turn_text = state['cops'], state['robber'], state['turn']
        
        nx.draw_networkx_edges(G, pos, ax=ax, edge_color='gray')
        nx.draw_networkx_nodes(G, pos, ax=ax, node_color='lightgray', node_size=200)
        nx.draw_networkx_labels(G, pos, ax=ax, font_size=7, font_weight='bold')
        
        nx.draw_networkx_nodes(G, pos, nodelist=[robber], ax=ax, node_color='red', node_size=350, label='Robber')
        nx.draw_networkx_nodes(G, pos, nodelist=cops, ax=ax, node_color='blue', node_size=350, label='Cop')
        
        ax.set_title(f"Step {step_idx + 1}/{len(history)}: {turn_text}", fontsize=14, fontweight='bold')
        ax.legend(loc="upper right")
        ax.axis('off')
        fig.canvas.draw_idle()

    draw_step(0)

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

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python visualizer.py <bin_file> [positions_file]")
        sys.exit(1)
        
    bin_file = sys.argv[1]
    pos_file = sys.argv[2] if len(sys.argv) > 2 else None
        
    positions = parse_positions(pos_file) if pos_file else None
    G, history = generate_history_from_bin(bin_file)
    visualize_interactive(G, history, pos_dict=positions)