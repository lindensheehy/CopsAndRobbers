/**
 * ============================================================================
 * FILE --- CopsAndRobbers_V4_Multithreaded.cpp
 * ============================================================================
 * 
 * OVERVIEW
 * Solves the Cops and Robbers graph game using (A) exact combinatorial state 
 * generation, (B) parallelized CSR transition building, and (C) a multithreaded 
 * Level-Synchronous BFS retrograde analysis driven by lock-free atomics.
 * 
 * DEEPER DIVE
 * - Level-Synchronous BFS - Instead of a single continuous queue, the workload is 
 * divided into "frontiers" or "waves". Thread workers process chunks of the 
 * `currentFrontier` simultaneously, pushing discovered winning states into local 
 * vectors. These local vectors are then merged to form the next wave.
 * - Lock-Free Concurrency (The Magic Tricks): Multiple threads will inevitably 
 * find different paths that lead back to the *same* prior state. To prevent a 
 * state from being pushed to the next frontier multiple times (which would cause 
 * an exponential explosion in redundant work), we use `std::atomic` arrays.
 * 1. `exchange(1)`: Sets a state to a win and returns what it *used* to be. 
 * If it returns 0, this specific thread was the one to flip it, so this 
 * thread "owns" the right to queue it.
 * 2. `fetch_sub(1)`: Decrements safe moves safely across threads. If it 
 * returns 1, this specific thread delivered the final blow that trapped 
 * the robber, granting it the right to queue the state.
 * - Parallel Prefix Sum: Transition building uses a map-reduce pattern where 
 * threads build local moves, and the main thread uses a prefix sum array to 
 * pre-calculate exact offsets for a unified, lock-free global insertion phase.
 * 
 * PERFORMANCE METRICS (on scotlandyard-yellow with 3 cops)
 * - Memory -> 3.68 GB 
 * - Time -> 14 seconds
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <cstdint>
#include <iomanip>

// --- BIT-PACKING CONSTANTS ---
// MSB is 1 for Robber's turn, 0 for Cop's turn. 
// The rest of the bits hold the stateId.
constexpr size_t ROBBER_TURN_BIT = (size_t)1 << (sizeof(size_t) * 8 - 1);
constexpr size_t STATE_ID_MASK = ~ROBBER_TURN_BIT;

// --- MEMORY TRACKING HELPER ---
double bytesToMB(size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// --- PROCEDURAL HELPERS ---

/**
 * Calculates exact state space size, allocates a precise contiguous array, 
 * and iteratively generates all unique, sorted cop configurations.
 */
constexpr size_t MAX_COPS = 256;
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    
    if (k > MAX_COPS) {
        std::cerr << "FATAL: Number of cops (k) exceeds maximum supported limit of " << MAX_COPS << ".\n";
        *outNumConfigs = 0;
        return nullptr;
    }

    {
        int n_val = N + k - 1;
        int k_val = k;
        
        if (k_val > n_val) {
            *outNumConfigs = 0;
        } else if (k_val == 0 || k_val == n_val) {
            *outNumConfigs = 1;
        } else {
            if (k_val > n_val / 2) k_val = n_val - k_val;
            size_t res = 1;
            for (int i = 1; i <= k_val; ++i) {
                res = res * (n_val - i + 1) / i;
            }
            *outNumConfigs = res;
        }
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
        while(p >= 0 && current[p] == N - 1) {
            p--;
        }
        
        if (p < 0) break; 
        
        current[p]++;
        
        for(uint32_t i = p + 1; i < k; ++i) {
            current[i] = current[p];
        }
    }
    
    return configs;
}

/**
 * Builds a Compressed Sparse Row (CSR) representation of all possible team moves
 * across multiple threads. Utilizes a Map-Reduce style pattern to avoid mutex 
 * locks during the transition generation.
 */
