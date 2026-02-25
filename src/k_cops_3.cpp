#include "Graph.h"
#include "AdjacencyList.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

#include <cstdint>

// MSB is 1 for Robber's turn, 0 for Cop's turn. 
// The rest of the bits hold the stateId.
constexpr size_t ROBBER_TURN_BIT = (size_t)1 << (sizeof(size_t) * 8 - 1);
constexpr size_t STATE_ID_MASK = ~ROBBER_TURN_BIT;

// --- PROCEDURAL HELPERS ---

// Generates all unique sorted cop configurations iteratively.
// outNumConfigs is passed by reference so the caller knows exactly how many states exist.
constexpr size_t MAX_COPS = 256;
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    
    // Failsafe for stack array size
    if (k > MAX_COPS) {
        std::cerr << "FATAL: Number of cops (k) exceeds maximum supported limit of " << MAX_COPS << ".\n";
        *outNumConfigs = 0;
        return nullptr;
    }

    // 1. Calculate exact state space size (Combinations with replacement)
    {
        int n_val = N + k - 1;
        int k_val = k;
        
        if (k_val > n_val) {
            *outNumConfigs = 0;
        } else if (k_val == 0 || k_val == n_val) {
            *outNumConfigs = 1;
        } else {
            if (k_val > n_val / 2) k_val = n_val - k_val;
            size_t res = 1;
            for (int i = 1; i <= k_val; ++i) {
                res = res * (n_val - i + 1) / i;
            }
            *outNumConfigs = res;
        }
    }
    
    // 2. Print memory footprint
    size_t totalBytes = (*outNumConfigs) * k;
    std::cout << "Allocating " << totalBytes / (1024.0 * 1024.0) 
              << " MB for " << *outNumConfigs << " cop configurations...\n";

    // 3. Allocate exact flat array
    uint8_t* configs = new uint8_t[totalBytes];
    
    // 4. Initialize the first configuration on the stack: [0, 0, ..., 0]
    uint8_t current[MAX_COPS];
    memset(current, 0, MAX_COPS);
    
    size_t offset = 0;
    
    // 5. Iteratively generate the next lexicographical combination
    while(true) {
        
        // Write the current configuration directly to our flat array
        memcpy(&(configs[offset]), current, k);
        offset += k;
        
        // Find the rightmost element that can be incremented
        int p = k - 1;
        while(p >= 0 && current[p] == N - 1) {
            p--;
        }
        
        // If all elements are N-1, we are done
        if (p < 0) break; 
        
        // Increment the found element
        current[p]++;
        
        // Set all subsequent elements to match this new value (maintaining sorted order)
        for(uint32_t i = p + 1; i < k; ++i) {
            current[i] = current[p];
        }

    }
    
    return configs;

}

// --- STEP 3: Build CSR Transitions ---
void buildTransitions(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                      std::vector<size_t>& outTransitionHeads, std::vector<size_t>& outTransitions) {
    
    outTransitionHeads.assign(configCount + 1, 0);
    outTransitions.clear();
    outTransitions.reserve(configCount * 8); 

    std::vector<size_t> tempMoves;
    tempMoves.reserve(1024); 

    std::cout << "Building transition table for " << configCount << " configurations...\n";

    uint8_t options[MAX_COPS][256];
    int optionCount[MAX_COPS];
    int odometer[MAX_COPS];
    uint8_t moveConfig[MAX_COPS];

    for (size_t cId = 0; cId < configCount; cId++) {
        tempMoves.clear(); 
        const uint8_t* currentCops = &configs[cId * k];
        
        for (int i = 0; i < k; i++) {
            uint8_t u = currentCops[i];
            options[i][0] = u; 
            int count = 1;
            
            uint8_t* edges = adj.getEdges(u);
            int eIdx = 0;
            while (edges[eIdx] != 255) {
                options[i][count++] = edges[eIdx++];
            }
            optionCount[i] = count;
        }

        memset(odometer, 0, MAX_COPS * sizeof(int));
        
        while (true) {
            for (int i = 0; i < k; ++i) {
                moveConfig[i] = options[i][odometer[i]];
            }
            
            std::sort(moveConfig, moveConfig + k);
            size_t nextId = static_cast<size_t>(-1); 
            size_t left = 0;
            size_t right = configCount - 1;
            while (left <= right) {
                size_t mid = left + (right - left) / 2;
                int cmp = std::memcmp(&configs[mid * k], moveConfig, k);
                if (cmp == 0) {
                    nextId = mid;
                    break;
                }
                if (cmp < 0) {
                    left = mid + 1;
                } else {
                    right = mid - 1;
                }
            }
            
            // Pre-multiplied by N for optimization
            tempMoves.push_back(nextId * N);
            
            int p = k - 1;
            while (p >= 0) {
                odometer[p]++;
                if (odometer[p] < optionCount[p]) break;
                odometer[p] = 0;
                p--;
            }
            if (p < 0) break;
        }

        std::sort(tempMoves.begin(), tempMoves.end());
        tempMoves.erase(std::unique(tempMoves.begin(), tempMoves.end()), tempMoves.end());
        
        outTransitions.insert(outTransitions.end(), tempMoves.begin(), tempMoves.end());
        outTransitionHeads[cId + 1] = outTransitions.size();
    }

    std::cout << "Transitions generated. Total edge pointers: " << outTransitions.size() << "\n";
}

