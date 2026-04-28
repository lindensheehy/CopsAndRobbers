/*

--- k_cops_2.cpp ---
Base cops and robbers game


EXAMPLE RUN (scotlandyard-all with 3 cops)

--- FINAL VERDICT ---
RESULT: WIN. 3 Cop(s) CAN win this graph.
Optimal Cop Start Positions: (46, 110, 127)

||>>>>>=====-----=====<<<<<     Memory Tracking Report     >>>>>=====-----=====<<<<<
||
||   Total Footprint -------------------------=>   2156925848 B / 2057.00 MB /  2.01 GB (100.00%)
||    -> Managed Internally -------------=>    534653300 B /  509.89 MB /  0.50 GB (24.79%)
||    -> Tracked Externally -------------=>   1622272548 B / 1547.12 MB /  1.51 GB (75.21%)
||
||
||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---
||
||   Managed Internally ----------------------=>    534653300 B /  509.89 MB /  0.50 GB
||
||    -> Arena Block 1 ------------------=>      3999900 B /    3.81 MB /  0.00 GB (0.19%)
||      -> Cop Configs -------------=>      3999900 B /    3.81 MB /  0.00 GB
||
||    -> Arena Block 2 ------------------=>    530653400 B /  506.07 MB /  0.49 GB (24.60%)
||      -> AuxGraph Per State Data -=>    530653400 B /  506.07 MB /  0.49 GB
||
||   Tracked Externally ----------------------=>   1622272548 B / 1547.12 MB /  1.51 GB
||    -> tempMoves (Peak Buffer) --------=>        65536 B /    0.06 MB /  0.00 GB (0.00%)
||    -> AuxGraph: Edges ----------------=>   1611538200 B / 1536.88 MB /  1.50 GB (74.71%)
||    -> AuxGraph: Heads ----------------=>     10666408 B /   10.17 MB /  0.01 GB (0.49%)
||    -> Graph Adj List -----------------=>         2404 B /    0.00 MB /  0.00 GB (0.00%)
||
||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<


||>>>>>=====-----=====<<<<<     Timing Profiler Report     >>>>>=====-----=====<<<<<
||
||   Total App Uptime ---------------=>      68.9374 s (100.00%)
||    -> Tracked Execution -----=>      68.9374 s (100.00%)
||
||
||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---
||
||  -> Load Graph ---------------=>       0.0058 s (  0.01%)
||  -> Idle ---------------------=>       0.0000 s (  0.00%)
||  -> Build Aux Graph ----------=>      19.9404 s ( 28.93%)
||  -> Initialize Captures ------=>       0.2973 s (  0.43%)
||  -> Main Loop ----------------=>      48.6912 s ( 70.63%)
||  -> Final Verdict Evaluation -=>       0.0027 s (  0.00%)
||
||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<

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

    struct {
        uint8_t marked : 1;
        uint8_t markedRound : 7;
    } copTurn;

    struct {
        uint8_t marked : 1;
        uint8_t markedRound : 7;
    } robberTurn;

};


Allocator mem;
AdjacencyList adj;
AuxGraph<DataItem> aux;

int winningStartConfigId = -1;


bool loadGraphFile(const char* filename) {

    Graph g(filename);

    if (g.nodeCount == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }

    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    return 0;

}

bool buildAuxGraph(int k) {
    
    aux.constructFrom(k, &adj, &mem);
    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    
    return 0;

}

bool initializeCaptures() {

    int initialWins = 0;
    DataItem* state;
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {

            if (aux.isInstantCatch(cId, r)) {
                state = aux.getState(cId, r);
                state->copTurn.marked = true;
                state->robberTurn.marked = true;
                initialWins++;
            }

        }
    }

    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";

    return 0;

}

bool mainLoop() {
    
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
                if (!state->robberTurn.marked && state->copTurn.marked) {

                    canEscape = false;
                    for (i = 0; rEdges[i] != 255; i++) {
                        nextState = aux.getState(cId, rEdges[i]);
                        if (!nextState->copTurn.marked) {
                            canEscape = true;
                            break; 
                        }
                    }

                    if (!canEscape) {
                        state->robberTurn.marked = 1;
                        newWinsThisPass++;
                    }
                }

                // --- LEFT SIDE: Cop's Turn ---
                if (!state->copTurn.marked) {
                    for (i = copTransStart; i < copTransEnd; ++i) {
                        nextState = &(aux.states[aux.transitions[i] + r]);
                        if (nextState->robberTurn.marked) {
                            state->copTurn.marked = 1;
                            newWinsThisPass++;
                            break; 
                        }
                    }
                }

                // NEW: If, after both turns, the cops STILL haven't secured a win 
                // from this state, then this cId is not a universal win on this pass.
                if (!state->copTurn.marked) {
                    universalWinForCId = false;
                }
            }

            if (universalWinForCId) {
                winningStartConfigId = cId;
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

    return 0;

}

bool findFinalResult(int k) {

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

    mem.print();

    return 0;

}