void buildTransitions(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                      std::vector<size_t>& outTransitionHeads, std::vector<size_t>& outTransitions) {
    
    // 1. Determine thread count and chunk sizes
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 8; // Fallback
    
    size_t chunkSize = (configCount + numThreads - 1) / numThreads;

    std::cout << "Building transition table using " << numThreads << " threads...\n";

    // 2. Thread-local storage and shared tracking
    std::vector<std::thread> threads;
    std::vector<std::vector<size_t>> allLocalTransitions(numThreads);
    std::vector<size_t> transitionCounts(configCount, 0); // Safe: threads write to distinct indices

    // 3. The Worker Lambda
    auto worker = [&](unsigned int tId, size_t startId, size_t endId) {
        // GENEROUS PRE-ALLOCATION: Guess an average of 12 moves per configuration
        allLocalTransitions[tId].reserve((endId - startId) * 12);

        std::vector<size_t> tempMoves;
        tempMoves.reserve(1024); 

        uint8_t options[MAX_COPS][256];
        int optionCount[MAX_COPS];
        int odometer[MAX_COPS];
        uint8_t moveConfig[MAX_COPS];

        for (size_t cId = startId; cId < endId; cId++) {
            tempMoves.clear(); 
            const uint8_t* currentCops = &configs[cId * k];
            
            for (int i = 0; i < k; i++) {
                uint8_t u = currentCops[i];
                options[i][0] = u; 
                int count = 1;
                
                uint8_t* edges = adj.getEdges(u);
                int eIdx = 0;
                while (edges[eIdx] != 255) {
                    options[i][count++] = edges[eIdx++];
                }
                optionCount[i] = count;
            }

            memset(odometer, 0, MAX_COPS * sizeof(int));
            
            while (true) {
                for (int i = 0; i < k; ++i) {
                    moveConfig[i] = options[i][odometer[i]];
                }
                
                std::sort(moveConfig, moveConfig + k);
                size_t nextId = static_cast<size_t>(-1); 
                size_t left = 0;
                size_t right = configCount - 1;
                while (left <= right) {
                    size_t mid = left + (right - left) / 2;
                    int cmp = std::memcmp(&configs[mid * k], moveConfig, k);
                    if (cmp == 0) {
                        nextId = mid;
                        break;
                    }
                    if (cmp < 0) left = mid + 1;
                    else right = mid - 1;
                }
                
                tempMoves.push_back(nextId * N);
                
                int p = k - 1;
                while (p >= 0) {
                    odometer[p]++;
                    if (odometer[p] < optionCount[p]) break;
                    odometer[p] = 0;
                    p--;
                }
                if (p < 0) break;
            }

            std::sort(tempMoves.begin(), tempMoves.end());
            tempMoves.erase(std::unique(tempMoves.begin(), tempMoves.end()), tempMoves.end());
            
            // Log the size and append to local thread storage
            transitionCounts[cId] = tempMoves.size();
            allLocalTransitions[tId].insert(allLocalTransitions[tId].end(), tempMoves.begin(), tempMoves.end());
        }
    };

    // 4. Spawn Threads
    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t startId = i * chunkSize;
        size_t endId = std::min(startId + chunkSize, configCount);
        if (startId >= configCount) break; // Prevent over-spawning on tiny graphs
        threads.emplace_back(worker, i, startId, endId);
    }

    // 5. Wait for all threads to finish their chunks
    for (auto& t : threads) {
        t.join();
    }

    // --- 6. THE MERGE PHASE ---
    outTransitionHeads.assign(configCount + 1, 0);
    size_t totalTransitions = 0;

    // Prefix sum to build the exact transition heads
    for (size_t i = 0; i < configCount; ++i) {
        outTransitionHeads[i] = totalTransitions;
        totalTransitions += transitionCounts[i];
    }
    outTransitionHeads[configCount] = totalTransitions;

    // Exact allocation for the global array (0 wasted space)
    outTransitions.clear();
    outTransitions.reserve(totalTransitions); 
    
    // Stitch the local chunks together safely in a single thread
    for (unsigned int i = 0; i < numThreads; ++i) {
        if (!allLocalTransitions[i].empty()) {
            outTransitions.insert(outTransitions.end(), 
                                  allLocalTransitions[i].begin(), 
                                  allLocalTransitions[i].end());
        }
    }

    std::cout << "Transitions generated. Total edge pointers: " << outTransitions.size() << "\n";
}

/**
 * Allocates `std::atomic` tracking arrays. Because we are using hardware atomics, 
 * we can no longer use a single flat memory pool (memset on atomics is undefined).
 * We initialize them strictly with `std::memory_order_relaxed`.
 */
void allocateGameStates(size_t configCount, int k, int N, 
                        std::atomic<uint8_t>*& outCopTurnWins, 
                        std::atomic<uint8_t>*& outRobberTurnWins, 
                        std::atomic<uint8_t>*& outRobberSafeMoves) {
    size_t numStates = configCount * N;

    std::cout << "Generating ATOMIC states for " << k << " cops...\n";
    std::cout << "Total States: " << numStates << "\n";
    
    outCopTurnWins = new std::atomic<uint8_t>[numStates];
    outRobberTurnWins = new std::atomic<uint8_t>[numStates];
    outRobberSafeMoves = new std::atomic<uint8_t>[numStates];

    // Initialize atomics safely
    for (size_t i = 0; i < numStates; ++i) {
        outCopTurnWins[i].store(0, std::memory_order_relaxed);
        outRobberTurnWins[i].store(0, std::memory_order_relaxed);
        outRobberSafeMoves[i].store(0, std::memory_order_relaxed);
    }
}

