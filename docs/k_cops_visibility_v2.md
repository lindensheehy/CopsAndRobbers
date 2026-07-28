# Technical Specification: On-Demand Trajectory Pruning (Macro-Turn Architecture)

**Context:** Retrograde Analysis Solver for Multi-Agent Pursuit-Evasion (1/p-Visibility)
**Objective:** Eradicate the "intermediate ghosting" (trajectory amnesia) flaw by replacing multi-stage retrograde propagation with a single-stage, bounded depth-first macro-turn evaluator.

---

## 1. Architectural Summary & Problem Statement

### 1.1 The Intermediate Ghosting Flaw (Trajectory Amnesia)
In a partial visibility model where cops only observe the robber every $p$ turns, breaking the invisibility window into independent, memoryless intermediate states causes trajectory amnesia. 

When reachability is computed solely at the cycle boundary ($d = p$) using static graph topology, the solver evaluates robber paths without historical awareness of intermediate cop positions. Consequently, robber trajectories that physically collide with cops at steps $d < p$ are not pruned. The robber is allowed to "ghost" through cop blockades. The boundary evaluation then demands the cops prove guaranteed wins against these illegal, post-mortem trajectories, falsely marking guaranteed winning configurations as losses.

### 1.2 The Macro-Turn Solution
The state space must be collapsed so that one state transition represents the entire $p$-step visibility cycle. 

Instead of propagating backward through intermediate columns, the solver evaluates an unmarked starting state by executing a bounded Depth-First Search (DFS) over the cops' candidate trajectories of depth $p$. The robber's reachability cloud is generated dynamically, expanding hop-by-hop and being mathematically intersected with the cops' physical coordinates at each intermediate step. If the reachability cloud is reduced to the empty set, the branch guarantees an intermediate capture, terminating the evaluation early and registering a win.

---

## 2. State Space Definition

### 2.1 Dimensionality Reduction
The discrete columns representing intermediate turns must be eliminated. The DP state space is reduced to a single evaluation plane representing the start of a visibility cycle.
*   **State Definition:** A state is defined strictly by $(C, r)$, where $C$ is the cop configuration and $r$ is the exact vertex where the robber was observed.

### 2.2 Cost Accounting (Ply Depth)
The value of a solved state is the worst-case number of plys (half-turns) required to force a capture.
*   **Ply Cost:** Each intermediate step $d \in [1, p]$ consists of one cop move and one robber move (2 plys).
*   **Horizon Cut-Off:** The solver must enforce a maximum ply depth limit to prevent cyclic overflow. Any sequence exceeding the horizon must be evaluated as an unprovable loss.

---

## 3. The Bounded Trajectory Algorithm

To evaluate whether a state $(C_0, r)$ is a guaranteed win, the solver must explore the $p$-step macro-turn using the following recursive logic.

### 3.1 Initial Conditions
*   **Cop Anchor:** $C_0$
*   **Robber Reachability Cloud:** $V_0 = \{r\}$

### 3.2 Recursive Trajectory Step (Depth $d \in [1, p]$)
For a given cop configuration $C_{d-1}$ and robber cloud $V_{d-1}$:

**Phase A: Candidate Generation**
Generate all valid cop configurations $C_d$ reachable from $C_{d-1}$ in one step. For each $C_d$, evaluate the resulting subgame.

**Phase B: Robber Cloud Expansion & On-Demand Pruning**
For a chosen $C_d$, compute the next robber reachability cloud $V_d$:
1.  **Expansion:** Generate $V_{\text{raw}}$ containing all vertices adjacent to any $v \in V_{d-1}$. If the game rules permit the robber to pass (stay still), include $V_{d-1}$ in $V_{\text{raw}}$.
2.  **Pruning:** Remove any vertex $u \in V_{\text{raw}}$ that is physically occupied by a cop in $C_d$.
3.  **Result:** $V_d = V_{\text{raw}} \setminus C_d$.

**Phase C: Early Exit (Intermediate Capture)**
If $V_d = \emptyset$, every possible robber trajectory was intercepted at or before step $d$. 
*   **Action:** Terminate this branch. Register a guaranteed win with a cost of $2d$ plys. Do not evaluate steps $d+1$ through $p$.

**Phase D: Boundary Resolution ($d = p$)**
If the maximum depth $p$ is reached and $V_p \neq \emptyset$, the visibility cycle ends.
*   **Action:** Verify if *every* vertex $v \in V_p$ maps to a previously solved winning state $(C_p, v)$ in the global DP table. 
*   If true, the branch is a win. The cost is $2p$ plus the maximum ply cost among all $v \in V_p$.
*   If false, the branch is a loss.

