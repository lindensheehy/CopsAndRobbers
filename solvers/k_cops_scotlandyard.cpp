/**
 * ============================================================================
 * FILE --- k_cops_scotlandyard.cpp
 * ============================================================================
 * OVERVIEW
 * Oracle-guided, finite-horizon Scotland Yard solver (docs/scotlandyard_solver_tidy.md §2).
 * Production port of experimental/oracle_cpu onto the app/main.cpp 6-step pipeline.
 *
 * Question answered: from a FIXED cop start (default: the V5 oracle's best full-visibility
 * start), can the cops guarantee capture within H rounds (default 24) when the robber is only
 * revealed after robber moves 3, 8, 13, 18, 24?  Each initially visible robber vertex is its
 * own root (cops may react to it), or one hidden initial cloud is solved with SY_HIDDEN_START.
 *
 * GAME (identical to the prototype and to k_cops_fullvis_v5, whose oracle we read)
 * - Sorted multiset of 1..4 cops. Every piece moves along an edge each turn. No added stay moves;
 *   matrix self-edges are honoured (AdjacencyList includes them, exactly as V5 did).
 * - A round = joint cop move, then robber move. Capture on either move. Captured robber
 *   possibilities are removed before the surviving cloud expands, and again after.
 * - Hidden robber turns union the cloud (ONE cop policy must handle all of it). At a reveal round,
 *   every surviving singleton must be won separately (AND). At every node, some joint cop move
 *   must win (OR). Capture must happen by the end of round H.
 *
 * ORACLE (read-only, memory mapped): cache/fullvis_v5_strategy__<graph>__<k>-cops.bin.
 * One byte per (C, r); bits 1-7 hold the full-visibility ROBBER-TURN distance. For a proposed cop
 * move C', max over the surviving cloud of R(C', r) is an optimistic lower bound for the partially
 * observed continuation (more information can't make capture harder), so a move whose bound
 * exceeds the remaining plies can be discarded without losing any winning strategy. Surviving moves
 * are searched best-bound first. Note: the oracle GUIDES the search; full-visibility best moves alone
 * are NOT a proof under hidden information.
 *
 * PERFORMANCE (vs. the prototype)
 * - scoreCandidates(): batch oracle scoring, AVX2 masked-max over the contiguous oracle row
 *   (runtime-dispatched, scalar fallback) with software prefetch. This is the seam for a future
 *   CUDA backend: it is a pure function of (candidate ids, cloud) -> bound bytes.
 * - Allocation-free joint move generation (odometer with symmetric-duplicate skipping,
 *   per-thread LIFO arena) instead of std::function recursion + vector + sort.
 * - Shared, lossy, lock-free (seqlock) transposition table with full keys (no false hits),
 *   shared by all threads.
 * - Work-stealing-lite parallelism: dynamic root queue, plus YBWC-style splitting of OR (cop move)
 *   and AND (reveal) nodes when workers are idle, with cancellation of losing/irrelevant siblings.
 * - Oracle pages are pre-warmed into the file cache so the search doesn't stall on random faults.
 *
 * CONFIGURATION: <graph> <k> [p] from app/main.cpp (p is ignored; the reveal schedule replaces it),
 * plus optional environment variables:
 *   SY_ORACLE        oracle path (default cache/fullvis_v5_strategy__<graph>__<k>-cops.bin)
 *   SY_ORACLE_MODE   bound (default) | order (sort only) | off (no oracle, needs SY_START)
 *   SY_START         cop start "a,b,c,d" (default: oracle best start)
 *   SY_ROUNDS        horizon H (default 24)
 *   SY_REVEALS       reveal rounds "3,8,13,18,24" (default) or "none"
 *   SY_ROBBERS       "all" (default) or list of initially visible robber vertices
 *   SY_HIDDEN_START  list: ONE initially hidden robber cloud instead of visible roots
 *   SY_THREADS       worker threads (default hardware concurrency)
 *   SY_TT_MB         transposition table MiB (default 2048); SY_TT_ENTRIES overrides (0 = off)
 *   SY_SPLIT_ROUNDS  deepest round at which idle workers may split a node (default 8)
 *   SY_FORCE_SPLIT   1 = split whenever possible (testing the parallel path)
 *   SY_MAX_SECONDS   time budget (default 0 = unlimited); SY_MAX_NODES node budget
 *   SY_WARM          0 disables oracle pre-warming
 *   SY_CHECKPOINT    completed-root checkpoint file; SY_RESUME=1 resumes from it
 *   SY_RESULTS       results JSON path (default cache/k_cops_scotlandyard__<graph>__<k>-cops__results.json)
 *
 * A time/node budget or Ctrl+C yields UNKNOWN for unfinished roots, never LOSS.
 * ============================================================================
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "Allocator.h"
#include "PackedAdjMatrix.h"
#include "VertexBitField.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define SY_X86 1
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sy {

constexpr int MAX_K = 4;
constexpr int MAX_N = 200;            // PackedAdjMatrix capacity
constexpr uint8_t SCORE_INF = 255;    // decoded oracle bound: robber escapes even with full info
constexpr int SMALL_CLOUD = 12;       // below this popcount, score by bit iteration
constexpr int PREFETCH_AHEAD = 6;
constexpr uint64_t ORACLE_HEADER = 64;

enum Res : uint8_t { LOSS = 0, WIN = 1, ABORT = 2 };
enum class Mode { Off, Order, Bound };

using Cloud = VertexBitField;

// ----------------------------------------------------------------------------
// Configuration / global read-only state
// ----------------------------------------------------------------------------
const char* graphPath = nullptr;
std::string graphStem;
int N = 0, K = 0;
int maxDegree = 0;
AdjacencyList adj;
PackedAdjMatrix packed;
Allocator mem;
std::vector<std::vector<uint8_t>> neighbors;   // copy of adj rows, compact
std::string canonicalGraph;                     // for checkpoint identity

uint64_t choose[MAX_N + MAX_K + 2][MAX_K + 1]{};
uint64_t configCount = 0;

std::string oraclePath, resultsPath, checkpointPath;
Mode mode = Mode::Bound;
std::string modeText = "bound";
int horizon = 24;
std::vector<int> reveals{3, 8, 13, 18, 24};
bool reveal[257]{};
std::string startText, robbersText = "all", hiddenText;
int threadCount = 1;
uint64_t ttEntriesRequested = 0;
bool ttEntriesSet = false;
uint64_t ttMiB = 2048;
int splitRounds = 8;
bool forceSplit = false;
double maxSeconds = 0;
uint64_t maxNodes = 0;
bool warmOracle = true;
bool resume = false;

uint8_t start[MAX_K]{};
uint32_t startId = 0;
std::vector<Cloud> roots;
struct RootResult { int value = -1; bool hasMove = false; uint32_t move = 0; };
std::vector<RootResult> results;
uint64_t identity = 0;

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------
inline int ctz64(uint64_t x) { return __builtin_ctzll(x); }
inline int popcount(const Cloud& b) {
    return __builtin_popcountll(b.words[0]) + __builtin_popcountll(b.words[1]) +
           __builtin_popcountll(b.words[2]) + __builtin_popcountll(b.words[3]);
}
template <class F> inline void eachVertex(const Cloud& b, F f) {
    for (int w = 0; w < 4; ++w) {
        uint64_t m = b.words[w];
        while (m) {
            f((w << 6) + ctz64(m));
            m &= m - 1;
        }
    }
}
inline bool sameCloud(const Cloud& a, const Cloud& b) {
    return a.words[0] == b.words[0] && a.words[1] == b.words[1] && a.words[2] == b.words[2] &&
           a.words[3] == b.words[3];
}
inline uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline uint32_t packCops(const uint8_t* c) {
    uint32_t p = 0;
    for (int i = 0; i < K; ++i) p |= uint32_t(c[i]) << (8 * i);
    return p;
}
inline void unpackCops(uint32_t p, uint8_t* c) {
    for (int i = 0; i < K; ++i) c[i] = uint8_t(p >> (8 * i));
}
inline Cloud copMask(uint32_t packedCops) {
    Cloud m;
    for (int i = 0; i < K; ++i) m.set((packedCops >> (8 * i)) & 0xFF);
    return m;
}
std::string copsText(uint32_t packedCops) {
    std::string s = "[";
    for (int i = 0; i < K; ++i) {
        if (i) s += ",";
        s += std::to_string((packedCops >> (8 * i)) & 0xFF);
    }
    return s + "]";
}

// Lexicographic rank of a nondecreasing multiset (identical to V5 rankCopConfig).
inline uint32_t rankCops(const uint8_t* c) {
    const int M = N + K - 1;
    uint64_t r = 0;
    int prev = -1;
    for (int i = 0; i < K; ++i) {
        const int b = int(c[i]) + i;
        r += choose[M - prev - 1][K - i] - choose[M - b][K - i];
        prev = b;
    }
    return uint32_t(r);
}
bool unrankCops(uint64_t id, uint8_t* c) {
    if (id >= configCount) return false;
    int lo = 0;
    for (int i = 0; i < K; ++i) {
        bool found = false;
        for (int v = lo; v < N; ++v) {
            const int left = K - i - 1;
            const uint64_t block = choose[N - v + left - 1][left];
            if (id < block) { c[i] = uint8_t(v); lo = v; found = true; break; }
            id -= block;
        }
        if (!found) return false;
    }
    return true;
}

// Env parsing -----------------------------------------------------------------
const char* env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}
bool parseUInt(const std::string& s, uint64_t& out) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || s.size() > 18)
        return false;
    out = std::stoull(s);
    return true;
}
bool parseList(const std::string& s, std::vector<int>& out) {
    out.clear();
    if (s == "none") return true;
    std::stringstream in(s);
    std::string t;
    while (std::getline(in, t, ',')) {
        uint64_t x;
        if (!parseUInt(t, x) || x > 65535) return false;
        out.push_back(int(x));
    }
    return !out.empty() && s.back() != ',';
}

// ----------------------------------------------------------------------------
// Oracle: read-only mapping + header + decode
// ----------------------------------------------------------------------------
struct Mapping {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
#else
    int fd = -1;
#endif
    const uint8_t* data = nullptr;
    uint64_t size = 0;

    bool open(const std::string& path, std::string& err) {
        std::error_code ec;
        size = std::filesystem::file_size(path, ec);
        if (ec) { err = "cannot stat oracle '" + path + "': " + ec.message(); return false; }
        if (size < ORACLE_HEADER) { err = "oracle file too small"; return false; }
#ifdef _WIN32
        file = CreateFileW(std::filesystem::path(path).wstring().c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping)
            data = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
#else
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            void* p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED) data = static_cast<const uint8_t*>(p);
        }
#endif
        if (!data) { close(); err = "cannot map oracle read-only: " + path; return false; }
        return true;
    }
    void close() {
#ifdef _WIN32
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        mapping = nullptr;
        file = INVALID_HANDLE_VALUE;
#else
        if (data) munmap(const_cast<uint8_t*>(data), size);
        if (fd >= 0) ::close(fd);
        fd = -1;
#endif
        data = nullptr;
    }
    ~Mapping() { close(); }
};

Mapping oracleMap;
const uint8_t* oracleRows = nullptr;   // data + 64; row of config id = oracleRows + id * N
uint32_t oracleBase = 0, oracleCap = 0;
uint64_t oracleBest = UINT64_MAX;
uint32_t oracleBestPly = 0;
uint8_t decodeLut[256];
bool haveAvx2 = false;

inline uint64_t headerField(int off, int bytes) {
    uint64_t x = 0;
    for (int i = 0; i < bytes; ++i) x |= uint64_t(oracleMap.data[off + i]) << (8 * i);
    return x;
}

bool loadOracle(std::string& err) {
    if (!oracleMap.open(oraclePath, err)) return false;
    oracleBase = uint32_t(headerField(52, 4));
    oracleCap = uint32_t(headerField(56, 4));
    oracleBest = headerField(32, 8);
    oracleBestPly = uint32_t(headerField(40, 4));
    const bool ok =
        headerField(0, 4) == 0x35564643 && headerField(4, 4) == 3 &&
        headerField(8, 4) == uint64_t(N) && headerField(12, 4) == uint64_t(K) &&
        headerField(16, 8) == configCount && headerField(24, 8) == configCount * N &&
        headerField(44, 4) == 2 && headerField(48, 4) == uint64_t(maxDegree) &&
        maxDegree <= 125 && oracleBase == uint32_t(maxDegree + 1) && oracleCap == 126 - oracleBase &&
        headerField(60, 4) == 127 && oracleMap.size == ORACLE_HEADER + configCount * N &&
        (oracleBest == UINT64_MAX || oracleBest < configCount);
    if (!ok) {
        err = "V5 oracle header/size does not match this graph, cop count and movement rules "
              "(expected v3 header, encoding 2, n=" + std::to_string(N) + ", k=" +
              std::to_string(K) + ", max degree " + std::to_string(maxDegree) + ")";
        return false;
    }
    oracleRows = oracleMap.data + ORACLE_HEADER;
    // Payload p = byte >> 1:  p < base -> unresolved at fixed point = robber escapes (INF)
    //                         p == 127 -> solved but saturated: conservative bound cap + 1
    //                         else     -> exact robber-turn plies p - base
    for (int b = 0; b < 256; ++b) {
        const uint32_t p = uint32_t(b) >> 1;
        decodeLut[b] = p < oracleBase ? SCORE_INF
                       : p == 127     ? uint8_t(std::min<uint32_t>(oracleCap + 1, 254))
                                      : uint8_t(p - oracleBase);
    }
    return true;
}

// Pull the whole oracle into the OS file cache with sequential I/O, so the search doesn't pay
// a random disk fault per candidate row. Pages are clean file-backed pages: the OS can still
// evict them under pressure, so this is a performance hint, never a correctness requirement.
void warmOracleCache() {
    const uint64_t bytes = oracleMap.size;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) && bytes > ms.ullTotalPhys / 10 * 7) {
        std::cout << "  oracle warm-up skipped: file is >70% of physical RAM\n";
        return;
    }
#endif
    if (bytes < (64ull << 20)) {
        volatile uint8_t sink = 0;
        for (uint64_t i = 0; i < bytes; i += 4096) sink ^= oracleMap.data[i];
        (void)sink;
        return;
    }
    auto t0 = std::chrono::steady_clock::now();
    std::atomic<uint64_t> next{0}, done{0};
    const uint64_t CHUNK = 64ull << 20;
    std::vector<std::thread> pool;
    const int warmThreads = std::max(1, std::min(threadCount, 8));
    for (int t = 0; t < warmThreads; ++t)
        pool.emplace_back([&] {
            volatile uint8_t sink = 0;
            for (;;) {
                const uint64_t s = next.fetch_add(CHUNK);
                if (s >= bytes) break;
                const uint64_t e = std::min(bytes, s + CHUNK);
#ifdef _WIN32
                // Async hint first (one large I/O), then touch to guarantee residency.
                WIN32_MEMORY_RANGE_ENTRY range{const_cast<uint8_t*>(oracleMap.data + s), size_t(e - s)};
                using PrefetchFn = BOOL(WINAPI*)(HANDLE, ULONG_PTR, PWIN32_MEMORY_RANGE_ENTRY, ULONG);
                static PrefetchFn prefetch = reinterpret_cast<PrefetchFn>(reinterpret_cast<void*>(
                    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PrefetchVirtualMemory")));
                if (prefetch) prefetch(GetCurrentProcess(), 1, &range, 0);
#else
                madvise(const_cast<uint8_t*>(oracleMap.data + (s & ~4095ull)), e - (s & ~4095ull),
                        MADV_WILLNEED);
#endif
                for (uint64_t i = s; i < e; i += 4096) sink ^= oracleMap.data[i];
                done.fetch_add(e - s);
            }
            (void)sink;
        });
    auto last = t0;
    while (done.load() < bytes) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::seconds(2)) {
            std::cout << "\r  warming oracle: " << std::fixed << std::setprecision(1)
                      << 100.0 * double(done.load()) / double(bytes) << "%   " << std::flush;
            last = now;
        }
    }
    for (auto& t : pool) t.join();
    std::cout << "\r  warmed oracle (" << std::fixed << std::setprecision(2)
              << double(bytes) / (1 << 30) << " GiB) in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
              << " s          \n";
}

// ----------------------------------------------------------------------------
// Candidate scoring  (the GPU seam)
// ----------------------------------------------------------------------------
struct Cand {
    uint32_t id;      // rank of the destination configuration
    uint32_t dest;    // packed sorted destination cops
    uint8_t score;    // oracle lower bound (robber-turn plies), SCORE_INF = hopeless
};

#ifdef SY_X86
__attribute__((target("avx2"))) static uint8_t rowMaxAvx2(const uint8_t* row,
                                                          const uint8_t* mask) {
    const __m256i lowBits = _mm256_set1_epi8(0x7F);
    const __m256i baseV = _mm256_set1_epi8(char(oracleBase));
    const __m256i v127 = _mm256_set1_epi8(127);
    const __m256i capV = _mm256_set1_epi8(char(decodeLut[254]));
    __m256i acc = _mm256_setzero_si256();
    int i = 0;
    for (; i + 32 <= N; i += 32) {
        const __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + i));
        const __m256i p = _mm256_and_si256(_mm256_srli_epi16(x, 1), lowBits);
        const __m256i inf = _mm256_cmpgt_epi8(baseV, p);           // p < base (both in 0..127)
        const __m256i sat = _mm256_cmpeq_epi8(p, v127);
        __m256i val = _mm256_subs_epu8(p, baseV);
        val = _mm256_blendv_epi8(val, capV, sat);
        val = _mm256_or_si256(val, inf);
        val = _mm256_and_si256(val, _mm256_load_si256(reinterpret_cast<const __m256i*>(mask + i)));
        acc = _mm256_max_epu8(acc, val);
    }
    __m128i m = _mm_max_epu8(_mm256_castsi256_si128(acc), _mm256_extracti128_si256(acc, 1));
    m = _mm_max_epu8(m, _mm_srli_si128(m, 8));
    m = _mm_max_epu8(m, _mm_srli_si128(m, 4));
    m = _mm_max_epu8(m, _mm_srli_si128(m, 2));
    m = _mm_max_epu8(m, _mm_srli_si128(m, 1));
    uint8_t best = uint8_t(_mm_cvtsi128_si32(m) & 0xFF);
    for (; i < N; ++i)  // scalar tail: never read past the end of the mapped row
        if (mask[i]) best = std::max(best, decodeLut[row[i]]);
    return best;
}
#endif

inline void prefetchRow(uint32_t id) {
    const uint8_t* row = oracleRows + uint64_t(id) * N;
    for (int off = 0; off < N + 63; off += 64) __builtin_prefetch(row + std::min(off, N - 1));
}

// For every candidate i: score_i = max over r in (cloud \ dest_i) of decoded R(id_i, r).
// `cloud` is the robber cloud after removing the CURRENT cops; candidates whose destination
// empties it have already been handled by the caller. Pure function of its inputs.
void scoreCandidates(Cand* cands, int n, const Cloud& cloud) {
    const int pc = popcount(cloud);
#ifdef SY_X86
    if (haveAvx2 && pc >= SMALL_CLOUD) {
        alignas(32) uint8_t mask[256];
        std::memset(mask, 0, sizeof(mask));
        eachVertex(cloud, [&](int r) { mask[r] = 0xFF; });
        for (int i = 0; i < n; ++i) {
            if (i + PREFETCH_AHEAD < n) prefetchRow(cands[i + PREFETCH_AHEAD].id);
            uint8_t saved[MAX_K];
            const uint32_t d = cands[i].dest;
            for (int j = 0; j < K; ++j) {   // captured by this move: exclude, then restore
                const int v = (d >> (8 * j)) & 0xFF;
                saved[j] = mask[v];
                mask[v] = 0;
            }
            cands[i].score = rowMaxAvx2(oracleRows + uint64_t(cands[i].id) * N, mask);
            for (int j = K - 1; j >= 0; --j) mask[(d >> (8 * j)) & 0xFF] = saved[j];
        }
        return;
    }
#endif
    (void)pc;
    for (int i = 0; i < n; ++i) {
        if (i + PREFETCH_AHEAD < n) prefetchRow(cands[i + PREFETCH_AHEAD].id);
        const uint8_t* row = oracleRows + uint64_t(cands[i].id) * N;
        const Cloud mid = Cloud::binaryAndNot(cloud, copMask(cands[i].dest));
        uint8_t best = 0;
        eachVertex(mid, [&](int r) { best = std::max(best, decodeLut[row[r]]); });
        cands[i].score = best;
    }
}

// ----------------------------------------------------------------------------
// Transposition table: shared, lossy, seqlocked, full keys
// ----------------------------------------------------------------------------
struct alignas(64) TTEntry {
    uint32_t seq;        // odd while being written
    uint32_t cfg;
    uint64_t cloud[4];
    uint32_t move;       // packed winning destination (valid when result == WIN)
    uint8_t round;       // 0 = empty
    uint8_t result;      // WIN / LOSS
    uint8_t pad[14];
};
static_assert(sizeof(TTEntry) == 64, "TT entry must be one cache line");

TTEntry* tt = nullptr;
uint64_t ttMask = 0;   // number of 2-entry buckets - 1
bool ttEnabled = false;

inline uint64_t ttHash(uint32_t cfg, const Cloud& b, int round) {
    uint64_t h = mix(uint64_t(cfg) * 0x9E3779B97F4A7C15ULL ^ uint64_t(round));
    for (int w = 0; w < 4; ++w) h = mix(h ^ b.words[w]);
    return h;
}

bool ttProbe(uint32_t cfg, const Cloud& b, int round, uint64_t h, uint8_t& result, uint32_t& move) {
    TTEntry* bucket = tt + ((h & ttMask) << 1);
    for (int s = 0; s < 2; ++s) {
        TTEntry* e = bucket + s;
        const uint32_t s1 = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1) continue;
        const uint8_t r = __atomic_load_n(&e->round, __ATOMIC_RELAXED);
        const uint32_t c = __atomic_load_n(&e->cfg, __ATOMIC_RELAXED);
        const uint64_t w0 = __atomic_load_n(&e->cloud[0], __ATOMIC_RELAXED);
        const uint64_t w1 = __atomic_load_n(&e->cloud[1], __ATOMIC_RELAXED);
        const uint64_t w2 = __atomic_load_n(&e->cloud[2], __ATOMIC_RELAXED);
        const uint64_t w3 = __atomic_load_n(&e->cloud[3], __ATOMIC_RELAXED);
        const uint8_t res = __atomic_load_n(&e->result, __ATOMIC_RELAXED);
        const uint32_t mv = __atomic_load_n(&e->move, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&e->seq, __ATOMIC_RELAXED) != s1) continue;   // torn read
        if (r == round && c == cfg && w0 == b.words[0] && w1 == b.words[1] && w2 == b.words[2] &&
            w3 == b.words[3]) {
            result = res;
            move = mv;
            return true;
        }
    }
    return false;
}

void ttStore(uint32_t cfg, const Cloud& b, int round, uint64_t h, uint8_t result, uint32_t move) {
    TTEntry* bucket = tt + ((h & ttMask) << 1);
    // Replacement: same key > empty > shallower remaining depth (larger round).
    int pick = -1;
    for (int s = 0; s < 2 && pick < 0; ++s) {
        TTEntry* e = bucket + s;
        const uint8_t r = __atomic_load_n(&e->round, __ATOMIC_RELAXED);
        if (r == 0 || (r == round && __atomic_load_n(&e->cfg, __ATOMIC_RELAXED) == cfg &&
                       __atomic_load_n(&e->cloud[0], __ATOMIC_RELAXED) == b.words[0]))
            pick = s;
    }
    if (pick < 0) {
        const uint8_t r0 = __atomic_load_n(&bucket[0].round, __ATOMIC_RELAXED);
        const uint8_t r1 = __atomic_load_n(&bucket[1].round, __ATOMIC_RELAXED);
        pick = r0 > r1 ? 0 : r1 > r0 ? 1 : int((h >> 63) & 1);
        // Don't evict a much deeper (more valuable) result with a near-leaf one.
        const uint8_t victim = pick ? r1 : r0;
        if (victim + 2 < round) pick = 1 - pick;
    }
    TTEntry* e = bucket + pick;
    uint32_t s = __atomic_load_n(&e->seq, __ATOMIC_RELAXED);
    if ((s & 1) || !__atomic_compare_exchange_n(&e->seq, &s, s + 1, false, __ATOMIC_ACQUIRE,
                                                __ATOMIC_RELAXED))
        return;   // another writer owns it: lossy table, just skip
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&e->cfg, cfg, __ATOMIC_RELAXED);
    for (int w = 0; w < 4; ++w) __atomic_store_n(&e->cloud[w], b.words[w], __ATOMIC_RELAXED);
    __atomic_store_n(&e->move, move, __ATOMIC_RELAXED);
    __atomic_store_n(&e->result, result, __ATOMIC_RELAXED);
    __atomic_store_n(&e->round, uint8_t(round), __ATOMIC_RELAXED);
    __atomic_store_n(&e->seq, s + 2, __ATOMIC_RELEASE);
}

// ----------------------------------------------------------------------------
// Run control, statistics, per-thread scratch
// ----------------------------------------------------------------------------
volatile std::sig_atomic_t interrupted = 0;
void onSignal(int) { interrupted = 1; }

std::atomic<bool> gStop{false};
std::string stopReason;
std::mutex stopMutex;
std::chrono::steady_clock::time_point runStart;
std::atomic<uint64_t> gNodes{0};

void requestStop(const char* why) {
    std::lock_guard<std::mutex> lock(stopMutex);
    if (!gStop.exchange(true)) stopReason = why;
}

struct Stats {
    uint64_t nodes = 0, candidates = 0, scored = 0, pruned = 0, ttHits = 0, splits = 0;
    void add(const Stats& o) {
        nodes += o.nodes; candidates += o.candidates; scored += o.scored;
        pruned += o.pruned; ttHits += o.ttHits; splits += o.splits;
    }
};

// LIFO arena of candidate buffers. Blocks never move, so pointers stay valid while frames nest
// (including frames of tasks executed while helping).
struct Arena {
    std::vector<std::unique_ptr<Cand[]>> blocks;
    std::vector<size_t> caps;
    size_t block = 0, used = 0;
    struct Mark { size_t block, used; };
    Mark mark() const { return {block, used}; }
    void release(Mark m) { block = m.block; used = m.used; }
    Cand* alloc(size_t n) {
        if (blocks.empty()) { blocks.emplace_back(new Cand[1 << 18]); caps.push_back(1 << 18); }
        if (used + n > caps[block]) {
            ++block;
            used = 0;
            while (block < blocks.size() && caps[block] < n) ++block;   // skip too-small blocks
            if (block >= blocks.size()) {
                const size_t c = std::max<size_t>(n, 1 << 18);
                blocks.emplace_back(new Cand[c]);
                caps.push_back(c);
                block = blocks.size() - 1;
            }
        }
        Cand* p = blocks[block].get() + used;
        used += n;
        return p;
    }
};

struct WorkerState {
    Arena arena;
    Stats stats;
    uint32_t tick = 0;
};
thread_local WorkerState* tw = nullptr;
std::mutex statsMutex;
Stats totals;

// Cancellation chain: a split node owns a flag; descendants poll the whole chain.
struct Cancel {
    std::atomic<bool> flag{false};
    const Cancel* parent = nullptr;
};
inline bool cancelled(const Cancel* c) {
    if (gStop.load(std::memory_order_relaxed)) return true;
    for (; c; c = c->parent)
        if (c->flag.load(std::memory_order_relaxed)) return true;
    return false;
}

inline void countNode() {
    WorkerState& w = *tw;
    ++w.stats.nodes;
    if (maxNodes) {
        if (gNodes.fetch_add(1, std::memory_order_relaxed) + 1 > maxNodes) requestStop("node budget");
        return;
    }
    if ((++w.tick & 1023) == 0) {
        gNodes.fetch_add(1024, std::memory_order_relaxed);
        if (interrupted) requestStop("interrupted");
        if (maxSeconds > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count() >=
                maxSeconds)
            requestStop("time budget");
    }
}

// ----------------------------------------------------------------------------
// Task pool (LIFO; tasks are short-lived objects owned by a waiting frame)
// ----------------------------------------------------------------------------
struct Task {
    int level = 0;            // 2*round (OR split) or 2*round+1 (AND split); roots = 0
    const void* group = nullptr;
    virtual void run() = 0;
    virtual ~Task() = default;
};

std::mutex poolMutex;
std::condition_variable poolCv;
std::vector<Task*> pool;
std::atomic<int> idleWorkers{0};
bool shuttingDown = false;
std::atomic<int> outstandingRoots{0};

void pushTasks(Task** tasks, int n) {
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        for (int i = n - 1; i >= 0; --i) pool.push_back(tasks[i]);   // first task ends on top
    }
    poolCv.notify_all();
}

// Take a task a waiting frame may run without unbounded nesting: its own group's tasks, or tasks
// strictly deeper than itself. Scans a bounded window from the top of the stack.
Task* tryPopFor(int level, const void* group) {
    std::lock_guard<std::mutex> lock(poolMutex);
    const int n = int(pool.size());
    for (int i = n - 1; i >= 0 && i >= n - 64; --i) {
        Task* t = pool[i];
        if (t->group == group || t->level > level) {
            pool.erase(pool.begin() + i);
            return t;
        }
    }
    return nullptr;
}

void helpUntil(std::atomic<int>& pending, int level, const void* group) {
    int spins = 0;
    while (pending.load(std::memory_order_acquire) > 0) {
        if (Task* t = tryPopFor(level, group)) {
            t->run();
            spins = 0;
        } else if (++spins < 64) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
}

inline bool shouldSplit(int round, int count) {
    if (threadCount <= 1 || count < 2 || round > splitRounds) return false;
    return forceSplit || idleWorkers.load(std::memory_order_relaxed) > 0;
}

// ----------------------------------------------------------------------------
// The search
// ----------------------------------------------------------------------------
Res solve(uint32_t cfg, uint32_t cops, Cloud b, int round, const Cancel* chain, uint32_t* winMove);

// Cop move `dest` has been made from a node whose cloud (after removing current cops) is `b`.
Res evalAction(const Cand& a, const Cloud& b, int round, const Cancel* chain);

struct OrGroup {
    Cancel cancel;
    std::atomic<int> pending{0};
    std::atomic<bool> found{false}, aborted{false};
    uint32_t move = 0;
    const Cloud* b = nullptr;
    int round = 0;
};
struct OrTask final : Task {
    OrGroup* g = nullptr;
    const Cand* cand = nullptr;
    void run() override {
        if (!cancelled(&g->cancel)) {
            const Res r = evalAction(*cand, *g->b, g->round, &g->cancel);
            if (r == WIN) {
                if (!g->found.exchange(true)) g->move = cand->dest;
                g->cancel.flag.store(true);
            } else if (r == ABORT) {
                g->aborted.store(true);
            }
        } else {
            g->aborted.store(true);
        }
        g->pending.fetch_sub(1, std::memory_order_release);
    }
};

struct AndGroup {
    Cancel cancel;
    std::atomic<int> pending{0};
    std::atomic<bool> lost{false}, aborted{false};
    uint32_t cfg = 0, cops = 0;
    int round = 0;
};
struct AndTask final : Task {
    AndGroup* g = nullptr;
    int robber = 0;
    void run() override {
        if (!cancelled(&g->cancel)) {
            Cloud single;
            single.set(robber);
            const Res r = solve(g->cfg, g->cops, single, g->round, &g->cancel, nullptr);
            if (r == LOSS) {
                g->lost.store(true);
                g->cancel.flag.store(true);
            } else if (r == ABORT) {
                g->aborted.store(true);
            }
        } else {
            g->aborted.store(true);
        }
        g->pending.fetch_sub(1, std::memory_order_release);
    }
};

Res evalAction(const Cand& a, const Cloud& b, int round, const Cancel* chain) {
    const Cloud dm = copMask(a.dest);
    const Cloud mid = Cloud::binaryAndNot(b, dm);
    const Cloud after = Cloud::binaryAndNot(packed.expand(mid, false), dm);
    if (after.none()) return WIN;
    if (round >= horizon) return LOSS;
    if (!reveal[round]) return solve(a.id, a.dest, after, round + 1, chain, nullptr);

    // Reveal: the next cop action may depend on the observed vertex; ALL singletons must win.
    uint8_t robbers[MAX_N];
    int n = 0;
    eachVertex(after, [&](int r) { robbers[n++] = uint8_t(r); });
    int i = 0;
    for (; i < n; ++i) {
        if (cancelled(chain)) return ABORT;
        if (i > 0 && shouldSplit(round, n - i)) break;
        Cloud single;
        single.set(robbers[i]);
        const Res r = solve(a.id, a.dest, single, round + 1, chain, nullptr);
        if (r != WIN) return r;
    }
    if (i == n) return WIN;

    AndGroup g;
    g.cancel.parent = chain;
    g.cfg = a.id;
    g.cops = a.dest;
    g.round = round + 1;
    const int m = n - i;
    std::vector<AndTask> tasks(m);
    std::vector<Task*> ptrs(m);
    for (int j = 0; j < m; ++j) {
        tasks[j].g = &g;
        tasks[j].robber = robbers[i + j];
        tasks[j].level = 2 * round + 1;
        tasks[j].group = &g;
        ptrs[j] = &tasks[j];
    }
    g.pending.store(m);
    ++tw->stats.splits;
    pushTasks(ptrs.data(), m);
    helpUntil(g.pending, 2 * round + 1, &g);
    if (g.lost.load()) return LOSS;
    if (g.aborted.load()) return ABORT;
    return WIN;
}

// Generate all distinct joint cop moves from `c` (sorted), K known at compile time.
template <int KK>
int generateMoves(const uint8_t* c, Cand* out) {
    const uint8_t* nb[MAX_K];
    int deg[MAX_K];
    for (int i = 0; i < KK; ++i) {
        nb[i] = neighbors[c[i]].data();
        deg[i] = int(neighbors[c[i]].size());
    }
    const int M = N + KK - 1;
    int n = 0;
    uint8_t d[MAX_K];
    auto emit = [&]() {
        uint8_t s[MAX_K];
        for (int i = 0; i < KK; ++i) s[i] = d[i];
        // Small sorting network
        auto cs = [&](int x, int y) { if (s[x] > s[y]) std::swap(s[x], s[y]); };
        if (KK == 2) cs(0, 1);
        if (KK == 3) { cs(0, 1); cs(1, 2); cs(0, 1); }
        if (KK == 4) { cs(0, 1); cs(2, 3); cs(0, 2); cs(1, 3); cs(1, 2); }
        uint64_t r = 0;
        int prev = -1;
        uint32_t p = 0;
        for (int i = 0; i < KK; ++i) {
            const int bb = int(s[i]) + i;
            r += choose[M - prev - 1][KK - i] - choose[M - bb][KK - i];
            prev = bb;
            p |= uint32_t(s[i]) << (8 * i);
        }
        out[n++] = Cand{uint32_t(r), p, 0};
    };
    // Cops sharing a vertex are interchangeable: enforce nondecreasing choice index among them.
    const bool s01 = KK > 1 && c[0] == c[1];
    const bool s12 = KK > 2 && c[1] == c[2];
    const bool s23 = KK > 3 && c[2] == c[3];
    for (int i0 = 0; i0 < deg[0]; ++i0) {
        d[0] = nb[0][i0];
        if (KK == 1) { emit(); continue; }
        for (int i1 = s01 ? i0 : 0; i1 < deg[1]; ++i1) {
            d[1] = nb[1][i1];
            if (KK == 2) { emit(); continue; }
            for (int i2 = s12 ? i1 : 0; i2 < deg[2]; ++i2) {
                d[2] = nb[2][i2];
                if (KK == 3) { emit(); continue; }
                for (int i3 = s23 ? i2 : 0; i3 < deg[3]; ++i3) {
                    d[3] = nb[3][i3];
                    emit();
                }
            }
        }
    }
    if (n > 1) {
        std::sort(out, out + n, [](const Cand& x, const Cand& y) { return x.id < y.id; });
        int w = 1;
        for (int i = 1; i < n; ++i)
            if (out[i].id != out[w - 1].id) out[w++] = out[i];
        n = w;
    }
    return n;
}

inline int maxMoves(const uint8_t* c) {
    int p = 1;
    for (int i = 0; i < K; ++i) p *= int(neighbors[c[i]].size());
    return p;
}

Res solve(uint32_t cfg, uint32_t cops, Cloud b, int round, const Cancel* chain, uint32_t* winMove) {
    b = Cloud::binaryAndNot(b, copMask(cops));
    if (b.none()) return WIN;
    if (round > horizon) return LOSS;
    countNode();
    if (cancelled(chain)) return ABORT;

    uint64_t h = 0;
    if (ttEnabled) {
        h = ttHash(cfg, b, round);
        uint8_t res;
        uint32_t mv;
        if (ttProbe(cfg, b, round, h, res, mv)) {
            ++tw->stats.ttHits;
            if (res == WIN && winMove) *winMove = mv;
            return Res(res);
        }
    }

    uint8_t c[MAX_K];
    unpackCops(cops, c);
    WorkerState& w = *tw;
    const Arena::Mark mk = w.arena.mark();
    Cand* cands = w.arena.alloc(size_t(maxMoves(c)));
    int n = 0;
    switch (K) {
        case 1: n = generateMoves<1>(c, cands); break;
        case 2: n = generateMoves<2>(c, cands); break;
        case 3: n = generateMoves<3>(c, cands); break;
        default: n = generateMoves<4>(c, cands); break;
    }
    w.stats.candidates += uint64_t(n);

    auto finish = [&](Res r, uint32_t mv) {
        w.arena.release(mk);
        if (r != ABORT && ttEnabled) ttStore(cfg, b, round, h, uint8_t(r), mv);
        if (r == WIN && winMove) *winMove = mv;
        return r;
    };

    // Immediate capture on the cop move.
    for (int i = 0; i < n; ++i) {
        const Cloud mid = Cloud::binaryAndNot(b, copMask(cands[i].dest));
        if (mid.none()) return finish(WIN, cands[i].dest);
    }

    if (mode != Mode::Off) {
        scoreCandidates(cands, n, b);
        w.stats.scored += uint64_t(n);
        if (mode == Mode::Bound) {
            // remaining plies from this cop turn = 2*(H-round+1); after the cop move, the
            // robber-turn continuation must fit in one ply less.
            const int limit = 2 * (horizon - round + 1) - 1;
            int keep = 0;
            for (int i = 0; i < n; ++i)
                if (int(cands[i].score) <= limit) cands[keep++] = cands[i];
            w.stats.pruned += uint64_t(n - keep);
            n = keep;
        }
        std::sort(cands, cands + n, [](const Cand& x, const Cand& y) {
            return x.score != y.score ? x.score < y.score : x.id < y.id;
        });
    }

    // OR over candidate cop moves; YBWC: first (most promising) child sequentially.
    int i = 0;
    for (; i < n; ++i) {
        if (cancelled(chain)) return finish(ABORT, 0);
        if (i > 0 && shouldSplit(round, n - i)) break;
        const Res r = evalAction(cands[i], b, round, chain);
        if (r == WIN) return finish(WIN, cands[i].dest);
        if (r == ABORT) return finish(ABORT, 0);
    }
    if (i == n) return finish(LOSS, 0);

    OrGroup g;
    g.cancel.parent = chain;
    g.b = &b;
    g.round = round;
    const int m = n - i;
    std::vector<OrTask> tasks(m);
    std::vector<Task*> ptrs(m);
    for (int j = 0; j < m; ++j) {
        tasks[j].g = &g;
        tasks[j].cand = &cands[i + j];
        tasks[j].level = 2 * round;
        tasks[j].group = &g;
        ptrs[j] = &tasks[j];
    }
    g.pending.store(m);
    ++w.stats.splits;
    pushTasks(ptrs.data(), m);
    helpUntil(g.pending, 2 * round, &g);
    if (g.found.load()) return finish(WIN, g.move);
    if (g.aborted.load()) return finish(ABORT, 0);
    return finish(LOSS, 0);
}

// ----------------------------------------------------------------------------
// Checkpoints / results files
// ----------------------------------------------------------------------------
std::mutex resultsMutex;

uint64_t fingerprint(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; }
    return h;
}

bool replaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExW(std::filesystem::path(from).wstring().c_str(),
                       std::filesystem::path(to).wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

bool writeAtomically(const std::string& path, const std::string& content) {
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << content;
        out.flush();
        if (!out) return false;
    }
    return replaceFile(tmp, path);
}

// Caller holds resultsMutex.
bool saveCheckpoint() {
    if (checkpointPath.empty()) return true;
    std::ostringstream out;
    out << "SY_CHECKPOINT_V2\n" << identity << ' ' << results.size() << '\n';
    for (const auto& r : results) out << r.value << ' ' << r.hasMove << ' ' << r.move << '\n';
    if (!writeAtomically(checkpointPath, out.str())) {
        std::cerr << "Warning: checkpoint write failed: " << checkpointPath << "\n";
        return false;
    }
    return true;
}

bool loadCheckpoint(std::string& err) {
    std::ifstream in(checkpointPath);
    if (!in) { err = "cannot open checkpoint " + checkpointPath; return false; }
    std::string magic;
    uint64_t id = 0, count = 0;
    in >> magic >> id >> count;
    if (magic != "SY_CHECKPOINT_V2" || id != identity || count != results.size()) {
        err = "checkpoint does not match these inputs";
        return false;
    }
    for (auto& r : results) {
        int v = -2, has = -1;
        uint64_t mv = 0;
        in >> v >> has >> mv;
        if (!in || v < -1 || v > 1 || has < 0 || has > 1 || mv > 0xFFFFFFFFull) {
            err = "corrupt checkpoint";
            return false;
        }
        r.value = v;
        r.hasMove = has;
        r.move = uint32_t(mv);
    }
    return true;
}

// ----------------------------------------------------------------------------
// Roots and worker threads
// ----------------------------------------------------------------------------
struct RootTask final : Task {
    size_t index = 0;
    void run() override {
        if (results[index].value == -1) {
            uint32_t mv = 0;
            const Res r = solve(startId, packCops(start), roots[index], 1, nullptr, &mv);
            if (r != ABORT) {
                std::lock_guard<std::mutex> lock(resultsMutex);
                results[index].value = r == WIN ? 1 : 0;
                // A first move exists only if the robber wasn't caught before any cop move.
                const Cloud left = Cloud::binaryAndNot(roots[index], copMask(packCops(start)));
                results[index].hasMove = r == WIN && !left.none();
                results[index].move = results[index].hasMove ? mv : 0;
                saveCheckpoint();
            }
        }
        outstandingRoots.fetch_sub(1);
    }
};

void workerMain(int /*id*/) {
    WorkerState state;
    tw = &state;
    for (;;) {
        Task* t = nullptr;
        {
            std::unique_lock<std::mutex> lock(poolMutex);
            if (pool.empty()) {
                idleWorkers.fetch_add(1);
                poolCv.wait(lock, [] { return shuttingDown || !pool.empty(); });
                idleWorkers.fetch_sub(1);
            }
            if (pool.empty() && shuttingDown) break;
            t = pool.back();
            pool.pop_back();
        }
        try {
            t->run();
        } catch (const std::exception& e) {
            std::cerr << "\nERROR in worker: " << e.what() << "\n";
            requestStop("worker error");
        }
    }
    std::lock_guard<std::mutex> lock(statsMutex);
    totals.add(state.stats);
}

}  // namespace sy

