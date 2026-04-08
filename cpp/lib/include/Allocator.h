#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <cstdint>

class Allocator {

    private:

        struct AllocRequest {
            void** targetPtr;
            size_t sizeBytes;
            size_t alignment;
            std::string name;
        };

        struct TrackedBlock {
            uint64_t sizeBytes;
            void* address;
            bool isPending;
        };

        std::vector<AllocRequest> pendingRequests;
        std::unordered_map<std::string, TrackedBlock> trackingMap;
        
        // Keeps track of the massive heap blocks so we can free them later
        std::vector<uint8_t*> memoryBlocks; 

        uint64_t totalAllocatedBytes;
        uint64_t totalPendingBytes;

    public:

        // Constructor
        Allocator();

        // Destructor automatically cleans up all arenas
        ~Allocator();

        // Template function safely extracts the size and alignment of the requested type
        template <typename T>
        void requestAlloc(const std::string& name, size_t count, T** targetPtr) {
            size_t sizeBytes = count * sizeof(T);
            size_t align = alignof(T);
            
            pendingRequests.push_back({reinterpret_cast<void**>(targetPtr), sizeBytes, align, name});
            
            trackingMap[name] = {sizeBytes, nullptr, true};
            totalPendingBytes += sizeBytes;
        }

        // Commits the allocations, building a single contiguous memory block
        void allocate();

        // Prints the current memory state, including pending and active allocations
        void print() const;

};
