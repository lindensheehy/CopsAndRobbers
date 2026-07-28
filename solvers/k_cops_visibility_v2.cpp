/**
 * ============================================================================
 * FILE --- k_cops_visibility_v2.cpp
 * ============================================================================
 *
 * On-Demand Trajectory Pruning (Macro-Turn Architecture).
 * See docs/k_cops_visibility_v2.md for the full spec.
 *
 * Unlike k_cops_visibility.cpp (which propagates backward through 2p
 * intermediate DP columns), this solver collapses the state space to a
 * single evaluation plane (C, r) per visibility cycle. A win at (C0, r) is
 * decided by a bounded depth-first search over the cops' p-step
 * trajectories, expanding and pruning the robber's reachability cloud
 * against each intermediate cop configuration on demand (spec section 3,
 * Phases A-E). This fixes "intermediate ghosting": robber trajectories that
 * physically collide with a cop before the cycle boundary were not being
 * pruned by the column-based propagation.
 *
 * Implementation notes (where this deliberately sharpens the spec):
 *
 *  - CAPTURE ON THE COP'S PLY. Spec Phase B only prunes the cloud after the
 *    robber's move (V_d = expand(V_{d-1}) \ C_d). But in the real game the
 *    cop move happens first, so a cop that steps onto a vertex in V_{d-1}
 *    captures that trajectory before the robber can move off it. We prune in
 *    two stages: V_mid = V_{d-1} \ C_d (capture at ply 2d-1), then
 *    V_d = expand(V_mid) \ C_d (capture at ply 2d -- the robber is forced to
 *    step onto a cop). This is required to reproduce v1's odd capture times
 *    at p=1, where the two solvers describe the identical game.
 *
 *  - SNAPSHOT PASSES. The outer fixed-point loop (spec section 4) evaluates
 *    every unmarked state against the DP table as it stood at the START of
 *    the pass; new marks are buffered and applied at the end of the pass.
 *    In-place marking (which spec 4.2 tolerates) is NOT sound for exact ply
 *    costs under freezing: a state swept early can consume an expensive
 *    dependency marked moments before, freeze, and never see the cheap
 *    dependency swept after it. With snapshot passes, a simple induction on
 *    pass number shows every recorded cost is exact: pass i can only record
 *    costs <= 2p*i, and every state of true value <= 2p*i has its optimal
 *    dependencies (value <= 2p*(i-1)) already frozen exactly, so it is
 *    marked at its true value and never earlier at a worse one.
 *
 *  - SOUND SKIP FILTERS. Two cheap, provably-lossless filters avoid running
 *    the exponential DFS on states that cannot possibly be marked this pass:
 *      (a) escape-set filter: vertices the robber can walk to while staying
 *          strictly out of range of every cop (a cop starting at c can be at
 *          u on step d only if dist(c,u) <= d) survive in V_p under EVERY
 *          cop trajectory. If any such vertex has no marked non-capture
 *          state anywhere in the table, no boundary resolution can succeed,
 *          and no mid-cycle capture is possible either (the escape walk
 *          survives) -- skip.
 *      (b) unchanged-region skip: a failed evaluation depends only on the
 *          marked sets of configs reachable in p cop-steps, all of which lie
 *          within graph distance p of C0's cops. If no config in that
 *          relaxed neighborhood has gained marks since (C0, r) last failed,
 *          the DFS would reproduce the same failure -- skip.
 */

#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include "CacheManager.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <climits>
#include <cstdint>

// --- DP STATE DEFINITION ---
// One state per (cop config, last-seen robber position) -- no column
// dimension. The p-step macro-turn is resolved on demand by the bounded
// DFS in mainLoop rather than by propagating through intermediate columns.
struct DataItem {
    uint8_t marked : 1;
    uint8_t markedRound : 7;
};

// The maximum round count representable by the 7-bit bitfield
constexpr uint8_t MAX_ROUND_COUNT = 0b1111111;

// APSP sentinel for "no path" (also safely larger than any real distance,
// since AdjacencyList caps the graph at 255 vertices)
constexpr int DIST_UNREACHABLE = 255;

const char* filename = nullptr;
int k = 0;          // Number of cops
int p = 0;          // Visibility fraction (e.g., p=2 means 1/2 visibility)

