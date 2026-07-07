# Technical Documentation: k_cops_visibility.cpp

## Overview

This file implements a **retrograde analysis solver** for the Cops and Robbers game on graphs, under a **partial (1/p) visibility** model. The game operates in cycles: cops see the robber once per cycle, then the robber moves invisibly for `p - 1` turns before being revealed again.

The solver determines whether `k` cops can *force* a capture against an optimally-playing robber, using backward induction over an AuxGraph state space.

This is the main research artifact for the Scotland Yard variant of Cops and Robbers with decreasing visibility. The immediate target is **1/5 visibility** (robber visible every 5 moves). The column sweep is now parameterized for arbitrary `p` (the original code was hardcoded for `p=2`); see *Known Issues* for the two bugs that were fixed in the generalization.

---

## Key Parameters

| Variable  | Meaning |
|-----------|---------|
| `k`       | Number of cops |
| `p`       | Visibility period: cops see the robber once every `p` robber turns. `p=1` is full visibility. |
| `columns` | Total DP columns = `2 * p`. Each column is one half-turn (cop or robber). |

---

## State Space

A DP state is a triple: **(Cop Positions, Last-Seen Robber Position `r`, Column Index)**.

- **Cop Positions** are encoded as a configuration ID `cId` into the AuxGraph.
- **`r`** is the robber's **last-seen position** — the node where cops observed the robber at col 0 of the current cycle. It does **not** change within a cycle. For cols 1 through `2p-1`, `r` is not the robber's actual position; it is purely a symbolic anchor that defines the starting point for the BFS expansion at the cycle boundary.
  - At **col 0**, `r` is both the last-seen position and the robber's true current position — cops can see the robber and act on that information.
  - At **col 1+**, the robber has moved (or will move) to some unknown destination. `r` remains unchanged in the state; it continues to represent only where the search should start from when the cloud is eventually cashed out at the max col.
- **Column Index** encodes where in the turn cycle the game currently is.

This design is directly reflected in the AuxGraph edge structure: for every robber turn that advances into an unknown position (odd columns > 1, up to but not including the max-col→col 0 wrap), each node `(cId, r, col)` has **exactly one outgoing edge** — to `(cId, r, col+1)`. The robber has no "choice" as far as the AuxGraph is concerned; the edge simply increments the column, deepening the implicit probability cloud rooted at `r`. The cloud is only materialized (via BFS) at the max-col boundary.

The AuxGraph does **not** store an expanded robber state space for invisible turns. The algorithm uses *deferred computation*: the invisibility cloud is only "cashed out" at the cycle boundary (the max column). This keeps memory proportional to `(Cop Configs) × (Graph Nodes) × (Columns)`, avoiding exponential blowup.

The intermediate invisible robber turns have a **1-to-1 structure** (no new edges needed, just a pass-through) which is a key memory optimization. Only at the max-col boundary does the full vertex set need to be computed (see Meeting 14_2).

---

## The Turn Cycle (Column Structure)

With `columns = 2 * p`, the columns map as follows:

| Column     | Actor  | Cop knows robber pos? | Node Type |
|------------|--------|-----------------------|-----------|
| 0          | Cops   | Yes (just saw it at `r`) | OR node (per-`r`) |
| 1          | Robber | —                     | DIRECT IF (deferred) — or AND boundary if `p=1` |
| 2          | Cops   | No (knows only `r`)   | OR node (per-`r`) |
| 3          | Robber | —                     | DIRECT IF (deferred) — or AND boundary if `p=2` |
| 4          | Cops   | No (knows only `r`)   | OR node (per-`r`) |
| ...        | ...    | ...                   | ... |
| `2p-2`     | Cops   | No (knows only `r`)   | OR node (per-`r`) |
| `2p-1`     | Robber | —                     | AND boundary (max col, BFS depth `p`) |

**Parity rules:**
- Even columns = Cop turn
- Odd columns = Robber turn

> **Key correction (post Meeting 15):** *Every* cop turn — visible (col 0) or "blind" (cols 2, 4, …) — is an **independent per-`r` OR node**. The cops always know the last-seen anchor `r`, so a blind cop turn is structurally identical to col 0; it is **not** a "uniform transition for all `r`" node. Cop blindness manifests only on robber turns, as the deferred cloud that is AND-checked at the max col. Likewise, only the **max col** materializes the cloud (BFS depth `p`); col 1 and all interior odd cols are deferred **DIRECT IF** pass-throughs (the lone exception is `p=1`, where col 1 *is* the max col).

