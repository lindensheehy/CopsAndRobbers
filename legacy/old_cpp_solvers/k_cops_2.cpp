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

EXAMPLE RUN (scotlandyard-all with 3 cops)
||>>>>>=====-----=====<<<<<     Memory Tracking Report     >>>>>=====-----=====<<<<<
||
||   Total Footprint -------------------------=>   1891599148 B / 1803.97 MB /  1.76 GB (100.00%)
||    -> Managed Internally -------------=>    269326600 B /  256.85 MB /  0.25 GB (14.24%)
||    -> Tracked Externally -------------=>   1622272548 B / 1547.12 MB /  1.51 GB (85.76%)
||
||
||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---
||
||   Managed Internally ----------------------=>    269326600 B /  256.85 MB /  0.25 GB
||
||    -> Arena Block 1 ------------------=>      3999900 B /    3.81 MB /  0.00 GB (0.21%)
||      -> Cop Configs -------------=>      3999900 B /    3.81 MB /  0.00 GB
||
||    -> Arena Block 2 ------------------=>    265326700 B /  253.04 MB /  0.25 GB (14.03%)
||      -> AuxGraph Per State Data -=>    265326700 B /  253.04 MB /  0.25 GB
||
||   Tracked Externally ----------------------=>   1622272548 B / 1547.12 MB /  1.51 GB
||    -> tempMoves (Peak Buffer) --------=>        65536 B /    0.06 MB /  0.00 GB (0.00%)
||    -> AuxGraph: Edges ----------------=>   1611538200 B / 1536.88 MB /  1.50 GB (85.19%)
||    -> AuxGraph: Heads ----------------=>     10666408 B /   10.17 MB /  0.01 GB (0.56%)
||    -> Graph Adj List -----------------=>         2404 B /    0.00 MB /  0.00 GB (0.00%)
||
||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<


||>>>>>=====-----=====<<<<<     Timing Profiler Report     >>>>>=====-----=====<<<<<
||
||   Total App Uptime ---------------=>      68.7470 s (100.00%)
||    -> Tracked Execution -----=>      68.7470 s (100.00%)
||
||
||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---
||
||  -> Load Graph ---------------=>       0.0003 s (  0.00%)
||  -> Idle ---------------------=>       0.0471 s (  0.07%)
||  -> Build Aux Graph ----------=>      21.1096 s ( 30.71%)
||  -> Initialize Captures ------=>       0.2985 s (  0.43%)
||  -> Main Loop ----------------=>      47.2891 s ( 68.79%)
||  -> Final Verdict Evaluation -=>       0.0002 s (  0.00%)
||  -> Print Memory Report ------=>       0.0023 s (  0.00%)
||
||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<

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
void solveCopsAndRobbers(const char* filename, int k, Profiler* p) {

    Allocator mem;


    /* --- Load Graph File --- */
    AdjacencyList adj;
    {
        p->enter("Load Graph");

        Graph g(filename);

        if (g.nodeCount == 0) {
            std::cerr << "Error: Graph is empty or failed to load.\n";
            return;
        }

        adj.constructFrom(&g);
        mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

        p->enter("Idle");
    }


    /* --- Build Aux Graph --- */
    AuxGraph<DataItem> aux;
    {
        p->enter("Build Aux Graph");

        aux.constructFrom(k, &adj, &mem);
        if (aux.configCount == 0) {
            std::cerr << "Error: Unable to generate aux graph.\n";
            return;
        }

        p->enter("Idle");
    }


    /* --- Initialize Captures --- */
    {
        p->enter("Initialize Captures");

        int initialWins = 0;
        DataItem* state;
        for (size_t cId = 0; cId < aux.configCount; ++cId) {
            for (int r = 0; r < adj.nodeCount; ++r) {

                if (aux.isInstantCatch(cId, r)) {
                    state = aux.getState(cId, r);
                    state->copTurnWins = 1;
                    state->robberTurnWins = 1;
                    initialWins++;
                }

            }
        }

        std::cout << "Initialized " << initialWins << " winning states (Captures).\n";

        p->enter("Idle");
    }


    /* --- Main Loop --- */
    int winningStartConfigId = -1;
    int captureRounds = -1;
    {
        p->enter("Main Loop");

        std::cout << "Starting Backward Induction Loop...\n";

        // Loop variables
        int passes = 0;
        int newWinsThisPass;
        size_t copTransStart; size_t copTransEnd;
        DataItem* state;
        DataItem* nextState;
        uint8_t* rEdges;
        bool canEscape;
        bool universalWinForCId;

        // Iterator variables
        size_t cId;
        int r;
        size_t i;

        while (true) {
            passes++;
            newWinsThisPass = 0;

            for (cId = 0; cId < aux.configCount; ++cId) {
                
                aux.getCopTransitions(cId, copTransStart, copTransEnd);
                universalWinForCId = true;
                
                for (r = 0; r < adj.nodeCount; ++r) {
                    
                    state = aux.getState(cId, r);
                    rEdges = adj.getEdges(r);

                    // --- RIGHT SIDE: Robber's Turn ---
                    if (!state->robberTurnWins && state->copTurnWins) {

                        canEscape = false;
                        for (i = 0; rEdges[i] != 255; i++) {
                            nextState = aux.getState(cId, rEdges[i]);
                            if (!nextState->copTurnWins) {
                                canEscape = true;
                                break; 
                            }
                        }

                        if (!canEscape) {
                            state->robberTurnWins = 1;
                            newWinsThisPass++;
                        }
                    }

                    // --- LEFT SIDE: Cop's Turn ---
                    if (!state->copTurnWins) {
                        for (i = copTransStart; i < copTransEnd; ++i) {
                            nextState = &(aux.states[aux.transitions[i] + r]);
                            if (nextState->robberTurnWins) {
                                state->copTurnWins = 1;
                                newWinsThisPass++;
                                break; 
                            }
                        }
                    }

                    // NEW: If, after both turns, the cops STILL haven't secured a win 
                    // from this state, then this cId is not a universal win on this pass.
                    if (!state->copTurnWins) {
                        universalWinForCId = false;
                    }
                }

                if (universalWinForCId) {
                    winningStartConfigId = cId;
                    captureRounds = passes;
                    break;
                }
            }

            if (winningStartConfigId != -1) {
                std::cout << "Pass " << passes << ": Optimal capture strategy found!\n";
                break; 
            }

            std::cout << "Pass " << passes << ": Found " << newWinsThisPass << " new winning states.\n";

            if (newWinsThisPass == 0) break;
        }

        p->enter("Idle");
    }


    /* --- Find Final Result --- */
    {
        p->enter("Final Verdict Evaluation");

        std::cout << "\n--- FINAL VERDICT ---\n";

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

        p->enter("Idle");
    }


    p->enter("Print Memory Report");
    mem.print();
    p->enter("Idle");

    return;

}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {
    
    Profiler p;

    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        std::cout << "Example: " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    const char* filename = argv[1];
    int k = std::stoi(argv[2]);
    
    solveCopsAndRobbers(filename, k, &p);

    p.print();

    return 0;

}