Allocator mem;
AdjacencyList adj;
AuxGraph<DataItem> aux;


// ---------------------------------------------------------------------------
// 256-bit vertex set. The graph format caps N at 255 (uint8_t vertices with
// 255 as the edge-list terminator), so one fixed-size mask covers any graph.
// Clouds, per-config cop occupancy and per-config marked sets all use this,
// which turns Phase B pruning and the Phase D subset test into a handful of
// word ops and satisfies the no-heap-allocation invariant (spec section 6).
// ---------------------------------------------------------------------------
struct VertexMask {
    uint64_t w[4];

    inline void clear() { w[0] = 0; w[1] = 0; w[2] = 0; w[3] = 0; }
    inline void set(int v) { w[v >> 6] |= (uint64_t)1 << (v & 63); }
    inline bool test(int v) const { return (w[v >> 6] >> (v & 63)) & 1; }
    inline bool empty() const { return (w[0] | w[1] | w[2] | w[3]) == 0; }
    inline void orWith(const VertexMask& o)  { w[0] |= o.w[0]; w[1] |= o.w[1]; w[2] |= o.w[2]; w[3] |= o.w[3]; }
    inline void andWith(const VertexMask& o) { w[0] &= o.w[0]; w[1] &= o.w[1]; w[2] &= o.w[2]; w[3] &= o.w[3]; }
    inline void andNot(const VertexMask& o)  { w[0] &= ~o.w[0]; w[1] &= ~o.w[1]; w[2] &= ~o.w[2]; w[3] &= ~o.w[3]; }
    inline bool isSubsetOf(const VertexMask& o) const {
        return ((w[0] & ~o.w[0]) | (w[1] & ~o.w[1]) | (w[2] & ~o.w[2]) | (w[3] & ~o.w[3])) == 0;
    }
};

template <typename Fn>
static inline void forEachVertex(const VertexMask& m, Fn fn) {
    for (int wi = 0; wi < 4; ++wi) {
        uint64_t bits = m.w[wi];
        while (bits) {
            fn((wi << 6) + __builtin_ctzll(bits));
            bits &= bits - 1;
        }
    }
}

// --- SOLVER WORKING DATA (built once at the top of mainLoop) ---

static std::vector<uint8_t> apsp;           // N*N all-pairs shortest paths (spec 5.3)
static std::vector<VertexMask> nbrMask;     // per vertex: its open neighborhood
static std::vector<VertexMask> copMask;     // per config: vertices holding a cop
static std::vector<VertexMask> markedMask;  // per config: marked robber positions (start-of-pass snapshot)
static VertexMask freeMarked;               // vertices with a marked NON-capture state in some config

// Bounded-DFS scratch, preallocated per spec section 6 (no heap allocation
// inside the trajectory search). All indexed by depth d.
struct Candidate {
    int score;
    size_t base;    // transition value == nextConfigId * N; states[base + r] is (nextConfig, r)
};
static std::vector<VertexMask> cloud;                 // [p+1] robber reachability clouds
static std::vector<std::vector<Candidate>> cands;     // [p+1] Phase A candidate buffers
static std::vector<std::vector<int>> cloudDist;       // [p+1] per-vertex distance to cloud, for 5.3 scoring
static int bestCost;                                  // 5.2 branch-and-bound bound (one (C0,r) evaluation)


bool loadGraphFile(const char* filename_param, int k_param, int p_param) {

    filename = filename_param;
    k = k_param;
    p = p_param;

    Graph g(filename);

    if (g.nodeCount == 0) {
        std::cerr << "Error: Graph is empty or failed to load.\n";
        return 1;
    }

    adj.constructFrom(&g);
    mem.trackExternal("Graph Adj List", adj.getMemoryFootprint());

    return 0;

}

