#pragma once

#include "AdjacencyList.h"
#include "Allocator.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iostream>

// Maximum supported number of cops to prevent stack overflow during generation
constexpr size_t MAX_COPS = 256;

template <typename StateData>
class AuxGraph {
public:
    int k;
    int N;
    size_t configCount;
    size_t numStates;

    uint8_t* configs;
    size_t* transitionHeads;
    std::vector<size_t> transitions;
    
    // The tightly bundled AoS DP Table
    StateData* states;

    const AdjacencyList* adj;

    AuxGraph() : k(0), N(0), configCount(0), numStates(0), configs(nullptr), 
          transitionHeads(nullptr), states(nullptr), adj(nullptr), mem(nullptr) {}

    // Constructor: Generates configs, queues memory, and builds transitions
    AuxGraph(int k, const AdjacencyList* adj, Allocator* mem) 
        : k(k), N(N), configCount(0), numStates(0), configs(nullptr), 
          transitionHeads(nullptr), states(nullptr), adj(adj), mem(mem) {
        this->constructFrom(k, adj, mem);
    }

    // Destructor: Cleans up the raw transitionHeads array
    ~AuxGraph() {
        delete[] this->transitionHeads;
    }
    

    // Deferred constructor
    void constructFrom(int k, const AdjacencyList* adj, Allocator* mem) {

        if (mem == nullptr || adj == nullptr) return;

        this->k = k;
        this->N = adj->nodeCount;
        this->adj = adj;
        this->mem = mem;

        // 1. Generate Configurations
        this->generateCopConfigs();
        
        if (this->configCount == 0) return;
        this->numStates = this->configCount * N;

        this->mem->requestAlloc<StateData>("AuxGraph Per State Data", this->numStates, &this->states);
        this->mem->allocate();

        // 3. Build the Transition Table
        this->createTransitions();

    }

    // --- Core Accessors ---

    // Maps a cop configuration ID and a robber position to a 1D state ID
    inline StateData* getState(size_t cId, int r) const {
        return &(this->states[cId * N + r]);
    }

    // Fetches the exact transition boundaries for a given cop configuration
    inline void getCopTransitions(size_t cId, size_t& startIdx, size_t& endIdx) const {
        startIdx = transitionHeads[cId];
        endIdx = transitionHeads[cId + 1];
    }

    // Evaluates if a specific state is an instant capture
    inline bool isInstantCatch(size_t cId, int r) const {
        const uint8_t* currentCops = &configs[cId * k];
        for (int i = 0; i < k; ++i) {
            if (currentCops[i] == r) return true;
        }
        return false;
    }

private:
    Allocator* mem;

    void generateCopConfigs() {
        if (this->k <= 0 || this->k > static_cast<int>(MAX_COPS)) {
            std::cerr << "FATAL: Number of cops (k) exceeds maximum supported limit of " << MAX_COPS << ".\n";
            this->configCount = 0;
            return;
        }

        int n_val = this->N + this->k - 1;
        int k_val = this->k;
        
        if (k_val > n_val) {
            this->configCount = 0;
        } else if (k_val == 0 || k_val == n_val) {
            this->configCount = 1;
        } else {
            if (k_val > n_val / 2) k_val = n_val - k_val;
            size_t res = 1;
            for (int i = 1; i <= k_val; ++i) {
                res = res * (n_val - i + 1) / i;
            }
            this->configCount = res;
        }
        
        if (this->configCount == 0) return;

        size_t totalBytes = this->configCount * this->k;
        
        if (this->mem != nullptr) {
            this->mem->requestAlloc<uint8_t>("Cop Configs", totalBytes, &this->configs);
            this->mem->allocate();
        } else {
            std::cerr << "FATAL: Allocator pointer is null.\n";
            this->configCount = 0;
            return;
        }

        if (this->configs == nullptr) return;
        
        uint8_t current[MAX_COPS];
        std::memset(current, 0, MAX_COPS);
        
        size_t offset = 0;
        
        while(true) {
            std::memcpy(&(this->configs[offset]), current, this->k);
            offset += this->k;
            
            int p = this->k - 1;
            while(p >= 0 && current[p] == this->N - 1) {
                p--;
            }
            
            if (p < 0) break; 
            
            current[p]++;
            
            for(uint32_t i = p + 1; i < (uint32_t)this->k; ++i) {
                current[i] = current[p];
            }
        }
    }

    void createTransitions() {
        this->transitionHeads = new size_t[this->configCount + 1]();
        this->transitions.reserve(this->configCount * 8); 

        std::vector<size_t> tempMoves;
        tempMoves.reserve(1024); 
        size_t peakTempMovesCapacity = 0;

        std::cout << "Building AuxGraph transition table for " << this->configCount << " configurations...\n";

        uint8_t options[MAX_COPS][256];
        int optionCount[MAX_COPS];
        int odometer[MAX_COPS];
        uint8_t moveConfig[MAX_COPS];

        for (size_t cId = 0; cId < this->configCount; cId++) {
            tempMoves.clear(); 
            const uint8_t* currentCops = &this->configs[cId * this->k];
            
            for (int i = 0; i < this->k; i++) {
                uint8_t u = currentCops[i];
                options[i][0] = u; 
                int count = 1;
                
                uint8_t* edges = this->adj->getEdges(u);
                int eIdx = 0;
                while (edges[eIdx] != 255) {
                    options[i][count++] = edges[eIdx++];
                }
                optionCount[i] = count;
            }

            std::memset(odometer, 0, MAX_COPS * sizeof(int));
            
            while (true) {
                for (int i = 0; i < this->k; ++i) {
                    moveConfig[i] = options[i][odometer[i]];
                }
                
                std::sort(moveConfig, moveConfig + this->k);
                size_t nextId = static_cast<size_t>(-1); 
                size_t left = 0;
                size_t right = this->configCount - 1;
                
                while (left <= right) {
                    size_t mid = left + (right - left) / 2;
                    int cmp = std::memcmp(&this->configs[mid * this->k], moveConfig, this->k);
                    if (cmp == 0) {
                        nextId = mid;
                        break;
                    }
                    if (cmp < 0) {
                        left = mid + 1;
                    } else {
                        right = mid - 1;
                    }
                }
                
                tempMoves.push_back(nextId * this->N);
                
                int p = this->k - 1;
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

            peakTempMovesCapacity = std::max(peakTempMovesCapacity, tempMoves.capacity());
            
            this->transitions.insert(this->transitions.end(), tempMoves.begin(), tempMoves.end());
            this->transitionHeads[cId + 1] = this->transitions.size();
        }

        this->transitions.shrink_to_fit();

        if (this->mem != nullptr) {
            size_t headsBytes = (this->configCount + 1) * sizeof(size_t);
            size_t transitionsBytes = this->transitions.capacity() * sizeof(size_t);
            size_t peakTempBytes = peakTempMovesCapacity * sizeof(size_t);
            
            this->mem->trackExternal("AuxGraph: Heads", headsBytes, this->transitionHeads);
            this->mem->trackExternal("AuxGraph: Edges", transitionsBytes, this->transitions.data());
            this->mem->trackExternal("tempMoves (Peak Buffer)", peakTempBytes, nullptr);
        }

        std::cout << "Transitions generated. Total edge pointers: " << this->transitions.size() << "\n";
    }
};
