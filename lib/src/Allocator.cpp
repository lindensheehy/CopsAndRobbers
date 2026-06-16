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
    
    // Calculate global total upfront so all percentages are relative to it
    uint64_t totalFootprint = this->totalAllocatedBytes + this->totalExternalBytes + this->totalPendingBytes;

    // Group the allocations by their lifecycle/arena
    std::map<int, std::vector<std::string>> managedBlocks;
    std::vector<std::string> externalNames;
    std::vector<std::string> pendingNames;

    size_t maxTier2 = 0; 
    size_t maxTier1 = 0; 
    size_t maxTier0 = 0;

    // Evaluate lengths of Tier 0 static headers
    maxTier0 = std::max({
        std::string("   Total Footprint ").length(),
        std::string("   Managed Internally ").length(),
        std::string("   Tracked Externally ").length(),
        std::string("   Pending Requests ").length()
    });

    // Evaluate lengths of Tier 1 static headers
    maxTier1 = std::max({
        std::string("    -> Managed Internally ").length(),
        std::string("    -> Tracked Externally ").length()
    });

    for (const auto& pair : this->trackingMap) {
        if (pair.second.isPending) {
            pendingNames.push_back(pair.first);
            maxTier1 = std::max(maxTier1, std::string("    -> " + pair.first + " ").length());
        } else if (pair.second.isExternal) {
            externalNames.push_back(pair.first);
            maxTier1 = std::max(maxTier1, std::string("    -> " + pair.first + " ").length());
        } else {
            managedBlocks[pair.second.blockId].push_back(pair.first);
            maxTier2 = std::max(maxTier2, std::string("      -> " + pair.first + " ").length());
        }
    }
    
    for (const auto& blockPair : managedBlocks) {
        maxTier1 = std::max(maxTier1, std::string("    -> Arena Block " + std::to_string(blockPair.first) + " ").length());
    }

    // Apply exact padding rules based on the longest bottom tier
    size_t A2 = maxTier2 + 3; // Baseline minimum alignment for Tier 2 ("-" + "=>" = 3)
    
    // Safety expansion in case upper tiers have extremely long names
    if (A2 < maxTier1 - 2) A2 = maxTier1 - 2;
    if (A2 < maxTier0 - 7) A2 = maxTier0 - 7;

    // Output formatting lambda
    auto printLine = [&](int tier, const std::string& prefixStr, uint64_t bytes, bool showPercent = false) {
        size_t L = prefixStr.length();
        
        // Calculate the absolute column alignment based on the tier
        size_t targetAlign = A2 + (2 - tier) * 5; 
        
        size_t numHyphens = (targetAlign > L + 2) ? (targetAlign - L - 2) : 1;
        
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        
        std::cout << "||" << prefixStr << std::string(numHyphens, '-') << "=> "
                  << std::right << std::setw(12) << bytes << " B / " 
                  << std::fixed << std::setprecision(2) << std::setw(7) << mb << " MB / " 
                  << std::setw(5) << gb << " GB";
                  
        if (showPercent) {
            if (totalFootprint > 0) {
                double pct = (static_cast<double>(bytes) / static_cast<double>(totalFootprint)) * 100.0;
                std::cout << " (" << std::fixed << std::setprecision(2) << pct << "%)";
            } else {
                std::cout << " (0.00%)";
            }
        }
        std::cout << "\n";
    };

    // --- RENDER REPORT ---

    std::cout << "\n||>>>>>=====-----=====<<<<<     Memory Tracking Report     >>>>>=====-----=====<<<<<\n";
    std::cout << "||\n";
    
    printLine(0, "   Total Footprint ", totalFootprint, true);
    if (this->totalAllocatedBytes > 0) {
        printLine(1, "    -> Managed Internally ", this->totalAllocatedBytes, true);
    }
    if (this->totalExternalBytes > 0) {
        printLine(1, "    -> Tracked Externally ", this->totalExternalBytes, true);
    }
    if (this->totalPendingBytes > 0) {
        printLine(1, "    -> Pending Requests ", this->totalPendingBytes, true);
    }
    
    std::cout << "||\n||\n";
    std::cout << "||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---\n";
    std::cout << "||\n";

    if (!managedBlocks.empty()) {
        printLine(0, "   Managed Internally ", this->totalAllocatedBytes);
        std::cout << "||\n";
        for (auto& blockPair : managedBlocks) {
            
            // Calculate aggregate size of this specific arena block
            uint64_t blockSize = 0;
            for (const auto& name : blockPair.second) blockSize += this->trackingMap.at(name).sizeBytes;
            
            printLine(1, "    -> Arena Block " + std::to_string(blockPair.first + 1) + " ", blockSize, true);
            
            // Sort to print sequentially in physical memory order
            std::sort(blockPair.second.begin(), blockPair.second.end(), [&](const std::string& a, const std::string& b) {
                return this->trackingMap.at(a).address < this->trackingMap.at(b).address;
            });
            
            for (const std::string& name : blockPair.second) {
                printLine(2, "      -> " + name + " ", this->trackingMap.at(name).sizeBytes);
            }
            std::cout << "||\n";
        }
    }
    
    if (!externalNames.empty()) {
        printLine(0, "   Tracked Externally ", this->totalExternalBytes);
        for (const std::string& name : externalNames) {
            printLine(1, "    -> " + name + " ", this->trackingMap.at(name).sizeBytes, true);
        }
        std::cout << "||\n";
    }
    
    if (!pendingNames.empty()) {
        printLine(0, "   Pending Requests ", this->totalPendingBytes);
        for (const std::string& name : pendingNames) {
            printLine(1, "    -> " + name + " ", this->trackingMap.at(name).sizeBytes, true);
        }
        std::cout << "||\n";
    }
    
    std::cout << "||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<\n\n";
}
