/**
 * ============================================================================
 * FILE --- k_cops_alternating.cpp
 * ============================================================================
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
#include <algorithm>

// --- DP STATE DEFINITION ---
struct DataItem {
    uint8_t marked : 1;
    uint8_t markedRound : 7;
};

// The maximum round count representable by the 7-bit bitfield
constexpr uint8_t MAX_ROUND_COUNT = 0b1111111;

const char* filename = nullptr;
int k = 0;          // Number of cops
int p = 0;          // Visibility fraction (e.g., p=2 means 1/2 visibility)
int columns = 0;    // Total DP columns. Will be initialized to 2 * p in loadGraphFile

Allocator mem;
AdjacencyList adj;
AuxGraph<DataItem> aux;


bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    filename = filename_param;
    k = k_param;
    p = p_param;
    columns = p * 2;

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
    std::string algoName = "k_cops_" + std::to_string(p) + "vis";

    // Set project configuration here
    SelfEdgeCop c_edge = SelfEdgeCop::FALSE;
    SelfEdgeRobber r_edge = SelfEdgeRobber::FALSE;

    bool failed = CacheManager::loadAuxGraph<DataItem>(algoName, filename, k, p, c_edge, r_edge, &aux, &mem, &adj);
    
    if (!failed) {
        std::cout << "Loaded AuxGraph from cache!\n";
        return 0;
    }

    std::cout << "Cache miss. Creating new AuxGraph with " << columns << " columns...\n";

    aux.setSelfEdges(c_edge, r_edge);
    aux.constructFrom(k, columns, &adj, &mem);
    
    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    return 0;
}

bool initializeCaptures() {

    /*
        This function should be correct. Verify if necessary
    */

    uint64_t initialWins = 0;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {
            
            bool caught = aux.isInstantCatch(cId, r);

            DataItem* state0 = aux.getState(cId, r, 0);
            DataItem* state1 = aux.getState(cId, r, 1);

            if (caught) {
                state0->marked = true;
                state0->markedRound = 0;
                state1->marked = true;
                state1->markedRound = 0;
                initialWins++;
            } else {
                state0->marked = false;
                state0->markedRound = MAX_ROUND_COUNT;
                state1->marked = false;
                state1->markedRound = MAX_ROUND_COUNT;
            }
            
        }
    }

    std::cout << "Initialized " << initialWins << " base captures.\n";

    return 0;

}