**Distance (robber reachability from `r`) at column `col`:**  
The robber moves on odd columns, so the maximum hop-distance from `r` after column `col` is:

```
distance = ceil(col / 2) = (col + 1) / 2   [integer division]
```

Examples: col 1 → 1 hop, col 3 → 2 hops, col 5 → 3 hops, ..., col `2p-1` → `p` hops.

---

## Retrograde Propagation Logic

The solver works **backwards** from known wins (captures) to mark all states from which cops can force a win.

### Initialization (`initializeCaptures`)

A state `(cId, r)` is a **base capture** if the robber at `r` is already on top of a cop in config `cId`, computed via `aux.isInstantCatch(cId, r)`. Both columns 0 and 1 are marked for such states with `markedRound = 0`. All other states start unmarked with `markedRound = MAX_ROUND_COUNT` (127).

### Outer Loop (`mainLoop`)

The algorithm iterates passes over all columns until no new states are marked (fixed point). If all valid starting states `(cId, r, 0)` become marked for some `cId`, cops force a win.

---

## Per-Column Marking Rules

### Col 0 — Visible Cop Turn (OR Node)

Cops **can see** the robber. They choose a transition independently for each robber position `r`.

**Mark rule:** Mark `(cId, r, 0)` if **there exists** a cop transition to config `nextCId` such that `(nextCId, r, 1)` is already marked.

**`markedRound`:** `min(nextState.markedRound) + 1` over all winning transitions (cops pick fastest capture).

---

### All Cop Turns (every even col) — Per-`r` OR Node

This is the same rule for **all** cop turns, visible or blind. The cops always know the last-seen anchor `r` (and their own config `cId`), so they make a free, independent choice for each `r`. There is **no** uniform-across-`r` constraint — that was the central bug fixed after Meeting 15 (it stalled the backward induction within ~2 passes and produced spurious results for every `p > 1`).

**Mark rule:** Mark `(cId, r, col)` if **there exists** a cop transition to config `nextCId` such that `(nextCId, r, next_col)` is already marked.

**`markedRound`:** `min(nextState.markedRound) + 1` over all winning transitions (cops pick fastest capture).

Why blindness does *not* constrain the cop move: the cop transition depends only on `(cId, r)`, both of which the cops legitimately know (`r` is the position they observed at col 0). The robber's *true* current position during a blind turn is never used by the cops and is never representable here — that uncertainty is captured entirely by the deferred cloud, which is AND-checked once at the max col. So a "blind" cop turn (col 2, 4, …) is structurally identical to the visible col-0 turn.

---

### Col 1 and Interior Odd Cols (`col < columns − 1`) — Deferred Robber Turn (DIRECT IF)

Every robber turn **except the max col** is a deferred pass-through. This includes **col 1** (for `p ≥ 2`) and every interior odd col. The robber does move on these turns, but the expansion is **not** materialized here; it is deferred entirely to the max-col BFS, which counts *all* `p` robber moves at once.

**Critical insight:** The robber's expanding probability cloud is deferred until the max-col boundary. The AuxGraph enforces this by giving each node `(cId, r, col)` **exactly one outgoing edge** — to `(cId, r, col+1)`. The robber has no representable "choice" here; the edge just advances the column, keeping the anchor `r` fixed and implicitly deepening the cloud rooted at `r`. This is also the memory optimization from Meeting 14_2: these transitions are 1-to-1 and require no new edge structures.

**Mark rule:** Mark `(cId, r, col)` if and only if `(cId, r, next_col)` is already marked.

**`markedRound`:** `nextState.markedRound + 1`.

**Do NOT** compute the expanded vertex set here. The DIRECT IF is correct and intentional.

> **Note on col 1:** Earlier drafts treated col 1 as a depth-1 BFS boundary that materialized the robber's first move (shifting the anchor `r → v`). That is wrong for `p ≥ 2`: it double-counts the first move, because the depth-`p` BFS at the max col already accounts for *all* `p` robber moves from the original `r`. Col 1 is a DIRECT IF like the other interior odd cols. The **only** exception is `p = 1`, where col 1 *is* the max col and therefore runs the BFS (depth 1) instead — which is exactly why `p = 1` reduces to the standard full-visibility game.

---