using namespace sy;

// ============================================================================
// 1. loadGraphFile
// ============================================================================
bool loadGraphFile(const char* filename_param, int k_param, int p_param) {
    graphPath = filename_param;
    K = k_param;
    graphStem = std::filesystem::path(filename_param).stem().string();

    if (K < 1 || K > MAX_K) {
        std::cerr << "Error: k_cops_scotlandyard supports 1..4 cops.\n";
        return true;
    }
    if (p_param != 1)
        std::cout << "Note: p=" << p_param
                  << " ignored; visibility follows the reveal schedule (SY_REVEALS).\n";

    Graph g(filename_param);
    if (g.nodeCount <= 0) {
        std::cerr << "Error: graph is empty or failed to load: " << filename_param << "\n";
        return true;
    }
    if (g.nodeCount > MAX_N) {
        std::cerr << "Error: at most " << MAX_N << " vertices supported (PackedAdjMatrix).\n";
        return true;
    }
    N = g.nodeCount;
    for (int u = 0; u < N; ++u)
        for (int v = u + 1; v < N; ++v)
            if (g.getEdge(u, v) != g.getEdge(v, u)) {
                std::cerr << "Error: graph must be undirected.\n";
                return true;
            }

    adj.constructFrom(&g);
    maxDegree = adj.maxDegree;
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());
    neighbors.assign(N, {});
    for (int u = 0; u < N; ++u) {
        const uint8_t* e = adj.getEdges(u);
        for (int i = 0; e[i] != 255; ++i) {
            neighbors[u].push_back(e[i]);
            packed.addEdge(u, e[i]);
        }
        if (neighbors[u].empty()) {
            std::cerr << "Error: vertex " << u << " is isolated; no-move rules are undefined.\n";
            return true;
        }
        for (int v = 0; v < N; ++v) canonicalGraph += g.getEdge(u, v) ? '1' : '0';
        canonicalGraph += '\n';
    }

    // ---- environment configuration ----
    auto fail = [](const std::string& m) { std::cerr << "Error: " << m << "\n"; return true; };
    uint64_t x;
    oraclePath = env("SY_ORACLE") ? env("SY_ORACLE")
                                  : (std::filesystem::path("cache") /
                                     ("fullvis_v5_strategy__" + graphStem + "__" +
                                      std::to_string(K) + "-cops.bin")).string();
    resultsPath = env("SY_RESULTS") ? env("SY_RESULTS")
                                    : (std::filesystem::path("cache") /
                                       ("k_cops_scotlandyard__" + graphStem + "__" +
                                        std::to_string(K) + "-cops__results.json")).string();
    if (env("SY_CHECKPOINT")) checkpointPath = env("SY_CHECKPOINT");
    resume = env("SY_RESUME") && std::string(env("SY_RESUME")) == "1";
    if (env("SY_ORACLE_MODE")) modeText = env("SY_ORACLE_MODE");
    if (modeText == "bound") mode = Mode::Bound;
    else if (modeText == "order") mode = Mode::Order;
    else if (modeText == "off") mode = Mode::Off;
    else return fail("SY_ORACLE_MODE must be bound, order or off");
    if (env("SY_START")) startText = env("SY_START");
    if (env("SY_ROUNDS")) {
        if (!parseUInt(env("SY_ROUNDS"), x) || x < 1 || x > 250) return fail("SY_ROUNDS must be 1..250");
        horizon = int(x);
    }
    if (env("SY_REVEALS") && !parseList(env("SY_REVEALS"), reveals))
        return fail("SY_REVEALS must be a comma list or 'none'");
    for (int r : reveals) {
        if (r < 1 || r > 256) return fail("reveal rounds must be 1..256");
        reveal[r] = true;
    }
    if (env("SY_ROBBERS")) robbersText = env("SY_ROBBERS");
    if (env("SY_HIDDEN_START")) hiddenText = env("SY_HIDDEN_START");
    if (env("SY_ROBBERS") && env("SY_HIDDEN_START")) return fail("set SY_ROBBERS or SY_HIDDEN_START, not both");
    threadCount = int(std::max(1u, std::thread::hardware_concurrency()));
    if (env("SY_THREADS")) {
        if (!parseUInt(env("SY_THREADS"), x) || x < 1 || x > 1024) return fail("SY_THREADS must be 1..1024");
        threadCount = int(x);
    }
    if (env("SY_TT_ENTRIES")) {
        if (!parseUInt(env("SY_TT_ENTRIES"), x)) return fail("bad SY_TT_ENTRIES");
        ttEntriesRequested = x;
        ttEntriesSet = true;
    } else if (env("SY_TT_MB")) {
        if (!parseUInt(env("SY_TT_MB"), x)) return fail("bad SY_TT_MB");
        ttMiB = x;
    }
    if (env("SY_SPLIT_ROUNDS")) {
        if (!parseUInt(env("SY_SPLIT_ROUNDS"), x) || x > 250) return fail("bad SY_SPLIT_ROUNDS");
        splitRounds = int(x);
    }
    forceSplit = env("SY_FORCE_SPLIT") && std::string(env("SY_FORCE_SPLIT")) == "1";
    if (env("SY_MAX_SECONDS")) {
        if (!parseUInt(env("SY_MAX_SECONDS"), x)) return fail("bad SY_MAX_SECONDS");
        maxSeconds = double(x);
    }
    if (env("SY_MAX_NODES")) {
        if (!parseUInt(env("SY_MAX_NODES"), x)) return fail("bad SY_MAX_NODES");
        maxNodes = x;
    }
    warmOracle = !(env("SY_WARM") && std::string(env("SY_WARM")) == "0");
    if (resume && checkpointPath.empty()) return fail("SY_RESUME=1 requires SY_CHECKPOINT");

