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
                col3[stateId] = 0; // FIX: Added base capture for invisible phases
                col4[stateId] = 0; // FIX: Added base capture for invisible phases
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
                    
                    auto check_r1 = [&](int r1) {
                        for(int i=0; i<k; i++) if (configs[cId*k + i] == r1) return true;
                        
                        uint8_t* N_r1 = adj.getEdges(r1);
                        if (col1[cId * N + r1] == -1) return false; 
                        // FIX: Bounded maxDegree check
                        for(int j=0; j < adj.maxDegree && N_r1[j] != 255; j++) {
                            if (col1[cId * N + N_r1[j]] == -1) return false; 
                        }
                        return true;
                    };

                    if (!check_r1(r0)) all_paths_caught = false;
                    else {
                        uint8_t* N_r0 = adj.getEdges(r0);
                        // FIX: Bounded maxDegree check
                        for(int i=0; i < adj.maxDegree && N_r0[i] != 255; i++) {
                            if (!check_r1(N_r0[i])) { all_paths_caught = false; break; }
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
        for (size_t cId = 0; cId < configCount; ++cId) {
            bool universalWin = true;
            for (int r = 0; r < N; ++r) {
                if (col1[cId * N + r] == -1) { universalWin = false; break; }
            }
            if (universalWin) { winningGroup = cId; break; }
        }
        
        if (winningGroup != -1) {
            std::cout << "\n>>> EARLY STOP: Verified winning cop configuration found! <<<\n";
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
        
        std::cout << "Dumping DP Tables to dp_tables/alternating_dp.csv...\n";
        std::ofstream dpFile("dp_tables/alternating_dp.csv");
        dpFile << "CopConfig,RobberNode,Col1_VisCopTurn,Col2_VisRobTurn,Col3_InvisCopTurn,Col4_InvisRobTurn\n";
        for (size_t cId = 0; cId < configCount; ++cId) {
            for (int r = 0; r < N; ++r) {
                size_t sId = cId * N + r;
                for(int i = 0; i < k; i++) dpFile << (int)configs[cId * k + i] << (i == k - 1 ? "" : "-");
                dpFile << "," << r << "," << col1[sId] << "," << col2[sId] << "," << col3[sId] << "," << col4[sId] << "\n";
            }
        }
        dpFile.close();
    } else {
        std::cout << "RESULT: LOSS. Cops cannot guarantee a win under alternating rules.\n";
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