### Max Col (`columns − 1 = 2p − 1`) — Invisible Robber Boundary (AND BFS Node)

This is the **cycle boundary** where the invisibility cloud collapses. The full set of vertices the robber could have reached from `r` over `p` hops is computed here.

**Step 1 — Compute the reachable vertex set via BFS:**  
BFS from `r` for exactly `depth = (col + 1) / 2 = p` steps.

- Expand hop-by-hop from `r`.
- Whether the robber can *stay still* is controlled by `aux.robberSelfEdges`. If `SelfEdgeRobber::TRUE`, include the current vertex in each BFS expansion step.
- Currently set to `FALSE` (robber cannot stay still, per Meeting 14).
- Compute **exactly** `depth` hops, not "at most". The self-edge parameter is the mechanism for "staying still."

**Step 2 — AND check against col 0:**  
`next_col = (col + 1) % columns = 0` at the max col, so this checks back against the start of the next cycle.

Mark `(cId, r, col)` only if **for all** vertices `v` in the BFS-reachable set, `(cId, v, 0)` is already marked.

**`markedRound`:** `max over v of nextState[v].markedRound + 1` (robber picks the state with the highest capture length — AND node).

**Edge case — p=1:** Max col is 1. `depth = (1+1)/2 = 1`. This matches the standard 1-hop visible robber turn exactly. No special casing needed.

---

## BFS Implementation

```cpp
std::fill(bfs_current.begin(), bfs_current.end(), 0);
bfs_current[r] = 1;

for (int step = 0; step < depth; step++) {
    std::fill(bfs_next.begin(), bfs_next.end(), 0);
    for (int v = 0; v < adj.nodeCount; v++) {
        if (!bfs_current[v]) continue;
        if (aux.robberSelfEdges == SelfEdgeRobber::TRUE) {
            bfs_next[v] = 1;  // robber can stay on this node
        }
        uint8_t* edges = adj.getEdges(v);
        for (int e = 0; edges[e] != 255; e++) {
            bfs_next[edges[e]] = 1;
        }
    }
    std::swap(bfs_current, bfs_next);
}
```

The sentinel value `255` marks the end of an adjacency list edge array.

This BFS runs in exactly **one** place: the **max col** (invisible robber boundary, depth = `p`), which cashes out all `p` deferred robber moves at once. For `p = 1` the max col happens to *be* col 1, so the depth-1 BFS there coincides with a standard full-visibility robber turn — but that is the same single code path, not a separate use.

---

## `markedRound` Semantics

`markedRound` is the **number of plys** (half-turns) until forced capture from a given state under optimal play. Stored as a 7-bit field, max representable value is 127 (`MAX_ROUND_COUNT = 0b1111111`).

| Node type | Rule | Reason |
|-----------|------|--------|
| Cop OR node (every even col — col 0 and all blind cop turns) | `min` over transitions | Cops pick the fastest route |
| Robber AND node (max col only) | `max` over reachable vertices | Robber picks the slowest route |
| DIRECT IF (col 1 and all interior odd cols) | direct copy | Single edge, no choice |
| Base capture | `0` | Already caught |

---

## Final Verdict (`findFinalResult`)

After fixed point, the solver scans all `(cId, r, 0)` states. A configuration `cId` is a **universal win** if every `r` is marked at col 0.

Reports:
- Win/loss.
- Optimal cop starting configuration.
- Worst-case capture time in plys and full cycles (`plys / columns`).

---

## Caching

`CacheManager` persists and reloads the AuxGraph (structure **and** the full `states` array) to/from disk, keyed by `algoName = "k_cops_<p>vis"` plus filename, `k`, `p`, and the self-edge flags.

A cache hit skips only `buildAuxGraph` (the structure rebuild). `main.cpp` still runs `initializeCaptures` and `mainLoop` unconditionally — it does **not** skip the solve. Therefore:

- **`initializeCaptures` must reset every column** of the loaded `states` array to unmarked before re-seeding base captures. `mainLoop` is monotone (it only adds marks), so any stale marks left in cols 2…max-1 from a previous run would permanently seed the propagation and corrupt the result. This was a real bug: the original code reset only cols 0 and 1, so cache-hit runs for `p ≥ 2` converged in 2–3 passes from a polluted starting point (cache-miss runs were fine because freshly-allocated state is zeroed). After the fix, cache-hit and cache-miss produce **identical** pass sequences and verdicts.
- The cache key is **not** versioned by solver logic. After changing the algorithm, the stale `states` are now harmlessly overwritten by `initializeCaptures`, but deleting `cache/k_cops_*vis*` is still the cleanest way to be sure.

