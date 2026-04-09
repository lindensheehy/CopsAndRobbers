/**
 * ============================================================================
 * FILE --- k_cops_2.cpp
 * ============================================================================
 * * OVERVIEW
 * Solves the Cops and Robbers graph game with improved performance and reduced 
 * overhead. It does this using (A) exact combinatorial calculation and iterative 
 * state generation, (B) a flat Compressed Sparse Row (CSR) format for transition 
 * lookup, and (C) direct pointer arithmetic and caching in the induction loop.
 * * DEEPER DIVE
 * - STL Removal: `std::vector` overhead is largely eliminated in favor of 
 * contiguous `uint8_t` arrays, drastically reducing fragmentation and 
 * improving cache locality.
 * - Exact Allocation: Instead of dynamic resizing, the state space size is 
 * pre-calculated using combinations with replacement. Memory is allocated upfront.
 * - CSR Transitions: Team moves are packed into `transitionHeads` and 
 * `transitions`. Instead of chasing pointers in a vector-of-vectors, the cops' 
 * moves for configuration `cId` are found sequentially between 
 * `transitionHeads[cId]` and `transitionHeads[cId + 1]`.
 * - Loop Optimization: The backward induction loop caches `cId * N` and utilizes 
 * pointer striding (`rEdges += adj.maxDegree`) to evaluate the robber's moves 
 * without redundant multiplication or array indexing.
 * * PERFORMANCE METRICS (on scotlandyard-yellow with 3 cops)
 * - Memory -> 1.82 GB 
 * - Time -> 150 seconds
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "copconfig.h"
#include "Allocator.h"
#include "Profiler.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <iomanip>

// --- PROCEDURAL HELPERS ---

/**
 * Builds a Compressed Sparse Row (CSR) representation of all possible team moves.
 * Determines every valid transition for every configuration and packs the target 
 * state IDs into a 1D array, using a 'heads' array to track starting indices.
 */
void buildTransitions(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                      std::vector<size_t>& outTransitionHeads, std::vector<size_t>& outTransitions, Allocator* allocator) {
    
    outTransitionHeads.assign(configCount + 1, 0);
    outTransitions.clear();
    outTransitions.reserve(configCount * 8); 

    std::vector<size_t> tempMoves;
    tempMoves.reserve(1024); 
    size_t peakTempMovesCapacity = 0;

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

        peakTempMovesCapacity = std::max(peakTempMovesCapacity, tempMoves.capacity());
        
        outTransitions.insert(outTransitions.end(), tempMoves.begin(), tempMoves.end());
        outTransitionHeads[cId + 1] = outTransitions.size();
    }

    // --- Register the externally managed vector allocations ---
    if (allocator != nullptr) {
        outTransitionHeads.shrink_to_fit();
        outTransitions.shrink_to_fit();

        size_t headsBytes = outTransitionHeads.size() * sizeof(size_t);
        size_t transitionsBytes = outTransitions.size() * sizeof(size_t);
        size_t peakTempBytes = peakTempMovesCapacity * sizeof(size_t);
        
        allocator->trackExternal("outTransitionHeads", headsBytes, outTransitionHeads.data());
        allocator->trackExternal("outTransitions", transitionsBytes, outTransitions.data());
        
        // Pass nullptr for the address, since tempMoves will be destroyed right after this function ends
        allocator->trackExternal("tempMoves (Peak Buffer)", peakTempBytes, nullptr);
    }

    std::cout << "Transitions generated. Total edge pointers: " << outTransitions.size() << "\n";
}

/**
 * Scans through all configurations and robber positions to identify states
 * where the robber starts on a node already occupied by a cop, marking them
 * as instant wins for the cops.
 */
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

void solveCopsAndRobbers(Graph* g, int k, Profiler* p) {

    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    Allocator mem;

    mem.trackExternal("Graph (Adj Matrix)", g->getMemoryFootprint());

    // STEP 1 --- Create an adjacency list out of the adjacency matrix for faster iteration
    p->enter("Build Adjacency List");
    AdjacencyList adj(g);
    mem.trackExternal("Adjacency List (CSR)", adj.getMemoryFootprint());

    // STEP 2 --- Generate all unique, sorted cop configurations
    p->enter("Generate Cop Configs");
    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount, &mem);
    if (!configs || configCount == 0) return;

    // STEP 3 --- Pre-calculate all team transitions (CSR Format)
    p->enter("Build Transitions");
    std::vector<size_t> transitionHeads;
    std::vector<size_t> transitions;
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions, &mem);

    // STEP 4 --- Allocate flat arrays for game states
    p->enter("Memory Allocation");
    uint8_t* copTurnWins = nullptr;
    uint8_t* robberTurnWins = nullptr;
    
    size_t numStates = configCount * N;
    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";

    mem.requestAlloc("Cop Turn Wins", numStates, &copTurnWins);
    mem.requestAlloc("Robber Turn Wins", numStates, &robberTurnWins);
    mem.allocate();

    p->enter("Print Memory Report");
    mem.print();

    // STEP 5 --- INITIALIZATION
    p->enter("Initialize Captures");
    initializeCaptures(configCount, k, N, configs, copTurnWins, robberTurnWins);

    // STEP 6 --- MAIN BACKWARD INDUCTION LOOP
    p->enter("Backward Induction (Main Loop)");
    {
        int passes = 0;
        int newWinsThisPass;
        size_t copTransStart; size_t copTransEnd;
        int r;
        size_t cId;
        size_t stateId;
        size_t baseStateId; 
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
                baseStateId = stateId; 
                
                rEdges = adj.getEdges(0); 

                for (r = 0; r < N; ++r) {

                    // --- RIGHT SIDE: Robber's Turn ---
                    if (!robberTurnWins[stateId]) {
                        canEscape = false;

                        if (!copTurnWins[stateId]) {
                            canEscape = true;
                        } else {
                            for (eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
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
                    rEdges += adj.maxDegree; 
                }
            }

            std::cout << "Pass " << passes << ": Found " << newWinsThisPass << " new winning states.\n";

            if (newWinsThisPass == 0) break;
        }
    }

    // STEP 7 --- FINAL VERDICT ---
    p->enter("Final Verdict Evaluation");
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

    p->stop(); // Stops the tracker before we execute the final IO print statements

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
}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        std::cout << "Example: " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    Profiler p; // Fire up the profiler at the absolute start
    
    p.enter("Load Graph File");
    const char* filename = argv[1];
    int k = std::stoi(argv[2]);

    Graph g(filename);
    
    solveCopsAndRobbers(&g, k, &p);

    p.print(); // Dump the timing metrics right before exiting

    return 0;
}