// --- STEP 4: Allocate Game States ---
void allocateGameStates(size_t configCount, int k, int N, 
                        uint8_t*& outStateMemoryPool, uint8_t*& outCopTurnWins, 
                        uint8_t*& outRobberTurnWins, uint8_t*& outRobberSafeMoves) {
    size_t numStates = configCount * N;
    size_t poolSize = numStates * 3; // Now 3 bytes per state

    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";
    std::cout << "Allocating State Memory Pool: " << poolSize / (1024.0 * 1024.0) << " MB\n";

    outStateMemoryPool = new uint8_t[poolSize];
    std::memset(outStateMemoryPool, 0, poolSize);

    outCopTurnWins = outStateMemoryPool;
    outRobberTurnWins = outStateMemoryPool + numStates;
    outRobberSafeMoves = outStateMemoryPool + (numStates * 2);
}

// --- STEP 5: Initialize Captures ---
void initializeCaptures(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                        uint8_t* copTurnWins, uint8_t* robberTurnWins, uint8_t* robberSafeMoves,
                        size_t* workQueue, size_t& qWriteHead) {
    
    // Precompute robber degrees (+1 for the ability to stay in place)
    uint8_t robberDegrees[256];
    for (int r = 0; r < N; ++r) {
        int eCount = 1; 
        uint8_t* edges = adj.getEdges(r);
        while (edges[eCount - 1] != 255) eCount++;
        robberDegrees[r] = static_cast<uint8_t>(eCount);
    }

    int initialWins = 0;
    const uint8_t* currentCops;
    size_t stateId;
    bool caught;
    
    for (size_t cId = 0; cId < configCount; ++cId) {
        currentCops = &configs[cId * k];
        
        for (int r = 0; r < N; ++r) {
            stateId = cId * N + r;
            
            caught = false;
            for (int i = 0; i < k; ++i) {
                if (currentCops[i] == r) {
                    caught = true;
                    break;
                }
            }
            
            if (caught) {
                copTurnWins[stateId] = 1;
                robberTurnWins[stateId] = 1;
                robberSafeMoves[stateId] = 0; 
                
                // Pack the bits and push
                workQueue[qWriteHead++] = stateId;                     // Cop's turn (MSB 0)
                workQueue[qWriteHead++] = stateId | ROBBER_TURN_BIT;   // Robber's turn (MSB 1)
                initialWins++;
            } else {
                robberSafeMoves[stateId] = robberDegrees[r];
            }
        }
    }

    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Raw Array Retrograde Analysis Queue...\n";
}

