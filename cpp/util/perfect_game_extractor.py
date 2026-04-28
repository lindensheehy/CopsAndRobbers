import struct
import numpy as np
import sys

def unpack_binary(filepath):
    print(f"Loading {filepath}...")
    
    with open(filepath, 'rb') as f:
        # 1. Read the 80-byte header
        header_bytes = f.read(80)
        header = struct.unpack('10Q', header_bytes)
        
        k = int(header[0])
        N = int(header[1])
        configCount = int(header[2])
        numStates = int(header[3])
        dataItemStride = int(header[4])
        
        sec1_configs = int(header[5])
        sec2_heads = int(header[6])
        sec3_trans = int(header[7])
        sec4_states = int(header[8])
        sec5_edges = int(header[9])

        # Read the entire file into memory for slicing
        f.seek(0)
        blob = f.read()

    print(f"Header parsed: k={k}, N={N}, Configs={configCount}, Stride={dataItemStride} bytes")

    # 2. Reconstruct the arrays
    # Configs: configCount * k (uint8)
    configs = np.frombuffer(blob[sec1_configs:sec2_heads], dtype=np.uint8).reshape((configCount, k))
    
    # Transition Heads: configCount + 1 (uint64)
    trans_heads = np.frombuffer(blob[sec2_heads:sec3_trans], dtype=np.uint64)
    
    # Transitions: flat array (uint64)
    trans_data = np.frombuffer(blob[sec3_trans:sec4_states], dtype=np.uint64)
    
    # States: numStates * dataItemStride
    # Because of bitfields, we treat this as raw bytes and decode it
    raw_states = np.frombuffer(blob[sec4_states:sec5_edges], dtype=np.uint8)
    
    # Edges: N * maxDegree (uint8)
    maxDegree = (len(blob) - sec5_edges) // N
    edges = np.frombuffer(blob[sec5_edges:], dtype=np.uint8).reshape((N, maxDegree))

    return k, N, configCount, configs, trans_heads, trans_data, raw_states, edges, dataItemStride

def get_state_data(raw_states, cId, r, turn, N, stride):
    # Base index mapping: (cId * N + r) * AUXGRAPH_COLUMN_COUNT + turn
    idx = (cId * N + r) * 2 + turn
    
    # If your C++ stride was 1 byte, we read 1 byte. If padding made it 2, we adjust.
    byte_val = raw_states[idx * stride] 
    
    # C++ Bitfield extraction:
    # uint8_t marked : 1;
    # uint8_t markedRound : 7;
    # Usually, 'marked' occupies the Least Significant Bit (LSB).
    marked = bool(byte_val & 0x01)
    markedRound = byte_val >> 1 
    
    return marked, markedRound

def extract_perfect_game(filepath):
    k, N, configCount, configs, trans_heads, trans_data, raw_states, edges, stride = unpack_binary(filepath)
    
    COPS_TURN = 0
    ROBBERS_TURN = 1
    
    # --- FIND BEST STARTING POSITION ---
    bestCId = -1
    overallMinWorstCase = 255
    
    for cId in range(configCount):
        universalWin = True
        worstCasePlys = 0
        
        for r in range(N):
            marked, markedRound = get_state_data(raw_states, cId, r, COPS_TURN, N, stride)
            if not marked:
                universalWin = False
                break
            if markedRound > worstCasePlys:
                worstCasePlys = markedRound
                
        if universalWin and worstCasePlys < overallMinWorstCase:
            overallMinWorstCase = worstCasePlys
            bestCId = cId

    if bestCId == -1:
        print("RESULT: LOSS. No universal win found.")
        return

    print(f"\nOptimal Cop Start: {configs[bestCId]} | Max Plys: {overallMinWorstCase}")
    
    # --- FIND ROBBER'S BEST START ---
    bestRStart = -1
    maxSteps = -1
    for r in range(N):
        marked, markedRound = get_state_data(raw_states, bestCId, r, COPS_TURN, N, stride)
        if markedRound > maxSteps:
            maxSteps = markedRound
            bestRStart = r

    currCId = bestCId
    currRobber = bestRStart
    
    print("\n--- PERFECT GAME PATH ---")
    
    ply_count = 0
    while True:
        # Check for capture
        if currRobber in configs[currCId]:
            print(f"Ply {ply_count}: Game Over! Robber captured at node {currRobber}.")
            break
            
        print(f"Ply {ply_count} [Cop Turn]: Cops at {configs[currCId]} | Robber at {currRobber}")
        
        # --- COP TURN (Minimize Robber's Ply Count) ---
        t_start = trans_heads[currCId]
        t_end = trans_heads[currCId + 1]
        
        bestNextCId = currCId
        minWorstCaseSteps = 999
        
        for i in range(t_start, t_end):
            nextCId = trans_data[i] // N # Undo the * N optimization
            
            # Check instant catch
            if currRobber in configs[nextCId]:
                bestNextCId = nextCId
                break # Can't get better than instant catch
                
            marked, markedRound = get_state_data(raw_states, nextCId, currRobber, ROBBERS_TURN, N, stride)
            
            if marked and markedRound < minWorstCaseSteps:
                minWorstCaseSteps = markedRound
                bestNextCId = nextCId
                
        currCId = bestNextCId
        ply_count += 1
        
        if currRobber in configs[currCId]:
            print(f"Ply {ply_count}: Cops moved to {configs[currCId]} and CAPTURED the robber!")
            break

        print(f"Ply {ply_count} [Robber Turn]: Cops at {configs[currCId]} | Robber at {currRobber}")

        # --- ROBBER TURN (Maximize Ply Count) ---
        bestNextRobber = currRobber
        maxStepsR = -1
        
        for nextR in edges[currRobber]:
            if nextR == 255: # End of adjacency list
                break
                
            marked, markedRound = get_state_data(raw_states, currCId, nextR, COPS_TURN, N, stride)
            if marked and markedRound > maxStepsR:
                maxStepsR = markedRound
                bestNextRobber = nextR
                
        currRobber = bestNextRobber
        ply_count += 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python extract_path.py <path_to_bin_file>")
    else:
        extract_perfect_game(sys.argv[1])