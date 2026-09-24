#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "VertexBitField.h"
#include "PackedAdjMatrix.h"
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

PackedAdjMatrix adjMatrix;

bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    filename = filename_param;
    k = k_param;
    p = p_param;

    Graph g(filename);

    if (g.nodeCount == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }

    if (g.nodeCount > 200) {
        std::cerr << "Error: graph exceeds the 200-vertex limit for PackedAdjMatrix.\n";
        return 1;
    }

    N = g.nodeCount;

    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    // Populate the packed bitwise adjacency matrix
    for (int u = 0; u < N; ++u) {

        // Start v at u to only scan the upper triangle (undirected)
        for (int v = u; v < N; ++v) {
            if (g.getEdge(u, v)) {
                adjMatrix.addEdge(u, v);
            }
        }

    }

    // PackedAdjMatrix is entirely flat, so sizeof() captures 100% of its footprint
    mem.trackExternal("Packed Adj Matrix", sizeof(PackedAdjMatrix));

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
        bool newMarksThisIteration = false;

        // Per node in the column
        for (size_t cId = 0; cId < aux.configCount; cId++) {
            for (uint8_t r = 0; r < N; r++) {

                DataItem* root = aux.getState(cId, r, 0);

                // Skip already marked nodes
                if (root->marked) continue;

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

                // DFS search
                while (stack.size() > 0) {

                    // Get top of stack
                    StackItem& current = stack.back();

                    // If we have iterated over all transitions from the current top of the stack.
                    // getCopTransitions hands back an EXCLUSIVE end index, so this is >=
                    if (current.nextTransition >= current.lastTransition) {
                        stack.pop_back();
                        continue;
                    }

                    // Current search depth in cop steps. The transition we are about to play
                    // moves the cops from depth-1 to depth, so it banks 2 more plys
                    const int depth = (int)stack.size();

                    // Get next transition from current.
                    // AuxGraph stores transition targets pre-multiplied by N so that consumers can
                    // index states flatly as transitions[i] + r, so the raw value is a state base,
                    // not a config id. Undo that here
                    size_t nextcId = aux.transitions[current.nextTransition] / N;
                    current.nextTransition++;

                    VertexBitField newRobberSet = current.robberSet;

                    // The cops positions AFTER this transition. Both the cop ply and the robber
                    // ply below prune against these, never against the previous config
                    const uint8_t* copPositions = &(aux.configs[nextcId * k]);

                    {
                        /*
                            Simulate cops turn

                            In this step, we let the cops play one possible move from the current tip of the DFS search. This effectively runs the search to one more depth than where the tip last was.

                            Here we trim the last robber set based on the new cop positions.

                            Either:
                            The new robbers set is empty. In this case, we have found a winning path. We mark the node. We save the depth we are currently at (since it is necessarily <= the current bestDepth), and continue with the rest of the DFS
                            OR...
                            The new robbers set is not empty. No win was found on this iteration. We pass through to simulating the robbers turn
                        */

                        // Trim robber set
                        for (int i = 0; i < k; i++) {
                            newRobberSet.reset(copPositions[i]);
                        }

                        // If the robber set is empty
                        if (newRobberSet.none()) {

                            // The capture landed on the cops ply, so the robbers reply at this
                            // depth is never played: 2(depth-1) banked plys, plus this one
                            if (2 * depth - 1 < bestDepth) bestDepth = 2 * depth - 1;

                            // 2*depth-1 is the floor for every sibling at this depth too, so
                            // nothing left on this level can improve on it. Abandon the level
                            stack.pop_back();
                            continue;

                        }
                    }

                    {
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

                        // Expand robbers set by 1 move. The robber has no self edge
                        // (SelfEdgeRobber::FALSE), so passing is not a legal move
                        newRobberSet = adjMatrix.expand(newRobberSet, false);

                        // Any robber whose only moves land on a cop is caught on its own ply.
                        // This compares against the NEW cop positions, so a robber stepping onto
                        // a vertex a cop just vacated correctly survives
                        for (int i = 0; i < k; i++) {
                            newRobberSet.reset(copPositions[i]);
                        }

                        // If every surviving robber was forced onto a cop
                        if (newRobberSet.none()) {

                            if (2 * depth < bestDepth) bestDepth = 2 * depth;

                            // Unlike the cop ply case, a sibling at this depth could still catch
                            // one ply sooner (2*depth-1), so the level is NOT abandoned
                            continue;

                        }

                        // If its a leaf node (last cop turn column) - robber becomes VISIBLE
                        if (depth == p) {

                            // Search possible robber transitions from this leaf
                            bool allMarked = true;
                            uint8_t activeNodes[256];
                            size_t nodeCount = 0;
                            
                            // Extract nodes
                            if (adjMatrix.extractVertices(newRobberSet, activeNodes, 256, nodeCount)) {
                                std::cerr << "FATAL: Robber set exceeded maximum buffer size.\n";
                                exit(1);
                            }

                            // Robber picks longest catch
                            int maxChildDepth = -1;

                            for (size_t i = 0; i < nodeCount; ++i) {
                                uint8_t r_end = activeNodes[i];
                                
                                DataItem* targetState = aux.getState(nextcId, r_end, 0);
                                
                                if (!targetState->marked) {
                                    allMarked = false;
                                    break;
                                }

                                if (targetState->markedRound > maxChildDepth) {
                                    maxChildDepth = targetState->markedRound;
                                }
                            }
                            
                            // If "for all" holds, we can mark this node
                            if (allMarked) {

                                int branchDepth = maxChildDepth + (p * 2); 

                                // Cop wants the fastest win, so conditionally update bestDepth
                                if (branchDepth < bestDepth) {
                                    bestDepth = branchDepth;
                                }

                            }

                            // Do NOT pop. The remaining transitions on this frame are the other
                            // candidate final cop moves, and the cops get to choose the best one
                            continue;
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

                    // The cheapest outcome anywhere below the node we are about to push is a
                    // capture on the cops ply at depth+1, costing 2*(depth+1)-1 plys. If that
                    // cannot beat what we already have, dont add a new node to the stack
                    if ((2 * depth) + 1 >= bestDepth) {
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

                // markedRound is a 7 bit field. A capture too slow to fit is left unproven
                // rather than silently truncated into a small, wrong value
                if (bestDepth < MAX_ROUND_COUNT) {
                    root->marked = true;
                    root->markedRound = bestDepth;
                    newMarksThisIteration = true;
                }

            }
        }

        if (!newMarksThisIteration) break;

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

        std::cout << "Capture Time: " << (int)overallMinWorstCase << " plys.\n";

    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    mem.print();

    return 0;

}

bool outputData() {

    std::string algoName = "k_cops_v3_" + std::to_string(p) + "vis";

    std::cout << "Saving filled AuxGraph to cache... ";

    bool failed = CacheManager::saveAuxGraph<DataItem>(algoName, filename, k, p, SelfEdgeCop::FALSE, SelfEdgeRobber::FALSE, &aux);

    if (failed) {
        std::cout << "Failed!\n";
        return 1;
    }

    std::cout << "Success!\n";

    return 0;

}
