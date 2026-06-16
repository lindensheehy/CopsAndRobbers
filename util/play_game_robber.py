import sys
import struct
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

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

def parse_positions(filepath):
    coords = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('-'): continue
            parts = line.split(',')
            if len(parts) >= 2: coords.append((float(parts[0]), float(parts[1])))
    pos_dict = {}
    if coords:
        max_y = max(c[1] for c in coords)
        for i, (x, y) in enumerate(coords):
            pos_dict[i] = (x, max_y - y)
    return pos_dict

class CopsAndRobbersGame:
    def __init__(self, bin_file, pos_file=None):
        self.k, self.N, self.configCount, self.configs, \
        self.trans_heads, self.trans_data, self.raw_states, \
        self.edges, self.stride = unpack_binary(bin_file)

        self.COPS_TURN = 0
        self.ROBBERS_TURN = 1
        self.rounds = 0
        self.history = [] # Stack for undo functionality

        # Build adjacency dictionary for UI validation
        self.adj = {i: [] for i in range(self.N)}
        for i in range(self.N):
            for neighbor in self.edges[i]:
                if neighbor == 255: break
                self.adj[i].append(int(neighbor))
            # Ensure self-loops exist so the robber can pass their turn
            if i not in self.adj[i]:
                self.adj[i].append(i)

        self.pos_dict = parse_positions(pos_file) if pos_file else None
        
        print("Brain loaded. Setting up the board...")
        self.setup_board()

    def setup_board(self):
        bestCId = -1
        overallMinWorstCase = 255

        for cId in range(self.configCount):
            universalWin = True
            worstCasePlys = 0
            for r in range(self.N):
                marked, markedRound = get_state_data(self.raw_states, cId, r, self.COPS_TURN, self.N, self.stride)
                if not marked:
                    universalWin = False
                    break
                if markedRound > worstCasePlys:
                    worstCasePlys = markedRound
            if universalWin and worstCasePlys < overallMinWorstCase:
                overallMinWorstCase = worstCasePlys
                bestCId = cId

        if bestCId == -1:
            print("Error: Cops cannot guarantee a win on this graph.")
            sys.exit(0)

        # Cache the optimal starting layout for quick resets
        self.initial_cId = bestCId
        self.currCId = self.initial_cId
        self.cops = tuple(self.configs[self.currCId])
        self.robber = None
        self.phase = "PLACE_ROBBER" 
        self.history.clear()
        self.rounds = 0
        
        self.G = nx.Graph()
        self.G.add_nodes_from(range(self.N))
        for u in range(self.N):
            for v in self.adj[u]:
                self.G.add_edge(u, v)

        if not self.pos_dict:
            self.pos_dict = nx.spring_layout(self.G, seed=42)

        self.fig, self.ax = plt.subplots(figsize=(12, 9))
        plt.subplots_adjust(bottom=0.2) # Make room for buttons
        
        # Setup Interactive Buttons
        ax_reset = plt.axes([0.3, 0.05, 0.15, 0.075])
        ax_undo = plt.axes([0.55, 0.05, 0.15, 0.075])
        self.b_reset = Button(ax_reset, 'Reset Game')
        self.b_undo = Button(ax_undo, 'Go Back (Undo)')
        
        self.b_reset.on_clicked(self.reset_game)
        self.b_undo.on_clicked(self.undo_move)

        self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.draw_board()
        plt.show()

    def save_state(self):
        """Snapshots the current board before a move is made."""
        self.history.append({
            'currCId': self.currCId,
            'cops': self.cops,
            'robber': self.robber,
            'phase': self.phase,
            'rounds': self.rounds
        })

    def undo_move(self, event):
        """Pops the last state off the stack and re-renders."""
        if not self.history:
            print("Nothing to undo!")
            return
            
        state = self.history.pop()
        self.currCId = state['currCId']
        self.cops = state['cops']
        self.robber = state['robber']
        self.phase = state['phase']
        self.rounds = state['rounds']
        self.draw_board()

    def reset_game(self, event):
        """Wipes history and returns to placement phase."""
        self.currCId = self.initial_cId
        self.cops = tuple(self.configs[self.currCId])
        self.robber = None
        self.phase = "PLACE_ROBBER"
        self.rounds = 0
        self.history.clear()
        self.draw_board()

    def draw_board(self):
        self.ax.clear()
        nx.draw_networkx_edges(self.G, self.pos_dict, ax=self.ax, edge_color='gray')
        nx.draw_networkx_nodes(self.G, self.pos_dict, ax=self.ax, node_color='lightgray', node_size=200)
        nx.draw_networkx_labels(self.G, self.pos_dict, ax=self.ax, font_size=7, font_weight='bold')

        if self.cops:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=list(self.cops), ax=self.ax, node_color='blue', node_size=350, label='Cops')
        if self.robber is not None:
            nx.draw_networkx_nodes(self.G, self.pos_dict, nodelist=[self.robber], ax=self.ax, node_color='red', node_size=350, label='Robber')

        title = ""
        if self.phase == "PLACE_ROBBER":
            title = "Game Start: Click ANY node to place your Robber!"
        elif self.phase == "ROBBER_TURN":
            title = f"Your Turn: Click an adjacent node to move (Ply: {self.rounds})"
        elif self.phase == "GAME_OVER":
            title = f"GAME OVER: The AI Caught You in {self.rounds} Plys."

        self.ax.set_title(title, fontsize=14, fontweight='bold')
        self.ax.axis('off')
        self.fig.canvas.draw_idle()

    def get_closest_node(self, x, y):
        best_node = None
        min_dist = float('inf')
        for node, (nx_val, ny_val) in self.pos_dict.items():
            dist = (x - nx_val)**2 + (y - ny_val)**2
            if dist < min_dist:
                min_dist = dist
                best_node = node
        return best_node if min_dist < 2000 else None

    def on_click(self, event):
        # Ignore clicks if it's not the board area or if game is over
        if event.inaxes != self.ax or self.phase == "GAME_OVER" or event.xdata is None or event.ydata is None:
            return
        
        clicked_node = self.get_closest_node(event.xdata, event.ydata)
        if clicked_node is None: return

        if self.phase == "PLACE_ROBBER":
            self.save_state() # Save before the first placement
            self.robber = clicked_node
            self.phase = "ROBBER_TURN"
            self.trigger_ai_cops()
            
        elif self.phase == "ROBBER_TURN":
            if clicked_node in self.adj[self.robber]:
                self.save_state() # Save before you commit your move
                self.robber = clicked_node
                self.rounds += 1
                self.trigger_ai_cops()
            else:
                print("Invalid Move: You must click an adjacent node (or your own node to stay put).")

    def trigger_ai_cops(self):
        if self.robber in self.cops:
            self.phase = "GAME_OVER"
            self.draw_board()
            return
            
        print("AI is moving...")
        bestNextCId = self.currCId
        minWorstCaseSteps = 999
        
        t_start = self.trans_heads[self.currCId]
        t_end = self.trans_heads[self.currCId + 1]
        
        for i in range(t_start, t_end):
            nextCId = self.trans_data[i] // self.N
            
            # Instant catch override
            if self.robber in self.configs[nextCId]:
                bestNextCId = nextCId
                break
                
            marked, markedRound = get_state_data(self.raw_states, nextCId, self.robber, self.ROBBERS_TURN, self.N, self.stride)
            if marked and markedRound < minWorstCaseSteps:
                minWorstCaseSteps = markedRound
                bestNextCId = nextCId
                
        self.currCId = bestNextCId
        self.cops = tuple(self.configs[self.currCId])
        self.rounds += 1
        
        if self.robber in self.cops:
            self.phase = "GAME_OVER"
        else:
            self.phase = "ROBBER_TURN"
            
        self.draw_board()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python play_game.py <bin_file> [positions_file]")
        sys.exit(1)
        
    b_file = sys.argv[1]
    p_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    game = CopsAndRobbersGame(b_file, p_file)