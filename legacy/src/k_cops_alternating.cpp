/**
 * ============================================================================
 * FILE --- k_cops_alternating.cpp
 * ============================================================================
 * * OVERVIEW:
 * Solves the Alternating Visibility variant of Cops and Robbers using a 
 * 4-Column state machine and outputs DP tables.
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

constexpr size_t MAX_COPS = 256;

// --- PROCEDURAL HELPERS ---
// Explicit helper to check if the robber is caught or can escape from node r1
bool check_RobberHiddenNode(int r1, int k, size_t cId, int N, const uint8_t* configs, const AdjacencyList& adj, const int* col1) {
    // 1. Instant Catch Rule: Is a cop physically standing on r1?
    for (int i = 0; i < k; i++) {
        if (configs[cId * k + i] == r1) return true; 
    }
    
    // 2. Escape Check Rule: Can the robber survive by staying at r1?
    if (col1[cId * N + r1] == -1) return false; 
    
    // 3. Move Escape Rule: Can the robber survive by moving to a neighbor?
    uint8_t* N_r1 = adj.getEdges(r1);
    for (int j = 0; j < adj.maxDegree && N_r1[j] != 255; j++) {
        if (col1[cId * N + N_r1[j]] == -1) return false; 
    }
    
    // If the cops weren't on r1, and all escape paths lead to a cop win, the robber is trapped.
    return true; 
}
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    if (k > MAX_COPS) { *outNumConfigs = 0; return nullptr; }

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

void buildTransitions(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                      size_t*& outTransitionHeads, size_t*& outTransitions) {
    outTransitionHeads = new size_t[configCount + 1];
    outTransitionHeads[0] = 0;
    
    std::vector<size_t> tempAllTransitions; 
    std::vector<size_t> tempMoves;

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
            // FIX: Bounded check prevents reading past node's allocated slot
            while (eIdx < adj.maxDegree && edges[eIdx] != 255) {
                options[i][count++] = edges[eIdx++];
            }
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
            
            if (nextId != static_cast<size_t>(-1)) {
                tempMoves.push_back(nextId);
            }
            
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

    outTransitions = new size_t[tempAllTransitions.size()];
    std::memcpy(outTransitions, tempAllTransitions.data(), tempAllTransitions.size() * sizeof(size_t));
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
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions);

    size_t numStates = configCount * N;

    int* col1 = new int[numStates]; std::fill_n(col1, numStates, -1);
    int* col2 = new int[numStates]; std::fill_n(col2, numStates, -1);
    int* col3 = new int[numStates]; std::fill_n(col3, numStates, -1);
    int* col4 = new int[numStates]; std::fill_n(col4, numStates, -1);

    std::vector<size_t> up1, up2, up3, up4;
    up1.reserve(numStates); up2.reserve(numStates); 
    up3.reserve(numStates); up4.reserve(numStates);

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
                col1[stateId] = 0;
                col2[stateId] = 0;
                // DO NOT SET col3 OR col4 HERE! r does not mean the true Robber location. It means the last known Robber location (the shadow). By setting col3 = 0 and col4 = 0 when the Cop stands on r, I literally told the mathematical DP table that stepping on the Robber's shadow counts as winning the game.
                initialWins++;
            }
        }
    }
    std::cout << "Initialized " << initialWins << " base captures.\n";
    std::cout << "Running 4-Column Alternating Backward Induction...\n";

    bool changed = true;
    int pass = 0;
    int winningGroup = -1;

    while (changed) {
        changed = false;
        pass++;
        up1.clear(); up2.clear(); up3.clear(); up4.clear();

        for (size_t cId = 0; cId < configCount; ++cId) {
            size_t copTransStart = transitionHeads[cId];
            size_t copTransEnd = transitionHeads[cId + 1];

            for (int r0 = 0; r0 < N; ++r0) {
                size_t stateId = cId * N + r0;

                // --- 1. Evaluate Col 4 (Depends on Col 1) ---
                if (col4[stateId] == -1) {
                    bool all_paths_caught = true;
                    
                    // Check if the robber stays at r0
                    if (!check_RobberHiddenNode(r0, k, cId, N, configs, adj, col1)) {
                        all_paths_caught = false;
                    } else {
                        // Check all neighbors of r0
                        uint8_t* N_r0 = adj.getEdges(r0);
                        for(int i=0; i < adj.maxDegree && N_r0[i] != 255; i++) {
                            if (!check_RobberHiddenNode(N_r0[i], k, cId, N, configs, adj, col1)) { 
                                all_paths_caught = false; 
                                break; 
                            }
                        }
                    }

                    if (all_paths_caught) up4.push_back(stateId);
                }

                // --- 2. Evaluate Col 3 (Depends on Col 4) ---
                if (col3[stateId] == -1) {
                    for (size_t i = copTransStart; i < copTransEnd; ++i) {
                        if (col4[transitions[i] * N + r0] != -1) {
                            up3.push_back(stateId); break;
                        }
                    }
                }

                // --- 3. Evaluate Col 2 (Depends on Col 3) ---
                if (col2[stateId] == -1) {
                    if (col3[stateId] != -1) up2.push_back(stateId);
                }

                // --- 4. Evaluate Col 1 (Depends on Col 2) ---
                if (col1[stateId] == -1) {
                    for (size_t i = copTransStart; i < copTransEnd; ++i) {
                        if (col2[transitions[i] * N + r0] != -1) {
                            up1.push_back(stateId); break;
                        }
                    }
                }
            }
        }

        // Apply updates synchronously
        for (size_t s : up4) { col4[s] = pass; changed = true; }
        for (size_t s : up3) { col3[s] = pass; changed = true; }
        for (size_t s : up2) { col2[s] = pass; changed = true; }
        for (size_t s : up1) { col1[s] = pass; changed = true; }

        if (changed) {
            std::cout << "Pass " << pass << " | New States (C1:" << up1.size() << ", C2:" << up2.size() 
                      << ", C3:" << up3.size() << ", C4:" << up4.size() << ")\n";
        }

        // EARLY STOPPING CHECK
        // for (size_t cId = 0; cId < configCount; ++cId) {
        //     bool universalWin = true;
        //     for (int r = 0; r < N; ++r) {
        //         if (col1[cId * N + r] == -1) { universalWin = false; break; }
        //     }
        //     if (universalWin) { winningGroup = cId; break; }
        // }
        
        // if (winningGroup != -1) {
        //     std::cout << "\n>>> EARLY STOP: Verified winning cop configuration found! <<<\n";
        //     break;
        // }
    }
 // <-- This is the closing bracket for: while (changed) { ... }

    // --- VERDICT EVALUATION ---
    // Now that the entire DP table is fully calculated, check if any cop 
    // configuration guarantees a win (no -1s in their Col 1 row).
    for (size_t cId = 0; cId < configCount; ++cId) {
        bool universalWin = true;
        for (int r = 0; r < N; ++r) {
            if (col1[cId * N + r] == -1) { 
                universalWin = false; 
                break; 
            }
        }
        if (universalWin) { 
            winningGroup = cId; 
            break; 
        }
    }

    // --- VERDICT & DP DUMP ---
    std::cout << "\n--- FINAL VERDICT ---\n";
    if (winningGroup != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win with alternating visibility.\n";
        std::cout << "Valid Start Configuration: (";
        for(int i = 0; i < k; i++) std::cout << (int)configs[winningGroup * k + i] << (i == k - 1 ? "" : ", ");
        std::cout << ")\n";
    } else {
        std::cout << "RESULT: LOSS. Cops cannot guarantee a win under alternating rules.\n";
    }

    std::cout << "Dumping raw binary DP tables...\n";
    std::ofstream binFile("assets/dp_tables/alternating_raw.bin", std::ios::binary);
    
    if (!binFile.is_open()) {
        std::cerr << "\n[FATAL ERROR] C++ could not create 'assets/dp_tables/alternating_raw.bin'.\n";
    } else {
        int32_t outN = N;
        int32_t outK = k;
        uint64_t outConfigs = configCount;
        
        binFile.write(reinterpret_cast<const char*>(&outN), sizeof(outN));
        binFile.write(reinterpret_cast<const char*>(&outK), sizeof(outK));
        binFile.write(reinterpret_cast<const char*>(&outConfigs), sizeof(outConfigs));
        binFile.write(reinterpret_cast<const char*>(configs), configCount * k);
        
        size_t memSize = numStates * sizeof(int);
        binFile.write(reinterpret_cast<const char*>(col1), memSize);
        binFile.write(reinterpret_cast<const char*>(col2), memSize);
        binFile.write(reinterpret_cast<const char*>(col3), memSize);
        binFile.write(reinterpret_cast<const char*>(col4), memSize);
        binFile.close();

        std::cout << "Launching Python bridge to generate NPZ...\n";
        std::string pyCmd = "python build_npz.py";
        std::system(pyCmd.c_str());
    }

    delete[] configs; delete[] transitionHeads; delete[] transitions;
    delete[] col1; delete[] col2; delete[] col3; delete[] col4;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        return 1;
    }
    Graph g(argv[1]);
    solveCopsAndRobbers(&g, std::stoi(argv[2]), argv[1]);
    return 0;
}