---

## Self-Edge Configuration

Currently hardcoded at the top of `buildAuxGraph`:

```cpp
SelfEdgeCop c_edge = SelfEdgeCop::FALSE;
SelfEdgeRobber r_edge = SelfEdgeRobber::FALSE;
```

Both cops and robber **cannot stay still** (Meeting 14). The self-edge parameter for the robber directly controls the BFS expansion behavior at the max-col boundary.

---

## Known Invariants

1. Every even col is a cop turn = an independent per-`r` OR node. Col 0 (visible) and the blind cop turns (cols 2, 4, …) use the **identical** rule — no uniform-across-`r` constraint anywhere.
2. Col 1 is a robber turn. For `p ≥ 2` it is a **DIRECT IF** (deferred), checking against col 2. Only when `p = 1` (where col 1 is the max col) does it run the BFS (depth 1, checking col 0).
3. All interior odd cols (`col < columns − 1`), including col 1 for `p ≥ 2`, collapse to DIRECT IF — do not compute vertex sets.
4. Max col (`2p-1`) is always an odd column, always a robber turn, always computes BFS to depth `p` from the fixed anchor `r`, always checks against col 0. It is the **only** place the cloud is materialized.
5. The anchor `r` stays fixed at the col-0 last-seen position for the entire cycle; no robber turn shifts it.
6. For `p=1`, columns=2: no blind cop turns and no deferred robber turns; the algorithm reduces to the standard full-visibility solver.
7. For `p=2`, columns=4: col 0 + col 2 are per-`r` cop OR nodes, col 1 is a DIRECT IF, col 3 is the max-col BFS boundary (depth 2).
8. For `p ≥ 2`, the max-col BFS depth `p` accounts for *all* `p` robber moves at once; materializing any earlier robber turn would double-count moves.

---

## Known Issues / Current Status (resolved post Meeting 15)

Two bugs from the `p=2`-hardcoded generalization were fixed:

1. **Uniform blind cop turn (the killer).** Blind cop turns were coded as a single uniform transition that had to work for *all* `r` simultaneously. This is wrong — cops always know the anchor `r`, so every cop turn is a per-`r` OR node. The uniform constraint is nearly unsatisfiable from sparse base captures, so the backward induction stalled within ~2 passes and produced spurious verdicts for every `p > 1`. Fix: all cop turns now use the per-`r` OR rule.
2. **Col 1 materialized the first robber move twice.** Col 1 ran a depth-1 BFS that shifted the anchor `r → v`, while the max-col BFS (depth `p`) already counted that move — double-counting. Fix: col 1 is a DIRECT IF for `p ≥ 2`; only the max col materializes the cloud.
3. **Cache pollution via partial re-initialization (the one that made the bug look "persistent").** `initializeCaptures` reset only cols 0 and 1, but a cache hit reloads the *entire* filled `states` array and `mainLoop` runs unconditionally on top of it. Stale marks in cols 2…max-1 survived and seeded the monotone propagation, so cache-hit `p ≥ 2` runs converged in 2–3 passes from a corrupted start (and pass counts varied with cache history). Fix: `initializeCaptures` now resets **all** columns. Cache-hit and cache-miss runs are now identical.

**Verification:** `p=1` unchanged (WIN, 14 passes, 15 plys on `scotlandyard-all`, k=2). `p=2` now propagates over ~53 passes instead of stalling at 2. Verdicts are monotonic in visibility (k=2: `p=1` WIN, `p≥2` LOSS), as required — more visibility can never hurt the cops. For `p ≥ 3`, convergence is fast because the depth-`p` robber cloud covers a large fraction of the graph, making the max-col AND rarely satisfiable; this is expected, not a stall.

**Caching caveat:** the filled AuxGraph cache is keyed by `algoName`, filename, `k`, `p` — *not* by solver-logic version. After any algorithm change, delete the stale `cache/k_cops_*vis*` files or the solver will replay an old (possibly buggy) DP fill via a cache hit.

- Development priority: resolve visibility correctness before returning to token/ticket work. *(Correctness mechanics now in place; verdict values still want a ground-truth sanity check — e.g. confirm k=3 wins at `p=2` where k=2 loses.)*
