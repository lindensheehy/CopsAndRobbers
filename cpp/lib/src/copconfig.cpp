#include "copconfig.h"
#include <iostream>
#include <cstring>

uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs) {
    
    // Failsafe for stack array size
    if (k > MAX_COPS) {
        std::cerr << "FATAL: Number of cops (k) exceeds maximum supported limit of " << MAX_COPS << ".\n";
        *outNumConfigs = 0;
        return nullptr;
    }

    // 1. Calculate exact state space size (Combinations with replacement)
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
    
    if (*outNumConfigs == 0) return nullptr;

    // 2. Allocate exact flat array
    size_t totalBytes = (*outNumConfigs) * k;
    uint8_t* configs = new uint8_t[totalBytes];
    
    // 3. Initialize the first configuration on the stack: [0, 0, ..., 0]
    uint8_t current[MAX_COPS];
    std::memset(current, 0, MAX_COPS);
    
    size_t offset = 0;
    
    // 4. Iteratively generate the next lexicographical combination
    while(true) {
        
        // Write the current configuration directly to our flat array
        std::memcpy(&(configs[offset]), current, k);
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
