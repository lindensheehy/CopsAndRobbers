/*

--- k_cops_2.cpp ---
Base cops and robbers game


EXAMPLE RUN (scotlandyard-all with 3 cops)

Executing: build/bin/k_cops.exe ..\assets\matrices\scotlandyard-all.txt 3
Building AuxGraph transition table for 1333300 configurations...
Transitions generated. Total edge pointers: 201442275
Initialized 3960100 winning states (Captures).
Starting Backward Induction Loop...
Pass 1: Found 17221861 new winning states.
Pass 2: Found 3468726 new winning states.
Pass 3: Found 6852216 new winning states.
Pass 4: Found 18410647 new winning states.
Pass 5: Found 47716524 new winning states.
Pass 6: Found 111329893 new winning states.
Pass 7: Found 182097918 new winning states.
Pass 8: Found 120116589 new winning states.
Pass 9: Found 15285707 new winning states.
Pass 10: Found 233119 new winning states.
Pass 11: Found 0 new winning states.

--- FINAL VERDICT ---
RESULT: WIN. 3 Cop(s) CAN win this graph.
Optimal Cop Start Positions: (12, 57, 139)
Capture Time: 9 plys (4 full rounds).

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


Preparing data for binary export...
Successfully packed 2056.94 MB into the binary blob.

||>>>>>=====-----=====<<<<<     Timing Profiler Report     >>>>>=====-----=====<<<<<
||
||   Total App Uptime ---------------=>     214.2649 s (100.00%)
||    -> Tracked Execution -----=>     214.2649 s (100.00%)
||
||
||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---
||
||  -> Load Graph File -----=>       0.0004 s (  0.00%)
||  -> Idle ----------------=>       0.0000 s (  0.00%)
||  -> Build Aux Graph -----=>      20.8602 s (  9.74%)
||  -> Initialize Captures -=>       0.3320 s (  0.15%)
||  -> Main Loop -----------=>     190.9133 s ( 89.10%)
||  -> Find Final Result ---=>       0.2229 s (  0.10%)
||  -> Output Data ---------=>       1.9362 s (  0.90%)
||
||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<

--- k_cops.exe finished in 214.5038 seconds ---

 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include "CacheManager.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>


// --- DP STATE DEFINITION ---
struct DataItem {
    uint8_t marked : 1;
    uint8_t markedRound : 7;
};

constexpr int P_VALUE = 1;
constexpr int AUXGRAPH_COLUMN_COUNT = P_VALUE * 2;
constexpr int COPS_TURN = 0;
constexpr int ROBBERS_TURN = 1;

constexpr uint8_t MAX_ROUND_COUNT = 0b1111111;


const char* filename;
int k;
Allocator mem;
AdjacencyList adj;
AuxGraph<DataItem> aux;


bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    filename = filename_param;
    k = k_param;

    Graph g(filename);

    if (g.nodeCount == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }

    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    return 0;

}

bool buildAuxGraph() {

    bool failed = CacheManager::loadAuxGraph<DataItem>("k_cops", filename, k, P_VALUE, &aux, &mem, &adj);
    if (!failed) {
        std::cout << "Loaded AuxGraph from cache!\n";
        return 0;
    }

    std::cout << "Creating new empty AuxGraph.\n";

    aux.constructFrom(k, AUXGRAPH_COLUMN_COUNT, &adj, &mem);
    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    
    return 0;

}

bool initializeCaptures() {

    uint64_t initialWins = 0;
    DataItem* stateCops;
    DataItem* stateRobbers;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {

            stateCops = aux.getState(cId, r, COPS_TURN);
            stateRobbers = aux.getState(cId, r, ROBBERS_TURN);

            if (aux.isInstantCatch(cId, r)) {

                stateCops->marked = true;
                stateCops->markedRound = 0;
                stateRobbers->marked = true;
                stateRobbers->markedRound = 0;

                initialWins++;

            }

            else {

                stateCops->marked = false;
                stateCops->markedRound = MAX_ROUND_COUNT;
                stateRobbers->marked = false;
                stateRobbers->markedRound = MAX_ROUND_COUNT;

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

    // Iterator variables
    size_t cId;
    int r;
    size_t i;

    while (true) {
        passes++;
        newWinsThisPass = 0;

        for (cId = 0; cId < aux.configCount; ++cId) {
            
            aux.getCopTransitions(cId, copTransStart, copTransEnd);

            // --- LEFT SIDE: Cop's Turn ---
            for (r = 0; r < adj.nodeCount; ++r) {

                state = aux.getState(cId, r, COPS_TURN);
                rEdges = adj.getEdges(r);

                if (!state->marked) {

                    uint8_t minRounds = MAX_ROUND_COUNT;
                    bool winFound = false;

                    for (i = copTransStart; i < copTransEnd; ++i) {

                        nextState = &(aux.states[(aux.transitions[i] + r) * AUXGRAPH_COLUMN_COUNT + ROBBERS_TURN]);

                        if (nextState->marked) {
                            winFound = true;
                            if (nextState->markedRound < minRounds) minRounds = nextState->markedRound;
                        }

                    }

                    if (winFound) {
                        state->marked = true;
                        state->markedRound = minRounds + 1;
                        newWinsThisPass++;
                    }

                }

            }

        }

        // --- RIGHT SIDE: Robber's Turn ---
        for (cId = 0; cId < aux.configCount; ++cId) {
            
            for (r = 0; r < adj.nodeCount; ++r) {
                
                state = aux.getState(cId, r, ROBBERS_TURN);
                rEdges = adj.getEdges(r);

                if (!state->marked && aux.getState(cId, r, COPS_TURN)->marked) {

                    uint8_t maxRounds = 0;
                    bool canEscape = false;

                    for (i = 0; rEdges[i] != 255; i++) {
                        nextState = aux.getState(cId, rEdges[i], COPS_TURN);
                        if (!nextState->marked) {
                            canEscape = true;
                            break; 
                        }
                        else {
                            if (maxRounds < nextState->markedRound) maxRounds = nextState->markedRound;
                        }
                    }

                    if (!canEscape) {
                        state->marked = true;
                        state->markedRound = maxRounds + 1;
                        newWinsThisPass++;
                    }
                    
                }

            }

        }

        std::cout << "Pass " << passes << ": Found " << newWinsThisPass << " new winning states.\n";

        if (newWinsThisPass == 0) break;

    }

    return 0;

}

bool findFinalResult() {

    std::cout << "\n--- FINAL VERDICT ---\n";

    int bestCId = -1;
    uint8_t overallMinWorstCase = MAX_ROUND_COUNT;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        
        bool universalWin = true;
        uint8_t worstCasePlys = 0;

        for (int r = 0; r < adj.nodeCount; ++r) {
            
            DataItem* state = aux.getState(cId, r, COPS_TURN);
            
            // If the cops can't win from this configuration against a robber at 'r',
            // this cId is not a universal winning start.
            if (!state->marked) {
                universalWin = false;
                break; 
            }
            
            // Track the robber's longest possible survival time against this cId
            if (state->markedRound > worstCasePlys) {
                worstCasePlys = state->markedRound;
            }
        }

        // If this cId guarantees a win, and does so faster than our previous best...
        if (universalWin && worstCasePlys < overallMinWorstCase) {
            overallMinWorstCase = worstCasePlys;
            bestCId = cId;
        }
    }

    if (bestCId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph.\n";
        
        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << (int)aux.configs[bestCId * k + i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";
        
        std::cout << "Capture Time: " << (int)overallMinWorstCase << " plys (" 
                  << (overallMinWorstCase / 2) << " full rounds).\n";

    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    mem.print();

    return 0;

}

bool outputData() {

    std::cout << "Saving filled AuxGraph to cache... ";
    bool failed = CacheManager::saveAuxGraph<DataItem>("k_cops", filename, k, P_VALUE, &aux);
    if (failed) {
        std::cout << "Failed!\n";
        return 1;
    }

    std::cout << "Success!\n";

    return 0;

}
