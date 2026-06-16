# Technical Documentation: k_cops_visibility.cpp

## Overview

This file implements a **retrograde analysis solver** for the Cops and Robbers game on graphs, under a **partial (1/p) visibility** model. The game operates in cycles: cops see the robber once per cycle, then the robber moves invisibly for `p - 1` turns before being revealed again.

The solver determines whether `k` cops can *force* a capture against an optimally-playing robber, using backward induction over an AuxGraph state space.

This is the main research artifact for the Scotland Yard variant of Cops and Robbers with decreasing visibility. The immediate target is **1/5 visibility** (robber visible every 5 moves). The current implementation bug (as of meeting 15) is that the iteration logic was hardcoded for `p=2` and needs to be parameterized for arbitrary `p`.

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

| Column     | Actor  | Visibility | Node Type |
|------------|--------|------------|-----------|
| 0          | Cops   | Visible    | OR node |
| 1          | Robber | Visible    | AND node |
| 2          | Cops   | Invisible  | OR node (uniform) |
| 3          | Robber | Invisible  | DIRECT IF (interior) or AND boundary |
| 4          | Cops   | Invisible  | OR node (uniform) |
| ...        | ...    | ...        | ... |
| `2p-2`     | Cops   | Invisible  | OR node (uniform) |
| `2p-1`     | Robber | Invisible  | AND boundary (max col) |

**Parity rules:**
- Even columns = Cop turn
- Odd columns = Robber turn

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

### Col 1 — First Robber Turn / BFS Boundary (Depth 1)

Col 1 is a robber turn where cops know `r` precisely — it is the last-seen position from col 0, and the robber is still there when col 1 begins. After the robber moves, its destination is unknown; `r` remains fixed as the cycle anchor.

Because `r` is the exact starting node, the set of places the robber could have moved to is just the 1-hop neighborhood of `r`. The solver must ensure ALL of those destinations are forced wins at col 2.

**Mark rule:** Mark `(cId, r, 1)` if **for all** vertices `v` reachable from `r` in exactly 1 step, `(cId, v, 2)` is already marked.

**`markedRound`:** `max(nextState.markedRound) + 1` (robber picks slowest-to-capture destination).

This is handled by the same **BFS boundary logic** as the max-col case, with `depth = (1+1)/2 = 1` and `next_col = 2`. For `p = 1`, col 1 *is* the max col and `next_col` wraps to 0 — no special handling needed. The depth-1 BFS is identical to what a standard full-visibility solver does on every robber turn, which is why `p=1` reduces correctly to the standard game.

---

### Even Cols > 0 — Blind Cop Turn (Uniform OR Node)

Cops **cannot see** the robber. They must commit to a **single transition** that works for **all** possible true robber positions simultaneously — because they don't know which `r` the robber is actually at.

**Mark rule:** Mark **all** `(cId, r, col)` for a given `cId` if **there exists** a cop transition to `nextCId` such that `(nextCId, r, next_col)` is marked **for every `r`**.

**`markedRound`:** `max over r of nextState[r].markedRound + 1` for the chosen transition (worst-case true robber position).

**Key constraint:** The same cop transition must work for all `r`. The algorithm picks the transition that minimizes the worst-case `markedRound` across all `r`.

**Early-out:** If all `(cId, r, col)` are already marked for every `r`, skip this `cId`.

---

### Intermediate Odd Cols (1 < col < columns − 1) — Invisible Robber, Interior (DIRECT IF)

These columns represent invisible robber turns that are **not** the final boundary of the cycle.

**Critical insight:** The robber's expanding probability cloud is deferred entirely until the max-col boundary. At these intermediate columns the AuxGraph enforces this by giving each node `(cId, r, col)` **exactly one outgoing edge** — to `(cId, r, col+1)`. The robber has no representable "choice" here; the edge just advances the column, implicitly deepening the cloud rooted at `r`. This is also the memory optimization from Meeting 14_2: middle transitions are 1-to-1 and require no new edge structures.

**Mark rule:** Mark `(cId, r, col)` if and only if `(cId, r, next_col)` is already marked.

**`markedRound`:** `nextState.markedRound + 1`.

**Do NOT** compute the expanded vertex set here. The DIRECT IF is correct and intentional.

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

This BFS is used in two places:
1. At col 1 (visible robber turn, depth = 1).
2. At the max col (invisible robber boundary, depth = p).

---

## `markedRound` Semantics

`markedRound` is the **number of plys** (half-turns) until forced capture from a given state under optimal play. Stored as a 7-bit field, max representable value is 127 (`MAX_ROUND_COUNT = 0b1111111`).

| Node type | Rule | Reason |
|-----------|------|--------|
| Cop OR node (col 0, or blind even cols) | `min` over transitions | Cops pick the fastest route |
| Robber AND node (col 1, or max col) | `max` over reachable vertices | Robber picks the slowest route |
| DIRECT IF (intermediate odd cols) | direct copy | Single edge, no choice |
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

`CacheManager` persists and reloads the filled AuxGraph to/from disk, keyed by `algoName = "k_cops_<p>vis"`. A cache hit skips `buildAuxGraph` + `mainLoop`.

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

1. Col 0 is always the visible cop turn (OR node, independent per `r`).
2. Col 1 is always a robber turn; it uses the BFS logic with depth 1, checking against col 2 (or col 0 if `p=1`).
3. Intermediate invisible robber turns (interior odd cols, only exist when `p ≥ 3`) collapse to DIRECT IF — do not compute vertex sets.
4. Max col (`2p-1`) is always an odd column, always a robber turn, always computes BFS to depth `p`, always checks against col 0.
5. Blind cop turns (even cols > 0) always require a **uniform** transition — same cop move for all `r`.
6. For `p=1`, columns=2 and there are no blind cop turns and no intermediate robber turns; the algorithm reduces to the standard full-visibility solver.
7. For `p=2`, columns=4; there is one blind cop turn (col 2) and one max-col boundary (col 3); no intermediate robber turns.
8. Intermediate robber turns first appear at `p=3` (col 3 with columns=6).

---

## Known Issues / Current Status (as of Meeting 15)

- The 1/3 visibility implementation produced a "7-nested-loop problem" when directly generalizing from 1/2. The correct fix is proper parameterization of the column sweep (the `columns` variable and `depth = (col+1)/2` formula), not nesting new loops.
- There is likely a latent bug in the 1/2 visibility code that only becomes apparent when extending to 1/3. Investigate 1/2 before assuming 1/3 logic is wrong.
- Development priority: resolve visibility correctness before returning to token/ticket work.
