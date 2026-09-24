# Functional Comparison: `k_cops_visibility_v2.cpp` vs `k_cops_visibility_v3.cpp`

**Context:** v2 is the tested, correct implementation of the Macro-Turn Architecture (spec: [k_cops_visibility_v2.md](k_cops_visibility_v2.md)). v3 is a hand rewrite aimed at better abstraction and encapsulation — `VertexBitField` / `PackedAdjMatrix` replace v2's inline `VertexMask`, and the recursive `trajectoryDFS` becomes an explicit stack machine.

**Scope:** this document records the *functional* differences between the two files as they stand. It is a comparison, not a change request; nothing here has been applied to v3.

**Sources read:** both solvers, the v2 spec, and the dependencies in `lib/` — `AuxGraph.h`, `AdjacencyList.h`, `VertexBitField.h`, `PackedAdjMatrix.h`.

---

## 1. Setup / plumbing

### `adj` is never built

v2 does `adj.constructFrom(&g)` in [loadGraphFile](../solvers/k_cops_visibility_v2.cpp#L163). v3 populates `adjMatrix` instead and leaves the global `AdjacencyList adj` default-constructed — `nodeCount == 0`, `edges == nullptr` ([AdjacencyList.h:24](../lib/include/AdjacencyList.h#L24)).

Consequences:

*   `aux.constructFrom(k, 1, &adj, &mem)` receives `N = 0`. In `generateCopConfigs`, `n_val = 0 + k - 1 = k-1 < k`, so `configCount = 0` and `buildAuxGraph` returns the "Unable to generate aux graph" error. **v3 currently cannot get past step 2.**
*   Even past that, `initializeCaptures` and `findFinalResult` loop `r < adj.nodeCount` (0 iterations) while `mainLoop` loops `r < N` (the `Graph` count). Both solvers use `adj.nodeCount` there, but v3 no longer has a populated `adj` to make it mean anything.

`adjMatrix` is a substitute for v2's `nbrMask`, not for `adj` — `AuxGraph` needs a real `AdjacencyList` to generate cop moves.

### Vertex cap

v2 rejects `N > 255` (the graph format limit, checked in `mainLoop`). v3 rejects `N > 200` at load time, because `PackedAdjMatrix` holds exactly 200 rows. Intentional narrowing.

### Cache

v2's `buildAuxGraph` attempts `CacheManager::loadAuxGraph` first, and `outputData` saves the filled table. v3 does neither — `outputData` is an empty stub. Every run is a cold solve, and results are discarded.

### Memory tracking

v2 tracks the adjacency list, the APSP table and the mask arrays. v3 tracks only `sizeof(PackedAdjMatrix)`.

---

## 2. Column index

`aux.constructFrom(..., columns=1, ...)`, so the only valid column is `0`. `getState(cId, r, col)` returns `&states[(cId*N + r)*columns + col]` ([AuxGraph.h:96](../lib/include/AuxGraph.h#L96)).

*   `initializeCaptures` and `findFinalResult` use column `0` (same as v2).
*   `mainLoop` uses column **`1`** — both for `root` ([v3:126](../solvers/k_cops_visibility_v3.cpp#L126)) and for the leaf lookup ([v3:244](../solvers/k_cops_visibility_v3.cpp#L244)).

So the main loop reads and writes a plane shifted one state forward, and runs off the end of the array on the last state. It never sees the seeded captures, and `findFinalResult` never sees its marks. v2 consistently uses column 0 / `aux.states[cId*N + r]`.

---

## 3. Transition values used as config IDs

`createTransitions` stores `nextId * N`, not `nextId` ([AuxGraph.h:241](../lib/include/AuxGraph.h#L241)). v2 recovers the config with `cIdNext = cand.base / N`, and indexes states with `aux.states[cand.base + v]`.

v3 takes `size_t nextcId = aux.transitions[current.nextTransition]` ([v3:173](../solvers/k_cops_visibility_v3.cpp#L173)) and uses that value directly in three places:

*   `aux.configs[nextcId]`
*   `aux.getState(nextcId, r_end, 1)`
*   `aux.getCopTransitions(nextcId, ...)`

All three are off by a factor of `N`.

Separately, cop positions are `k` bytes per config: v2 uses `&aux.configs[cId * k]`, v3 uses `&aux.configs[nextcId]` ([v3:193](../solvers/k_cops_visibility_v3.cpp#L193)) — missing the `* k` stride as well.

---

## 4. Capture semantics inside the macro-turn

This is the substantive algorithmic divergence, independent of the indexing issues above.

### The robber's ply prune is gone

v2 prunes twice per depth:

```
mid  = prev \ C_d        // capture on the cop ply  -> cost 2d-1
next = expand(mid) \ C_d // robber forced onto a cop -> cost 2d
```

v3 does the first prune ([v3:194-196](../solvers/k_cops_visibility_v3.cpp#L194-L196)) but after `expand` there is no `\ C_d` and no second emptiness test. A robber whose every move lands on a cop in `C_d` survives in v3's cloud and is carried to the next depth or into the boundary test. Spec Phase B step 3 (`V_d = V_raw \ C_d`) and Phase C are both only half-implemented.

### Cost accounting is shifted

v3 records `bestDepth = stack.size() * 2` for the cop-ply capture, i.e. `2d`. v2 records `2d - 1` for the same event — its file header argues at length that this is what reproduces v1's odd capture times at `p=1` — and reserves `2d` for the robber-forced-onto-a-cop case that v3 doesn't detect.

Net effect: every v3 capture time is one ply high relative to v2, and v2's even-cost captures have no v3 equivalent.

### The robber may pass

`PackedAdjMatrix::expand` defaults `allowPass = true` ([PackedAdjMatrix.h:37](../lib/include/PackedAdjMatrix.h#L37)), and v3 calls `adjMatrix.expand(newRobberSet)` with no second argument — so the cloud includes staying still. v2's `expandFrom` is neighbors-only, matching the `SelfEdgeRobber::FALSE` that v3 also passes to `setSelfEdges`. The two solvers now disagree about the robber's move set.

### The leaf level is truncated

At `stack.size() == p`, v3 evaluates one leaf candidate and then does `stack.pop_back(); continue;` ([v3:268](../solvers/k_cops_visibility_v3.cpp#L268)). That pops the frame whose transition list holds all the depth-`p` siblings, so only the *first* boundary candidate per parent is ever examined. v2's `leafDepth` branch falls through to the next candidate in the loop. This loses the minimum over the last cop move.

By contrast, the `pop_back` on the empty-set path at [v3:201](../solvers/k_cops_visibility_v3.cpp#L201) does match v2's `return` — abandoning the level there is sound, because `2d` is the floor for that level.

### The empty-set bound update is assignment, not `min`

v2 guards every update (`if (cost < bestCost)`); v3 writes `bestDepth = stack.size()*2` unconditionally. Given the push guard, `bestDepth >= 2d` holds whenever the DFS is at depth `d`, so this happens not to regress today — but it is load-bearing on that invariant in a way v2 is not.

### Bound check placement

v2 tests `2*d - 1 >= bestCost` at function entry *and* again per candidate. v3 tests `(stack.size()*2) + 1 >= bestDepth` only at the push site ([v3:286](../solvers/k_cops_visibility_v3.cpp#L286)), which is the per-candidate analogue; there is no re-check before evaluating a leaf.

### The overflow guard is dropped

v2 only marks when `bestCost < MAX_ROUND_COUNT`, since `markedRound` is a 7-bit field. v3 assigns `root->markedRound = bestDepth` with no range test — a cost of 128 or more silently truncates into a small, wrong value.

---

## 5. Fixed-point loop

### In-place vs. snapshot marking

v2 buffers every mark into `pending` and applies them only at the end of a pass, with a written argument that in-place marking is unsound for *exact ply costs* under freezing: a state swept early can consume an expensive dependency and freeze before the cheap dependency is swept. v3 marks directly on `root` inside the sweep. Win/loss verdicts still converge; the recorded `markedRound` values can be too high.

### Dropped skip filters and move ordering

All of these are pure performance in v2, and all are absent from v3:

*   APSP table (one BFS per vertex).
*   Heuristic move ordering by distance-to-cloud (spec 5.3).
*   Escape-set filter (`freeMarked` / `allowed[t]`).
*   Unchanged-region skip (`changedByPass` / `lastEvalPass`).

Expect the v3 DFS to be the full `O(b^p)` per state per pass in exchange.

### Allocation inside the search

Spec section 6 requires no heap allocation inside the trajectory search; v2 preallocates `cloud` / `cands` / `cloudDist`. v3 constructs a fresh `std::vector<StackItem> stack` per `(cId, r)` ([v3:138](../solvers/k_cops_visibility_v3.cpp#L138)), each `StackItem` carrying a 32-byte `VertexBitField` by value. The `reserve(p)` is adequate — depth never exceeds `p` — so it is one allocation per state, not per node.

### Per-pass reporting

v2 prints new-marks-per-pass and the base capture count. v3's `passes` counter is incremented and never read.

---

## 6. Minor

*   `extractVertices` returns "buffer too short" when `count` *reaches* `bufferSize` ([PackedAdjMatrix.h:81](../lib/include/PackedAdjMatrix.h#L81)), so a legitimately 200-vertex cloud trips v3's `FATAL ... exit(1)` at [v3:234](../solvers/k_cops_visibility_v3.cpp#L234).
*   v3's leaf initializes `maxChildDepth = -1`; if `nodeCount == 0` the branch scores `2p - 1`. Unreachable today (the set is non-empty before `expand`, and `allowPass` keeps it non-empty), but it becomes reachable the moment the post-expand `\ C_d` prune is added without an emptiness test.
*   `for (uint8_t r = 0; r < N; r++)` in v3's `mainLoop` relies on the 200-vertex cap to terminate; v2 uses `int`.
*   Both files return `1`/`0` from `bool` functions, matching [main.cpp](../app/main.cpp#L28)'s "nonzero = failed" convention. v3's `loadGraphFile` error paths do the same — consistent, just worth noting that the `bool`-as-status idiom is preserved.

---

## Summary

The load-order issue (section 1) and the column index (section 2) are what stop v3 from running at all. Sections 3 and 4 are what would make it disagree with v2 once it does.