**Phase E: Induction**
If $d < p$ and $V_d \neq \emptyset$, recurse to step $d+1$.

### 3.3 State Resolution
The state $(C_0, r)$ is marked as a win if there exists at least one valid cop trajectory ($C_1 \dots C_p$) that guarantees capture. The optimal strategy minimizes the worst-case ply cost.

---

## 4. Global Fixed-Point Induction

Section 3 defines a **local** decision procedure: given a fixed, fully up-to-date DP table, it decides whether one state $(C_0, r)$ is a win and at what cost. But Phase D reads from that same DP table (states $(C_p, v)$), which is itself populated by this exact procedure applied to other states. The local evaluator must therefore be wrapped in a global, monotonic **value-iteration loop** — the same retrograde control flow used by the legacy column-propagation solver — so that macro-turn cycles can chain into one another.

### 4.1 Base Case Seeding
Before the first sweep, seed every state $(C, r)$ where the robber already occupies a cop position as a marked win with cost $0$. This is identical to `initializeCaptures` in the legacy solver. All other states start unmarked.

### 4.2 Outer Sweep
Repeat the following pass over the full state space until convergence:
1.  For every **currently unmarked** state $(C_0, r)$, run the Section 3 macro-turn evaluator against the DP table **as it stands at the start of this pass** (or, if using an in-place sweep, as it stands at the moment of evaluation — see 4.3).
2.  If the evaluator proves a win, mark $(C_0, r)$ now, recording the ply cost returned by the Branch-and-Bound search (Section 5).
3.  If a full pass marks **zero** new states, the DP table has reached a fixed point. Stop.

### 4.3 Monotonicity & Freezing
Once a state is marked, it is **never re-evaluated or revisited** in a later pass or later within the same pass. Its recorded ply cost is permanent.

This is safe and yields the exact optimal ply cost, not merely an eventual correct win/loss verdict: a state can only be marked by consuming dependencies ($(C_p, v)$ states read in Phase D) that are *themselves already frozen with their final, optimal value*. By induction on marking order — base captures are exactly optimal at cost $0$, and every subsequently marked state computes its cost strictly from already-final dependency values — every recorded `markedRound` is exact, regardless of sweep order or how many passes convergence takes. This is the same guarantee the legacy column-propagation solver (`k_cops_visibility.cpp`) already relies on; this architecture does not weaken it.

### 4.4 Multi-Cycle Chaining
This outer loop is precisely what allows multiple visibility cycles to compose into a full-game solution. A state $(C_0, r)$ whose only winning trajectories terminate (Phase D) at boundary states $(C_p, v)$ that are themselves only provable via their *own* $p$-step macro-turn becomes markable exactly once those deeper cycles are resolved in an earlier pass. Capture strategies that require several chained visibility cycles to force a win therefore still fall out of repeated sweeps, not a single evaluation.

### 4.5 Termination
The state space is finite ($\text{configCount} \times N$), and marking is monotonic (states only ever transition unmarked → marked, never the reverse). The loop is therefore guaranteed to terminate in at most $\text{configCount} \times N$ passes.

---

## 5. Branch-and-Bound Pruning & Move Ordering

Section 3.3 requires the **exact minimum** worst-case ply cost, not just a win/loss verdict — a boolean short-circuit on the first winning trajectory found is not sufficient. Combined with Phase A's branching over every candidate cop transition at every depth, this makes the naive DFS $O(b^p)$ per state per pass. This section defines the mandatory pruning strategy required to keep $p \geq 3$ tractable.

### 5.1 Cost Monotonicity Property
The DFS spends exactly one cop move and one robber move per depth level, so a branch that has reached depth $d$ has unconditionally accumulated $2d$ plys, **regardless of which candidate trajectory was taken to get there**. No branch can ever produce a total cost lower than $2d$ once it has reached depth $d$. This deterministic, branch-independent accumulation is what makes a simple depth-based bound sound.

### 5.2 Mandatory Depth Cutoff
Each top-level evaluation of a state $(C_0, r)$ maintains a local `best_known_cost`, initialized to $+\infty$ (unset) at the start of that state's search.