bool buildAuxGraph() {
    std::string algoName = "k_cops_v2_" + std::to_string(p) + "vis";

    // Set project configuration here
    SelfEdgeCop c_edge = SelfEdgeCop::FALSE;
    SelfEdgeRobber r_edge = SelfEdgeRobber::FALSE;

    bool failed = CacheManager::loadAuxGraph<DataItem>(algoName, filename, k, p, c_edge, r_edge, &aux, &mem, &adj);

    if (!failed) {
        std::cout << "Loaded AuxGraph from cache!\n";
        return 0;
    }

    std::cout << "Cache miss. Creating new AuxGraph (single-plane state space)...\n";

    aux.setSelfEdges(c_edge, r_edge);

    // Only one column: the state space is strictly (C, r), per spec 2.1.
    // The p-step trajectory is resolved on demand inside mainLoop's bounded
    // DFS, not by propagating through 2p intermediate DP columns.
    aux.constructFrom(k, /*columns=*/1, &adj, &mem);

    if (aux.configCount == 0) {
        std::cerr << "Error: Unable to generate aux graph.\n";
        return 1;
    }
    return 0;
}

bool initializeCaptures() {

    uint64_t initialWins = 0;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < adj.nodeCount; ++r) {

            DataItem* state = aux.getState(cId, r, 0);
            state->marked = false;
            state->markedRound = MAX_ROUND_COUNT;

            if (aux.isInstantCatch(cId, r)) {
                state->marked = true;
                state->markedRound = 0;
                initialWins++;
            }

        }
    }

    std::cout << "Initialized " << initialWins << " base captures.\n";

    return 0;

}

// ---------------------------------------------------------------------------
// One-hop cloud expansion (robber must move; SelfEdgeRobber::FALSE)
// ---------------------------------------------------------------------------
static inline VertexMask expandFrom(const VertexMask& src) {
    VertexMask out;
    out.clear();
    forEachVertex(src, [&](int v) { out.orWith(nbrMask[v]); });
    return out;
}

// ---------------------------------------------------------------------------
// The bounded trajectory search, spec section 3 + 5. Evaluates cop step d
// (1-based) given the previous config and cloud[d-1]. Every winning
// trajectory found lowers the evaluation-local bound bestCost; the search
// exhausts (modulo the sound 5.2 depth cutoff) so the final bound is the
// exact minimum worst-case ply cost.
// ---------------------------------------------------------------------------
static void trajectoryDFS(int d, size_t cIdPrev) {

    // 5.2 depth cutoff: this branch has banked 2(d-1) plys unconditionally;
    // the cheapest outcome still available below is a capture on this step's
    // cop ply, 2d-1 total. (One deeper than the spec's 2d, because of the
    // capture-on-cop-ply extension -- see file header.)
    if (2 * d - 1 >= bestCost) return;

    const int N = adj.nodeCount;
    const VertexMask& prev = cloud[d - 1];

    size_t tStart, tEnd;
    aux.getCopTransitions(cIdPrev, tStart, tEnd);

    // 5.3 move ordering. Only worth doing on interior depths: at d == p the
    // candidates are leaves that all get evaluated anyway (the bound cannot
    // cut siblings at the same depth except through the cheap entry guard),
    // so scoring and sorting there would be pure overhead.
    const bool leafDepth = (d == p);

    std::vector<Candidate>& cs = cands[d];
    cs.clear();

    if (leafDepth) {
        for (size_t i = tStart; i < tEnd; ++i) cs.push_back({0, aux.transitions[i]});
    } else {
        // Distance-to-cloud transform: dc[u] = min over v in cloud of dist(u,v)
        std::vector<int>& dc = cloudDist[d];
        for (int u = 0; u < N; ++u) dc[u] = DIST_UNREACHABLE;
        forEachVertex(prev, [&](int v) {
            const uint8_t* row = &apsp[(size_t)v * N];
            for (int u = 0; u < N; ++u) {
                if (row[u] < dc[u]) dc[u] = row[u];
            }
        });

        for (size_t i = tStart; i < tEnd; ++i) {
            size_t base = aux.transitions[i];
            const uint8_t* cops = &aux.configs[(base / N) * k];
            int score = 0;
            for (int c = 0; c < k; ++c) score += dc[cops[c]];
            cs.push_back({score, base});
        }
        std::sort(cs.begin(), cs.end(), [](const Candidate& a, const Candidate& b) {
            return (a.score != b.score) ? (a.score < b.score) : (a.base < b.base);
        });
    }

    for (const Candidate& cand : cs) {

        if (2 * d - 1 >= bestCost) return;   // bound may have tightened mid-loop

        size_t cIdNext = cand.base / N;
        const VertexMask& occupied = copMask[cIdNext];

        // Phase B, cop ply: a cop stepping onto a live robber vertex captures
        // that trajectory NOW, before the robber moves (see file header).
        VertexMask mid = prev;
        mid.andNot(occupied);
        if (mid.empty()) {
            // Every surviving trajectory was intercepted on this cop move.
            // 2d-1 is the floor for this depth, so nothing here or deeper can
            // beat it -- record and abandon the whole level.
            bestCost = 2 * d - 1;
            return;
        }

        // Phase B, robber ply: expand one hop, then remove vertices occupied
        // by C_d (NOT C_{d-1} -- simultaneous-collision rule, spec section 6).
        VertexMask next = expandFrom(mid);
        next.andNot(occupied);

        // Phase C: every robber left alive was forced onto a cop this step.
        if (next.empty()) {
            if (2 * d < bestCost) bestCost = 2 * d;
            continue;   // a sibling could still achieve 2d-1
        }

        if (leafDepth) {
            // Phase D boundary resolution: the cycle ends with the robber
            // observed somewhere in V_p; the branch wins iff every possible
            // observation maps to an already-solved state in the snapshot.
            if (!next.isSubsetOf(markedMask[cIdNext])) continue;

            int maxRound = 0;
            forEachVertex(next, [&](int v) {
                int round = aux.states[cand.base + v].markedRound;   // columns == 1
                if (round > maxRound) maxRound = round;
            });

            int cost = 2 * p + maxRound;
            if (cost < bestCost) bestCost = cost;
        } else {
            // Phase E: induction to step d+1
            cloud[d] = next;
            trajectoryDFS(d + 1, cIdNext);
        }
    }
}

