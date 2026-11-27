/*
    k_cops_scotlandyard_beginner.cpp

    Exact solver for the Scotland Yard BEGINNER rules (docs/beginner_rules/beginner_rules_brief.md).

    Question answered:
        From the fixed starting positions, can the cops GUARANTEE a capture within the game length,
        no matter how the robber plays?

    Rules modelled (see the brief for the full text):
        - The robber moves first in every round, then the cops.
        - Every piece must move every turn (no self edges, no passing).
        - Two cops may never share a station. Cops move one at a time in any order, so a cop may move
          into a station another cop vacated earlier in the same turn, but two cops can never swap
          (and three can never rotate).
        - The robber may never move onto a cop. If he has no free station, the cops win.
        - A cop landing on the robber's station is a capture (even while the robber is hidden).
        - If the cops have no legal joint move on their turn, the cops lose (our own rule).
        - In a hidden round, the robber's move is not shown, so the cops must pick their move without
          knowing it. He is visible again before the cops' next move.
        - The robber wins if every round is played without a capture.

    Method (backwards induction, one table per round):

        WIN[t][C] = the set of robber stations r such that, at the START of round t (robber to move),
                    with the cops on the station set C and the robber visibly on r, the cops can force
                    a capture before the game ends.

        WIN[R+1][*] is empty (the game is over, the robber survived). Each WIN[t] is computed only
        from WIN[t+1], so only two tables are ever in memory. The answer is whether the robber's start
        is in WIN[1][cop start].

        A robber station set is a VertexBitField, so one table entry covers all robber stations at
        once. Cop sets are indexed by their rank among all k-subsets of the stations.

        Visible round t, cops on C:
            GOOD = union over legal cop moves M of ( M  |  WIN[t+1][M] )
                 = robber stations (after the robber's move) from which the cops, now seeing him, win.
            r is in WIN[t][C]  iff  every free neighbour of r is in GOOD
                               iff  r is not next to any station of BAD = everything \ GOOD \ C.
            So WIN[t][C] = everything \ C \ expand(BAD). (A robber with no free station is a win.)

        Hidden round t, cops on C:
            The robber goes to some free neighbour of r, but the cops do not know which, so ONE cop
            move must work for all of them:
            r is in WIN[t][C]  iff  some legal M has  ( free neighbours of r ) subset of ( M | WIN[t+1][M] ).

    Pruning that does not change the answer:
        A forward pass first marks which cop sets can occur at the start of each round, and which robber
        stations can (ignoring blocking). Entries outside those are never read, so they are skipped.

    Configuration: <graph> <k> [p]   (p is unused) plus optional environment variables.
    Station numbers are 1-based: station s is row s-1 of the graph file.
        SYB_ROBBER    robber start station          (default 82)
        SYB_COPS      cop start stations "a,b,c"    (default 41,46,124 for k=3, plus 142 for k=4)
        SYB_ROUNDS    number of rounds              (default 13)
        SYB_HIDDEN    hidden rounds "3,8,13"/"none" (default 3,8,13)
        SYB_THREADS   worker threads                (default: all hardware threads)

    The intended graph is assets/matrices/scotlandyard-beginner.txt (taxi + bus, no underground).
*/

#include "Graph.h"
#include "AdjacencyList.h"
#include "PackedAdjMatrix.h"
#include "VertexBitField.h"
#include "Allocator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// GCC cannot see that k <= MAX_K (it is checked at load time), so it warns that fixed-size
// per-cop arrays might be overrun. They cannot be; silence that one false positive.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"