bool mainLoop() {

    std::cout << "Starting Backward Induction Loop for " << p << "-Visibility...\n";

    int passes = 0;
    int newWinsThisPass;
    size_t copTransStart; size_t copTransEnd;
    DataItem* state;
    DataItem* nextState;
    uint8_t* rEdges;
    std::vector<uint8_t> bfs_current(adj.nodeCount, 0);
    std::vector<uint8_t> bfs_next(adj.nodeCount, 0);

    while (true) {
        passes++;
        newWinsThisPass = 0;

        for (int col = 0; col < columns; ++col) {

            int next_col = (col + 1) % columns;

            bool isCopTurn = (col % 2 == 0);
            bool isBlindCopTurn = isCopTurn && (col > 0);

            // --- BLIND COP TURN (col > 0, even) ---
            // Cops cannot see the robber, so they must commit to a single transition
            // that works for ALL robber positions simultaneously.
            if (isBlindCopTurn) {

                for (size_t cId = 0; cId < aux.configCount; ++cId) {

                    // Early-out: skip if all (cId, *, col) already marked
                    bool skip = true;
                    for (int r = 0; r < adj.nodeCount; ++r) {
                        if (!aux.getState(cId, r, col)->marked) { skip = false; break; }
                    }
                    if (skip) continue;

                    aux.getCopTransitions(cId, copTransStart, copTransEnd);

                    // Find the best uniform transition: minimize worst-case markedRound across all r
                    size_t bestTransOffset = (size_t)-1;
                    uint8_t bestMax = 0;

                    for (size_t i = copTransStart; i < copTransEnd; ++i) {
                        size_t nextConfigN = aux.transitions[i];
                        bool allMarked = true;
                        uint8_t maxRound = 0;

                        for (int r = 0; r < adj.nodeCount; ++r) {
                            nextState = &(aux.states[(nextConfigN + r) * columns + next_col]);
                            if (!nextState->marked) { allMarked = false; break; }
                            if (nextState->markedRound > maxRound) maxRound = nextState->markedRound;
                        }

                        if (allMarked) {
                            if (bestTransOffset == (size_t)-1 || maxRound < bestMax) {
                                bestMax = maxRound;
                                bestTransOffset = nextConfigN;
                            }
                        }
                    }

                    if (bestTransOffset != (size_t)-1) {
                        for (int r = 0; r < adj.nodeCount; ++r) {
                            state = aux.getState(cId, r, col);
                            if (!state->marked) {
                                nextState = &(aux.states[(bestTransOffset + r) * columns + next_col]);
                                state->marked = true;
                                state->markedRound = nextState->markedRound + 1;
                                newWinsThisPass++;
                            }
                        }
                    }
                }

            }

            // --- VISIBLE COP TURN (col == 0) ---
            // Cops can see the robber, so they pick the best transition per r independently.
            else if (isCopTurn) {

                for (size_t cId = 0; cId < aux.configCount; ++cId) {

                    aux.getCopTransitions(cId, copTransStart, copTransEnd);

                    for (int r = 0; r < adj.nodeCount; ++r) {

                        state = aux.getState(cId, r, col);

                        if (state->marked) continue;

                        uint8_t minRounds = MAX_ROUND_COUNT;
                        bool winFound = false;

                        for (size_t i = copTransStart; i < copTransEnd; ++i) {
                            nextState = &(aux.states[(aux.transitions[i] + r) * columns + next_col]);

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

            // --- ROBBER'S TURN (odd cols) ---
            else {

                // Intermediate invisible robber turn: DIRECT IF.
                // The state has exactly one outgoing edge (incrementing the column), so the AND
                // node collapses to a single check against the same (cId, r) at the next column.
                if (col > 1 && col < columns - 1) {

                    for (size_t cId = 0; cId < aux.configCount; ++cId) {
                        for (int r = 0; r < adj.nodeCount; ++r) {

                            state = aux.getState(cId, r, col);
                            if (state->marked) continue;

                            nextState = aux.getState(cId, r, next_col);
                            if (nextState->marked) {
                                state->marked = true;
                                state->markedRound = nextState->markedRound + 1;
                                newWinsThisPass++;
                            }
                        }
                    }

                }

                // Visible robber turn (col == 1) or max col boundary (col == columns - 1):
                // Expand exactly (col+1)/2 hops from r (= ceil(col/2)) and AND-check all
                // reachable vertices against next_col. For p=1 these two cases coincide.
                else {

                    int depth = (col + 1) / 2;

                    for (size_t cId = 0; cId < aux.configCount; ++cId) {
                        for (int r = 0; r < adj.nodeCount; ++r) {

                            state = aux.getState(cId, r, col);
                            if (state->marked) continue;

                            // BFS: vertices reachable from r in exactly `depth` steps
                            std::fill(bfs_current.begin(), bfs_current.end(), 0);
                            bfs_current[r] = 1;

                            for (int step = 0; step < depth; step++) {
                                std::fill(bfs_next.begin(), bfs_next.end(), 0);
                                for (int v = 0; v < adj.nodeCount; v++) {
                                    if (!bfs_current[v]) continue;
                                    if (aux.robberSelfEdges == SelfEdgeRobber::TRUE) {
                                        bfs_next[v] = 1;
                                    }
                                    uint8_t* edges = adj.getEdges(v);
                                    for (int e = 0; edges[e] != 255; e++) {
                                        bfs_next[edges[e]] = 1;
                                    }
                                }
                                std::swap(bfs_current, bfs_next);
                            }

                            // AND check: cops must win at every vertex the robber could occupy
                            bool allMarked = true;
                            uint8_t maxRound = 0;

                            for (int v = 0; v < adj.nodeCount; v++) {
                                if (!bfs_current[v]) continue;
                                nextState = aux.getState(cId, v, next_col);
                                if (!nextState->marked) {
                                    allMarked = false;
                                    break;
                                }
                                if (nextState->markedRound > maxRound) maxRound = nextState->markedRound;
                            }

                            if (allMarked) {
                                state->marked = true;
                                state->markedRound = maxRound + 1;
                                newWinsThisPass++;
                            }
                        }
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
            
            DataItem* state = aux.getState(cId, r, 0);
            
            if (!state->marked) {
                universalWin = false;
                break; 
            }
            
            if (state->markedRound > worstCasePlys) {
                worstCasePlys = state->markedRound;
            }
        }

        if (universalWin && worstCasePlys < overallMinWorstCase) {
            overallMinWorstCase = worstCasePlys;
            bestCId = cId;
        }
    }

    if (bestCId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph with 1/" << p << " visibility.\n";
        
        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << (int)aux.configs[bestCId * k + i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";
        
        std::cout << "Capture Time: " << (int)overallMinWorstCase << " plys (" 
                  << (overallMinWorstCase / columns) << " full cycles).\n";

    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    mem.print();

    return 0;

}

bool outputData() {

    std::string algoName = "k_cops_" + std::to_string(p) + "vis";

    std::cout << "Saving filled AuxGraph to cache... ";
    
    bool failed = CacheManager::saveAuxGraph<DataItem>(algoName, filename, k, p, SelfEdgeCop::FALSE, SelfEdgeRobber::FALSE, &aux);
    
    if (failed) {
        std::cout << "Failed!\n";
        return 1;
    }

    std::cout << "Success!\n";

    return 0;

}