/**
 * Identifies immediate capture states (robber and cop share a node).
 * Flags them, sets safe moves to 0, and pushes them to the initial wave (frontier)
 * to kickstart the BFS.
 */
void initializeCaptures(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                        std::atomic<uint8_t>* copTurnWins, std::atomic<uint8_t>* robberTurnWins, std::atomic<uint8_t>* robberSafeMoves,
                        std::vector<size_t>& currentFrontier) {
    
    uint8_t robberDegrees[256];
    for (int r = 0; r < N; ++r) {
        int eCount = 1; 
        uint8_t* edges = adj.getEdges(r);
        while (edges[eCount - 1] != 255) eCount++;
        robberDegrees[r] = static_cast<uint8_t>(eCount);
    }

    int initialWins = 0;
    
    for (size_t cId = 0; cId < configCount; ++cId) {
        const uint8_t* currentCops = &configs[cId * k];
        
        for (int r = 0; r < N; ++r) {
            size_t stateId = cId * N + r;
            
            bool caught = false;
            for (int i = 0; i < k; ++i) {
                if (currentCops[i] == r) {
                    caught = true;
                    break;
                }
            }
            
            if (caught) {
                copTurnWins[stateId].store(1, std::memory_order_relaxed);
                robberTurnWins[stateId].store(1, std::memory_order_relaxed);
                robberSafeMoves[stateId].store(0, std::memory_order_relaxed); 
                
                // Push both turn phases into the initial frontier
                currentFrontier.push_back(stateId);                     // Cop's turn
                currentFrontier.push_back(stateId | ROBBER_TURN_BIT);   // Robber's turn
                initialWins++;
            } else {
                robberSafeMoves[stateId].store(robberDegrees[r], std::memory_order_relaxed);
            }
        }
    }

    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Multi-Threaded Level-Synchronous BFS...\n";
}

// --- MAIN ALGORITHM ---

