#pragma once

#include <cstdint>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "VertexBitField.h"

class PackedAdjMatrix {
    private:

        // Bitfields for each vertex's neighbors.
        // adj[i] contains a 1 for every vertex reachable from i in 1 step.
        VertexBitField adj[200];
        
    public:

        // Sets an undirected edge
        inline void addEdge(size_t u, size_t v) {
            this->adj[u].set(v);
            this->adj[v].set(u);
        }

        // Cross-compiler wrapper for 64-bit Count Trailing Zeros
        inline int getLowestSetBit(uint64_t mask) const {
            #ifdef _MSC_VER
                unsigned long bitPos;
                _BitScanForward64(&bitPos, mask);
                return static_cast<int>(bitPos);
            #else
                return __builtin_ctzll(mask);
            #endif
        }

        // Returns a new bitfield representing the robber set after allowing at most 1 move from any node in the existing set
        inline VertexBitField expand(const VertexBitField& currentSet, bool allowPass = true) const {

            // If the robber can stay still, their next reachable cloud inherently
            // includes everywhere they already are.
            VertexBitField expandedSet = allowPass ? currentSet : VertexBitField();

            for (int w = 0; w < 4; ++w) {
                uint64_t mask = currentSet.words[w];

                while (mask != 0) {

                    // 1. Find the lowest set bit
                    int bitPos = this->getLowestSetBit(mask);

                    // 2. Map the local 0-63 bit position to the global 0-199 vertex ID
                    int vertexId = (w << 6) + bitPos;
                    
                    // 3. AVX2 bitwise OR the neighbor cloud into the new set
                    expandedSet = VertexBitField::binaryOr(expandedSet, this->adj[vertexId]);
                    
                    // 4. Instantly erase the lowest set bit to jump to the next one
                    mask &= (mask - 1);

                }
            }

            return expandedSet;

        }

        // Extracts all set vertex IDs into a provided buffer
        // Returns true if the buffer was too short, false otherwise
        inline bool extractVertices(const VertexBitField& field, uint8_t* outBuffer, size_t bufferSize, size_t& bytesWrittenOut) const {

            size_t count = 0;

            for (int w = 0; w < 4; ++w) {
                uint64_t mask = field.words[w];
                
                while (mask != 0) {
                    int bitPos = getLowestSetBit(mask);
                    outBuffer[count++] = static_cast<uint8_t>((w << 6) + bitPos);
                    mask &= (mask - 1);
                    
                    if (count >= bufferSize) {
                        bytesWrittenOut = count;
                        return true;
                    }
                }
            }

            bytesWrittenOut = count;
            return false;
        }

};