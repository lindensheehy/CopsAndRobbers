/**
 * ============================================================================
 * FILE --- k_cops_3.cpp
 * ============================================================================
 * * OVERVIEW
 * Solves the Cops and Robbers graph game using (A) a queue-based retrograde 
 * analysis algorithm, (B) bit-packing for a highly lean memory footprint, and 
 * (C) pre-calculated CSR transitions for fast adjacency lookups.
 * * DEEPER DIVE
 * - Queue-Based Retrograde Analysis: Instead of iterating over the entire state 
 * space repeatedly until no changes occur, this version works strictly backwards 
 * from known winning states. When a state is confirmed as a win for the cops, 
 * it goes into the queue to propagate that win to its predecessors.
 * - Bit-Packing: The work queue needs to track both the `stateId` and whose turn 
 * it is. To avoid struct padding overhead and maintain a purely flat `size_t` 
 * array, the Most Significant Bit (MSB) of the `size_t` is hijacked to flag if 
 * it is the Robber's turn (1) or the Cop's turn (0).
 * - Algorithmic Flow:
 * 1. Pop a confirmed winning state from the queue.
 * 2. If it was the Robber's turn (MSB 1): The preceding Cop turns that can 
 * reach this state are automatically winning states. Flag them and push.
 * 3. If it was the Cop's turn (MSB 0): The preceding Robber turns that could 
 * have moved here lose one of their "safe escape routes". Decrement their 
 * safe move counter. If it hits 0, the Robber is trapped—flag it and push.
 * * PERFORMANCE METRICS (on scotlandyard-yellow with 3 cops)
 * - Memory -> 6.12 GB 
 * - Time -> 60 seconds
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "copconfig.h"
#include "Allocator.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <iomanip>

// --- BIT-PACKING CONSTANTS ---
// MSB is 1 for Robber's turn, 0 for Cop's turn. 
// The rest of the bits hold the actual stateId.
constexpr size_t ROBBER_TURN_BIT = (size_t)1 << (sizeof(size_t) * 8 - 1);
constexpr size_t STATE_ID_MASK = ~ROBBER_TURN_BIT;

// --- PROCEDURAL HELPERS ---

/**
 * Builds a Compressed Sparse Row (CSR) representation of all possible team moves.
 */
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

/**
 * Identifies immediate capture states (robber and cop share a node).
 * Flags them, sets safe moves to 0, and immediately pushes them to the queue 
 * with the appropriate turn bits set to bootstrap the retrograde loop.
 */
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
                
                // Pack the bits and push both turn states to the queue
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

    // STEP 1 --- Adjacency List
    AdjacencyList adj(g);

    // STEP 2 --- Cop Configurations
    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount);
    if (!configs || configCount == 0) return;

    double configsMB = static_cast<double>(configCount * k * sizeof(uint8_t)) / (1024.0 * 1024.0);
    std::cout << "[Memory] configs array: " << std::fixed << std::setprecision(2) << configsMB << " MB\n";

    // STEP 3 --- CSR Transitions
    std::vector<size_t> transitionHeads;
    std::vector<size_t> transitions;
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions);

    double transitionsMB = static_cast<double>((transitionHeads.capacity() + transitions.capacity()) * sizeof(size_t)) / (1024.0 * 1024.0);
    std::cout << "[Memory] transitions CSR: " << std::fixed << std::setprecision(2) << transitionsMB << " MB\n";

    // STEP 4 --- Allocate Game States & Queue via Arena Allocator
    Allocator mem;
    uint8_t* copTurnWins = nullptr;
    uint8_t* robberTurnWins = nullptr;
    uint8_t* robberSafeMoves = nullptr;
    size_t* workQueue = nullptr;
    
    size_t numStates = configCount * N;
    size_t maxQueueSize = numStates * 2;
    
    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";

    mem.requestAlloc("Cop Turn Wins", numStates, &copTurnWins);
    mem.requestAlloc("Robber Turn Wins", numStates, &robberTurnWins);
    mem.requestAlloc("Robber Safe Moves", numStates, &robberSafeMoves);
    mem.requestAlloc("Analysis Work Queue", maxQueueSize, &workQueue);
    
    mem.allocate();
    mem.print(); // Fully automatic alignment and tracking for uint8_t and size_t

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
                // STATE: Cops won, and it was the Robber's turn.
                // LOGIC: The Cops' previous turn is guaranteed to be a win if they 
                // simply CHOOSE to transition to this state.
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
                // STATE: Cops won, and it was the Cops' turn.
                // LOGIC: The Robber's previous turn loses a safe escape route because 
                // stepping here allows the cops to force a win.
                
                // 1. Robber stayed in place
                prevStateId = cId * N + r;
                if (!robberTurnWins[prevStateId]) {
                    robberSafeMoves[prevStateId]--;
                    if (robberSafeMoves[prevStateId] == 0) {
                        robberTurnWins[prevStateId] = 1;
                        workQueue[qWriteHead++] = prevStateId | ROBBER_TURN_BIT; // Push Robber's turn (MSB 1)
                    }
                }

                // 2. Robber moved from an adjacent node
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
    // Allocator automatically deletes the Game States and Work Queue arrays
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