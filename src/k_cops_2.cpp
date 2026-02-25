#include "Graph.h"
#include "AdjacencyList.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

#include <cstdint>

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
                        uint8_t*& outStateMemoryPool, uint8_t*& outCopTurnWins, uint8_t*& outRobberTurnWins) {
    size_t numStates = configCount * N;
    size_t poolSize = numStates * 2;

    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";
    std::cout << "Allocating State Memory Pool: " << poolSize / (1024.0 * 1024.0) << " MB\n";

    outStateMemoryPool = new uint8_t[poolSize];
    std::memset(outStateMemoryPool, 0, poolSize);

    outCopTurnWins = outStateMemoryPool;
    outRobberTurnWins = outStateMemoryPool + numStates;
}

// --- STEP 5: Initialize Captures ---
void initializeCaptures(size_t configCount, int k, int N, const uint8_t* configs, 
                        uint8_t* copTurnWins, uint8_t* robberTurnWins) {
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
                initialWins++;
            }
        }
    }

    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Backward Induction Loop...\n";
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
    allocateGameStates(configCount, k, N, stateMemoryPool, copTurnWins, robberTurnWins);

    // STEP 5 --- INITIALIZATION
    initializeCaptures(configCount, k, N, configs, copTurnWins, robberTurnWins);

    // STEP 6 --- MAIN BACKWARD INDUCTION LOOP
    {
        int passes = 0;
        int newWinsThisPass;
        size_t copTransStart; size_t copTransEnd;
        int r;
        size_t cId;
        size_t stateId;
        size_t baseStateId; // Added to remove multiplication in the robber loop
        bool canEscape;
        uint8_t* rEdges;
        int eIdx;
        size_t nextStateId;
        size_t i;

        while (true) {
            passes++;
            newWinsThisPass = 0;

            for (cId = 0; cId < configCount; ++cId) {
                
                copTransStart = transitionHeads[cId];
                copTransEnd = transitionHeads[cId + 1];
                stateId = cId * N;
                baseStateId = stateId; // Cache the base ID (cId * N)
                
                // 1. Initialize pointer to the 0th node's edges outside the 'r' loop
                rEdges = adj.getEdges(0); 

                for (r = 0; r < N; ++r) {

                    // --- RIGHT SIDE: Robber's Turn ---
                    if (!robberTurnWins[stateId]) {
                        canEscape = false;

                        // 1. Can the robber safely stay in place?
                        if (!copTurnWins[stateId]) {
                            canEscape = true;
                        } else {
                            // 2. Can the robber move to a safe neighbor?
                            for (eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                                // 3. Replaced (cId * N) with our cached baseStateId
                                nextStateId = baseStateId + rEdges[eIdx]; 
                                if (!copTurnWins[nextStateId]) {
                                    canEscape = true;
                                    break; 
                                }
                            }
                        }

                        if (!canEscape) {
                            robberTurnWins[stateId] = 1;
                            newWinsThisPass++;
                        }
                    }

                    // --- LEFT SIDE: Cop's Turn ---
                    if (!copTurnWins[stateId]) {
                        
                        for (i = copTransStart; i < copTransEnd; ++i) {
                            nextStateId = transitions[i] + r;
                            
                            if (robberTurnWins[nextStateId]) {
                                copTurnWins[stateId] = 1;
                                newWinsThisPass++;
                                break; 
                            }
                        }

                    }
                
                    stateId++;
                    
                    // 4. Advance the edge pointer by exactly one stride for the next 'r'
                    rEdges += adj.maxDegree; 

                }
            }

            std::cout << "Pass " << passes << ": Found " << newWinsThisPass << " new winning states.\n";

            if (newWinsThisPass == 0) break;

        }
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
