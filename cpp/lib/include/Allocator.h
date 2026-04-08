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
            bool isExternal; 
            int blockId; // Groups internally managed allocations by their parent arena
        };

        std::vector<AllocRequest> pendingRequests;
        std::unordered_map<std::string, TrackedBlock> trackingMap;
        
        // Keeps track of the massive heap blocks so we can free them later
        std::vector<uint8_t*> memoryBlocks; 

        uint64_t totalAllocatedBytes;
        uint64_t totalPendingBytes;
        uint64_t totalExternalBytes; // Tracks unmanaged memory separately

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
            
            // blockId is initialized to -1 while pending
            trackingMap[name] = {sizeBytes, nullptr, true, false, -1};
            totalPendingBytes += sizeBytes;
        }

        // Registers an allocation not owned by this allocator purely for profiling
        void trackExternal(const std::string& name, size_t sizeBytes, void* address = nullptr);

        // Commits the allocations, building a single contiguous memory block
        void allocate();

        // Prints the current memory state, including pending and active allocations
        void print() const;

};
