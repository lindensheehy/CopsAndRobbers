/**
 * ============================================================================
 * FILE --- k_cops_alternating_impl.cpp (Included via ALGORITHM_INCLUDE)
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

    bool failed = CacheManager::loadAuxGraph<DataItem>(algoName, filename, k, p, &aux, &mem, &adj);
    
    if (!failed) {
        std::cout << "Loaded AuxGraph from cache!\n";
        return 0;
    }

    std::cout << "Cache miss. Creating new AuxGraph with " << columns << " columns...\n";

    aux.constructFrom(k, columns, &adj, &mem);
    
    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    
    return 0;

}

bool initializeCaptures() {

    uint64_t initialWins = 0;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {
            
            bool caught = aux.isInstantCatch(cId, r);

            for (int col = 0; col < columns; ++col) {
                DataItem* state = aux.getState(cId, r, col);
                
                if (caught && col < 2) {
                    state->marked = true;
                    state->markedRound = 0;
                } else {
                    state->marked = false;
                    state->markedRound = MAX_ROUND_COUNT;
                }
            }

            if (caught) {
                initialWins++; 
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

    while (true) {
        passes++;
        newWinsThisPass = 0;

        for (int col = 0; col < columns; ++col) {
            
            int next_col = (col + 1) % columns;
            
            bool isCopTurn = (col % 2 == 0); 

            for (size_t cId = 0; cId < aux.configCount; ++cId) {
                
                aux.getCopTransitions(cId, copTransStart, copTransEnd);

                for (int r = 0; r < adj.nodeCount; ++r) {
                    
                    state = aux.getState(cId, r, col);
                    
                    if (state->marked) continue;

                    // --- COP'S TURN ---
                    if (isCopTurn) {
                        uint8_t minRounds = MAX_ROUND_COUNT;
                        bool winFound = false;

                        for (size_t i = copTransStart; i < copTransEnd; ++i) {
                            
                            // FIX: aux.transitions[i] is ALREADY pre-multiplied by N.
                            // We bypass getState() and access the raw states array directly.
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

                    // --- ROBBER'S TURN ---
                    else {
                        uint8_t maxRounds = 0;
                        bool canEscape = false;

                        nextState = aux.getState(cId, r, next_col);
                        if (!nextState->marked) {
                            canEscape = true;
                        } else {
                            maxRounds = nextState->markedRound;
                        }

                        if (!canEscape) {
                            rEdges = adj.getEdges(r);
                            for (int e = 0; rEdges[e] != 255; e++) {
                                nextState = aux.getState(cId, rEdges[e], next_col);
                                
                                if (!nextState->marked) {
                                    canEscape = true;
                                    break;
                                } else {
                                    if (nextState->markedRound > maxRounds) maxRounds = nextState->markedRound;
                                }
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
    
    bool failed = CacheManager::saveAuxGraph<DataItem>(algoName, filename, k, p, &aux);
    
    if (failed) {
        std::cout << "Failed!\n";
        return 1;
    }

    std::cout << "Success!\n";

    return 0;

}
