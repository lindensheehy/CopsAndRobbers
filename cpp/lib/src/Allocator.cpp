#include "Allocator.h"
#include <cstring>
#include <map>
#include <algorithm>

Allocator::Allocator() {
    this->totalAllocatedBytes = 0;
    this->totalPendingBytes = 0;
    this->totalExternalBytes = 0;
}

Allocator::~Allocator() {
    for (uint8_t* block : this->memoryBlocks) {
        delete[] block;
    }
}

void Allocator::trackExternal(const std::string& name, size_t sizeBytes, void* address) {
    // Add directly to the tracking map as active and external (blockId = -1)
    this->trackingMap[name] = {sizeBytes, address, false, true, -1};
    this->totalExternalBytes += sizeBytes;
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
    
    // The ID of this new arena will just be its index in the memoryBlocks vector
    int currentBlockId = static_cast<int>(this->memoryBlocks.size());
    
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

        // Update the tracking map with address, active state, and its parent arena ID
        this->trackingMap[req.name].address = massiveBlock + currentOffset;
        this->trackingMap[req.name].isPending = false;
        this->trackingMap[req.name].blockId = currentBlockId;

        currentOffset += req.sizeBytes;
    }

    // 4. Clear pending state
    this->totalPendingBytes = 0;
    this->pendingRequests.clear();
}

void Allocator::print() const {
    
    // 1. Scan to find the longest label length
    size_t maxLabelLen = 22; 
    
    // 2. Group the allocations by their lifecycle/arena while we find the max length
    std::map<int, std::vector<std::string>> managedBlocks;
    std::vector<std::string> externalNames;
    std::vector<std::string> pendingNames;

    for (const auto& pair : this->trackingMap) {
        maxLabelLen = std::max(maxLabelLen, pair.first.length() + 2); 
        
        if (pair.second.isPending) {
            pendingNames.push_back(pair.first);
        } else if (pair.second.isExternal) {
            externalNames.push_back(pair.first);
        } else {
            managedBlocks[pair.second.blockId].push_back(pair.first);
        }
    }

    // Define the absolute column position for the '>' character
    size_t alignWidth = maxLabelLen + 4; 

    // Modified lambda to take an optional offset parameter
    auto printLine = [&](const std::string& label, uint64_t bytes, void* address = nullptr, bool pending = false, bool external = false, int64_t offset = -1) {
        
        size_t numEquals = alignWidth - label.length() - 2; 
        std::string prefix = label + " " + std::string(numEquals, '=') + "> ";

        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        
        std::cout << "[Memory] " << prefix 
                  << std::right << std::setw(12) << bytes << " B / " 
                  << std::fixed << std::setprecision(2) << std::setw(7) << mb << " MB / " 
                  << std::setw(5) << gb << " GB";
                  
        if (pending) {
            std::cout << " [PENDING]";
        } else if (external) {
            std::cout << " [EXTERNAL]";
            if (address != nullptr) std::cout << " @ " << address;
        } else if (offset >= 0) {
            std::cout << " [OFFSET: +" << offset << " B]";
        } else if (address != nullptr) {
            std::cout << " @ " << address;
        }
        std::cout << "\n";
    };

    std::cout << "\n";
    
    // Print summaries
    uint64_t totalFootprint = this->totalAllocatedBytes + this->totalExternalBytes;
    printLine("--- Total Footprint", totalFootprint);
    
    if (this->totalAllocatedBytes > 0) {
        printLine("--- Managed Internally", this->totalAllocatedBytes);
    }
    if (this->totalExternalBytes > 0) {
        printLine("--- Tracked Externally", this->totalExternalBytes);
    }
    if (this->totalPendingBytes > 0) {
        printLine("--- Total Pending", this->totalPendingBytes, nullptr, true);
    }
    
    if (!this->trackingMap.empty()) {
        std::cout << "[Memory] --- Drill Down ---\n";
        
        // Print Externally Tracked Data
        if (!externalNames.empty()) {
            std::cout << "[Memory] --- External Trackers ---\n";
            for (const std::string& name : externalNames) {
                const auto& tb = this->trackingMap.at(name);
                printLine("- " + name, tb.sizeBytes, tb.address, tb.isPending, tb.isExternal);
            }
        }
        
        // Print Pending Allocations
        if (!pendingNames.empty()) {
            std::cout << "[Memory] --- Pending Requests ---\n";
            for (const std::string& name : pendingNames) {
                const auto& tb = this->trackingMap.at(name);
                printLine("- " + name, tb.sizeBytes, tb.address, tb.isPending, tb.isExternal);
            }
        }

        // Print Internally Managed Blocks AT THE BOTTOM
        if (!managedBlocks.empty()) {
            std::cout << "[Memory] \n";
            for (auto& blockPair : managedBlocks) {
                std::cout << "[Memory] --- Arena Block " << blockPair.first << " ---\n";
                
                // Sort by physical address so the layout prints in true sequential order
                std::sort(blockPair.second.begin(), blockPair.second.end(), [&](const std::string& a, const std::string& b) {
                    return this->trackingMap.at(a).address < this->trackingMap.at(b).address;
                });

                uint8_t* baseAddress = this->memoryBlocks[blockPair.first];
                for (const std::string& name : blockPair.second) {
                    const auto& tb = this->trackingMap.at(name);
                    int64_t offset = static_cast<uint8_t*>(tb.address) - baseAddress;
                    printLine("- " + name, tb.sizeBytes, nullptr, false, false, offset);
                }
            }
        }
    }
    std::cout << "\n";
}