namespace syb {

constexpr int MAX_K = 4;
constexpr int MAX_ROUNDS = 64;

// A legal joint cop move: the rank of the resulting cop set and its stations (sorted)
struct CopMove {
    uint32_t rank;
    uint8_t stations[MAX_K];
};

// ---------------------------------------------------------------------------------------------
// Configuration and global read-only state
// ---------------------------------------------------------------------------------------------

const char* filename = nullptr;
std::string graphStem;
int k = 0;
int N = 0;
int rounds = 13;
bool hidden[MAX_ROUNDS + 2] = {};
int threadCount = 1;

int robberStart = 0;                 // 0-based vertex
uint8_t copStart[MAX_K] = {};        // 0-based vertices, sorted

Allocator mem;
AdjacencyList adj;
PackedAdjMatrix adjMatrix;
VertexBitField everything;           // all N stations

uint64_t choose[256][MAX_K + 1] = {};
uint64_t copSetCount = 0;

// copReach[t] is a bit per cop set: can this set occur at the start of round t?
uint64_t* copReach = nullptr;
size_t reachWords = 0;
VertexBitField robberReach[MAX_ROUNDS + 2];

// The two round tables. winNext = WIN[t+1], winCur = WIN[t]
VertexBitField* tableA = nullptr;
VertexBitField* tableB = nullptr;
VertexBitField* winNext = nullptr;
VertexBitField* winCur = nullptr;

// Diagnostics from the forward pass
std::atomic<uint64_t> blockedSets{0};
std::atomic<uint64_t> uncoveredSets{0};

std::string summary;   // the final result, also written by outputData()
bool copsWin = false;

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

// Parses "a,b,c" into integers. "none" gives an empty list
bool parseList(const std::string& s, std::vector<int>& out) {
    out.clear();
    if (s == "none") return true;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos) return false;
        out.push_back(std::stoi(item));
    }
    return !out.empty();
}

// Rank of a sorted set of k distinct stations among all k-subsets (combinatorial number system)
inline uint32_t rankCops(const uint8_t* c) {
    uint64_t r = 0;
    for (int i = 0; i < k; ++i) r += choose[c[i]][i + 1];
    return uint32_t(r);
}

inline bool isReachable(int round, uint32_t rank) {
    return (copReach[size_t(round) * reachWords + (rank >> 6)] >> (rank & 63)) & 1;
}

inline void markReachable(int round, uint32_t rank) {
    uint64_t* word = &copReach[size_t(round) * reachWords + (rank >> 6)];
    __atomic_fetch_or(word, uint64_t(1) << (rank & 63), __ATOMIC_RELAXED);
}

inline VertexBitField maskOf(const uint8_t* stations) {
    VertexBitField m;
    for (int i = 0; i < k; ++i) m.set(stations[i]);
    return m;
}

inline VertexBitField neighboursOf(int station) {
    VertexBitField single;
    single.set(station);
    return adjMatrix.expand(single, false);
}

std::string stationList(const uint8_t* stations) {
    std::string s = "{";
    for (int i = 0; i < k; ++i) s += (i ? ", " : "") + std::to_string(stations[i] + 1);
    return s + "}";
}

