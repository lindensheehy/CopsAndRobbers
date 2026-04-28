/**
 * ============================================================================
 * FILE --- k_cops_3.cpp
 * ============================================================================
 * * OVERVIEW
 * Solves the Cops and Robbers graph game using (A) a queue-based retrograde 
 * analysis algorithm, (B) bit-packing for a highly lean memory footprint, and 
 * (C) the generalized AuxGraph template for fast topology management.
 * * DEEPER DIVE
 * - Queue-Based Retrograde Analysis: Instead of iterating over the entire state 
 * space repeatedly, this version works strictly backwards from known winning 
 * states via a queue.
 * - Bit-Packing: The work queue tracks both the `stateId` and whose turn it is. 
 * The Most Significant Bit (MSB) of the `size_t` is hijacked to flag if it is 
 * the Robber's turn (1) or the Cop's turn (0).
 * - AuxGraph Integration: The Cartesian product generation and CSR lookup tables 
 * are handled entirely by AuxGraph, leaving this file to focus strictly on the 
 * retrograde queue logic and DP state definitions.
 * * PERFORMANCE METRICS (on scotlandyard-yellow with 3 cops)
 * - Memory -> 5.72 GB 
 * - Time -> 70 seconds
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include <iostream>
#include <vector>
#include <string>

// --- BIT-PACKING CONSTANTS ---
// MSB is 1 for Robber's turn, 0 for Cop's turn. 
// The rest of the bits hold the actual stateId.
constexpr size_t ROBBER_TURN_BIT = (size_t)1 << (sizeof(size_t) * 8 - 1);
constexpr size_t STATE_ID_MASK = ~ROBBER_TURN_BIT;

// --- DP STATE DEFINITION ---
struct DataItem {
    uint8_t copTurnWins : 1;
    uint8_t robberTurnWins : 1;
    uint8_t robberSafeMoves : 6;
};

// --- MAIN ALGORITHM ---

void solveCopsAndRobbers(Graph* g, int k, Profiler* p) {

    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    Allocator mem;
    mem.trackExternal("Graph (Adj Matrix)", g->getMemoryFootprint());

    // STEP 1 --- Adjacency List
    p->enter("Build Adjacency List");
    AdjacencyList adj(g);
    mem.trackExternal("Adjacency List (CSR)", adj.getMemoryFootprint());

    // STEP 2 --- Build Aux Graph & Queue DP Allocation
    p->enter("Build Aux Graph");
    AuxGraph<DataItem> aux(k, N, &adj, &mem);
    if (aux.configCount == 0) return;

    // STEP 3 --- Allocate Custom Queue & Commit Memory
    p->enter("Memory Allocation");
    size_t* workQueue = nullptr;
    size_t maxQueueSize = aux.numStates * 2; 

    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << aux.numStates << "\n";

    mem.requestAlloc("Analysis Work Queue", maxQueueSize, &workQueue);
    mem.allocate(); // Allocates both aux.states and workQueue cleanly

    p->enter("Print Memory Report");
    mem.print();

    // STEP 4 --- INITIALIZATION
    p->enter("Initialize Captures");
    size_t qWriteHead = 0;
    size_t qReadHead = 0;

    // Precompute robber degrees (+1 for the ability to stay in place)
    uint8_t robberDegrees[256];
    for (int r = 0; r < N; ++r) {
        int eCount = 1; 
        uint8_t* edges = adj.getEdges(r);
        while (edges[eCount - 1] != 255) eCount++;
        robberDegrees[r] = static_cast<uint8_t>(eCount);
    }

    int initialWins = 0;
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < N; ++r) {
            size_t stateId = aux.getStateId(cId, r);
            
            if (aux.isInstantCatch(cId, r)) {
                aux.states[stateId].copTurnWins = 1;
                aux.states[stateId].robberTurnWins = 1;
                aux.states[stateId].robberSafeMoves = 0; 
                
                // Pack the bits and push both turn states to the queue
                workQueue[qWriteHead++] = stateId;                     // Cop's turn (MSB 0)
                workQueue[qWriteHead++] = stateId | ROBBER_TURN_BIT;   // Robber's turn (MSB 1)
                initialWins++;
            } else {
                aux.states[stateId].robberSafeMoves = robberDegrees[r];
            }
        }
    }

    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Raw Array Retrograde Analysis Queue...\n";

    // STEP 5 --- MAIN RETROGRADE ANALYSIS LOOP
    p->enter("Backward Induction (Queue Loop)");
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
                
                aux.getCopTransitions(cId, copTransStart, copTransEnd);
                
                for (i = copTransStart; i < copTransEnd; ++i) {
                    prevStateId = aux.transitions[i] + r; 
                    
                    if (!aux.states[prevStateId].copTurnWins) {
                        aux.states[prevStateId].copTurnWins = 1;
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
                if (!aux.states[prevStateId].robberTurnWins) {
                    aux.states[prevStateId].robberSafeMoves--;
                    if (aux.states[prevStateId].robberSafeMoves == 0) {
                        aux.states[prevStateId].robberTurnWins = 1;
                        workQueue[qWriteHead++] = prevStateId | ROBBER_TURN_BIT; // Push Robber's turn (MSB 1)
                    }
                }

                // 2. Robber moved from an adjacent node
                rEdges = adj.getEdges(r);
                eIdx = 0;
                while (rEdges[eIdx] != 255) {
                    prevStateId = cId * N + rEdges[eIdx];
                    if (!aux.states[prevStateId].robberTurnWins) {
                        aux.states[prevStateId].robberSafeMoves--;
                        if (aux.states[prevStateId].robberSafeMoves == 0) {
                            aux.states[prevStateId].robberTurnWins = 1;
                            workQueue[qWriteHead++] = prevStateId | ROBBER_TURN_BIT; // Push Robber's turn (MSB 1)
                        }
                    }
                    eIdx++;
                }
            }
        }
        std::cout << "Queue empty. Processed " << qWriteHead << " winning state propagations.\n";
    }

    // STEP 6 --- FINAL VERDICT ---
    p->enter("Final Verdict Evaluation");
    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartConfigId = -1;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        bool universalWin = true;
        for (int rStart = 0; rStart < N; ++rStart) {
            size_t stateId = aux.getStateId(cId, rStart);
            if (!aux.states[stateId].copTurnWins) {
                universalWin = false;
                break;
            }
        }
        if (universalWin) {
            winningStartConfigId = cId;
            break;
        }
    }

    p->stop(); 

    if (winningStartConfigId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph.\n";
        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << (int)aux.configs[winningStartConfigId * k + i] << (i == k - 1 ? "" : ", ");
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

    Profiler p; 
    
    p.enter("Load Graph File");
    const char* filename = argv[1];
    int k = std::stoi(argv[2]);

    Graph g(filename);
    
    solveCopsAndRobbers(&g, k, &p);

    p.print(); 

    return 0;
}