#ifdef SY_X86
    haveAvx2 = __builtin_cpu_supports("avx2");
#endif

    std::cout << "Scotland Yard oracle-guided solver\n"
              << "  graph          : " << graphStem << " (" << N << " vertices, max degree "
              << maxDegree << ")\n"
              << "  cops           : " << K << "\n"
              << "  rounds         : " << horizon << "\n"
              << "  reveals after  : ";
    if (reveals.empty()) std::cout << "none";
    for (size_t i = 0; i < reveals.size(); ++i) std::cout << (i ? "," : "") << reveals[i];
    std::cout << "\n  oracle mode    : " << modeText << "\n"
              << "  oracle         : " << (mode == Mode::Off ? "(not used)" : oraclePath) << "\n"
              << "  threads        : " << threadCount << " (split rounds <= " << splitRounds
              << (forceSplit ? ", forced" : "") << ")\n"
              << "  scoring        : " << (haveAvx2 ? "AVX2" : "scalar") << "\n"
              << "  budget         : "
              << (maxSeconds > 0 ? std::to_string(uint64_t(maxSeconds)) + " s" : "unlimited time")
              << (maxNodes ? ", " + std::to_string(maxNodes) + " nodes" : "") << "\n";
    return false;
}

// ============================================================================
// 2. buildAuxGraph  (no explicit aux graph: oracle, roots, transposition table)
// ============================================================================
bool buildAuxGraph() {
    for (int a = 0; a < MAX_N + MAX_K + 2; ++a) {
        choose[a][0] = 1;
        for (int b = 1; b <= MAX_K; ++b)
            choose[a][b] = b > a ? 0 : (b == a ? 1 : choose[a - 1][b - 1] + choose[a - 1][b]);
    }
    configCount = choose[N + K - 1][K];
    std::cout << "  cop configs    : " << configCount << "\n";

    if (mode != Mode::Off) {
        std::string err;
        if (!loadOracle(err)) {
            std::cerr << "Error: " << err << "\n"
                      << "  (generate it with k_cops_fullvis_v5 and COPS_SAVE_FULLVIS_V5=1, set "
                         "SY_ORACLE, or use SY_ORACLE_MODE=off with SY_START)\n";
            return true;
        }
        mem.trackExternal("Oracle (memory-mapped, read-only)", oracleMap.size);
        std::cout << "  oracle header  : OK (" << oracleMap.size << " bytes, best start ply "
                  << oracleBestPly << ")\n";
        if (warmOracle) warmOracleCache();
    }

    // ---- cop start ----
    if (startText.empty()) {
        if (mode == Mode::Off || oracleBest == UINT64_MAX) {
            std::cerr << "Error: set SY_START (no oracle best start available).\n";
            return true;
        }
        if (!unrankCops(oracleBest, start)) {
            std::cerr << "Error: oracle best start id out of range.\n";
            return true;
        }
    } else {
        std::vector<int> v;
        if (!parseList(startText, v) || int(v.size()) != K) {
            std::cerr << "Error: SY_START must list exactly " << K << " vertices.\n";
            return true;
        }
        for (int i = 0; i < K; ++i) {
            if (v[i] >= N) { std::cerr << "Error: SY_START vertex out of range.\n"; return true; }
            start[i] = uint8_t(v[i]);
        }
        for (int i = 1; i < K; ++i)
            for (int j = i; j > 0 && start[j] < start[j - 1]; --j) std::swap(start[j], start[j - 1]);
    }
    startId = rankCops(start);
    std::cout << "  cop start      : " << copsText(packCops(start)) << "\n";

    // ---- roots ----
    auto vertexList = [&](const std::string& s, std::vector<int>& out) {
        out.clear();
        if (s == "all") {
            for (int r = 0; r < N; ++r) out.push_back(r);
            return true;
        }
        if (!parseList(s, out) || out.empty()) return false;
        for (int r : out) if (r >= N) return false;
        return true;
    };
    std::vector<int> verts;
    if (hiddenText.empty()) {
        if (!vertexList(robbersText, verts)) {
            std::cerr << "Error: SY_ROBBERS must be 'all' or a list of vertices < " << N << ".\n";
            return true;
        }
        for (int r : verts) {
            Cloud b;
            b.set(r);
            roots.push_back(b);
        }
    } else {
        if (!vertexList(hiddenText, verts)) {
            std::cerr << "Error: SY_HIDDEN_START must be 'all' or a list of vertices < " << N << ".\n";
            return true;
        }
        Cloud b;
        for (int r : verts) b.set(r);
        roots.push_back(b);
    }
    results.assign(roots.size(), RootResult{});
    std::cout << "  roots          : " << roots.size()
              << (hiddenText.empty() ? " visible robber starts" : " hidden cloud") << "\n";

    // ---- identity for checkpoints ----
    {
        std::ostringstream key;
        key << "SY2\n" << canonicalGraph << K << ' ' << startId << ' ' << horizon << ' ' << modeText << '\n';
        for (int r : reveals) key << r << ',';
        for (const auto& b : roots) {
            key << '\n';
            for (int w = 0; w < 4; ++w) key << b.words[w] << ',';
        }
        if (mode != Mode::Off) {
            key << oracleMap.size;
            for (int i = 0; i < 64; ++i) key << ',' << int(oracleMap.data[i]);
        }
        identity = fingerprint(key.str());
    }

    // ---- transposition table ----
    uint64_t entries = ttEntriesSet ? ttEntriesRequested : (ttMiB << 20) / sizeof(TTEntry);
    if (entries >= 2) {
        uint64_t buckets = 1;
        while (buckets * 2 * 2 <= entries) buckets *= 2;
        mem.requestAlloc("Transposition Table", size_t(buckets * 2), &tt);
        mem.allocate();
        if (!tt) {
            std::cerr << "Error: cannot allocate transposition table.\n";
            return true;
        }
        ttMask = buckets - 1;
        ttEnabled = true;
        std::cout << "  trans. table   : " << buckets * 2 << " entries ("
                  << std::fixed << std::setprecision(1)
                  << double(buckets * 2 * sizeof(TTEntry)) / (1 << 20) << " MiB)\n";
    } else {
        std::cout << "  trans. table   : disabled\n";
    }
    return false;
}