*   **Prune condition:** before expanding a branch at depth $d$ (i.e. before Phase A generates candidate configs, or equivalently at entry to the depth-$d$ recursive step), if $2d \geq \text{best\_known\_cost}$, abandon the branch immediately without generating candidates or expanding the robber cloud. By 5.1, nothing beneath this point can improve on the current best.
*   **Bound update:** whenever Phase C (early exit) or Phase D (boundary resolution) yields a winning cost for a branch, set $\text{best\_known\_cost} \gets \min(\text{best\_known\_cost}, \text{cost})$ and **continue the search** — backtrack to sibling branches rather than stopping, since a cheaper trajectory may still exist elsewhere in the tree.
*   **Result:** when the DFS for $(C_0, r)$ completes, a finite `best_known_cost` is the exact worst-case-minimizing ply cost for that state this pass. If it is still $+\infty$, the state is not provably a win on this pass (it may become one on a later outer pass, per Section 4).

### 5.3 Heuristic Move Ordering
The depth cutoff in 5.2 is only effective once a strong `best_known_cost` exists — an empty bound prunes nothing. Phase A's candidate configurations $C_d$ must therefore be evaluated in an order that front-loads cheap winning trajectories, rather than arbitrary/index order.

*   **Ordering rule:** score each candidate $C_d$ by a proximity heuristic — the sum (or minimum; fix one convention at implementation time and apply it consistently) of graph distance from each cop's new position to its nearest vertex in $V_{d-1}$. Explore candidates in ascending score order (cops closest to the current robber cloud first).
*   **Precomputation requirement:** computing graph distances during the DFS would both violate the no-heap-allocation invariant (Section 6) and add a fresh search per candidate per branch. Distances must instead be served from a precomputed **All-Pairs Shortest Path (APSP)** table — an $N \times N$ distance matrix built once (e.g. one BFS per vertex) during `buildAuxGraph` — so scoring a candidate during the DFS is $O(k)$ table lookups, not a search.
*   **Correctness note:** move ordering is a pure performance heuristic. It changes *which* branch is explored first, never *whether* a branch is eventually explored. The mandatory depth cutoff (5.2) alone guarantees the exact global minimum is found for every state; ordering only affects how quickly a strong bound emerges, and therefore how many branches 5.2 gets to skip.

### 5.4 Bound Scope
`best_known_cost` is local to a single macro-turn evaluation — one $(C_0, r)$, one outer pass (Section 4.2) — and is discarded once that evaluation ends. It must **not** persist across states or across passes: a state re-evaluated in a later pass (because it wasn't won on a previous pass) starts its bound fresh at $+\infty$, since the global DP table has more entries marked by then and a cheaper trajectory may now be provable that wasn't reachable before.

---

## 6. Algorithmic Invariants & Edge Cases

Any implementation must satisfy the following mathematical and logical constraints:

| Condition | Required Behavior |
| :--- | :--- |
| **$p=1$ (Full Visibility)** | The algorithm must natively reduce to a standard minimax depth-1 evaluation. The boundary resolution ($d=p$) must trigger immediately after 1 hop without double-counting moves. |
| **Memory Allocation** | The implementation must not perform dynamic heap allocations for the $V_d$ reachability clouds inside the trajectory search. Memory must be pre-allocated and reused to prevent combinatorial processing bottlenecks. |
| **Simultaneous Collision** | If a robber moves onto a node that a cop *just vacated* at step $d$, it is NOT a capture. Pruning must strictly compare the robber's position at step $d$ against the cops' positions at step $d$ ($C_d$, not $C_{d-1}$). |
| **Exact Boundary Capture** | If the last remaining robber path is intercepted on the exact final step ($d=p$), it must register as an early exit ($V_p = \emptyset$), returning exactly $2p$ plys without querying future DP states. |
| **Branch-and-Bound Soundness** | The depth cutoff ($2d \geq \text{best\_known\_cost}$, Section 5.2) is the only permitted pruning condition; it must never eliminate a branch that could still tie or improve on `best_known_cost`. Heuristic move ordering (Section 5.3) may only affect traversal order — it must never itself be used as a prune condition. |
| **`best_known_cost` Scope** | `best_known_cost` is local to a single $(C_0, r)$ evaluation within a single outer pass (Section 4.2/5.4). It must not leak across states or persist across passes. |
| **Marked-State Freezing** | Once a state is marked by the outer loop (Section 4.3), it is never re-evaluated, even if a later pass could theoretically find a trajectory through newly-marked dependencies. This is required for termination and does not sacrifice optimality (see 4.3). |