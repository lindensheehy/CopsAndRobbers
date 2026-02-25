#include "Graph.h"
#include "AdjacencyList.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <iomanip>

// MSB is 1 for Robber's turn, 0 for Cop's turn. 
// The rest of the bits hold the stateId.
constexpr size_t ROBBER_TURN_BIT = (size_t)1 << (sizeof(size_t) * 8 - 1);
constexpr size_t STATE_ID_MASK = ~ROBBER_TURN_BIT;

// --- STATE BIT-PACKING CONSTANTS (uint8_t) ---
// We pack everything into 1 byte (std::atomic<uint8_t>)
// Bit 0: Cop Turn Win
// Bits 1-7: Robber Safe Moves Counter (Max value 127)
constexpr uint8_t COP_WIN_BIT      = 1 << 0;
constexpr uint8_t SAFE_MOVES_SHIFT = 1;
constexpr uint8_t SAFE_MOVES_MASK  = 0xFE; // 1111 1110

// --- PROCEDURAL HELPERS ---

// Generates all unique sorted cop configurations iteratively.
// outNumConfigs is passed by reference so the caller knows exactly how many states exist.
constexpr size_t MAX_COPS = 256;
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    
    // Failsafe for stack array size
    if (k > MAX_COPS) {
        std::cerr << "FATAL: Number of cops (k) exceeds maximum supported limit of " << MAX_COPS << ".\n";
        *outNumConfigs = 0;
        return nullptr;
    }

    // 1. Calculate exact state space size (Combinations with replacement)
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
    
    // 2. Print memory footprint
    size_t totalBytes = (*outNumConfigs) * k;
    std::cout << "Allocating " << totalBytes / (1024.0 * 1024.0 * 1024.0) 
              << " GB for " << *outNumConfigs << " cop configurations...\n";

    // 3. Allocate exact flat array
    uint8_t* configs = new uint8_t[totalBytes];
    
    // 4. Initialize the first configuration on the stack: [0, 0, ..., 0]
    uint8_t current[MAX_COPS];
    memset(current, 0, MAX_COPS);
    
    size_t offset = 0;
    
    // 5. Iteratively generate the next lexicographical combination
    while(true) {
        
        // Write the current configuration directly to our flat array
        memcpy(&(configs[offset]), current, k);
        offset += k;
        
        // Find the rightmost element that can be incremented
        int p = k - 1;
        while(p >= 0 && current[p] == N - 1) {
            p--;
        }
        
        // If all elements are N-1, we are done
        if (p < 0) break; 
        
        // Increment the found element
        current[p]++;
        
        // Set all subsequent elements to match this new value (maintaining sorted order)
        for(uint32_t i = p + 1; i < k; ++i) {
            current[i] = current[p];
        }

    }
    
    return configs;

}

// --- STEP 4: Allocate Game States (ATOMIC) ---
void allocateGameStates(size_t configCount, int N, std::atomic<uint8_t>*& outGameStates) {
    size_t numStates = configCount * N;

    std::cout << "Generating ATOMIC states...\n";
    std::cout << "Total States: " << numStates << "\n";
    std::cout << "Allocating Bit-Packed Atomic Memory Pool: " 
              << (numStates * sizeof(std::atomic<uint8_t>)) / (1024.0 * 1024.0 * 1024.0) << " GB\n";
    
    // 1. Single contiguous allocation of 8-bit integers
    outGameStates = new std::atomic<uint8_t>[numStates];

    // 2. Initialize atomics safely in one perfectly flat pass
    for (size_t i = 0; i < numStates; ++i) {
        outGameStates[i].store(0, std::memory_order_relaxed);
    }
}

// --- STEP 5: Initialize Captures ---
void initializeCaptures(size_t configCount, int k, int N, const uint8_t* configs, const AdjacencyList& adj,
                        std::atomic<uint8_t>* gameStates, std::vector<size_t>& currentFrontier) {
    
    uint8_t robberDegrees[256];
    for (int r = 0; r < N; ++r) {
        int eCount = 1; 
        uint8_t* edges = adj.getEdges(r);
        while (edges[eCount - 1] != 255) eCount++;
        robberDegrees[r] = static_cast<uint8_t>(eCount);
    }

    int initialWins = 0;
    auto lastPrintTime = std::chrono::steady_clock::now();
    
    for (size_t cId = 0; cId < configCount; ++cId) {
        
        // --- PROGRESS TRACKER ---
        if (cId % 4096 == 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPrintTime).count() >= 1) {
                std::cout << "\rInitializing Captures: " << (cId * 100) / configCount << "%" << std::flush;
                lastPrintTime = now;
            }
        }

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
                gameStates[stateId].store(COP_WIN_BIT, std::memory_order_relaxed);
                currentFrontier.push_back(stateId);                     
                currentFrontier.push_back(stateId | ROBBER_TURN_BIT);   
                initialWins++;
            } else {
                uint8_t packedDegree = static_cast<uint8_t>(robberDegrees[r]) << SAFE_MOVES_SHIFT;
                gameStates[stateId].store(packedDegree, std::memory_order_relaxed);
            }
        }
    }

    // Clear the progress line
    std::cout << "\rInitializing Captures: 100% completed.         \n";
    std::cout << "Initialized " << initialWins << " winning states (Captures).\n";
    std::cout << "Starting Multi-Threaded Level-Synchronous BFS...\n";
}

