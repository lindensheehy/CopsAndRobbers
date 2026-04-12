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
 * - Memory -> 1.76 GB 
 * - Time -> 85 seconds
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

// --- DP STATE DEFINITION ---
struct DataItem {
    uint8_t copTurnWins : 1;
    uint8_t robberTurnWins : 1;
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

    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << aux.numStates << "\n";

    // Commit allocations (Populates aux.states natively)
    mem.allocate();

    p->enter("Print Memory Report");
    mem.print();

    // STEP 3 --- INITIALIZATION
    p->enter("Initialize Captures");
    int initialWins = 0;
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < N; ++r) {
            if (aux.isInstantCatch(cId, r)) {
                size_t stateId = aux.getStateId(cId, r);
                aux.states[stateId].copTurnWins = 1;
                aux.states[stateId].robberTurnWins = 1;
                initialWins++;
            }
        }
    }
    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Backward Induction Loop...\n";

    // STEP 4 --- MAIN BACKWARD INDUCTION LOOP
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

            for (cId = 0; cId < aux.configCount; ++cId) {
                
                aux.getCopTransitions(cId, copTransStart, copTransEnd);
                stateId = aux.getStateId(cId, 0);
                baseStateId = stateId; 
                
                rEdges = adj.getEdges(0); 

                for (r = 0; r < N; ++r) {

                    // --- RIGHT SIDE: Robber's Turn ---
                    if (!aux.states[stateId].robberTurnWins) {
                        canEscape = false;

                        if (!aux.states[stateId].copTurnWins) {
                            canEscape = true;
                        } else {
                            for (eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                                nextStateId = baseStateId + rEdges[eIdx]; 
                                if (!aux.states[nextStateId].copTurnWins) {
                                    canEscape = true;
                                    break; 
                                }
                            }
                        }

                        if (!canEscape) {
                            aux.states[stateId].robberTurnWins = 1;
                            newWinsThisPass++;
                        }
                    }

                    // --- LEFT SIDE: Cop's Turn ---
                    if (!aux.states[stateId].copTurnWins) {
                        for (i = copTransStart; i < copTransEnd; ++i) {
                            nextStateId = aux.transitions[i] + r;
                            
                            if (aux.states[nextStateId].robberTurnWins) {
                                aux.states[stateId].copTurnWins = 1;
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

    // STEP 5 --- FINAL VERDICT ---
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