void solveCopsAndRobbers(Graph* g, int k) {

    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    // STEP 1 --- Adjacency List
    AdjacencyList adj(g);

    // STEP 2 --- Cop Configurations
    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount);
    if (!configs || configCount == 0) return;

    // --> MEMORY TRACKING: configs array
    size_t configsBytes = configCount * k * sizeof(uint8_t);
    std::cout << "[Memory] configs array: " << std::fixed << std::setprecision(2) << bytesToMB(configsBytes) << " MB\n";

    // STEP 3 --- CSR Transitions
    std::vector<size_t> transitionHeads;
    std::vector<size_t> transitions;
    buildTransitions(configCount, k, N, configs, adj, transitionHeads, transitions);

    // --> MEMORY TRACKING: transitions CSR
    size_t transitionsBytes = (transitionHeads.capacity() * sizeof(size_t)) + (transitions.capacity() * sizeof(size_t));
    std::cout << "[Memory] transitions CSR: " << bytesToMB(transitionsBytes) << " MB\n";

    // STEP 4 --- Allocate Game States
    std::atomic<uint8_t>* copTurnWins = nullptr;
    std::atomic<uint8_t>* robberTurnWins = nullptr;
    std::atomic<uint8_t>* robberSafeMoves = nullptr;
    allocateGameStates(configCount, k, N, copTurnWins, robberTurnWins, robberSafeMoves);

    // --> MEMORY TRACKING: Atomic State Arrays
    size_t stateArraysBytes = configCount * N * 3 * sizeof(std::atomic<uint8_t>);
    std::cout << "[Memory] Game State Arrays (Atomics): " << bytesToMB(stateArraysBytes) << " MB\n";

    std::vector<size_t> currentFrontier;
    // Pre-allocate to prevent reallocations on Pass 1
    currentFrontier.reserve(configCount * N); 

    // --> MEMORY TRACKING: BFS Frontier
    size_t frontierBytes = currentFrontier.capacity() * sizeof(size_t);
    std::cout << "[Memory] BFS Frontier Queue: " << bytesToMB(frontierBytes) << " MB\n";

    std::cout << "[Memory] TOTAL MAJOR ALLOCATIONS: " 
              << bytesToMB(configsBytes + transitionsBytes + stateArraysBytes + frontierBytes) << " MB\n\n";

    // STEP 5 --- INITIALIZATION
    initializeCaptures(configCount, k, N, configs, adj, copTurnWins, robberTurnWins, robberSafeMoves, currentFrontier);

    // STEP 6 --- MAIN MULTI-THREADED RETROGRADE LOOP
    {
        int passes = 0;
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 8;

        while (!currentFrontier.empty()) {
            passes++;
            size_t frontierSize = currentFrontier.size();
            size_t chunkSize = (frontierSize + numThreads - 1) / numThreads;

            std::vector<std::vector<size_t>> localNextFrontiers(numThreads);
            std::vector<std::thread> threads;

            auto worker = [&](unsigned int tId, size_t startIdx, size_t endIdx) {
                localNextFrontiers[tId].reserve(chunkSize * 2);

                for (size_t q = startIdx; q < endIdx; ++q) {
                    size_t packedNode = currentFrontier[q];
                    bool isRobberTurn = (packedNode & ROBBER_TURN_BIT) != 0;
                    size_t stateId = packedNode & STATE_ID_MASK;
                    
                    size_t cId = stateId / N;
                    int r = stateId % N;

                    if (isRobberTurn) {
                        size_t copTransStart = transitionHeads[cId];
                        size_t copTransEnd = transitionHeads[cId + 1];
                        
                        for (size_t i = copTransStart; i < copTransEnd; ++i) {
                            size_t prevStateId = transitions[i] + r; 
                            
                            // MAGIC TRICK 1: exchange(1) returns the OLD value.
                            // If it returns 0, WE are the exact thread that changed it to 1.
                            if (copTurnWins[prevStateId].exchange(1, std::memory_order_relaxed) == 0) {
                                localNextFrontiers[tId].push_back(prevStateId); // Cop Turn (MSB 0)
                            }
                        }
                    } 
                    else {
                        // Lambda to handle the Robber's backward moves
                        auto processRobberMove = [&](size_t prevId) {
                            // MAGIC TRICK 2: fetch_sub(1) returns the OLD value.
                            // If it returns 1, WE delivered the killing blow to the Robber.
                            if (robberSafeMoves[prevId].fetch_sub(1, std::memory_order_relaxed) == 1) {
                                robberTurnWins[prevId].store(1, std::memory_order_relaxed);
                                localNextFrontiers[tId].push_back(prevId | ROBBER_TURN_BIT); // Robber Turn (MSB 1)
                            }
                        };

                        // 1. Robber stayed in place
                        processRobberMove(cId * N + r);

                        // 2. Robber moved from adjacent
                        uint8_t* rEdges = adj.getEdges(r);
                        for (int eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                            processRobberMove(cId * N + rEdges[eIdx]);
                        }
                    }
                }
            };

            // Spawn threads for this wave
            for (unsigned int i = 0; i < numThreads; ++i) {
                size_t startIdx = i * chunkSize;
                size_t endIdx = std::min(startIdx + chunkSize, frontierSize);
                if (startIdx >= frontierSize) break;
                threads.emplace_back(worker, i, startIdx, endIdx);
            }

            // Wait for wave to complete
            for (auto& t : threads) {
                t.join();
            }

            // --- MERGE PHASE ---
            size_t newFrontierSize = 0;
            for (unsigned int i = 0; i < numThreads; ++i) {
                newFrontierSize += localNextFrontiers[i].size();
            }

            currentFrontier.clear();
            currentFrontier.reserve(newFrontierSize); // Guarantee no reallocations during insert
            
            for (unsigned int i = 0; i < numThreads; ++i) {
                if (!localNextFrontiers[i].empty()) {
                    currentFrontier.insert(currentFrontier.end(), 
                                           localNextFrontiers[i].begin(), 
                                           localNextFrontiers[i].end());
                }
            }

            std::cout << "Wave " << passes << " merged. New states to process: " << newFrontierSize << "\n";
        }
    }

    // STEP 7 --- FINAL VERDICT ---
    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartConfigId = -1;

    for (size_t cId = 0; cId < configCount; ++cId) {
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
            std::cout << (int)configs[winningStartConfigId * k + i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";
    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    // --- CLEANUP ---
    delete[] configs;
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

    Graph g(filename);
    
    solveCopsAndRobbers(&g, k);

    return 0;
}