// --- MAIN ALGORITHM (LEAN MEMORY + PROGRESS TRACKING) ---
void solveCopsAndRobbers(Graph* g, int k) {

    int N = g->nodeCount;
    if (N == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return;
    }

    AdjacencyList adj(g);

    size_t configCount;
    uint8_t* configs = generateCopConfigs(k, N, &configCount);
    if (!configs || configCount == 0) return;

    std::atomic<uint8_t>* gameStates = nullptr;
    allocateGameStates(configCount, N, gameStates);

    std::vector<size_t> currentFrontier;
    currentFrontier.reserve(10000000); 

    initializeCaptures(configCount, k, N, configs, adj, gameStates, currentFrontier);

    size_t totalStateSpace = configCount * N * 2;
    size_t statesProcessedPriorWaves = 0;

    // STEP 4 --- MAIN MULTI-THREADED RETROGRADE LOOP
    {
        int passes = 0;
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 8;

        while (!currentFrontier.empty()) {
            passes++;
            size_t frontierSize = currentFrontier.size();
            
            std::cout << "Starting Wave " << passes << " (" << frontierSize << " states)...\n";

            std::vector<std::vector<size_t>> localNextFrontiers(numThreads);
            std::vector<std::thread> threads;
            
            // 1. THE ATOMIC WORK DISPENSER
            std::atomic<size_t> sharedIndex{0};
            const size_t BATCH_SIZE = 4096;

            auto worker = [&](unsigned int tId) {
                // Reserve a generous guess to prevent local reallocations
                localNextFrontiers[tId].reserve((frontierSize / numThreads) * 2);

                uint8_t options[MAX_COPS][256];
                int optionCount[MAX_COPS];
                int odometer[MAX_COPS];
                uint8_t moveConfig[MAX_COPS];
                
                auto lastPrintTime = std::chrono::steady_clock::now();

                // Dynamic Work Loop: Keep grabbing batches until the queue is empty
                while (true) {
                    size_t startIdx = sharedIndex.fetch_add(BATCH_SIZE, std::memory_order_relaxed);
                    if (startIdx >= frontierSize) break;
                    
                    size_t endIdx = std::min(startIdx + BATCH_SIZE, frontierSize);

                    // --- GLOBAL PROGRESS TRACKER (Thread 0 Only) ---
                    if (tId == 0) {
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPrintTime).count() >= 1) {
                            size_t totalProcessed = statesProcessedPriorWaves + startIdx;
                            double percent = (static_cast<double>(totalProcessed) / totalStateSpace) * 100.0;
                            
                            std::cout << std::fixed << std::setprecision(3);
                            std::cout << "\r  -> Global Progress: " << percent << "% (" 
                                      << totalProcessed << " / " << totalStateSpace << " states)" << std::flush;
                            lastPrintTime = now;
                        }
                    }

                    for (size_t q = startIdx; q < endIdx; ++q) {
                        size_t packedNode = currentFrontier[q];
                        bool isRobberTurn = (packedNode & ROBBER_TURN_BIT) != 0;
                        size_t stateId = packedNode & STATE_ID_MASK;
                        
                        size_t cId = stateId / N;
                        int r = stateId % N;

                        if (isRobberTurn) {
                            const uint8_t* currentCops = &configs[cId * k];
                            
                            // 1. Build movement options for each cop
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
                                odometer[i] = 0; 
                            }

                            // 2. Cartesian product to generate all previous configurations
                            while (true) {
                                for (int i = 0; i < k; ++i) {
                                    moveConfig[i] = options[i][odometer[i]];
                                }
                                
                                std::sort(moveConfig, moveConfig + k);
                                
                                // 3. Fast-path binary search (No std::memcmp overhead)
                                size_t prev_cId = static_cast<size_t>(-1); 
                                size_t left = 0;
                                size_t right = configCount - 1;
                                
                                while (left <= right) {
                                    size_t mid = left + (right - left) / 2;
                                    size_t mid_base = mid * k;
                                    int cmp = 0;

                                    // MAGIC TRICK 3: Unrolled Register Comparison
                                    if (k == 4) {
                                        if (configs[mid_base] != moveConfig[0]) cmp = configs[mid_base] < moveConfig[0] ? -1 : 1;
                                        else if (configs[mid_base + 1] != moveConfig[1]) cmp = configs[mid_base + 1] < moveConfig[1] ? -1 : 1;
                                        else if (configs[mid_base + 2] != moveConfig[2]) cmp = configs[mid_base + 2] < moveConfig[2] ? -1 : 1;
                                        else if (configs[mid_base + 3] != moveConfig[3]) cmp = configs[mid_base + 3] < moveConfig[3] ? -1 : 1;
                                    } else {
                                        for (int x = 0; x < k; ++x) {
                                            if (configs[mid_base + x] != moveConfig[x]) {
                                                cmp = configs[mid_base + x] < moveConfig[x] ? -1 : 1;
                                                break;
                                            }
                                        }
                                    }

                                    if (cmp == 0) {
                                        prev_cId = mid;
                                        break;
                                    }
                                    if (cmp < 0) left = mid + 1;
                                    else right = mid - 1;
                                }
                                
                                // 4. Process the valid previous state (Uses prev_cId)
                                if (prev_cId != static_cast<size_t>(-1)) {
                                    size_t prevStateId = prev_cId * N + r; 
                                    uint8_t oldVal = gameStates[prevStateId].fetch_or(COP_WIN_BIT, std::memory_order_relaxed);
                                    if ((oldVal & COP_WIN_BIT) == 0) {
                                        localNextFrontiers[tId].push_back(prevStateId); 
                                    }
                                }
                                
                                // 5. Advance odometer (Uses odometer and optionCount)
                                int p = k - 1;
                                while (p >= 0) {
                                    odometer[p]++;
                                    if (odometer[p] < optionCount[p]) break;
                                    odometer[p] = 0;
                                    p--;
                                }
                                if (p < 0) break;
                            }
                        } 
                        else {
                            auto processRobberMove = [&](size_t prevId) {
                                uint8_t oldVal = gameStates[prevId].fetch_sub(1 << SAFE_MOVES_SHIFT, std::memory_order_relaxed);
                                if (((oldVal & SAFE_MOVES_MASK) >> SAFE_MOVES_SHIFT) == 1) {
                                    localNextFrontiers[tId].push_back(prevId | ROBBER_TURN_BIT); 
                                }
                            };

                            processRobberMove(cId * N + r);

                            uint8_t* rEdges = adj.getEdges(r);
                            for (int eIdx = 0; rEdges[eIdx] != 255; eIdx++) {
                                processRobberMove(cId * N + rEdges[eIdx]);
                            }
                        }
                    }
                }
            };

            // Spawn the compute threads
            for (unsigned int i = 0; i < numThreads; ++i) {
                threads.emplace_back(worker, i);
            }
            for (auto& t : threads) {
                t.join();
            }

            // Clear the thread 0 progress line
            std::cout << "\r  -> Global Progress: Wave " << passes << " complete.                               \n";

            // Add this wave's size to the running total
            statesProcessedPriorWaves += frontierSize;

            // --- 2. THE PARALLEL MERGE PHASE ---
            size_t newFrontierSize = 0;
            std::vector<size_t> threadOffsets(numThreads, 0);
            
            // Calculate exact start positions for each thread's chunk
            for (unsigned int i = 0; i < numThreads; ++i) {
                threadOffsets[i] = newFrontierSize;
                newFrontierSize += localNextFrontiers[i].size();
            }

            // Allocate the exact size needed, all at once
            currentFrontier.resize(newFrontierSize); 
            
            // Lambda for parallel copying
            auto mergeWorker = [&](unsigned int tId) {
                if (!localNextFrontiers[tId].empty()) {
                    std::copy(localNextFrontiers[tId].begin(), 
                              localNextFrontiers[tId].end(), 
                              currentFrontier.begin() + threadOffsets[tId]);
                }
            };

            // Spawn the merge threads
            threads.clear();
            for (unsigned int i = 0; i < numThreads; ++i) {
                threads.emplace_back(mergeWorker, i);
            }
            for (auto& t : threads) {
                t.join();
            }

            std::cout << "Wave " << passes << " merged. New states to process: " << newFrontierSize << "\n\n";
        }
    }

    std::cout << "\n--- FINAL VERDICT ---\n";
    int winningStartConfigId = -1;

    for (size_t cId = 0; cId < configCount; ++cId) {
        bool universalWin = true;
        for (int rStart = 0; rStart < N; ++rStart) {
            size_t stateId = cId * N + rStart;
            if (!(gameStates[stateId].load(std::memory_order_relaxed) & COP_WIN_BIT)) {
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

    delete[] configs;
    delete[] gameStates;
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