// ============================================================================
// 3. initializeCaptures  (round 0: robber starts on a cop; checkpoint resume)
// ============================================================================
bool initializeCaptures() {
    if (!checkpointPath.empty()) {
        if (resume) {
            std::string err;
            if (!loadCheckpoint(err)) {
                std::cerr << "Error: " << err << "\n";
                return true;
            }
        } else if (std::filesystem::exists(checkpointPath)) {
            std::cerr << "Error: checkpoint exists; set SY_RESUME=1 or choose another file.\n";
            return true;
        }
    }
    const Cloud copsNow = copMask(packCops(start));
    int immediate = 0, resumed = 0;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (results[i].value != -1) { ++resumed; continue; }
        if (Cloud::binaryAndNot(roots[i], copsNow).none()) {
            results[i] = RootResult{1, false, 0};
            ++immediate;
        }
    }
    std::cout << "  immediate captures: " << immediate;
    if (resume) std::cout << ", resumed roots: " << resumed;
    std::cout << "\n";
    return false;
}

// ============================================================================
// 4. mainLoop
// ============================================================================
bool mainLoop() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    runStart = std::chrono::steady_clock::now();

    std::vector<RootTask> rootTasks(roots.size());
    std::vector<Task*> ptrs;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (results[i].value != -1) continue;
        rootTasks[i].index = i;
        rootTasks[i].level = 0;
        rootTasks[i].group = nullptr;
        ptrs.push_back(&rootTasks[i]);
    }
    outstandingRoots.store(int(ptrs.size()));
    std::cout << "Searching " << ptrs.size() << " root(s) with " << threadCount << " thread(s)...\n";

    std::vector<std::thread> workers;
    for (int t = 0; t < threadCount; ++t) workers.emplace_back(workerMain, t);
    if (!ptrs.empty()) pushTasks(ptrs.data(), int(ptrs.size()));

    auto last = std::chrono::steady_clock::now();
    while (outstandingRoots.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto now = std::chrono::steady_clock::now();
        if (interrupted) requestStop("interrupted");
        if (maxSeconds > 0 && std::chrono::duration<double>(now - runStart).count() >= maxSeconds)
            requestStop("time budget");
        if (now - last >= std::chrono::seconds(5)) {
            int done = 0, wins = 0, losses = 0;
            {
                std::lock_guard<std::mutex> lock(resultsMutex);
                for (const auto& r : results) {
                    done += r.value != -1;
                    wins += r.value == 1;
                    losses += r.value == 0;
                }
            }
            const double secs = std::chrono::duration<double>(now - runStart).count();
            std::cout << "\r  [" << std::fixed << std::setprecision(0) << secs << " s] roots "
                      << done << "/" << results.size() << " (W " << wins << ", L " << losses
                      << ")  nodes ~" << gNodes.load() << "  idle " << idleWorkers.load()
                      << "      " << std::flush;
            last = now;
        }
    }
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        shuttingDown = true;
    }
    poolCv.notify_all();
    for (auto& t : workers) t.join();
    std::cout << "\n";
    return false;
}

