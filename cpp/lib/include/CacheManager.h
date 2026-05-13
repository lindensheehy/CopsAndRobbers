#pragma once

#include <string>
#include <filesystem>
#include <cstring>
#include <iostream>
#include "AuxGraph.h"
#include "Allocator.h"
#include "fileio.h"

class CacheManager {

public:

    // Added 'int p' to the signature
    static std::string buildCacheKey(const std::string& type, const std::string& algoName, const std::string& graphName, int k, int p) {
        std::string dir = "cache/";
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        
        std::string cleanName = std::filesystem::path(graphName).stem().string();
        
        // Injected 'p' into the deterministic file name
        return dir + algoName + "__" + cleanName + "__" + std::to_string(k) + "-cops__" + std::to_string(p) + "-vis__" + type + ".bin";
    }

    template <typename T>
    static bool saveAuxGraph(const std::string& algoName, const std::string& graphName, int k, int p, AuxGraph<T>* inGraph) {

        // Pass 'p' down to buildCacheKey
        std::string filepath = buildCacheKey("auxgraph", algoName, graphName, k, p);

        // Calculate section byte sizes
        uint64_t configsSize    = static_cast<uint64_t>(inGraph->configCount) * inGraph->k * sizeof(uint8_t);
        uint64_t transHeadsSize = static_cast<uint64_t>(inGraph->configCount + 1) * sizeof(size_t);
        uint64_t transDataSize  = static_cast<uint64_t>(inGraph->transitions.size()) * sizeof(size_t);
        uint64_t statesSize     = static_cast<uint64_t>(inGraph->numStates) * sizeof(T);

        uint64_t headerSize = 10 * sizeof(uint64_t); 
                
        uint64_t sec1_offset = headerSize;
        uint64_t sec2_offset = sec1_offset + configsSize;
        uint64_t sec3_offset = sec2_offset + transHeadsSize;
        uint64_t sec4_offset = sec3_offset + transDataSize;
        uint64_t totalBlobSize = sec4_offset + statesSize;

        // Allocate contiguous blob
        uint8_t* blob = new uint8_t[totalBlobSize];

        uint64_t header[10] = {
            static_cast<uint64_t>(inGraph->k),
            static_cast<uint64_t>(inGraph->N),
            static_cast<uint64_t>(inGraph->columns),
            static_cast<uint64_t>(inGraph->configCount),
            static_cast<uint64_t>(inGraph->numStates),
            static_cast<uint64_t>(sizeof(T)),
            sec1_offset, sec2_offset, sec3_offset, sec4_offset
        };

        // Pack the data
        std::memcpy(blob, header, headerSize);
        std::memcpy(blob + sec1_offset, inGraph->configs, configsSize);
        std::memcpy(blob + sec2_offset, inGraph->transitionHeads, transHeadsSize);
        std::memcpy(blob + sec3_offset, inGraph->transitions.data(), transDataSize);
        std::memcpy(blob + sec4_offset, inGraph->states, statesSize);

        // Hand off to fileio
        bool isError = createFile(filepath.c_str(), blob, totalBlobSize);
        delete[] blob;

        return isError;

    }

    template <typename T>
    static bool loadAuxGraph(const std::string& algoName, const std::string& graphName, int k, int p, AuxGraph<T>* outGraph, Allocator* mem, const AdjacencyList* adj) {

        // Pass 'p' down to buildCacheKey
        std::string filepath = buildCacheKey("auxgraph", algoName, graphName, k, p);

        uintmax_t fileSize = 0;
        uint8_t* blob = readFile(filepath.c_str(), &fileSize);
        
        // Cache miss
        if (!blob) return 1; 

        uint64_t* header = reinterpret_cast<uint64_t*>(blob);

        if (header[5] != sizeof(T)) {
            std::cerr << "Cache type mismatch: Cached size " << header[5] << " vs Current " << sizeof(T) << "\n";
            delete[] blob;
            return 1;
        }

        // Sanity check: Ensure the cached columns match our requested 2 * p
        if (header[2] != static_cast<uint64_t>(p * 2)) {
            std::cerr << "Cache visibility mismatch: Cached columns " << header[2] << " vs Requested " << (p * 2) << "\n";
            delete[] blob;
            return 1;
        }

        // Read metadata
        outGraph->k = header[0];
        outGraph->N = header[1];
        outGraph->columns = header[2];
        outGraph->configCount = header[3];
        outGraph->numStates = header[4];

        outGraph->mem = mem;
        outGraph->adj = adj;

        uint64_t sec1_offset = header[6];
        uint64_t sec2_offset = header[7];
        uint64_t sec3_offset = header[8];
        uint64_t sec4_offset = header[9];

        // 1. Calculate precise byte sizes directly from the header offsets
        size_t configsBytes     = sec2_offset - sec1_offset;
        size_t transHeadsBytes  = sec3_offset - sec2_offset;
        size_t transitionsBytes = sec4_offset - sec3_offset;
        size_t statesBytes      = fileSize - sec4_offset;

        // 2. Allocate and copy
        outGraph->configs = new uint8_t[configsBytes];
        std::memcpy(outGraph->configs, blob + sec1_offset, configsBytes);

        outGraph->transitionHeads = new size_t[transHeadsBytes / sizeof(size_t)];
        std::memcpy(outGraph->transitionHeads, blob + sec2_offset, transHeadsBytes);

        outGraph->transitions.resize(transitionsBytes / sizeof(size_t));
        std::memcpy(outGraph->transitions.data(), blob + sec3_offset, transitionsBytes);

        outGraph->states = new T[statesBytes / sizeof(T)];
        std::memcpy(outGraph->states, blob + sec4_offset, statesBytes);

        // Track allocations externally if needed
        mem->trackExternal("Cache: AuxGraph configs", configsBytes);
        mem->trackExternal("Cache: AuxGraph transHeads", transHeadsBytes);
        mem->trackExternal("Cache: AuxGraph transitions", transitionsBytes);
        mem->trackExternal("Cache: AuxGraph states", statesBytes);

        delete[] blob;
        return 0; 

    }

};
