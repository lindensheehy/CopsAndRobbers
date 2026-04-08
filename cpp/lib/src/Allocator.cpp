#include "Allocator.h"
#include <cstring>

Allocator::Allocator() {
    this->totalAllocatedBytes = 0;
    this->totalPendingBytes = 0;
}

Allocator::~Allocator() {
    for (uint8_t* block : this->memoryBlocks) {
        delete[] block;
    }
}

void Allocator::allocate() {
    
    if (this->pendingRequests.empty()) return;

    // 1. Calculate the exact total size needed, including alignment padding
    size_t currentOffset = 0;
    for (const auto& req : this->pendingRequests) {
        // Bitwise magic to push the offset forward to the nearest alignment boundary
        currentOffset = (currentOffset + req.alignment - 1) & ~(req.alignment - 1);
        currentOffset += req.sizeBytes;
    }

    // 2. Allocate the massive contiguous block
    uint8_t* massiveBlock = new uint8_t[currentOffset];
    
    // Safety zero-out (optional, but good for your DP tables)
    std::memset(massiveBlock, 0, currentOffset);
    
    this->memoryBlocks.push_back(massiveBlock);
    this->totalAllocatedBytes += currentOffset;

    // 3. Do a second pass to assign the calculated pointers
    currentOffset = 0;
    for (const auto& req : this->pendingRequests) {
        currentOffset = (currentOffset + req.alignment - 1) & ~(req.alignment - 1);
        
        // Write the location back to the user's double pointer
        *req.targetPtr = massiveBlock + currentOffset;

        // Update the tracking map
        this->trackingMap[req.name].address = massiveBlock + currentOffset;
        this->trackingMap[req.name].isPending = false;

        currentOffset += req.sizeBytes;
    }

    // 4. Clear pending state
    this->totalPendingBytes = 0;
    this->pendingRequests.clear();
}

void Allocator::print() const {
    
    auto printLine = [](const std::string& label, uint64_t bytes, void* address = nullptr, bool pending = false) {
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        
        std::cout << "[Memory] " << std::left << std::setw(25) << label 
                  << " => " << std::right << std::setw(12) << bytes << " B / " 
                  << std::fixed << std::setprecision(2) << std::setw(7) << mb << " MB / " 
                  << std::setw(5) << gb << " GB";
                  
        if (pending) {
            std::cout << " [PENDING]";
        } else if (address != nullptr) {
            std::cout << " @ " << address;
        }
        std::cout << "\n";
    };

    std::cout << "\n";
    printLine("--- Total Allocated", this->totalAllocatedBytes);
    if (this->totalPendingBytes > 0) {
        printLine("--- Total Pending", this->totalPendingBytes, nullptr, true);
    }
    
    if (!this->trackingMap.empty()) {
        std::cout << "[Memory] --- Drill Down ---\n";
        for (const auto& pair : this->trackingMap) {
            printLine("- " + pair.first, pair.second.sizeBytes, pair.second.address, pair.second.isPending);
        }
    }
    std::cout << "\n";
}
