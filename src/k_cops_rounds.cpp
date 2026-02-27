/**
 * ============================================================================
 * FILE --- CopsAndRobbers_Minimax.cpp
 * ============================================================================
 * 
 * OVERVIEW
 * Solves the Cops and Robbers graph game to find the mathematically perfect 
 * game path (Minimax). It achieves high speed by (A) using raw contiguous arrays, 
 * (B) perfectly precomputing Compressed Sparse Row (CSR) transitions, and (C) 
 * tracking steps-to-win to extract the optimal Minimax path, finally offloading 
 * file formatting to a Python bridge.
 * 
 * DEEPER DIVE
 * - Raw Array Architecture: Strips out all `std::vector` overhead in the hot 
 * path. State arrays and transition tables are allocated as massive contiguous 
 * blocks of memory, maximizing CPU cache locality and eliminating heap fragmentation.
 * - Precomputed CSR Transitions: Avoids the catastrophic slowdown of calculating 
 * Cartesian products on the fly. All possible team moves are calculated exactly 
 * once upfront and packed into a flat 1D array, allowing instant adjacency 
 * lookups during the synchronous induction loop.
 * - Minimax Path Extraction: By tracking `stepsToWin`, the algorithm evaluates 
 * not just *if* the cops win, but *how fast*. During extraction, it walks the 
 * DP table, with cops choosing moves that minimize the robber's survival time, 
 * and the robber choosing moves that maximize it.
 * - Python Bridging: Offloads the heavy lifting of JSON formatting and Numpy 
 * binary (.npz) compression to `export_helper.py` via system calls. This keeps 
 * the C++ engine incredibly lean and focused strictly on raw graph mathematics.
 * 
 * PERFORMANCE METRICS (on scotlandyard-yellow with 3 cops)
 * - Memory -> Not Tracked Yet
 * - Time -> 6 seconds
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <cstdlib>

// --- MEMORY TRACKING HELPER ---
double bytesToMB(size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

constexpr size_t MAX_COPS = 256;

// --- STEP 1: Generate Configurations (Raw Array) ---
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    if (k > MAX_COPS) {
        std::cerr << "FATAL: Number of cops exceeds limit.\n";
        *outNumConfigs = 0; return nullptr;
    }

    int n_val = N + k - 1;
    int k_val = k;
    if (k_val > n_val) *outNumConfigs = 0;
    else if (k_val == 0 || k_val == n_val) *outNumConfigs = 1;
    else {
        if (k_val > n_val / 2) k_val = n_val - k_val;
        size_t res = 1;
        for (int i = 1; i <= k_val; ++i) res = res * (n_val - i + 1) / i;
        *outNumConfigs = res;
    }
    
    size_t totalBytes = (*outNumConfigs) * k;
    uint8_t* configs = new uint8_t[totalBytes];
    uint8_t current[MAX_COPS];
    memset(current, 0, MAX_COPS);
    
    size_t offset = 0;
    while(true) {
        memcpy(&(configs[offset]), current, k);
        offset += k;
        int p = k - 1;
        while(p >= 0 && current[p] == N - 1) p--;
        if (p < 0) break; 
        current[p]++;
        for(uint32_t i = p + 1; i < k; ++i) current[i] = current[p];
    }
    return configs;
}

// --- STEP 2: Build CSR Transitions (Raw Array Outputs) ---
void buildTransitions(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                      size_t*& outTransitionHeads, size_t*& outTransitions, size_t& totalTransCount) {
    
    outTransitionHeads = new size_t[configCount + 1];
    outTransitionHeads[0] = 0;
    
    std::vector<size_t> tempAllTransitions; // Temporary for building
    tempAllTransitions.reserve(configCount * 8); 

    std::vector<size_t> tempMoves;
    tempMoves.reserve(1024); 

    uint8_t options[MAX_COPS][256];
    int optionCount[MAX_COPS];
    int odometer[MAX_COPS];
    uint8_t moveConfig[MAX_COPS];

    for (size_t cId = 0; cId < configCount; cId++) {
        tempMoves.clear(); 
        const uint8_t* currentCops = &configs[cId * k];
        
        for (int i = 0; i < k; i++) {
            uint8_t u = currentCops[i];
            options[i][0] = u; 
            int count = 1;
            uint8_t* edges = adj.getEdges(u);
            int eIdx = 0;
            while (edges[eIdx] != 255) options[i][count++] = edges[eIdx++];
            optionCount[i] = count;
        }

        memset(odometer, 0, MAX_COPS * sizeof(int));
        
        while (true) {
            for (int i = 0; i < k; ++i) moveConfig[i] = options[i][odometer[i]];
            std::sort(moveConfig, moveConfig + k);
            
            size_t nextId = static_cast<size_t>(-1); 
            size_t left = 0, right = configCount - 1;
            while (left <= right) {
                size_t mid = left + (right - left) / 2;
                int cmp = std::memcmp(&configs[mid * k], moveConfig, k);
                if (cmp == 0) { nextId = mid; break; }
                if (cmp < 0) left = mid + 1;
                else right = mid - 1;
            }
            
            tempMoves.push_back(nextId * N);
            
            int p = k - 1;
            while (p >= 0) {
                odometer[p]++;
                if (odometer[p] < optionCount[p]) break;
                odometer[p] = 0; p--;
            }
            if (p < 0) break;
        }

        std::sort(tempMoves.begin(), tempMoves.end());
        tempMoves.erase(std::unique(tempMoves.begin(), tempMoves.end()), tempMoves.end());
        
        tempAllTransitions.insert(tempAllTransitions.end(), tempMoves.begin(), tempMoves.end());
        outTransitionHeads[cId + 1] = tempAllTransitions.size();
    }

    totalTransCount = tempAllTransitions.size();
    outTransitions = new size_t[totalTransCount];
    std::memcpy(outTransitions, tempAllTransitions.data(), totalTransCount * sizeof(size_t));
}

// --- MAIN ENGINE ---
void solveCopsAndRobbers(Graph* g, int k, const char* filename) {
    int N = g->nodeCount;
    if (N == 0) return;

    AdjacencyList adj(g);

    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount);
    if (!configs) return;

    size_t* transitionHeads = nullptr;
    size_t* transitions = nullptr;
    size_t totalTransCount = 0;
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions, totalTransCount);

    size_t numStates = configCount * N;

    // Allocate Flat Arrays (No vectors in hot path!)
    uint8_t* copTurnWins = new uint8_t[numStates]{0};
    uint8_t* robberTurnWins = new uint8_t[numStates]{0};
    int* stepsToWin = new int[numStates];
    std::fill_n(stepsToWin, numStates, -1);

    // Buffers for synchronous updating
    size_t* copWinsToApply = new size_t[numStates];
    size_t* robberWinsToApply = new size_t[numStates];

    // --- INITIALIZATION ---
    int initialWins = 0;
    for (size_t cId = 0; cId < configCount; ++cId) {
        for (int r = 0; r < N; ++r) {
            size_t stateId = cId * N + r;
            bool caught = false;
            for (int i = 0; i < k; ++i) {
                if (configs[cId * k + i] == r) { caught = true; break; }
            }
            if (caught) {
                copTurnWins[stateId] = 1;
                robberTurnWins[stateId] = 1;
                stepsToWin[stateId] = 0;
                initialWins++;
            }
        }
    }
    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";

    // --- SYNCHRONOUS MINIMAX LOOP ---
    bool changed = true;
    int passes = 0;

    while (changed) {
        changed = false;
        passes++;
        
        size_t copWinsCount = 0;
        size_t robberWinsCount = 0;

        for (size_t cId = 0; cId < configCount; ++cId) {
            size_t copTransStart = transitionHeads[cId];
            size_t copTransEnd = transitionHeads[cId + 1];
            size_t baseStateId = cId * N;
            uint8_t* rEdges = adj.getEdges(0); 

            for (int r = 0; r < N; ++r) {
                size_t stateId = baseStateId + r;

                if (copTurnWins[stateId] && robberTurnWins[stateId]) {
                    rEdges += adj.maxDegree;
                    continue;
                }

                // RIGHT SIDE: Robber's Turn
                if (!robberTurnWins[stateId]) {
                    bool canEscape = false;
                    if (!copTurnWins[stateId]) canEscape = true;
                    else {
                        for (int eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                            if (!copTurnWins[baseStateId + rEdges[eIdx]]) {
                                canEscape = true; break; 
                            }
                        }
                    }
                    if (!canEscape) robberWinsToApply[robberWinsCount++] = stateId;
                }

                // LEFT SIDE: Cop's Turn
                if (!copTurnWins[stateId]) {
                    bool canWin = false;
                    for (size_t i = copTransStart; i < copTransEnd; ++i) {
                        size_t nextStateId = transitions[i] + r;
                        if (robberTurnWins[nextStateId]) {
                            canWin = true; break; 
                        }
                    }
                    if (canWin) copWinsToApply[copWinsCount++] = stateId;
                }
                rEdges += adj.maxDegree;
            }
        }

        // CONVERSIVE UPDATE
        for (size_t i = 0; i < robberWinsCount; i++) {
            size_t s = robberWinsToApply[i];
            if (!robberTurnWins[s]) { robberTurnWins[s] = 1; changed = true; }
        }
        
        int newWinsThisPass = 0;
        for (size_t i = 0; i < copWinsCount; i++) {
            size_t s = copWinsToApply[i];
            if (!copTurnWins[s]) { 
                copTurnWins[s] = 1; 
                stepsToWin[s] = (passes + 1) / 2; // 2 passes = 1 full round
                changed = true; 
                newWinsThisPass++;
            }
        }

        if (newWinsThisPass > 0) {
            std::cout << "Pass " << passes << " (Round " << (passes + 1) / 2 << "): Found " << newWinsThisPass << " new states.\n";
        }
    }

    // --- FINAL VERDICT & PATH EXTRACTION ---
    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartCId = -1;
    int overallMinWorstCase = 999999;

    for (size_t cId = 0; cId < configCount; ++cId) {
        bool universalWin = true;
        int worstCaseSteps = 0;
        for (int rStart = 0; rStart < N; ++rStart) {
            size_t stateId = cId * N + rStart;
            if (!copTurnWins[stateId]) { universalWin = false; break; }
            if (stepsToWin[stateId] > worstCaseSteps) worstCaseSteps = stepsToWin[stateId];
        }
        if (universalWin && worstCaseSteps < overallMinWorstCase) {
            overallMinWorstCase = worstCaseSteps;
            winningStartCId = cId;
        }
    }

    if (winningStartCId != -1) {
        std::cout << "RESULT: WIN. Best Cop Position: (";
        for(int i = 0; i < k; i++) std::cout << (int)configs[winningStartCId * k + i] << (i == k - 1 ? "" : ", ");
        std::cout << ")\nCapture Time: " << overallMinWorstCase << " rounds.\n";
        
        std::cout << "Extracting perfect game path...\n";
        std::ofstream pathFile("temp_path.txt");
        
        int bestRStart = -1;
        int maxSteps = -1;
        for (int r = 0; r < N; ++r) {
            size_t sId = winningStartCId * N + r;
            if (stepsToWin[sId] > maxSteps) { maxSteps = stepsToWin[sId]; bestRStart = r; }
        }

        size_t currCId = winningStartCId;
        int currRobber = bestRStart;
        
        while (true) {
            bool caught = false;
            for(int i = 0; i < k; i++) {
                if (configs[currCId * k + i] == currRobber) caught = true;
            }

            // Cop Turn Path Write
            for(int i = 0; i < k; i++) pathFile << (int)configs[currCId * k + i] << (i == k - 1 ? "" : ",");
            pathFile << "|" << currRobber << (caught ? "|Game Over - Captured!\n" : "|Cop's Turn\n");
            if (caught) break;

            // --- INSTANT COP MOVE CALCULATION (Using CSR Transitions) ---
            size_t bestNextCId = currCId;
            int minWorstCaseSteps = 999999;
            
            size_t copTransStart = transitionHeads[currCId];
            size_t copTransEnd = transitionHeads[currCId + 1];
            
            for (size_t i = copTransStart; i < copTransEnd; ++i) {
                size_t nextCId = transitions[i] / N; // Undo the * N optimization from building
                
                int worstCaseRobberResponse = -1;
                bool isValidCopMove = true;
                bool instantCatch = false;
                
                for(int j = 0; j < k; j++) {
                    if (configs[nextCId * k + j] == currRobber) instantCatch = true;
                }

                if (instantCatch) {
                    worstCaseRobberResponse = 0;
                } else {
                    uint8_t* rEdges = adj.getEdges(currRobber);
                    for (int eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                        size_t nextStateId = nextCId * N + rEdges[eIdx];
                        if (!copTurnWins[nextStateId]) { isValidCopMove = false; break; }
                        if (stepsToWin[nextStateId] > worstCaseRobberResponse) {
                            worstCaseRobberResponse = stepsToWin[nextStateId];
                        }
                    }
                }

                if (isValidCopMove && worstCaseRobberResponse < minWorstCaseSteps) {
                    minWorstCaseSteps = worstCaseRobberResponse;
                    bestNextCId = nextCId;
                }
            }
            currCId = bestNextCId;
            
            // Check instant catch after cop move
            caught = false;
            for(int i = 0; i < k; i++) {
                if (configs[currCId * k + i] == currRobber) caught = true;
            }
            if (caught) {
                for(int i = 0; i < k; i++) pathFile << (int)configs[currCId * k + i] << (i == k - 1 ? "" : ",");
                pathFile << "|" << currRobber << "|Game Over - Captured!\n";
                break;
            }

            // Robber Turn Path Write
            for(int i = 0; i < k; i++) pathFile << (int)configs[currCId * k + i] << (i == k - 1 ? "" : ",");
            pathFile << "|" << currRobber << "|Robber's Turn\n";

            // Find best next robber move
            int bestNextRobber = currRobber;
            int maxStepsR = -1;
            uint8_t* rEdges = adj.getEdges(currRobber);
            for (int eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                size_t nextStateId = currCId * N + rEdges[eIdx];
                if (copTurnWins[nextStateId] && stepsToWin[nextStateId] > maxStepsR) {
                    maxStepsR = stepsToWin[nextStateId];
                    bestNextRobber = rEdges[eIdx];
                }
            }
            currRobber = bestNextRobber;
        }
        pathFile.close();

        std::cout << "Dumping raw DP Table...\n";
        std::ofstream dpFile("temp_dp.txt");
        for (size_t cId = 0; cId < configCount; ++cId) {
            for (int r = 0; r < N; ++r) {
                size_t sId = cId * N + r;
                for(int i = 0; i < k; i++) dpFile << (int)configs[cId * k + i] << (i == k - 1 ? "" : ",");
                dpFile << "|" << r << "|" << stepsToWin[sId] << "\n";
            }
        }
        dpFile.close();

        // Launch Python script
        std::string pyCmd = "python python/export_helper.py \"" + std::string(filename) + "\" " + std::to_string(k);
        std::system(pyCmd.c_str());

    } else {
        std::cout << "RESULT: LOSS. Robber can evade forever.\n";
    }

    // Cleanup Raw Arrays
    delete[] configs;
    delete[] transitionHeads;
    delete[] transitions;
    delete[] copTurnWins;
    delete[] robberTurnWins;
    delete[] stepsToWin;
    delete[] copWinsToApply;
    delete[] robberWinsToApply;
}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        return 1;
    }
    const char* filename = argv[1];
    int k = std::stoi(argv[2]);

    Graph g(filename);
    solveCopsAndRobbers(&g, k, filename);
    return 0;
}