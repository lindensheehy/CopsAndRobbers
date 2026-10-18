/**
 * Modern V5 full-visibility solver.
 *
 * This is intentionally based on legacy/src/k_cops_5.cpp:
 *   - reverse / retrograde level-synchronous BFS
 *   - one packed byte per (cop configuration, robber vertex)
 *   - cop predecessors generated on demand (NO global CSR transition table)
 *   - multithreaded dynamic work distribution
 *
 * Modern-project changes:
 *   - implements the six functions required by app/main.cpp
 *   - p must be 1 (full visibility)
 *   - no artificial self/stay moves for cops or robber
 *   - disk-backed BFS frontiers so 4-cop Scotland Yard does not need multi-GB
 *     std::vector frontiers in RAM
 *   - direct combinatorial ranking replaces V5's binary search over cop configs
 *   - exact best universal-start capture ply is recovered from BFS wave number
 *   - current Graph / AdjacencyList / Allocator infrastructure is reused
 *
 * State byte layout keeps V5's one-byte-per-(C,r) memory target, but preserves
 * enough minimax information to reconstruct perfect moves after the solve.
 * The split is derived from the loaded graph -- there is no hard-coded degree:
 *   bit 0    : this COP-TURN state is winning
 *   bits 1-7 : payload for the ROBBER-TURN state
 *              0..D       = unresolved safe-move count, D = graph max degree
 *              D+1..126   = solved exact robber-turn ply distance
 *                            as code = (D + 1) + ply_distance
 *              127        = solved distance beyond the exact packed range
 *
 * Therefore the exact packed robber-turn range is 0..(125-D) plys.  If a
 * solved value exceeds that range, WIN/LOSS remains exact but that value is
 * marked saturated and perfect-move reconstruction reports it explicitly.
 * The graph is required to be undirected, matching the project game model.
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "Allocator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int MAX_SUPPORTED_COPS = 4;
constexpr uint8_t COP_WIN_BIT = 0x01;
constexpr uint8_t PAYLOAD_SHIFT = 1;
constexpr uint8_t PAYLOAD_MASK = 0xFE;
constexpr uint8_t OVERFLOW_ROBBER_PAYLOAD = 127;

// These encoding parameters are derived from AdjacencyList::maxDegree after
// loading the graph.  They are then read-only for the rest of the solve.
uint32_t graphMaxDegree = 0;
uint8_t solvedRobberBase = 1;
uint32_t maxExactRobberPly = 125;
constexpr uint32_t INF_PLY = std::numeric_limits<uint32_t>::max() / 4;
constexpr uint32_t OVERFLOW_PLY = INF_PLY / 2;

constexpr uint64_t ROBBER_TURN_BIT = uint64_t{1} << 63;
constexpr uint64_t STATE_ID_MASK = ~ROBBER_TURN_BIT;
constexpr uint64_t INVALID_CONFIG = std::numeric_limits<uint64_t>::max();

// Disk I/O is intentionally chunky.  Each worker opens the current wave read-only
// and obtains disjoint ranges through sharedIndex.
constexpr uint64_t INPUT_BATCH_ENTRIES = 1u << 16;   // 65,536 entries = 512 KiB
constexpr size_t OUTPUT_BUFFER_ENTRIES = 1u << 20;   // 1,048,576 entries = 8 MiB/thread
constexpr size_t MERGE_BUFFER_BYTES = 8u << 20;       // 8 MiB

const char* inputFilename = nullptr;
int k = 0;
int p = 0;
int N = 0;

AdjacencyList adj;
Allocator mem;

size_t configCount = 0;
size_t numStates = 0;
uint8_t* configs = nullptr;
uint8_t* gameStates = nullptr;
uint8_t* copWinCounts = nullptr; // one byte/config; N <= 255 in this project

uint64_t chooseTable[260][MAX_SUPPORTED_COPS + 1]{};

std::filesystem::path workDir;
std::filesystem::path currentFrontierPath;
uint64_t currentFrontierCount = 0;

uint64_t totalCopStatesDiscovered = 0;
uint64_t totalRobberStatesDiscovered = 0;

size_t bestStartCId = static_cast<size_t>(-1);
uint32_t bestStartPly = std::numeric_limits<uint32_t>::max();

inline double bytesToGiB(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

inline uint8_t atomicLoadByte(const uint8_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline uint8_t atomicFetchOrByte(uint8_t* ptr, uint8_t value) {
    return __atomic_fetch_or(ptr, value, __ATOMIC_RELAXED);
}

inline uint8_t atomicAddByte(uint8_t* ptr, uint8_t value) {
    return __atomic_add_fetch(ptr, value, __ATOMIC_RELAXED);
}

inline uint8_t payloadOf(uint8_t stateByte) {
    return static_cast<uint8_t>((stateByte & PAYLOAD_MASK) >> PAYLOAD_SHIFT);
}

inline uint8_t makeSolvedRobberPayload(uint32_t robberPly) {
    if (robberPly > maxExactRobberPly) return OVERFLOW_ROBBER_PAYLOAD;
    return static_cast<uint8_t>(static_cast<uint32_t>(solvedRobberBase) + robberPly);
}

inline uint8_t makeStateByte(uint8_t payload, bool copWin) {
    return static_cast<uint8_t>((payload << PAYLOAD_SHIFT) | (copWin ? COP_WIN_BIT : 0));
}

inline uint32_t robberPlyFromByte(uint8_t stateByte) {
    const uint8_t payload = payloadOf(stateByte);
    if (payload < solvedRobberBase) return INF_PLY;
    if (payload == OVERFLOW_ROBBER_PAYLOAD) return OVERFLOW_PLY;
    return static_cast<uint32_t>(payload - solvedRobberBase);
}

// Fixed tiny sort avoids std::sort's aggressive GCC bounds warning on a 4-byte array.
inline void sortSmallConfig(uint8_t* values, int count) {
    for (int i = 1; i < count; ++i) {
        const uint8_t x = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > x) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = x;
    }
}

// Decrement V5's unresolved robber safe-move count.  When the final safe child
// becomes winning, atomically repurpose the payload to hold the exact robber-turn
// minimax distance.  Returns true only for the thread that solves this state.
inline bool consumeSafeMove(uint8_t* ptr, uint32_t solvedRobberPly) {
    uint8_t oldValue = atomicLoadByte(ptr);
    while (true) {
        const uint8_t payload = payloadOf(oldValue);

        // payload >= solvedRobberBase means this robber-turn state has already
        // been solved.  Zero means there is nothing left to consume.
        if (payload == 0 || payload >= solvedRobberBase) return false;

        uint8_t newValue;
        if (payload == 1) {
            newValue = makeStateByte(makeSolvedRobberPayload(solvedRobberPly),
                                     (oldValue & COP_WIN_BIT) != 0);
        } else {
            newValue = static_cast<uint8_t>(oldValue - (uint8_t{1} << PAYLOAD_SHIFT));
        }

        if (__atomic_compare_exchange_n(ptr, &oldValue, newValue,
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return payload == 1;
        }
    }
}

inline bool containsCop(const uint8_t* copConfig, int robberVertex) {
    for (int i = 0; i < k; ++i) {
        if (copConfig[i] == robberVertex) return true;
    }
    return false;
}

void buildChooseTable(int maxN) {
    std::memset(chooseTable, 0, sizeof(chooseTable));
    for (int n = 0; n <= maxN; ++n) {
        chooseTable[n][0] = 1;
        for (int r = 1; r <= MAX_SUPPORTED_COPS; ++r) {
            if (r > n) {
                chooseTable[n][r] = 0;
            } else if (r == n) {
                chooseTable[n][r] = 1;
            } else if (n > 0) {
                chooseTable[n][r] = chooseTable[n - 1][r - 1] + chooseTable[n - 1][r];
            }
        }
    }
}

// Rank a nondecreasing multiset configuration in exactly the lexicographic
// order used by generateCopConfigs().  Transform a_i -> b_i=a_i+i, then rank
// the strict k-combination b in lexicographic order using hockey-stick sums.
inline size_t rankCopConfig(const uint8_t* sortedConfig) {
    const int M = N + k - 1;
    uint64_t rank = 0;
    int previousB = -1;

    for (int i = 0; i < k; ++i) {
        const int b = static_cast<int>(sortedConfig[i]) + i;
        const int remaining = k - i;
        rank += chooseTable[M - previousB - 1][remaining]
              - chooseTable[M - b][remaining];
        previousB = b;
    }
    return static_cast<size_t>(rank);
}

bool computeConfigCount() {
    const int nVal = N + k - 1;
    if (k <= 0 || k > nVal) return false;
    configCount = static_cast<size_t>(chooseTable[nVal][k]);
    return configCount != 0;
}

bool generateCopConfigs() {
    if (configCount > std::numeric_limits<size_t>::max() / static_cast<size_t>(k)) {
        std::cerr << "FATAL: cop configuration allocation size overflow.\n";
        return false;
    }

    const size_t bytes = configCount * static_cast<size_t>(k);
    configs = new (std::nothrow) uint8_t[bytes];
    if (!configs) {
        std::cerr << "FATAL: unable to allocate " << bytesToGiB(bytes)
                  << " GiB for cop configurations.\n";
        return false;
    }

    uint8_t current[MAX_SUPPORTED_COPS]{};
    size_t offset = 0;
    while (true) {
        std::memcpy(configs + offset, current, static_cast<size_t>(k));
        offset += static_cast<size_t>(k);

        int pos = k - 1;
        while (pos >= 0 && current[pos] == N - 1) --pos;
        if (pos < 0) break;

        ++current[pos];
        for (int i = pos + 1; i < k; ++i) current[i] = current[pos];
    }

    if (offset != bytes) {
        std::cerr << "FATAL: generated config count disagrees with combinatorial count.\n";
        return false;
    }
    return true;
}

bool validateRanking() {
    if (configCount == 0) return false;

    auto check = [&](size_t cId) {
        return rankCopConfig(configs + cId * static_cast<size_t>(k)) == cId;
    };

    // Exhaustively validate small test graphs; sample the giant run.
    if (configCount <= 1000000) {
        for (size_t cId = 0; cId < configCount; ++cId) {
            if (!check(cId)) return false;
        }
        return true;
    }

    const size_t samples[] = {
        0,
        1,
        configCount / 7,
        configCount / 3,
        configCount / 2,
        (configCount * 5) / 7,
        configCount - 2,
        configCount - 1
    };
    for (size_t cId : samples) {
        if (cId < configCount && !check(cId)) return false;
    }
    return true;
}

class BufferedFrontierWriter {
public:
    BufferedFrontierWriter() = default;

    bool open(const std::filesystem::path& path) {
        path_ = path;
        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) return false;
        buffer_.reserve(OUTPUT_BUFFER_ENTRIES);
        count_ = 0;
        return true;
    }

    bool push(uint64_t value) {
        buffer_.push_back(value);
        ++count_;
        if (buffer_.size() >= OUTPUT_BUFFER_ENTRIES) return flush();
        return true;
    }

    bool close() {
        if (!flush()) return false;
        out_.close();
        return static_cast<bool>(out_) || !out_.fail();
    }

    uint64_t count() const { return count_; }

private:
    bool flush() {
        if (buffer_.empty()) return true;
        out_.write(reinterpret_cast<const char*>(buffer_.data()),
                   static_cast<std::streamsize>(buffer_.size() * sizeof(uint64_t)));
        if (!out_) return false;
        buffer_.clear();
        return true;
    }

    std::filesystem::path path_;
    std::ofstream out_;
    std::vector<uint64_t> buffer_;
    uint64_t count_ = 0;
};

bool appendFile(const std::filesystem::path& source, std::ofstream& destination,
                std::vector<char>& buffer) {
    std::ifstream in(source, std::ios::binary);
    if (!in) return false;

    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) destination.write(buffer.data(), got);
        if (!destination) return false;
    }
    return true;
}

bool mergeThreadFrontiers(const std::vector<std::filesystem::path>& threadPaths,
                          const std::filesystem::path& nextPath) {
    std::ofstream out(nextPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    std::vector<char> buffer(MERGE_BUFFER_BYTES);
    for (const auto& path : threadPaths) {
        if (!appendFile(path, out, buffer)) return false;
    }
    out.close();
    return static_cast<bool>(out) || !out.fail();
}

void cleanupWorkingFiles() {
    std::error_code ec;
    if (!workDir.empty()) std::filesystem::remove_all(workDir, ec);
}

bool allocateStateTables() {
    if (configCount > std::numeric_limits<size_t>::max() / static_cast<size_t>(N)) {
        std::cerr << "FATAL: state count overflow.\n";
        return false;
    }
    numStates = configCount * static_cast<size_t>(N);

    gameStates = new (std::nothrow) uint8_t[numStates];
    copWinCounts = new (std::nothrow) uint8_t[configCount];
    if (!gameStates || !copWinCounts) {
        std::cerr << "FATAL: unable to allocate V5 state tables. Required core state table: "
                  << std::fixed << std::setprecision(2)
                  << bytesToGiB(numStates) << " GiB.\n";
        return false;
    }
    std::memset(copWinCounts, 0, configCount);

    mem.trackExternal("V5 Cop Configurations",
                      configCount * static_cast<size_t>(k), configs);
    mem.trackExternal("V5 Packed State Table", numStates, gameStates);
    mem.trackExternal("V5 Cop Win Counts", configCount, copWinCounts);
    return true;
}

inline uint32_t robberTurnPly(size_t cId, int robberVertex) {
    if (cId >= configCount || robberVertex < 0 || robberVertex >= N) return INF_PLY;
    const size_t stateId = cId * static_cast<size_t>(N) + static_cast<size_t>(robberVertex);
    return robberPlyFromByte(atomicLoadByte(gameStates + stateId));
}

struct PerfectCopMove {
    bool valid = false;
    size_t nextCId = static_cast<size_t>(-1);
    uint32_t copPly = INF_PLY;
    uint32_t childRobberPly = INF_PLY;
};

PerfectCopMove choosePerfectCopMove(size_t cId, int robberVertex) {
    PerfectCopMove best;
    if (cId >= configCount || robberVertex < 0 || robberVertex >= N) return best;

    const uint8_t* currentCops = configs + cId * static_cast<size_t>(k);
    if (containsCop(currentCops, robberVertex)) {
        best.valid = true;
        best.nextCId = cId;
        best.copPly = 0;
        best.childRobberPly = 0;
        return best;
    }

    uint8_t options[MAX_SUPPORTED_COPS][256];
    int optionCount[MAX_SUPPORTED_COPS]{};
    int odometer[MAX_SUPPORTED_COPS]{};
    uint8_t moveConfig[MAX_SUPPORTED_COPS]{};

    for (int i = 0; i < k; ++i) {
        const uint8_t* edges = adj.getEdges(currentCops[i]);
        int count = 0;
        while (edges[count] != 255) {
            options[i][count] = edges[count];
            ++count;
        }
        if (count == 0) return best; // this cop cannot move; no legal joint cop move
        optionCount[i] = count;
    }

    while (true) {
        for (int i = 0; i < k; ++i) moveConfig[i] = options[i][odometer[i]];
        sortSmallConfig(moveConfig, k);
        const size_t nextCId = rankCopConfig(moveConfig);
        if (nextCId < configCount) {
            const uint32_t child = robberTurnPly(nextCId, robberVertex);
            if (child != INF_PLY) {
                const uint32_t score = child + 1u;
                if (!best.valid || score < best.copPly ||
                    (score == best.copPly && nextCId < best.nextCId)) {
                    best.valid = true;
                    best.nextCId = nextCId;
                    best.copPly = score;
                    best.childRobberPly = child;
                }
            }
        }

        int pos = k - 1;
        while (pos >= 0) {
            ++odometer[pos];
            if (odometer[pos] < optionCount[pos]) break;
            odometer[pos] = 0;
            --pos;
        }
        if (pos < 0) break;
    }
    return best;
}

struct PerfectRobberMove {
    bool hasLegalMove = false;
    int nextRobber = -1;
    uint32_t robberPly = 0;
    uint32_t childCopPly = 0;
};

PerfectRobberMove choosePerfectRobberMove(size_t cId, int robberVertex) {
    PerfectRobberMove best;
    const uint8_t* edges = adj.getEdges(robberVertex);

    for (int e = 0; edges[e] != 255; ++e) {
        const int nextR = edges[e];
        // Moving onto a cop is legal as a move but loses immediately; the
        // captured child has cop-turn value 0 and is therefore naturally bad
        // for the maximizing robber.
        const PerfectCopMove child = choosePerfectCopMove(cId, nextR);
        const uint32_t childValue = child.valid ? child.copPly : INF_PLY;
        if (!best.hasLegalMove || childValue > best.childCopPly ||
            (childValue == best.childCopPly && nextR < best.nextRobber)) {
            best.hasLegalMove = true;
            best.nextRobber = nextR;
            best.childCopPly = childValue;
            best.robberPly = childValue == INF_PLY ? INF_PLY : childValue + 1u;
        }
    }
    return best;
}

void printCopConfig(size_t cId) {
    const uint8_t* c = configs + cId * static_cast<size_t>(k);
    std::cout << "(";
    for (int i = 0; i < k; ++i) {
        if (i) std::cout << ", ";
        std::cout << static_cast<int>(c[i]);
    }
    std::cout << ")";
}

bool printPerfectPlayTrace() {
    if (bestStartCId == static_cast<size_t>(-1)) return true;

    int worstRobberStart = -1;
    uint32_t worstStartValue = 0;
    for (int r = 0; r < N; ++r) {
        const PerfectCopMove value = choosePerfectCopMove(bestStartCId, r);
        if (!value.valid) {
            std::cerr << "Perfect-move reconstruction failed: winning start has an unsolved robber start.\n";
            return false;
        }
        if (worstRobberStart < 0 || value.copPly > worstStartValue) {
            worstRobberStart = r;
            worstStartValue = value.copPly;
        }
    }

    if (worstStartValue >= OVERFLOW_PLY) {
        std::cerr << "Perfect-move reconstruction needs a robber-turn value above the "
                  << maxExactRobberPly
                  << "-ply exact packed range for this graph; the compact oracle "
                     "saturated that value. The WIN/LOSS solve is still exact.\n";
        return false;
    }

    if (bestStartPly != std::numeric_limits<uint32_t>::max() &&
        worstStartValue != bestStartPly) {
        std::cerr << "Perfect-move reconstruction check failed: stored start value "
                  << bestStartPly << " but reconstructed " << worstStartValue << ".\n";
        return false;
    }

    std::cout << "\n--- PERFECT PLAY TRACE (one deterministic optimal line) ---\n";
    std::cout << "Cops start : ";
    printCopConfig(bestStartCId);
    std::cout << "\nRobber chooses worst start: " << worstRobberStart
              << "  [game value " << worstStartValue << " plys]\n";

    size_t cId = bestStartCId;
    int r = worstRobberStart;
    uint32_t ply = 0;
    const uint32_t guard = worstStartValue + 4u;

    while (ply <= guard) {
        const uint8_t* cops = configs + cId * static_cast<size_t>(k);
        if (containsCop(cops, r)) {
            std::cout << "CAPTURE at vertex " << r << " after " << ply << " plys.\n";
            return true;
        }

        const PerfectCopMove copMove = choosePerfectCopMove(cId, r);
        if (!copMove.valid || copMove.copPly == INF_PLY) {
            std::cerr << "Perfect-move reconstruction failed at cop turn.\n";
            return false;
        }

        ++ply;
        std::cout << "Ply " << ply << " COP    : ";
        printCopConfig(cId);
        std::cout << " -> ";
        printCopConfig(copMove.nextCId);
        std::cout << "  [value " << copMove.copPly << " -> "
                  << copMove.childRobberPly << "]\n";
        cId = copMove.nextCId;

        const uint8_t* movedCops = configs + cId * static_cast<size_t>(k);
        if (containsCop(movedCops, r)) {
            std::cout << "CAPTURE at vertex " << r << " after " << ply << " plys.\n";
            return true;
        }

        const uint32_t storedRobberValue = robberTurnPly(cId, r);
        if (storedRobberValue == INF_PLY) {
            std::cerr << "Perfect-move reconstruction found an unsolved robber-turn child.\n";
            return false;
        }

        const PerfectRobberMove robberMove = choosePerfectRobberMove(cId, r);
        if (!robberMove.hasLegalMove) {
            std::cout << "ROBBER has no legal move; cops win after " << ply << " plys.\n";
            return true;
        }
        if (robberMove.robberPly != storedRobberValue) {
            std::cerr << "Perfect-move reconstruction check failed at robber turn: stored "
                      << storedRobberValue << " but reconstructed "
                      << robberMove.robberPly << ".\n";
            return false;
        }

        ++ply;
        std::cout << "Ply " << ply << " ROBBER : " << r << " -> "
                  << robberMove.nextRobber << "  [value "
                  << storedRobberValue << " -> " << robberMove.childCopPly << "]\n";
        r = robberMove.nextRobber;
    }

    std::cerr << "Perfect-move reconstruction exceeded expected game length.\n";
    return false;
}

} // namespace

bool loadGraphFile(const char* filename_param, int k_param, int p_param) {
    inputFilename = filename_param;
    k = k_param;
    p = p_param;

    if (p != 1) {
        std::cerr << "Error: k_cops_fullvis_v5 is intentionally FULL VISIBILITY only; use p=1.\n";
        return true;
    }
    if (k < 1 || k > MAX_SUPPORTED_COPS) {
        std::cerr << "Error: k_cops_fullvis_v5 supports 1..4 cops (target run is k=4).\n";
        return true;
    }

    Graph g(filename_param);
    if (g.nodeCount <= 0) {
        std::cerr << "Error: graph is empty or failed to load.\n";
        return true;
    }
    if (g.nodeCount > 255) {
        std::cerr << "Error: graph exceeds the uint8_t / 255-sentinel vertex limit.\n";
        return true;
    }

    // V5's reverse predecessor generation uses ordinary adjacency as reverse
    // adjacency, so require the undirected graphs used by this project.
    for (int u = 0; u < g.nodeCount; ++u) {
        for (int v = u + 1; v < g.nodeCount; ++v) {
            if (g.getEdge(u, v) != g.getEdge(v, u)) {
                std::cerr << "Error: V5 full-vis solver requires an undirected graph.\n";
                return true;
            }
        }
    }

    N = g.nodeCount;
    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    // AdjacencyList computes maxDegree directly from the loaded graph.  V5's
    // seven payload bits can represent values 0..127.  We reserve:
    //   0..D     unresolved robber safe-move counts
    //   D+1..126 exact solved robber-turn distances
    //   127      saturated solved-distance marker
    // This is the only representability constraint introduced by storing the
    // perfect-move value inside the original V5 byte.
    graphMaxDegree = static_cast<uint32_t>(adj.maxDegree);
    if (graphMaxDegree > 125u) {
        std::cerr << "Error: graph max degree " << graphMaxDegree
                  << " leaves no distinct solved-value code in the one-byte V5 "
                     "perfect-move encoding.\n";
        return true;
    }
    solvedRobberBase = static_cast<uint8_t>(graphMaxDegree + 1u);
    maxExactRobberPly = 126u - static_cast<uint32_t>(solvedRobberBase);

    std::cout << "Modern V5 full-visibility solver\n"
              << "  graph vertices : " << N << "\n"
              << "  graph max degree: " << graphMaxDegree << "\n"
              << "  cops           : " << k << "\n"
              << "  visibility p   : 1 (full)\n"
              << "  cop stay       : NO\n"
              << "  robber stay    : NO\n"
              << "  transitions    : generated on demand (V5)\n"
              << "  exact value cap: " << maxExactRobberPly
              << " robber-turn plys before saturation\n";
    return false;
}

bool buildAuxGraph() {
    // There is deliberately NO AuxGraph transition table in V5.  This step
    // builds only the compact configuration/state structures needed by V5.
    buildChooseTable(N + k);
    if (!computeConfigCount()) {
        std::cerr << "Error: unable to compute cop configuration count.\n";
        return true;
    }

    if (!generateCopConfigs()) return true;
    if (!validateRanking()) {
        std::cerr << "FATAL: direct cop-configuration rank does not match generated order.\n";
        return true;
    }
    if (!allocateStateTables()) return true;

    std::cout << "V5 compact state space:\n"
              << "  cop configurations : " << configCount << "\n"
              << "  (C,r) states       : " << numStates << "\n"
              << "  configs RAM        : " << std::fixed << std::setprecision(3)
              << bytesToGiB(configCount * static_cast<uint64_t>(k)) << " GiB\n"
              << "  state-table RAM    : " << bytesToGiB(numStates) << " GiB\n"
              << "  win-count RAM      : " << bytesToGiB(configCount) << " GiB\n";
    return false;
}

bool initializeCaptures() {
    std::vector<uint8_t> rowTemplate(static_cast<size_t>(N), 0);
    std::vector<int> zeroDegreeVertices;

    for (int r = 0; r < N; ++r) {
        int degree = 0;
        const uint8_t* edges = adj.getEdges(r);
        while (edges[degree] != 255) ++degree;

        // This should be impossible because adj.maxDegree was computed from the
        // same adjacency list.  Keep the check as an internal-consistency guard,
        // not as a graph-specific assumption.
        if (degree < 0 || static_cast<uint32_t>(degree) > graphMaxDegree) {
            std::cerr << "FATAL: adjacency degree exceeds the graph max degree "
                         "computed by AdjacencyList.\n";
            return true;
        }

        rowTemplate[r] = makeStateByte(static_cast<uint8_t>(degree), false);
        if (degree == 0) zeroDegreeVertices.push_back(r);
    }

    // Unique work directory for this invocation.  Frontiers are disk-backed so
    // the 4-cop run does not reserve multiple multi-gigabyte vectors in RAM.
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    workDir = std::filesystem::path("cache") /
              ("fullvis_v5_work_" + std::to_string(static_cast<long long>(stamp)));
    std::error_code ec;
    std::filesystem::create_directories(workDir, ec);
    if (ec) {
        std::cerr << "Error: cannot create V5 frontier directory: " << workDir << "\n";
        return true;
    }

    currentFrontierPath = workDir / "wave_0.bin";
    BufferedFrontierWriter frontier;
    if (!frontier.open(currentFrontierPath)) {
        std::cerr << "Error: cannot create initial disk frontier.\n";
        return true;
    }

    uint64_t initialCaptureStates = 0;
    uint64_t initialNoMoveRobberStates = 0;

    for (size_t cId = 0; cId < configCount; ++cId) {
        uint8_t* row = gameStates + cId * static_cast<size_t>(N);
        std::memcpy(row, rowTemplate.data(), static_cast<size_t>(N));

        const uint8_t* cops = configs + cId * static_cast<size_t>(k);
        uint8_t uniqueCaptures = 0;
        for (int i = 0; i < k; ++i) {
            if (i > 0 && cops[i] == cops[i - 1]) continue;
            const int r = cops[i];
            const uint64_t stateId = static_cast<uint64_t>(cId) * N + static_cast<uint64_t>(r);
            row[r] = makeStateByte(solvedRobberBase, true); // capture: both turn-states have value 0

            if (!frontier.push(stateId) || !frontier.push(stateId | ROBBER_TURN_BIT)) {
                std::cerr << "Error writing initial V5 frontier.\n";
                return true;
            }
            ++uniqueCaptures;
            ++initialCaptureStates;
        }
        copWinCounts[cId] = uniqueCaptures;

        // Project rule: if the robber has no legal move, robber-turn loses.
        // Scotland Yard has no isolates, so this is normally zero overhead.
        for (int r : zeroDegreeVertices) {
            if (containsCop(cops, r)) continue; // already seeded as terminal capture
            const uint64_t stateId = static_cast<uint64_t>(cId) * N + static_cast<uint64_t>(r);
            row[r] = makeStateByte(solvedRobberBase, false); // robber-to-move loses immediately
            if (!frontier.push(stateId | ROBBER_TURN_BIT)) {
                std::cerr << "Error writing no-move seed to V5 frontier.\n";
                return true;
            }
            ++initialNoMoveRobberStates;
        }

        if (uniqueCaptures == N && bestStartCId == static_cast<size_t>(-1)) {
            bestStartCId = cId;
            bestStartPly = 0;
        }
    }

    if (!frontier.close()) {
        std::cerr << "Error closing initial V5 frontier.\n";
        return true;
    }
    currentFrontierCount = frontier.count();

    totalCopStatesDiscovered = initialCaptureStates;
    totalRobberStatesDiscovered = initialCaptureStates + initialNoMoveRobberStates;

    std::cout << "Initialized V5 base states:\n"
              << "  capture (C,r) states : " << initialCaptureStates << "\n"
              << "  no-move robber states: " << initialNoMoveRobberStates << "\n"
              << "  wave-0 entries       : " << currentFrontierCount << "\n"
              << "  frontier storage     : DISK (" << workDir.string() << ")\n";
    return false;
}

bool mainLoop() {
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 8;

    // Avoid pathological memory from per-thread I/O buffers on very high-core hosts.
    numThreads = std::min<unsigned int>(numThreads, 32);

    uint32_t wave = 0;
    while (currentFrontierCount != 0) {
        std::cout << "Wave " << wave << " [exact ply " << wave << "] : processing "
                  << currentFrontierCount << " winning turn-states using "
                  << numThreads << " threads...\n";

        std::atomic<uint64_t> sharedIndex{0};
        std::atomic<uint64_t> newCopStates{0};
        std::atomic<uint64_t> newRobberStates{0};
        std::atomic<size_t> waveBestUniversal{static_cast<size_t>(-1)};
        std::atomic<bool> workerFailed{false};

        std::vector<std::filesystem::path> threadPaths(numThreads);
        std::vector<uint64_t> produced(numThreads, 0);
        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        for (unsigned int tId = 0; tId < numThreads; ++tId) {
            threadPaths[tId] = workDir /
                ("wave_" + std::to_string(wave + 1) + "_thread_" + std::to_string(tId) + ".bin");

            threads.emplace_back([&, tId]() {
                std::ifstream input(currentFrontierPath, std::ios::binary);
                BufferedFrontierWriter output;
                if (!input || !output.open(threadPaths[tId])) {
                    workerFailed.store(true, std::memory_order_relaxed);
                    return;
                }

                std::vector<uint64_t> batch(INPUT_BATCH_ENTRIES);
                uint8_t options[MAX_SUPPORTED_COPS][256];
                int optionCount[MAX_SUPPORTED_COPS];
                int odometer[MAX_SUPPORTED_COPS];
                uint8_t moveConfig[MAX_SUPPORTED_COPS];

                while (!workerFailed.load(std::memory_order_relaxed)) {
                    const uint64_t start = sharedIndex.fetch_add(INPUT_BATCH_ENTRIES,
                                                                 std::memory_order_relaxed);
                    if (start >= currentFrontierCount) break;
                    const uint64_t take = std::min<uint64_t>(INPUT_BATCH_ENTRIES,
                                                              currentFrontierCount - start);

                    input.seekg(static_cast<std::streamoff>(start * sizeof(uint64_t)), std::ios::beg);
                    input.read(reinterpret_cast<char*>(batch.data()),
                               static_cast<std::streamsize>(take * sizeof(uint64_t)));
                    if (input.gcount() != static_cast<std::streamsize>(take * sizeof(uint64_t))) {
                        workerFailed.store(true, std::memory_order_relaxed);
                        break;
                    }

                    for (uint64_t q = 0; q < take; ++q) {
                        const uint64_t packedNode = batch[q];
                        const bool isRobberTurn = (packedNode & ROBBER_TURN_BIT) != 0;
                        const uint64_t stateId = packedNode & STATE_ID_MASK;
                        const size_t cId = static_cast<size_t>(stateId / static_cast<uint64_t>(N));
                        const int r = static_cast<int>(stateId % static_cast<uint64_t>(N));

                        if (isRobberTurn) {
                            // V5 OR-predecessor propagation: a winning robber-turn state
                            // makes every cop-turn predecessor that can move into it a win.
                            // NO self/stay option is injected here.
                            const uint8_t* currentCops = configs + cId * static_cast<size_t>(k);
                            bool hasAllPredecessors = true;

                            for (int i = 0; i < k; ++i) {
                                const uint8_t* edges = adj.getEdges(currentCops[i]);
                                int count = 0;
                                while (edges[count] != 255) {
                                    options[i][count] = edges[count];
                                    ++count;
                                }
                                optionCount[i] = count;
                                odometer[i] = 0;
                                if (count == 0) {
                                    hasAllPredecessors = false;
                                    break;
                                }
                            }
                            if (!hasAllPredecessors) continue;

                            while (true) {
                                for (int i = 0; i < k; ++i) {
                                    moveConfig[i] = options[i][odometer[i]];
                                }
                                sortSmallConfig(moveConfig, k);
                                const size_t prevCId = rankCopConfig(moveConfig);
                                if (prevCId >= configCount) {
                                    workerFailed.store(true, std::memory_order_relaxed);
                                    break;
                                }

                                const uint64_t prevStateId =
                                    static_cast<uint64_t>(prevCId) * N + static_cast<uint64_t>(r);
                                uint8_t* statePtr = gameStates + static_cast<size_t>(prevStateId);
                                const uint8_t oldValue = atomicFetchOrByte(statePtr, COP_WIN_BIT);

                                if ((oldValue & COP_WIN_BIT) == 0) {
                                    if (!output.push(prevStateId)) {
                                        workerFailed.store(true, std::memory_order_relaxed);
                                        break;
                                    }
                                    newCopStates.fetch_add(1, std::memory_order_relaxed);

                                    const uint8_t countNow = atomicAddByte(copWinCounts + prevCId, 1);
                                    if (countNow == static_cast<uint8_t>(N)) {
                                        size_t observed = waveBestUniversal.load(std::memory_order_relaxed);
                                        while (prevCId < observed &&
                                               !waveBestUniversal.compare_exchange_weak(
                                                   observed, prevCId,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
                                        }
                                    }
                                }

                                int pos = k - 1;
                                while (pos >= 0) {
                                    ++odometer[pos];
                                    if (odometer[pos] < optionCount[pos]) break;
                                    odometer[pos] = 0;
                                    --pos;
                                }
                                if (pos < 0) break;
                            }
                        } else {
                            // V5 AND-predecessor propagation: this cop-turn child has
                            // become winning, so each robber predecessor loses one safe
                            // move.  NO robber stay predecessor is included.
                            const uint8_t* cops = configs + cId * static_cast<size_t>(k);
                            const uint8_t* rEdges = adj.getEdges(r);
                            for (int eIdx = 0; rEdges[eIdx] != 255; ++eIdx) {
                                const int rPrev = rEdges[eIdx];

                                // Terminal capture robber states were already seeded at
                                // wave 0 and must not have their counter decremented.
                                if (containsCop(cops, rPrev)) continue;

                                const uint64_t prevId =
                                    static_cast<uint64_t>(cId) * N + static_cast<uint64_t>(rPrev);
                                if (consumeSafeMove(gameStates + static_cast<size_t>(prevId), wave + 1)) {
                                    if (!output.push(prevId | ROBBER_TURN_BIT)) {
                                        workerFailed.store(true, std::memory_order_relaxed);
                                        break;
                                    }
                                    newRobberStates.fetch_add(1, std::memory_order_relaxed);
                                }
                            }
                        }

                        if (workerFailed.load(std::memory_order_relaxed)) break;
                    }
                }

                if (!output.close()) workerFailed.store(true, std::memory_order_relaxed);
                produced[tId] = output.count();
            });
        }

        for (auto& thread : threads) thread.join();
        if (workerFailed.load(std::memory_order_relaxed)) {
            std::cerr << "Error: V5 worker/frontier I/O failed.\n";
            cleanupWorkingFiles();
            return true;
        }

        uint64_t nextCount = 0;
        for (uint64_t c : produced) nextCount += c;

        const auto nextPath = workDir / ("wave_" + std::to_string(wave + 1) + ".bin");
        if (!mergeThreadFrontiers(threadPaths, nextPath)) {
            std::cerr << "Error: unable to merge next V5 frontier.\n";
            cleanupWorkingFiles();
            return true;
        }

        std::error_code ec;
        std::filesystem::remove(currentFrontierPath, ec);
        for (const auto& path : threadPaths) {
            ec.clear();
            std::filesystem::remove(path, ec);
        }

        totalCopStatesDiscovered += newCopStates.load(std::memory_order_relaxed);
        totalRobberStatesDiscovered += newRobberStates.load(std::memory_order_relaxed);

        const size_t universalThisWave = waveBestUniversal.load(std::memory_order_relaxed);
        if (bestStartCId == static_cast<size_t>(-1) &&
            universalThisWave != static_cast<size_t>(-1)) {
            bestStartCId = universalThisWave;
            bestStartPly = wave + 1;
            std::cout << "  First universal cop start reached at exact ply "
                      << bestStartPly << ". Continuing to fixed point for full correctness map.\n";
        }

        std::cout << "  discovered cop-turn wins    : "
                  << newCopStates.load(std::memory_order_relaxed) << "\n"
                  << "  discovered robber-turn wins : "
                  << newRobberStates.load(std::memory_order_relaxed) << "\n"
                  << "  next frontier entries       : " << nextCount << "\n";

        currentFrontierPath = nextPath;
        currentFrontierCount = nextCount;
        ++wave;
    }

    std::cout << "V5 fixed point reached after " << wave << " propagation waves.\n";
    return false;
}

bool findFinalResult() {
    if (bestStartCId == static_cast<size_t>(-1)) {
        // Defensive fallback: counts are much cheaper to scan than all (C,r) states.
        for (size_t cId = 0; cId < configCount; ++cId) {
            if (atomicLoadByte(copWinCounts + cId) == static_cast<uint8_t>(N)) {
                bestStartCId = cId;
                break;
            }
        }
    }

    std::cout << "\n--- FINAL VERDICT (Modern V5, full visibility) ---\n";
    if (bestStartCId == static_cast<size_t>(-1)) {
        std::cout << "RESULT: LOSS. " << k
                  << " cop(s) cannot guarantee capture under the configured rules.\n";
        return false;
    }

    std::cout << "RESULT: WIN. " << k << " cop(s) can guarantee capture.\n";
    std::cout << "Best cop start found at minimum worst-case ply: (";
    const uint8_t* best = configs + bestStartCId * static_cast<size_t>(k);
    for (int i = 0; i < k; ++i) {
        if (i) std::cout << ", ";
        std::cout << static_cast<int>(best[i]);
    }
    std::cout << ")\n";
    if (bestStartPly != std::numeric_limits<uint32_t>::max()) {
        std::cout << "Worst-case capture time: " << bestStartPly
                  << " plys (level-synchronous minimax distance).\n";
    }

    if (!printPerfectPlayTrace()) {
        std::cerr << "FATAL: solve completed, but perfect-move reconstruction failed.\n";
        return true;
    }
    return false;
}

bool outputData() {
    // Optional strategy-oracle dump. Disabled by default because the Scotland
    // Yard 4-cop table is ~13.4 GB. Version 3 stores the graph-derived payload
    // encoding parameters in the header so a later reader can decode it without
    // any external degree assumptions. Enable explicitly when the table is wanted:
    //   set COPS_SAVE_FULLVIS_V5=1   (Windows cmd)
    //   export COPS_SAVE_FULLVIS_V5=1 (bash)
    const char* save = std::getenv("COPS_SAVE_FULLVIS_V5");
    if (save && std::string(save) == "1") {
        std::filesystem::create_directories("cache");
        const std::filesystem::path output =
            std::filesystem::path("cache") /
            ("fullvis_v5_strategy__" + std::filesystem::path(inputFilename).stem().string() +
             "__" + std::to_string(k) + "-cops.bin");

        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "Error: cannot open oracle output file " << output << "\n";
            cleanupWorkingFiles();
            return true;
        }

        const uint32_t magic = 0x35564643; // "CFV5" little-endian marker
        const uint32_t version = 3;
        const uint32_t n32 = static_cast<uint32_t>(N);
        const uint32_t k32 = static_cast<uint32_t>(k);
        const uint64_t configs64 = static_cast<uint64_t>(configCount);
        const uint64_t states64 = static_cast<uint64_t>(numStates);
        const uint64_t bestStart64 = bestStartCId == static_cast<size_t>(-1)
            ? std::numeric_limits<uint64_t>::max()
            : static_cast<uint64_t>(bestStartCId);
        const uint32_t bestPly32 = bestStartPly;
        const uint32_t encoding32 = 2; // dynamic-degree packed robber-turn encoding
        const uint32_t graphMaxDegree32 = graphMaxDegree;
        const uint32_t solvedRobberBase32 = static_cast<uint32_t>(solvedRobberBase);
        const uint32_t maxExactRobberPly32 = maxExactRobberPly;
        const uint32_t overflowPayload32 = OVERFLOW_ROBBER_PAYLOAD;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&n32), sizeof(n32));
        out.write(reinterpret_cast<const char*>(&k32), sizeof(k32));
        out.write(reinterpret_cast<const char*>(&configs64), sizeof(configs64));
        out.write(reinterpret_cast<const char*>(&states64), sizeof(states64));
        out.write(reinterpret_cast<const char*>(&bestStart64), sizeof(bestStart64));
        out.write(reinterpret_cast<const char*>(&bestPly32), sizeof(bestPly32));
        out.write(reinterpret_cast<const char*>(&encoding32), sizeof(encoding32));
        out.write(reinterpret_cast<const char*>(&graphMaxDegree32), sizeof(graphMaxDegree32));
        out.write(reinterpret_cast<const char*>(&solvedRobberBase32), sizeof(solvedRobberBase32));
        out.write(reinterpret_cast<const char*>(&maxExactRobberPly32), sizeof(maxExactRobberPly32));
        out.write(reinterpret_cast<const char*>(&overflowPayload32), sizeof(overflowPayload32));
        out.write(reinterpret_cast<const char*>(gameStates),
                  static_cast<std::streamsize>(numStates));
        out.close();
        if (!out) {
            std::cerr << "Error while writing oracle output.\n";
            cleanupWorkingFiles();
            return true;
        }
        std::cout << "Saved V5 perfect-move strategy oracle to " << output << "\n";
    } else {
        std::cout << "Strategy oracle dump skipped (set COPS_SAVE_FULLVIS_V5=1 to save it).\n";
    }

    cleanupWorkingFiles();

    delete[] copWinCounts;
    copWinCounts = nullptr;
    delete[] gameStates;
    gameStates = nullptr;
    delete[] configs;
    configs = nullptr;
    return false;
}