// --- MAIN ALGORITHM ---
void solveCopsAndRobbers(Graph* g, int k) {

    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    // STEP 1 --- Create an adjacency list out of the adjacency matrix for faster iteration in the main loop
    AdjacencyList adj(g);

    // STEP 2 --- Generate all unique, sorted cop configurations
    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount);
    if (!configs || configCount == 0) return; // <-- Add this!

    // STEP 3 --- Pre-calculate all team transitions (CSR Format)
    std::vector<size_t> transitionHeads;
    std::vector<size_t> transitions;
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions);

    // STEP 4 --- Allocate flat arrays for game states
    uint8_t* stateMemoryPool = nullptr;
    uint8_t* copTurnWins = nullptr;
    uint8_t* robberTurnWins = nullptr;
    uint8_t* robberSafeMoves = nullptr;
    allocateGameStates(configCount, k, N, stateMemoryPool, copTurnWins, robberTurnWins, robberSafeMoves);

    // Setup the Raw Array Queue (now just a flat array of size_t)
    size_t numStates = configCount * N;
    size_t maxQueueSize = numStates * 2;
    std::cout << "Allocating Queue: " << maxQueueSize / (1024.0 * 1024.0) << " MB\n";
    size_t* workQueue = new size_t[maxQueueSize];
    size_t qWriteHead = 0;
    size_t qReadHead = 0;

    // STEP 5 --- INITIALIZATION
    initializeCaptures(configCount, k, N, configs, adj, copTurnWins, robberTurnWins, robberSafeMoves, workQueue, qWriteHead);

    // STEP 6 --- MAIN RETROGRADE ANALYSIS LOOP
    {
        size_t cId;
        int r;
        size_t copTransStart, copTransEnd, i;
        size_t prevStateId;
        uint8_t* rEdges;
        int eIdx;

        while (qReadHead < qWriteHead) {
            // Unpack the node
            size_t packedNode = workQueue[qReadHead++];
            bool isRobberTurn = (packedNode & ROBBER_TURN_BIT) != 0;
            size_t stateId = packedNode & STATE_ID_MASK;
            
            cId = stateId / N;
            r = stateId % N;

            if (isRobberTurn) {
                // STATE: Cops won, Robber's turn.
                copTransStart = transitionHeads[cId];
                copTransEnd = transitionHeads[cId + 1];
                
                for (i = copTransStart; i < copTransEnd; ++i) {
                    prevStateId = transitions[i] + r; 
                    
                    if (!copTurnWins[prevStateId]) {
                        copTurnWins[prevStateId] = 1;
                        workQueue[qWriteHead++] = prevStateId; // Push Cop's turn (MSB 0)
                    }
                }
            } 
            else {
                // STATE: Cops won, Cops' turn.
                
                // 1. Robber stayed in place
                prevStateId = cId * N + r;
                if (!robberTurnWins[prevStateId]) {
                    robberSafeMoves[prevStateId]--;
                    if (robberSafeMoves[prevStateId] == 0) {
                        robberTurnWins[prevStateId] = 1;
                        workQueue[qWriteHead++] = prevStateId | ROBBER_TURN_BIT; // Push Robber's turn (MSB 1)
                    }
                }

                // 2. Robber moved from adjacent
                rEdges = adj.getEdges(r);
                eIdx = 0;
                while (rEdges[eIdx] != 255) {
                    prevStateId = cId * N + rEdges[eIdx];
                    if (!robberTurnWins[prevStateId]) {
                        robberSafeMoves[prevStateId]--;
                        if (robberSafeMoves[prevStateId] == 0) {
                            robberTurnWins[prevStateId] = 1;
                            workQueue[qWriteHead++] = prevStateId | ROBBER_TURN_BIT; // Push Robber's turn (MSB 1)
                        }
                    }
                    eIdx++;
                }
            }
        }
        std::cout << "Queue empty. Processed " << qWriteHead << " winning state propagations.\n";
    }

    // STEP 7 --- FINAL VERDICT ---
    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartConfigId = -1;

    for (size_t cId = 0; cId < configCount; ++cId) {
        bool universalWin = true;
        for (int rStart = 0; rStart < N; ++rStart) {
            size_t stateId = cId * N + rStart;
            if (!copTurnWins[stateId]) {
                universalWin = false;
                break;
            }
        }
        if (universalWin) {
            winningStartConfigId = cId;
            break;
        }
    }

    if (winningStartConfigId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph.\n";
        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << (int)configs[winningStartConfigId * k + i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";
    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    // --- CLEANUP ---
    delete[] configs;
    delete[] stateMemoryPool;
    delete[] workQueue;
}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        std::cout << "Example: " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    const char* filename = argv[1];
    int k = std::stoi(argv[2]);

    Graph g(filename);
    
    solveCopsAndRobbers(&g, k);

    return 0;
    
}
