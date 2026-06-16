# 4-Column Algorithm for Alternating Visibility

## 1. Column Structure

The alternating visibility game uses a **4-column auxiliary graph** to capture the cycle of cop and robber turns with changing visibility.

### Transition Rules Between Columns

| Transition | Rule | Notes |
| :--- | :--- | :--- |
| Col 1 → Col 2 | **There exists** a neighbor that is marked | Standard cop-move marking rule |
| Col 2 → Col 3 | **1-to-1 mapping** — mark Col 2 if the corresponding Col 3 node is marked | Uses "for all" direction since the mapping is exact |
| Col 3 → Col 4 | **There exists** a neighbor that is marked | Standard cop-move marking rule |
| Col 4 → Col 1 | **For all** robber positions in the set, **and for all** possible moves from those positions, the result is a winning state | Checks the closed neighborhood of each node in the closed neighborhood of the initial robber position |

### Win Condition
- When cop position = robber position: cop wins immediately — nothing to check.
- A **full group** of marked nodes for some cop configuration signals a win, same as the original algorithm.

---

## 2. Writing Guidelines
- Always **define labels before using them** (e.g., "let $k$ be the number of cops").
- Be as **precise as possible** with wording.

---

## 3. Next Meeting
Friday (time TBD — will be decided by email during the week).
