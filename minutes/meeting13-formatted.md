# Aux Graph Architecture & Token Path Design

## 1. Architecture Change
- Shift from a **library** to an **API** for the auxiliary graph.

---

## 2. Token System Design
- Robber has **wildcard tokens:** can be used anytime regardless of edge type.
- Robber can also use tokens that **mirror the cop's move type**.
- Wildcard tokens will be treated as a **separate resource category**.

---

## 3. Near-Term Goals
1. Complete the auxiliary graph.
2. Figure out the path algorithm with token constraints (robber is unconstrained for now — any move is valid).
3. Build the 1/k visibility algorithm based on the 1/2 visibility implementation.

---

## 4. 22-Round Constraint *(Brainstorm)*
- Potentially enforce the 22-round limit directly in the auxiliary graph.
- Select nodes that require **≤ 21 rounds** to reach a win.
- Find the **minimum token usage** to reach those nodes.
- Test one token at a time: if the number of required rounds exceeds the threshold, cops can't win under that constraint.
- Think of the aux graph as having **22 columns**, one per round — the last column is the winning state.
- Approach inspired by **Floyd-Warshall**, working backwards from the end state.

---

## 5. Reverse Traversal
- **Fix cop starting positions** (many robber start nodes, but cop starts stay fixed).
- First nodes in traversal = cop is on top of robber (immediate win states).

---

## 6. Next Meeting
Wednesday, May 13th, 12:00–1:00pm, online.