// ============================================================================
// 5. findFinalResult
// ============================================================================
const char* verdict = "UNKNOWN";
double solveSeconds = 0;

bool findFinalResult() {
    solveSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
    bool all = true, loss = false;
    for (const auto& r : results) {
        all &= r.value == 1;
        loss |= r.value == 0;
    }
    verdict = loss ? "LOSS" : all ? "WIN" : "UNKNOWN";

    std::cout << "\n--- FINAL VERDICT ---\n";
    if (results.size() <= 256) {
        for (size_t i = 0; i < roots.size(); ++i) {
            std::cout << "  robber ";
            if (hiddenText.empty()) {
                eachVertex(roots[i], [](int r) { std::cout << std::setw(3) << r; });
            } else {
                std::cout << "cloud(" << popcount(roots[i]) << ")";
            }
            const auto& r = results[i];
            std::cout << " : " << (r.value < 0 ? "UNKNOWN" : r.value ? "WIN" : "LOSS");
            if (r.hasMove) std::cout << "  first cop move " << copsText(r.move);
            std::cout << "\n";
        }
    }
    std::cout << "RESULT: " << verdict << ". Cop start " << copsText(packCops(start)) << ", "
              << horizon << " rounds";
    if (std::string(verdict) == "WIN")
        std::cout << ": the cops capture every requested robber start.\n";
    else if (std::string(verdict) == "LOSS")
        std::cout << ": at least one robber start provably escapes this deadline from this start.\n";
    else
        std::cout << ": search stopped early (" << (stopReason.empty() ? "budget" : stopReason)
                  << "); no loss proved.\n";

    const double nps = solveSeconds > 0 ? double(totals.nodes) / solveSeconds : 0;
    std::cout << "Search stats: " << totals.nodes << " nodes, " << totals.candidates
              << " candidate moves, " << totals.scored << " oracle rows, " << totals.pruned
              << " pruned, " << totals.ttHits << " TT hits, " << totals.splits << " splits, "
              << std::fixed << std::setprecision(0) << nps << " nodes/s, " << std::setprecision(3)
              << solveSeconds << " s\n";
    mem.print();
    return false;
}