// Runs body(i) for i in [0, count) across all worker threads
template <class Body>
void parallelFor(uint64_t count, Body body) {
    std::atomic<uint64_t> next{0};
    const uint64_t chunk = 4096;
    auto worker = [&]() {
        for (;;) {
            const uint64_t begin = next.fetch_add(chunk);
            if (begin >= count) return;
            const uint64_t end = std::min(count, begin + chunk);
            for (uint64_t i = begin; i < end; ++i) body(i);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threadCount; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

// Visits every cop set (sorted stations + rank) whose bit is set for this round
template <class Body>
void forEachReachableCopSet(int round, Body body) {
    parallelFor(reachWords, [&](uint64_t w) {
        uint64_t bits = copReach[size_t(round) * reachWords + w];
        while (bits) {
            const uint32_t rank = uint32_t(w * 64 + __builtin_ctzll(bits));
            bits &= bits - 1;

            // Unrank: largest c with choose[c][i+1] <= remaining, from the top cop down
            uint8_t c[MAX_K];
            uint64_t rest = rank;
            int v = N - 1;
            for (int i = k - 1; i >= 0; --i) {
                while (choose[v][i + 1] > rest) --v;
                c[i] = uint8_t(v);
                rest -= choose[v][i + 1];
                --v;
            }
            body(rank, c);
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Cop move generation
// ---------------------------------------------------------------------------------------------

/*
    Every cop picks a neighbour d[i]. The joint move is legal iff:
        - all d[i] are distinct (no two cops end on one station), and
        - there is an order to make the moves in. Cop i must wait for cop j when d[i] is the
          station j currently stands on. A cycle in these "waits for" links (a swap, a rotation)
          means no order works. Each cop waits for at most one other, so following the links
          from any cop either stops, or loops back.
    Different assignments can produce the same set of stations; duplicates are removed.
*/
int generateMoves(const uint8_t* c, std::vector<CopMove>& out) {
    out.clear();

    const uint8_t* options[MAX_K];
    int optionCount[MAX_K];
    for (int i = 0; i < k; ++i) {
        options[i] = adj.getEdges(c[i]);
        optionCount[i] = 0;
        while (options[i][optionCount[i]] != 255) optionCount[i]++;
    }

    int pick[MAX_K] = {};
    uint8_t d[MAX_K];

    while (true) {

        for (int i = 0; i < k; ++i) d[i] = options[i][pick[i]];

        bool legal = true;

        // Distinct destinations
        for (int i = 0; i < k && legal; ++i)
            for (int j = i + 1; j < k && legal; ++j)
                if (d[i] == d[j]) legal = false;

        // No cycle of cops waiting on each other
        int waitsFor[MAX_K];
        for (int i = 0; i < k; ++i) {
            waitsFor[i] = -1;
            for (int j = 0; j < k; ++j)
                if (j != i && d[i] == c[j]) waitsFor[i] = j;
        }
        for (int i = 0; i < k && legal; ++i) {
            int at = waitsFor[i];
            for (int steps = 0; steps < k && at != -1; ++steps) {
                if (at == i) { legal = false; break; }
                at = waitsFor[at];
            }
        }

        if (legal) {
            CopMove m;
            // Insertion sort: at most 4 stations
            for (int i = 0; i < k; ++i) {
                int j = i;
                while (j > 0 && m.stations[j - 1] > d[i]) { m.stations[j] = m.stations[j - 1]; --j; }
                m.stations[j] = d[i];
            }
            m.rank = rankCops(m.stations);
            out.push_back(m);
        }

        // Odometer step
        int p = k - 1;
        while (p >= 0 && ++pick[p] == optionCount[p]) { pick[p] = 0; p--; }
        if (p < 0) break;
    }

    std::sort(out.begin(), out.end(), [](const CopMove& a, const CopMove& b) { return a.rank < b.rank; });
    out.erase(std::unique(out.begin(), out.end(), [](const CopMove& a, const CopMove& b) { return a.rank == b.rank; }), out.end());
    return int(out.size());
}

// One scratch move list per thread
std::vector<CopMove>& scratchMoves() {
    thread_local std::vector<CopMove> moves;
    return moves;
}

// ---------------------------------------------------------------------------------------------
// One round of the backwards pass
// ---------------------------------------------------------------------------------------------

// Fills winCur[C] = WIN[t][C] for every cop set C reachable at round t, from winNext = WIN[t+1]
void solveRound(int t) {

    forEachReachableCopSet(t, [&](uint32_t rank, const uint8_t* c) {

        std::vector<CopMove>& moves = scratchMoves();
        generateMoves(c, moves);

        const VertexBitField copsNow = maskOf(c);
        const VertexBitField candidates = VertexBitField::binaryAndNot(robberReach[t], copsNow);

        // GOOD: robber stations (after his move) from which the cops win, if they know where he is
        VertexBitField good;
        for (const CopMove& m : moves)
            good = VertexBitField::binaryOr(good, VertexBitField::binaryOr(maskOf(m.stations), winNext[m.rank]));

        VertexBitField win;

        if (!hidden[t]) {

            // The robber loses from r iff none of his free neighbours is a BAD station
            const VertexBitField bad = VertexBitField::binaryAndNot(VertexBitField::binaryAndNot(everything, good), copsNow);
            const VertexBitField robberEscapes = adjMatrix.expand(bad, false);
            win = VertexBitField::binaryAndNot(candidates, robberEscapes);

        } else {

            // One cop move must cover every station the hidden robber might have moved to
            uint8_t robbers[256];
            size_t robberCount = 0;
            adjMatrix.extractVertices(candidates, robbers, 256, robberCount);

            for (size_t i = 0; i < robberCount; ++i) {

                const int r = robbers[i];
                const VertexBitField cloud = VertexBitField::binaryAndNot(neighboursOf(r), copsNow);

                bool copsWinHere = cloud.none();   // the robber has no free station

                // Quick reject: if even the union of all moves misses part of the cloud, no single move can cover it
                if (!copsWinHere && VertexBitField::isSubset(good, cloud)) {
                    for (const CopMove& m : moves) {
                        const VertexBitField covered = VertexBitField::binaryOr(maskOf(m.stations), winNext[m.rank]);
                        if (VertexBitField::isSubset(covered, cloud)) { copsWinHere = true; break; }
                    }
                }

                if (copsWinHere) win.set(r);
            }
        }

        winCur[rank] = win;
    });
}

}  // namespace syb

using namespace syb;

// =============================================================================================
// 1. loadGraphFile
// =============================================================================================
bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    (void)p_param;
    filename = filename_param;
    graphStem = std::filesystem::path(filename_param).stem().string();
    k = k_param;

    if (k < 1 || k > MAX_K) {
        std::cerr << "Error: k must be 1.." << MAX_K << ".\n";
        return 1;
    }

    Graph g(filename);
    if (g.nodeCount <= 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }
    if (g.nodeCount > 200) {
        std::cerr << "Error: graph exceeds the 200-vertex limit for PackedAdjMatrix.\n";
        return 1;
    }
    N = g.nodeCount;

    for (int u = 0; u < N; ++u) {
        if (g.getEdge(u, u)) {
            std::cerr << "Error: self edges are not allowed (no piece may stay in place).\n";
            return 1;
        }
        for (int v = u + 1; v < N; ++v) {
            if (g.getEdge(u, v) != g.getEdge(v, u)) {
                std::cerr << "Error: graph must be undirected.\n";
                return 1;
            }
            if (g.getEdge(u, v)) adjMatrix.addEdge(u, v);
        }
        everything.set(u);
    }

    adj.constructFrom(&g);
    for (int u = 0; u < N; ++u) {
        if (adj.getEdges(u)[0] == 255) {
            std::cerr << "Error: station " << u + 1 << " has no neighbours; every piece must be able to move.\n";
            return 1;
        }
    }
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());
    mem.trackExternal("Packed Adj Matrix", sizeof(PackedAdjMatrix));

    // ---- configuration ----
    std::vector<int> list;

    robberStart = 82;
    if (env("SYB_ROBBER")) {
        if (!parseList(env("SYB_ROBBER"), list) || list.size() != 1) { std::cerr << "Error: bad SYB_ROBBER.\n"; return 1; }
        robberStart = list[0];
    }

    std::vector<int> cops = {41, 46, 124, 142};
    if (env("SYB_COPS")) {
        if (!parseList(env("SYB_COPS"), cops)) { std::cerr << "Error: bad SYB_COPS.\n"; return 1; }
        if (int(cops.size()) != k) { std::cerr << "Error: SYB_COPS must list exactly k stations.\n"; return 1; }
    } else if (k < 3) {
        std::cerr << "Error: the beginner rules define starts for 3 or 4 cops; set SYB_COPS for k=" << k << ".\n";
        return 1;
    } else {
        cops.resize(size_t(k));   // 3 cops: 41, 46, 124. 4 cops: also 142
    }

    if (env("SYB_ROUNDS")) {
        if (!parseList(env("SYB_ROUNDS"), list) || list.size() != 1 || list[0] < 1 || list[0] > MAX_ROUNDS) {
            std::cerr << "Error: SYB_ROUNDS must be 1.." << MAX_ROUNDS << ".\n";
            return 1;
        }
        rounds = list[0];
    }

    std::vector<int> hiddenRounds = {3, 8, 13};
    if (env("SYB_HIDDEN") && !parseList(env("SYB_HIDDEN"), hiddenRounds)) { std::cerr << "Error: bad SYB_HIDDEN.\n"; return 1; }
    for (int h : hiddenRounds) {
        if (h < 1 || h > MAX_ROUNDS) { std::cerr << "Error: hidden rounds must be 1.." << MAX_ROUNDS << ".\n"; return 1; }
        hidden[h] = true;
    }

    threadCount = std::max(1, int(std::thread::hardware_concurrency()));
    if (env("SYB_THREADS")) {
        if (!parseList(env("SYB_THREADS"), list) || list.size() != 1 || list[0] < 1) { std::cerr << "Error: bad SYB_THREADS.\n"; return 1; }
        threadCount = list[0];
    }

    // ---- validate the starting position (convert 1-based stations to 0-based vertices) ----
    if (robberStart < 1 || robberStart > N) { std::cerr << "Error: robber start out of range.\n"; return 1; }
    robberStart -= 1;
    for (int i = 0; i < k; ++i) {
        if (cops[i] < 1 || cops[i] > N) { std::cerr << "Error: cop start out of range.\n"; return 1; }
        copStart[i] = uint8_t(cops[i] - 1);
    }
    std::sort(copStart, copStart + k);
    for (int i = 1; i < k; ++i)
        if (copStart[i] == copStart[i - 1]) { std::cerr << "Error: two cops may not share a start station.\n"; return 1; }
    for (int i = 0; i < k; ++i)
        if (copStart[i] == robberStart) { std::cerr << "Error: the robber may not start on a cop.\n"; return 1; }

    std::cout << "Scotland Yard beginner-rules solver\n"
              << "  graph        : " << graphStem << " (" << N << " stations, max degree " << adj.maxDegree << ")\n"
              << "  cops         : " << k << ", starting on " << stationList(copStart) << "\n"
              << "  robber       : starting on " << robberStart + 1 << "\n"
              << "  rounds       : " << rounds << " (robber moves first)\n"
              << "  hidden rounds: ";
    bool any = false;
    for (int t = 1; t <= rounds; ++t) if (hidden[t]) { std::cout << (any ? "," : "") << t; any = true; }
    std::cout << (any ? "" : "none") << "\n"
              << "  threads      : " << threadCount << "\n";

    return 0;
}

// =============================================================================================
// 2. buildAuxGraph  (no aux graph: cop set ranking, reachability bits, the two round tables)
// =============================================================================================
bool buildAuxGraph() {

    for (int a = 0; a < 256; ++a) {
        choose[a][0] = 1;
        for (int b = 1; b <= MAX_K; ++b)
            choose[a][b] = (a == 0) ? 0 : choose[a - 1][b - 1] + choose[a - 1][b];
    }
    copSetCount = choose[N][k];
    reachWords = size_t((copSetCount + 63) / 64);

    std::cout << "  cop sets     : " << copSetCount << "\n";

    mem.requestAlloc<uint64_t>("Reachable cop sets (bit per set per round)", reachWords * size_t(rounds + 2), &copReach);
    mem.requestAlloc<VertexBitField>("Round table A", size_t(copSetCount), &tableA);
    mem.requestAlloc<VertexBitField>("Round table B", size_t(copSetCount), &tableB);
    mem.allocate();

    if (!copReach || !tableA || !tableB) {
        std::cerr << "Error: unable to allocate the round tables.\n";
        return 1;
    }

    std::fill(copReach, copReach + reachWords * size_t(rounds + 2), 0);
    return 0;
}

// =============================================================================================
// 3. initializeCaptures  (forward pass: which positions can occur at the start of each round)
// =============================================================================================
bool initializeCaptures() {

    auto t0 = std::chrono::steady_clock::now();

    robberReach[1].set(robberStart);
    markReachable(1, rankCops(copStart));

    for (int t = 1; t <= rounds; ++t) {

        robberReach[t + 1] = adjMatrix.expand(robberReach[t], false);

        std::atomic<uint64_t> reachable{0};
        forEachReachableCopSet(t, [&](uint32_t, const uint8_t* c) {
            reachable.fetch_add(1, std::memory_order_relaxed);
            std::vector<CopMove>& moves = scratchMoves();
            if (generateMoves(c, moves) == 0) blockedSets.fetch_add(1, std::memory_order_relaxed);

            VertexBitField reachableStations;
            for (const CopMove& m : moves) {
                markReachable(t + 1, m.rank);
                reachableStations = VertexBitField::binaryOr(reachableStations, maskOf(m.stations));
            }

            // A free station next to a cop that no complete legal joint move lands on. If the robber
            // stood there, a real cop could still step onto him and end the game early, but this
            // solver (which treats a turn as one complete joint move) would not count that capture
            const VertexBitField copsNow = maskOf(c);
            const VertexBitField adjacent = VertexBitField::binaryAndNot(adjMatrix.expand(copsNow, false), copsNow);
            if (!VertexBitField::isSubset(reachableStations, adjacent)) uncoveredSets.fetch_add(1, std::memory_order_relaxed);
        });

        std::cout << "  round " << (t < 10 ? " " : "") << t << ": " << reachable.load() << " reachable cop sets" << std::endl;
    }

    std::cout << "  cop sets (any round) with NO legal joint move: " << blockedSets.load() << "\n"
              << "  cop sets (any round) where a cop could step onto a station that no complete joint move reaches: "
              << uncoveredSets.load() << "\n"
              << "  forward pass took "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s\n";
    return 0;
}

// =============================================================================================
// 4. mainLoop  (backwards pass: WIN[rounds] down to WIN[1])
// =============================================================================================
bool mainLoop() {

    std::cout << "Solving rounds backwards...\n";

    winNext = tableA;
    winCur = tableB;

    // WIN[rounds + 1] is empty everywhere: the game is over and the robber survived
    std::fill(winNext, winNext + copSetCount, VertexBitField());

    for (int t = rounds; t >= 1; --t) {

        auto t0 = std::chrono::steady_clock::now();
        solveRound(t);

        std::cout << "  round " << (t < 10 ? " " : "") << t << (hidden[t] ? " (hidden)" : "         ")
                  << " done in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s" << std::endl;

        // Keep WIN[1] in winCur and WIN[2] in winNext for the final report
        if (t > 1) std::swap(winNext, winCur);
    }

    return 0;
}

// =============================================================================================
// 5. findFinalResult
// =============================================================================================
bool findFinalResult() {

    const uint32_t startRank = rankCops(copStart);
    const VertexBitField copsNow = maskOf(copStart);
    copsWin = winCur[startRank].test(robberStart);

    std::ostringstream out;
    out << "RESULT: " << (copsWin ? "WIN" : "LOSS") << ". " << k << " cops starting on " << stationList(copStart)
        << ", robber starting on " << robberStart + 1 << ", " << rounds << " rounds: "
        << (copsWin ? "the cops can guarantee a capture.\n" : "the robber can always escape.\n");

    // Explain the first round. winNext holds WIN[2] here (or nothing, for a 1-round game)
    std::vector<CopMove>& moves = scratchMoves();
    generateMoves(copStart, moves);
    auto coverOf = [&](const CopMove& m) {
        VertexBitField covered = maskOf(m.stations);
        if (rounds >= 2) covered = VertexBitField::binaryOr(covered, winNext[m.rank]);
        return covered;
    };

    const VertexBitField firstMoves = VertexBitField::binaryAndNot(neighboursOf(robberStart), copsNow);
    uint8_t robbers[256];
    size_t robberCount = 0;
    adjMatrix.extractVertices(firstMoves, robbers, 256, robberCount);

    if (hidden[1]) {
        const CopMove* winning = nullptr;
        for (const CopMove& m : moves)
            if (VertexBitField::isSubset(coverOf(m), firstMoves)) { winning = &m; break; }
        if (winning) out << "  round 1 is hidden; a winning first cop move is " << stationList(winning->stations) << "\n";
        else out << "  round 1 is hidden; no single first cop move covers every robber option\n";
    } else {
        for (size_t i = 0; i < robberCount; ++i) {
            const int r = robbers[i];
            const CopMove* reply = nullptr;
            for (const CopMove& m : moves)
                if (coverOf(m).test(r)) { reply = &m; break; }
            out << "  robber's first move to " << r + 1 << ": ";
            if (reply) out << "cops win by moving to " << stationList(reply->stations) << "\n";
            else out << "no cop reply wins (the robber escapes this way)\n";
        }
    }

    summary = out.str();
    std::cout << "\n--- FINAL VERDICT ---\n" << summary;
    mem.print();
    return 0;
}

// =============================================================================================
// 6. outputData
// =============================================================================================
bool outputData() {

    const std::filesystem::path path = std::filesystem::path("cache") /
        ("k_cops_scotlandyard_beginner__" + graphStem + "__" + std::to_string(k) + "-cops__result.txt");

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path);
    file << summary;
    if (!file) {
        std::cerr << "Error: could not write " << path.string() << "\n";
        return 1;
    }

    std::cout << "Result written to " << path.string() << "\n";
    return 0;
}

#pragma GCC diagnostic pop
