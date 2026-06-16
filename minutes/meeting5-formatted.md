# Invisible Robber — Strategy & Complexity

## 1. Strategies for the Invisible Robber
Approaches being explored when the robber's position is unknown:

- **Subset of Possible Positions:** Track a set of nodes where the robber could be, rather than a single known position. Use this subset to drive decision-making.
- **Blocking Strategy:** Two cops block off a region of the graph; a third cop enters and sweeps to catch the robber.
- **Propagation Strategy:** Block off part of the graph using one cop, then send another cop to explore the isolated section.
- **Maximum Degree Approach:** Use vertex degree as a heuristic for cop placement. Not optimal, but potentially useful as a baseline.
- **Assumption Avoidance:** It is not safe to make assumptions about what the cop is doing — strategy must be robust to any cop behavior.

---

## 2. Complexity of the Subset Approach
- When tracking subsets of possible robber positions, the state space becomes **4 columns** rather than 2.
- The column tracking subsets is bounded — it does **not** grow larger than the standard $n^2$ state space.
- Total complexity: approximately $4n^2$.

---

## 3. Current Priority
**Top priority:** Implement the alternating visibility variant where the robber is invisible every other turn.