// ============================================================================
// 6. outputData  (results JSON + final checkpoint)
// ============================================================================
bool outputData() {
    std::ostringstream js;
    js << "{\"status\":\"" << verdict << "\",\"graph\":\"" << graphStem << "\",\"cops\":" << K
       << ",\"rounds\":" << horizon << ",\"reveals\":[";
    for (size_t i = 0; i < reveals.size(); ++i) js << (i ? "," : "") << reveals[i];
    js << "],\"oracle_mode\":\"" << modeText << "\",\"start\":" << copsText(packCops(start))
       << ",\"roots\":[";
    for (size_t i = 0; i < roots.size(); ++i) {
        js << (i ? "," : "") << "{\"robbers\":[";
        bool first = true;
        eachVertex(roots[i], [&](int r) { js << (first ? "" : ",") << r; first = false; });
        const auto& r = results[i];
        js << "],\"status\":\"" << (r.value < 0 ? "UNKNOWN" : r.value ? "WIN" : "LOSS")
           << "\",\"first_cop_move\":" << (r.hasMove ? copsText(r.move) : "null") << "}";
    }
    js << "],\"nodes\":" << totals.nodes << ",\"oracle_rows\":" << totals.scored
       << ",\"pruned\":" << totals.pruned << ",\"tt_hits\":" << totals.ttHits
       << ",\"splits\":" << totals.splits << ",\"seconds\":" << solveSeconds << "}\n";
    if (!writeAtomically(resultsPath, js.str())) {
        std::cerr << "Error: cannot write results " << resultsPath << "\n";
        return true;
    }
    std::cout << "Results written to " << resultsPath << "\n";
    std::lock_guard<std::mutex> lock(resultsMutex);
    saveCheckpoint();
    return false;
}