bool mainLoop() {

    std::cout << "Starting Macro-Turn Trajectory Search for " << p << "-Visibility...\n";

    const int N = adj.nodeCount;
    if (N > 255) {
        std::cerr << "Error: graph exceeds the 255-vertex format limit.\n";
        return 1;
    }

    // =====================================================================
    // One-time precomputation
    // =====================================================================

    // APSP via one BFS per vertex (spec 5.3: distances must be table lookups
    // during the DFS, never searches).
    apsp.assign((size_t)N * N, (uint8_t)DIST_UNREACHABLE);
    {
        std::vector<int> queue(N);
        for (int src = 0; src < N; ++src) {
            uint8_t* row = &apsp[(size_t)src * N];
            row[src] = 0;
            int head = 0, tail = 0;
            queue[tail++] = src;
            while (head < tail) {
                int u = queue[head++];
                const uint8_t* edges = adj.getEdges(u);
                for (int e = 0; edges[e] != 255; ++e) {
                    int v = edges[e];
                    if (row[v] != DIST_UNREACHABLE) continue;
                    row[v] = row[u] + 1;
                    queue[tail++] = v;
                }
            }
        }
    }

    nbrMask.assign(N, VertexMask{});
    for (int v = 0; v < N; ++v) {
        const uint8_t* edges = adj.getEdges(v);
        for (int e = 0; edges[e] != 255; ++e) nbrMask[v].set(edges[e]);
    }

    copMask.assign(aux.configCount, VertexMask{});
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int c = 0; c < k; ++c) copMask[cId].set(aux.configs[cId * k + c]);
    }

    // Start-of-pass marked snapshot, plus the global escape-filter mask.
    // freeMarked only counts non-capture marks: boundary clouds never contain
    // a cop-occupied vertex, so capture seeds can never serve as a Phase D
    // dependency and must not satisfy the filter.
    markedMask.assign(aux.configCount, VertexMask{});
    freeMarked.clear();
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        for (int r = 0; r < N; ++r) {
            if (!aux.states[cId * N + r].marked) continue;
            markedMask[cId].set(r);
            if (!copMask[cId].test(r)) freeMarked.set(r);
        }
    }

    // DFS scratch (spec section 6: preallocated, reused, never resized inside
    // the search)
    size_t maxTrans = 0;
    for (size_t cId = 0; cId < aux.configCount; ++cId) {
        maxTrans = std::max(maxTrans, aux.transitionHeads[cId + 1] - aux.transitionHeads[cId]);
    }
    cloud.assign(p + 1, VertexMask{});
    cands.assign(p + 1, {});
    for (auto& c : cands) c.reserve(maxTrans);
    cloudDist.assign(p + 1, std::vector<int>(N));

    mem.trackExternal("APSP Table", apsp.size());
    mem.trackExternal("Vertex/Config Masks", (nbrMask.size() + copMask.size() + markedMask.size()) * sizeof(VertexMask));

    // =====================================================================
    // Outer fixed-point induction (spec section 4), snapshot flavor
    // =====================================================================

    std::vector<std::pair<size_t, uint8_t>> pending;    // (stateIdx, plyCost) buffered marks
    std::vector<int> minCopDist(N);                     // per C0: distance to nearest cop
    std::vector<VertexMask> allowed(p + 1);             // escape filter: allowed[t] = {u : minCopDist[u] > t}

    // Unchanged-region skip bookkeeping: which configs gained marks in each
    // completed pass, and the last pass each config's states were evaluated
    // against. A config whose relaxed p-neighborhood saw no changes since its
    // last (failed) evaluation would just fail identically again.
    std::vector<std::vector<uint32_t>> changedByPass;   // [pass] -> configs that gained marks
    std::vector<int> lastEvalPass(aux.configCount, -1);
    constexpr size_t RELEVANCE_SCAN_CAP = 4096;         // beyond this, just evaluate

    int passes = 0;

    while (true) {
        passes++;
        pending.clear();

        for (size_t cId = 0; cId < aux.configCount; ++cId) {

            const uint8_t* cops = &aux.configs[cId * k];
            bool prepared = false;
            bool skipUnchanged = false;

            // Unchanged-region skip: collect changes since this config was
            // last evaluated; if none of them are within distance p of C0's
            // cops, every unmarked (C0, r) reproduces its previous failure.
            if (lastEvalPass[cId] >= 0) {
                size_t toScan = 0;
                for (int pass = lastEvalPass[cId]; pass < (int)changedByPass.size(); ++pass) {
                    toScan += changedByPass[pass].size();
                }
                if (toScan <= RELEVANCE_SCAN_CAP) {
                    bool relevant = false;
                    for (int pass = lastEvalPass[cId]; pass < (int)changedByPass.size() && !relevant; ++pass) {
                        for (uint32_t changedCId : changedByPass[pass]) {
                            // Any config reachable in p cop-steps has all its
                            // cops within distance p of some cop of C0.
                            bool inRange = true;
                            const uint8_t* changedCops = &aux.configs[(size_t)changedCId * k];
                            for (int j = 0; j < k && inRange; ++j) {
                                int best = DIST_UNREACHABLE;
                                for (int i = 0; i < k; ++i) {
                                    int dist = apsp[(size_t)cops[i] * N + changedCops[j]];
                                    if (dist < best) best = dist;
                                }
                                inRange = (best <= p);
                            }
                            if (inRange) { relevant = true; break; }
                        }
                    }
                    skipUnchanged = !relevant;
                }
            }
            if (skipUnchanged) continue;
            lastEvalPass[cId] = (int)changedByPass.size();

            for (int r = 0; r < N; ++r) {

                if (aux.states[cId * N + r].marked) continue;   // 4.3 freezing (snapshot: pending not yet applied)

                if (!prepared) {
                    // Shared across all r of this config: nearest-cop
                    // distances and the escape filter's step masks.
                    for (int u = 0; u < N; ++u) {
                        int best = DIST_UNREACHABLE;
                        for (int c = 0; c < k; ++c) {
                            int dist = apsp[(size_t)cops[c] * N + u];
                            if (dist < best) best = dist;
                        }
                        minCopDist[u] = best;
                    }
                    for (int t = 1; t <= p; ++t) {
                        allowed[t].clear();
                        for (int u = 0; u < N; ++u) {
                            if (minCopDist[u] > t) allowed[t].set(u);
                        }
                    }
                    prepared = true;
                }

                // Escape-set filter. Walk vertices v_0=r, v_1, ..., v_p where
                // v_j stays strictly out of every cop's step-range for both
                // plies that could prune it (cop ply d prunes v_{d-1}, robber
                // ply d prunes v_d; a cop is within distance d of its start
                // on step d). Such walks survive under EVERY cop trajectory,
                // so E = endpoints is a guaranteed subset of V_p: no
                // mid-cycle capture exists, and Phase D needs every vertex of
                // E marked -- if one has no usable mark anywhere, skip.
                VertexMask esc;
                esc.clear();
                if (allowed[1].test(r)) esc.set(r);
                for (int j = 1; j <= p && !esc.empty(); ++j) {
                    esc = expandFrom(esc);
                    esc.andWith(allowed[std::min(j + 1, p)]);
                }
                if (!esc.empty()) {
                    VertexMask unresolved = esc;
                    unresolved.andNot(freeMarked);
                    if (!unresolved.empty()) continue;
                }

                // Full macro-turn evaluation (spec sections 3 + 5).
                // bestCost is strictly local to this (C0, r) and pass (5.4).
                bestCost = INT_MAX;
                cloud[0].clear();
                cloud[0].set(r);
                trajectoryDFS(1, cId);

                // Overflow guard (same rationale as v1): a capture needing
                // MAX_ROUND_COUNT or more plys doesn't fit the 7-bit field;
                // treat it as unprovable rather than wrapping.
                if (bestCost < MAX_ROUND_COUNT) {
                    pending.push_back({cId * N + r, (uint8_t)bestCost});
                }
            }
        }

        // Apply the buffered marks: this is the ONLY place the DP table and
        // its masks mutate, which is what makes the sweep a true snapshot.
        std::vector<uint32_t> changedConfigs;
        for (const auto& pm : pending) {
            DataItem* state = &aux.states[pm.first];
            state->marked = true;
            state->markedRound = pm.second;

            size_t cIdMarked = pm.first / N;
            int rMarked = (int)(pm.first % N);
            markedMask[cIdMarked].set(rMarked);
            freeMarked.set(rMarked);
            if (changedConfigs.empty() || changedConfigs.back() != (uint32_t)cIdMarked) {
                changedConfigs.push_back((uint32_t)cIdMarked);
            }
        }
        changedByPass.push_back(std::move(changedConfigs));

        std::cout << "Pass " << passes << ": Found " << pending.size() << " new winning states.\n";

        if (pending.empty()) break;
    }

    return 0;
}

