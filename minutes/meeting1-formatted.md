# Cops & Robbers: Algorithm & Graph Theory Notes

## 1. Algorithm Expansion: Generalizing for $k$-Cops
The current focus is upgrading the solver to support a variable number of cops ($k$) against 1 robber.

### State Representation
- **Transition:** Move from fixed pairs to tuples of size $k+1$.
- **State Format:** `(Cop1, Cop2, ..., CopK, Robber)`
- **Complexity:** $O(n^{k+1})$. We must be mindful of the "hidden constant" as $k$ increases.

### Optimization Strategy
To prevent state space explosion, we treat the set of cops as unordered (indistinguishable).
- **Problem:** State `((u, v), r)` is functionally identical to `((v, u), r)`.
- **Solution:** Enforce **Lexicographical Ordering** on cop positions.
  - Instead of permutations, generate combinations.
  - Sort cop positions such that $c_1 \le c_2 \le \dots \le c_k$.
  - *Impact:* Significantly reduces the graph size by removing redundant states.

### Minimax / Marking Logic
The algorithm uses backward induction (marking) to determine win states:
1. **Base Case:** Mark any state where a cop occupies the same node as the robber ($c_i = r$).
2. **Cop Turn (Maximizer):** A state is marked if **there exists** a move to a state that is already marked.
3. **Robber Turn (Minimizer):** A state is marked if **for all** possible moves (neighbors), the resulting state is marked (i.e., the robber has no escape).
   - *Constraint:* If all neighbors are marked, the robber loses.

---

## 2. Theoretical Background

### Dismantlable Graphs ($k=1$)
A graph has Cop Number 1 if and only if it is **dismantlable**.

**Definitions:**
- **Open Neighborhood $N(v)$:** Set of all neighbors of $v$.
- **Closed Neighborhood $N[v]$:** $N(v) \cup \{v\}$.
- **Corner (Dominated Vertex):** A vertex $u$ is a "corner" of $v$ if $N[u] \subseteq N[v]$.
  - *Implication:* If a cop is on $v$, they "see" everything $u$ sees. The robber cannot escape past $v$.

**Dismantling Process:**
1. Identify a corner vertex.
2. Remove it from the graph.
3. Repeat until only 1 vertex remains.
- *Examples:* Paths and Trees are inherently dismantlable.

### Graph Classes & Cop Numbers
Testing suite expansion plan:

| Class | Cop Number ($c(G)$) | Notes |
| :--- | :--- | :--- |
| **Paths / Trees** | $1$ | Dismantlable. |
| **Cycles** | $2$ | Simple loop requires 2 cops. |
| **Outerplanar** | $\le 2$ | Planar graphs with all vertices on the outer face. Some are dismantlable ($1$), but max is $2$. |
| **Planar** | $\le 3$ | Graphs drawn without edge intersections. Max cop number is $3$. |
| **Dodecahedron** | $3$ | Benchmark graph. |
| **Petersen** | $3$ | Benchmark graph. |
| **Robertson** | $4$ | The smallest 4-regular graph with girth 5. |

---

## 3. Next Steps & Action Items

### Development
- **Variable $k$:** Refactor Python implementation to accept number of cops as a parameter.
- **Optimization:** Implement lexicographical sorting for cop position tuples.
- **Visualization:** Add visual output for graph states and agent movements.
- **Graph Generation:** Generate input matrices for the specific test cases (Robertson, Petersen, Outerplanar variants).

### Schedule & Logistics
- **Reference:** Scotland Yard board image to be received this weekend.
- **Meeting 1:** Wednesday, Jan 28th @ 4:00 PM (Online).
- **Meeting 2:** Friday, Feb 13th @ 4:00 PM (In-person).
  - *Alternative:* Feb 10th (Morning, before 11:30 AM).