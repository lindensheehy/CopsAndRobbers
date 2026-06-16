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
    static std::string buildCacheKey(const std::string& type, const std::string& algoName, const std::string& graphName, int k, int p, SelfEdgeCop c, SelfEdgeRobber r) {
        std::string dir = "cache/";
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        
        std::string cleanName = std::filesystem::path(graphName).stem().string();
        std::string flags = (c == SelfEdgeCop::TRUE ? "1" : "0") + std::string(r == SelfEdgeRobber::TRUE ? "1" : "0");
        
        return dir + algoName + "__" + cleanName + "__" + std::to_string(k) + "-cops__" + std::to_string(p) + "-vis__" + flags + "__" + type + ".bin";
    }

    template <typename T>
    static bool saveAuxGraph(const std::string& algoName, const std::string& graphName, int k, int p, SelfEdgeCop c, SelfEdgeRobber r, AuxGraph<T>* inGraph) {

        std::string filepath = buildCacheKey("auxgraph", algoName, graphName, k, p, c, r);

        // Calculate section byte sizes (same as before)
        uint64_t configsSize    = static_cast<uint64_t>(inGraph->configCount) * inGraph->k * sizeof(uint8_t);
        uint64_t transHeadsSize = static_cast<uint64_t>(inGraph->configCount + 1) * sizeof(size_t);
        uint64_t transDataSize  = static_cast<uint64_t>(inGraph->transitions.size()) * sizeof(size_t);
        uint64_t statesSize     = static_cast<uint64_t>(inGraph->numStates) * sizeof(T);

        // Expanded to 12 fields (96 bytes)
        uint64_t headerSize = 12 * sizeof(uint64_t); 
                
        uint64_t sec1_offset = headerSize;
        uint64_t sec2_offset = sec1_offset + configsSize;
        uint64_t sec3_offset = sec2_offset + transHeadsSize;
        uint64_t sec4_offset = sec3_offset + transDataSize;
        uint64_t totalBlobSize = sec4_offset + statesSize;

        uint8_t* blob = new uint8_t[totalBlobSize];

        uint64_t header[12] = {
            static_cast<uint64_t>(inGraph->k),
            static_cast<uint64_t>(inGraph->N),
            static_cast<uint64_t>(inGraph->columns),
            static_cast<uint64_t>(inGraph->configCount),
            static_cast<uint64_t>(inGraph->numStates),
            static_cast<uint64_t>(sizeof(T)),
            static_cast<uint64_t>(inGraph->copSelfEdges == SelfEdgeCop::TRUE ? 1 : 0),
            static_cast<uint64_t>(inGraph->robberSelfEdges == SelfEdgeRobber::TRUE ? 1 : 0),
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
    static bool loadAuxGraph(const std::string& algoName, const std::string& graphName, int k, int p, SelfEdgeCop reqCop, SelfEdgeRobber reqRobber, AuxGraph<T>* outGraph, Allocator* mem, const AdjacencyList* adj) {

        std::string filepath = buildCacheKey("auxgraph", algoName, graphName, k, p, reqCop, reqRobber);

        uintmax_t fileSize = 0;
        uint8_t* blob = readFile(filepath.c_str(), &fileSize);
        if (!blob) return 1; 

        uint64_t* header = reinterpret_cast<uint64_t*>(blob);

        uint64_t expectedCop = (reqCop == SelfEdgeCop::TRUE) ? 1 : 0;
        uint64_t expectedRobber = (reqRobber == SelfEdgeRobber::TRUE) ? 1 : 0;

        // Security check
        if (header[6] != expectedCop || header[7] != expectedRobber) {
            std::cerr << "Cache rules mismatch: Cached SelfEdges [" << header[6] << header[7] << "] vs Requested [" << expectedCop << expectedRobber << "]\n";
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

        uint64_t sec1_offset = header[8];
        uint64_t sec2_offset = header[9];
        uint64_t sec3_offset = header[10];
        uint64_t sec4_offset = header[11];

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
