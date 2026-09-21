#pragma once

#include <cstdint>
#include <cstring>

struct VertexBitField {
    uint64_t words[4] = {0, 0, 0, 0};

    // Reset all bits to 0 (Empty set)
    inline void clear() {
        memset(words, 0x00, sizeof(uint64_t) * 4);
    }

    // Set a specific vertex bit
    inline void set(size_t vertex) {
        words[vertex >> 6] |= (1ULL << (vertex & 63));
    }

    // Clear a specific vertex bit
    inline void reset(size_t vertex) {
        words[vertex >> 6] &= ~(1ULL << (vertex & 63));
    }

    // Check if a specific vertex is present in the set
    inline bool test(size_t vertex) const {
        return (words[vertex >> 6] & (1ULL << (vertex & 63)));
    }

    // Returns true if the set is completely empty
    inline bool none() const {
        return (words[0] | words[1] | words[2] | words[3]) == 0;
    }

    // Bitwise OR
    static inline VertexBitField binaryOr(const VertexBitField& arg1, const VertexBitField& arg2) {
        VertexBitField result;
        result.words[0] = arg1.words[0] | arg2.words[0];
        result.words[1] = arg1.words[1] | arg2.words[1];
        result.words[2] = arg1.words[2] | arg2.words[2];
        result.words[3] = arg1.words[3] | arg2.words[3];
        return result;
    }

    // Bitwise AND NOT
    static inline VertexBitField binaryAndNot(const VertexBitField& arg1, const VertexBitField& arg2) {
        VertexBitField result;
        result.words[0] = arg1.words[0] & ~arg2.words[0];
        result.words[1] = arg1.words[1] & ~arg2.words[1];
        result.words[2] = arg1.words[2] & ~arg2.words[2];
        result.words[3] = arg1.words[3] & ~arg2.words[3];
        return result;
    }

    // Returns true if all set vertices in the second arg are also set in the first
    static inline bool isSubset(const VertexBitField& superset, const VertexBitField& subset) {
        return (
            (subset.words[0] & ~superset.words[0]) == 0 &&
            (subset.words[1] & ~superset.words[1]) == 0 &&
            (subset.words[2] & ~superset.words[2]) == 0 &&
            (subset.words[3] & ~superset.words[3]) == 0
        );
    }
};
