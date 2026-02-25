#include "Graph.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

// --- PROCEDURAL HELPERS ---

// Recursive helper to generate combinations with replacement
void generateCopConfigs(int k, int N, int currentVal, std::vector<int>& currentConfig, std::vector<std::vector<int>>& outCopConfigs) {
    if (currentConfig.size() == k) {
        outCopConfigs.push_back(currentConfig);
        return;
    }
    for (int i = currentVal; i < N; ++i) {
        currentConfig.push_back(i);
        generateCopConfigs(k, N, i, currentConfig, outCopConfigs);
        currentConfig.pop_back();
    }
}

// Recursive helper to generate all possible moves for a team of cops
void generateTeamMoves(int k, const std::vector<int>& config, int copIdx, std::vector<int>& currentMoves, size_t configId, 
                       const std::vector<std::vector<int>>& adj, 
                       const std::vector<std::vector<int>>& copConfigs, 
                       std::vector<std::vector<size_t>>& outCopTransitions) {
    
    if (copIdx == k) {
        std::vector<int> sortedMoves = currentMoves;
        std::sort(sortedMoves.begin(), sortedMoves.end());
        
        // Find the ID of this resulting sorted configuration
        auto it = std::lower_bound(copConfigs.begin(), copConfigs.end(), sortedMoves);
        size_t nextId = std::distance(copConfigs.begin(), it);
        
        // Add to transitions if not already there
        auto& trans = outCopTransitions[configId];
        if (std::find(trans.begin(), trans.end(), nextId) == trans.end()) {
            trans.push_back(nextId);
        }
        return;
    }

    int u = config[copIdx];
    for (int v : adj[u]) {
        currentMoves.push_back(v);
        generateTeamMoves(k, config, copIdx + 1, currentMoves, configId, adj, copConfigs, outCopTransitions);
        currentMoves.pop_back();
    }
}

// --- MAIN ALGORITHM ---

void solveCopsAndRobbers(Graph* g, int k) {
    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    // 1. Build fast adjacency list including self-loops
    std::vector<std::vector<int>> adj(N);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (g->getEdge(i, j) || i == j) { // i == j adds the self-loop
                adj[i].push_back(j);
            }
        }
    }
    std::cout << "Graph loaded: " << N << " nodes.\n";

    // 2. Generate all unique, sorted cop configurations
    std::vector<std::vector<int>> copConfigs;
    std::vector<int> tempConfig;
    generateCopConfigs(k, N, 0, tempConfig, copConfigs);
    
    // 3. Pre-calculate all team transitions
    std::vector<std::vector<size_t>> copTransitions(copConfigs.size());
    for (size_t id = 0; id < copConfigs.size(); ++id) {
        std::vector<int> tempMoves;
        generateTeamMoves(k, copConfigs[id], 0, tempMoves, id, adj, copConfigs, copTransitions);
    }

    // 4. Allocate flat arrays for game states
    size_t numStates = copConfigs.size() * N;
    bool* copTurnWins = new bool[numStates]{false};
    bool* robberTurnWins = new bool[numStates]{false};
    int* robberSafeMoves = new int[numStates]{0};

    std::cout << "Generating states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";

    // --- STEP 1: INITIALIZATION ---
    int initialWins = 0;
    for (size_t cId = 0; cId < copConfigs.size(); ++cId) {
        for (int r = 0; r < N; ++r) {
            size_t stateId = cId * N + r;
            
            // Is robber caught?
            bool caught = std::find(copConfigs[cId].begin(), copConfigs[cId].end(), r) != copConfigs[cId].end();
            
            if (caught) {
                copTurnWins[stateId] = true;
                robberTurnWins[stateId] = true;
                robberSafeMoves[stateId] = 0;
                initialWins++;
            } else {
                robberSafeMoves[stateId] = adj[r].size();
            }
        }
    }
    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Backward Induction Loop...\n";

    // --- STEP 2: MAIN LOOP ---
    bool changed = true;
    int passes = 0;

    while (changed) {
        changed = false;
        passes++;
        int newWinsThisPass = 0;

        for (size_t cId = 0; cId < copConfigs.size(); ++cId) {
            for (int r = 0; r < N; ++r) {
                size_t stateId = cId * N + r;

                if (copTurnWins[stateId] && robberTurnWins[stateId]) continue;

                // RIGHT SIDE: Robber's Turn
                if (!robberTurnWins[stateId]) {
                    int safeCount = 0;
                    for (int rNext : adj[r]) {
                        size_t nextStateId = cId * N + rNext;
                        if (!copTurnWins[nextStateId]) {
                            safeCount++;
                        }
                    }
                    robberSafeMoves[stateId] = safeCount;

                    if (safeCount == 0) {
                        robberTurnWins[stateId] = true;
                        changed = true;
                        newWinsThisPass++;
                    }
                }

                // LEFT SIDE: Cop's Turn
                if (!copTurnWins[stateId]) {
                    bool canWin = false;
                    for (size_t nextCId : copTransitions[cId]) {
                        size_t nextStateId = nextCId * N + r;
                        if (robberTurnWins[nextStateId]) {
                            canWin = true;
                            break;
                        }
                    }

                    if (canWin) {
                        copTurnWins[stateId] = true;
                        changed = true;
                        newWinsThisPass++;
                    }
                }
            }
        }
        std::cout << "Pass " << passes << ": Found " << newWinsThisPass << " new winning states.\n";
    }

    // --- STEP 3: FINAL VERDICT ---
    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartConfigId = -1;

    for (size_t cId = 0; cId < copConfigs.size(); ++cId) {
        bool universalWin = true;
        for (int rStart = 0; rStart < N; ++rStart) {
            size_t stateId = cId * N + rStart;
            if (!copTurnWins[stateId]) {
                universalWin = false;
                break;
            }
        }
        if (universalWin) {
            winningStartConfigId = cId;
            break;
        }
    }

    if (winningStartConfigId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph.\n";
        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << copConfigs[winningStartConfigId][i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";
    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    // --- CLEANUP ---
    delete[] copTurnWins;
    delete[] robberTurnWins;
    delete[] robberSafeMoves;
}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        std::cout << "Example: " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    const char* filename = argv[1];
    int k = std::stoi(argv[2]);

    // Graph parsing using the procedural Graph constructor we finalized
    Graph g(filename);
    
    // Pass it cleanly to the solver
    solveCopsAndRobbers(&g, k);

    return 0;
    
}