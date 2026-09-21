#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "VertexBitField.h"
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

    for (size_t cId = 0; cId < aux.configCount; cId++) {
        for (int r = 0; r < adj.nodeCount; r++) {

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
        for (size_t cId = 0; cId < aux.configCount; cId++) {
            for (uint8_t r = 0; r < N; r++) {

                // Skip already marked nodes
                if (aux.getState(cId, r, 1)->marked) continue;

                struct StackItem {
                    size_t cId;
                    VertexBitField robberSet;
                    size_t nextTransition;
                    size_t lastTransition;
                };

                std::vector<StackItem> stack;
                stack.reserve(p);

                // Get robber set
                VertexBitField robberSet;
                robberSet.set(r);

                // Get range of cop transitions to iterate through
                size_t firstTransitionOut;
                size_t lastTransitionOut;
                aux.getCopTransitions(cId, firstTransitionOut, lastTransitionOut);

                // Add to stack to start the search from this root
                stack.push_back({
                    cId,
                    robberSet,
                    firstTransitionOut,
                    lastTransitionOut
                });

                int bestDepth = INT_MAX;

                while (stack.size() > 0) {

                    // Get top of stack
                    StackItem& current = stack.back();

                    // If we have iterated over all transitions from the current top of the stack
                    if (current.nextTransition > current.lastTransition) {
                        stack.pop_back();
                        continue;
                    }

                    // Get next transition from current
                    size_t nextcId = aux.transitions[current.nextTransition];
                    current.nextTransition++;

                    VertexBitField newRobberSet = current.robberSet;

                    /*
                        Simulate cops turn

                        In this step, we let the cops play one possible move from the current tip of the DFS search. This effectively runs the search to one more depth than where the tip last was.

                        Here we trim the last robber set based on the new cop positions.

                        Either:
                        The new robbers set is empty. In this case, we have found a winning path. We mark the node. We save the depth we are currently at (since it is necessarily <= the current bestDepth), and continue with the rest of the DFS
                        OR...
                        The new robbers set is not empty. No win was found on this iteration. We pass through to simulating the robbers turn
                    */
                    {
                        
                        // Trim robber set
                        uint8_t* copPositions = &(aux.configs[nextcId]);
                        for (int i = 0; i < k; i++) {
                            newRobberSet.reset(copPositions[i]);
                        }

                        // If the robber set is empty
                        if (newRobberSet.none()) {
                            bestDepth = stack.size() * 2;
                            stack.pop_back();
                            continue;
                        }
                    }
                    
                    /*
                        Simulate robbers turn
                        
                        Because at this point the robber set must NOT be empty, we then 
                        simulate the robbers turn.
                        
                        This is where we either:

                        Expand the robbers set with all possible next moves (for a column in the middle of the aux graph)
                        OR...
                        Play the robbers last invisible turn, and compute the for all in 
                        the last column transition
                    */
                    {
                        // If its a leaf node (last cop turn column)
                        // This means the robber becomes now VISIBLE on this 2 ply turn (cop moves, then robber moves, they are now visible)
                        // We need to check the for all condition, as normal
                        if (stack.size() == p) {

                            // Search possible robber transitions from this leaf
                            // If "for all" holds, we can mark this node
                            
                            // If we can mark it
                            {   
                                // Record this as the best found depth
                                bestDepth = p * 2; 
                            }
                            
                            // Pop from stack
                            stack.pop_back();

                            // Go to the next node in the stack
                            continue;
                            
                        // Its not a leaf node (middle column)
                        // One to one mapping, no "for all" to be done
                        } else {
                            
                            // Expand the robbers set based on reachable nodes from the current set

                        }
                    }

                    /*
                        The core DFS loop logic

                        At this point, we know:
                        - The cops did not catch the robber on this search path (1 turn of cops moving and then robbers moving)
                        - The robber set is not empty
                        - The current node does not wrap around to the first column in that range of 2 ply (cops turn and robbers turn)

                        So our job is to take the new node we just found, find the transitions it must iterate over for its own recursive DFS, then push it to the stack
                    */

                    // If the next depth, AFTER this node we just checked, will be worse than the best depth we found already, we dont add a new node to the stack
                    if ((stack.size() * 2) + 1 >= bestDepth) {
                        continue;
                    }

                    // Get next transitions
                    aux.getCopTransitions(nextcId, firstTransitionOut, lastTransitionOut);

                    // Push the new node to the stack
                    stack.push_back({
                        nextcId,
                        newRobberSet,
                        firstTransitionOut,
                        lastTransitionOut
                    });

                }

            }
            
        }
    }

    return 0;
}