bool findFinalResult() {

    std::cout << "\n--- FINAL VERDICT ---\n";

    int bestCId = -1;
    uint8_t overallMinWorstCase = MAX_ROUND_COUNT;

    for (size_t cId = 0; cId < aux.configCount; ++cId) {

        bool universalWin = true;
        uint8_t worstCasePlys = 0;

        for (int r = 0; r < adj.nodeCount; ++r) {

            DataItem* state = aux.getState(cId, r, 0);

            if (!state->marked) {
                universalWin = false;
                break;
            }

            if (state->markedRound > worstCasePlys) {
                worstCasePlys = state->markedRound;
            }
        }

        if (universalWin && worstCasePlys < overallMinWorstCase) {
            overallMinWorstCase = worstCasePlys;
            bestCId = cId;
        }
    }

    if (bestCId != -1) {
        std::cout << "RESULT: WIN. " << k << " Cop(s) CAN win this graph with 1/" << p << " visibility.\n";

        std::cout << "Optimal Cop Start Positions: (";
        for (int i = 0; i < k; ++i) {
            std::cout << (int)aux.configs[bestCId * k + i] << (i == k - 1 ? "" : ", ");
        }
        std::cout << ")\n";

        std::cout << "Capture Time: " << (int)overallMinWorstCase << " plys.\n";

    } else {
        std::cout << "RESULT: LOSS. " << k << " Cop(s) CANNOT guarantee a win.\n";
        std::cout << "(The Robber has a strategy to survive indefinitely against any start).\n";
    }

    mem.print();

    return 0;

}

bool outputData() {

    std::string algoName = "k_cops_v2_" + std::to_string(p) + "vis";

    std::cout << "Saving filled AuxGraph to cache... ";

    bool failed = CacheManager::saveAuxGraph<DataItem>(algoName, filename, k, p, SelfEdgeCop::FALSE, SelfEdgeRobber::FALSE, &aux);

    if (failed) {
        std::cout << "Failed!\n";
        return 1;
    }

    std::cout << "Success!\n";

    return 0;

}
