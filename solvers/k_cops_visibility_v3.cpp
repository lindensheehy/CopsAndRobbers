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
#include <climits>
#include <cstdint>

struct DataItem {
    uint8_t marked : 1;
    uint8_t markedRound : 7;
};

constexpr uint8_t MAX_ROUND_COUNT = 0b1111111;

const char* filename = nullptr;
int k = 0;          // Number of cops
int p = 0;          // Visibility fraction (e.g., p=2 means 1/2 visibility)
int N = 0;          // Node count in the graph

Allocator mem;
AdjacencyList adj;
AuxGraph<DataItem> aux;

bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    filename = filename_param;
    k = k_param;
    p = p_param;

    Graph g(filename);

    if (g.nodeCount == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }

    if (g.nodeCount > 255) {
        std::cerr << "Error: graph exceeds the 255-vertex format limit.\n";
        return 1;
    }

    N = g.nodeCount;

    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    return 0;

}

bool buildAuxGraph() {

    aux.setSelfEdges(SelfEdgeCop::FALSE, SelfEdgeRobber::FALSE);

    aux.constructFrom(k, 1, &adj, &mem);

    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    
    return 0;

}

bool initializeCaptures() {

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {

            DataItem* state = aux.getState(cId, r, 0);
            state->marked = false;
            state->markedRound = MAX_ROUND_COUNT;

            if (aux.isInstantCatch(cId, r)) {
                state->marked = true;
                state->markedRound = 0;
            }

        }
    }

    return 0;

}

bool mainLoop() {

    std::cout << "Starting Main Loop...\n";

    int passes = 0;

    // Macro Iteration
    while (true) {
        passes++;

        // Per node in the column
        for (size_t cId = 0; cId < aux.configCount; ++cId) {

            const uint8_t* cops = &aux.configs[cId * k];
            bool prepared = false;
            bool skipUnchanged = false;

            struct StackItem {
                // cop config
                // robber set
                // next transition
            };

            // add node to stack

            int bestDepth = INT_MAX;

            // while (stack.size() > 0) {  // stack is not empty

                // Read top of stack

                // if stack.size() == p*2
                    // search in one go. for all
                    
                    // if forced win
                        // bestDepth == p*2
                        // pop from stack
                        continue;

                    // else
                        // pop from stack
                        continue;

                // else

                    // get next transition from current

                    // trim robber set

                    // if set is empty
                        // if (currentDepth < bestDepth) bestDepth = currentDepth;
                        // pop from stack
                        continue;

                    // if currentDepth >= bestdepth
                        // pop from stack
                        continue;

                    // find first transition of new node
                    // push to stack

            //}
            
        }
    }

    return 0;
}
