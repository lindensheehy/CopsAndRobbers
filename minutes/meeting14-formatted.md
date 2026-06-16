# Partial Visibility Columns, Movement Rules & Token Aux Graph Design

## 1. Column Structure for Partial Visibility
- **1/2 visibility** → **4 columns**
- **1/3 visibility** → **6 columns**
- **General:** $1/p$ visibility → $2p$ columns

### Memory Optimization
- With more than 2 columns, the **cop-to-robber and robber-to-cop edges are reused** (same structure).
- The middle transition is a **1-to-1 mapping** — no new edges needed there.
- This optimization applies for **any value of $p$** and reduces memory usage significantly.

---

## 2. Movement Rules
- Both cops and robbers have the *option* to stay still (self-loop edges).
- **For our purposes: set both to CAN'T STAY STILL** (remove self-loop edges).

---

## 3. Token Work — Starting Point
- Begin token implementation on the **full-visibility graph** (simpler base case).
- Simplified rules:
  - **No black tokens** for now.
  - Robber must use the **same edge type** as the cop.

### Aux Graph Modification
When the cop makes a move, the **robber is forced to use the same edge type**.

| Cop's available edges | Robber enforcement |
| :--- | :--- |
| Taxi only | Robber must use taxi |
| Bus only | Robber must use bus |
| Both (taxi + bus) | **Duplicate** the robber node — one copy per edge type, each with only that edge type available |

- Duplication ensures the rule is enforced while preserving choice in the aux graph.
- Pruning ensures each path uses exactly one edge type per move.
- Moves are **strictly enforced** — the logic breaks down otherwise.
- **If the robber cannot move, they lose.**
- Dead ends (robber has no valid move) need to be handled by the solver — likely not a problem in practice.

### Self-Edges & Black Tokens
- Self-edges are removed while implementing token logic.
- Future consideration: staying still = using a **black (wildcard) token** — a free move that bypasses the edge-type rule (5 uses total in Scotland Yard rules, but ignored for now).

---

## 4. Current Priority
Finalize the **$1/p$ visibility algorithm** before returning to tokens.

---

## 5. Brainstorm
- What would the game look like if the robber had **one black token** under the enforced-move rules?
  - Starting point for future wildcard token work.

---

## 6. Next Meeting
Tuesday, May 26th, 12:00–1:00pm, online.
