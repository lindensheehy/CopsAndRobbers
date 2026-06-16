# Tokens, AI/ML Design & Development Steps

## 1. Immediate Development Steps
1. Ensure the **alternating visibility** algorithm works correctly.
2. Extract and format `.npz` files properly.
3. Work backwards from the DP table to derive the token solution.

---

## 2. AI/ML Project Design
- Train **two AIs against each other** (cop AI vs. robber AI).
- Game settings for ML: visible for certain $k$ (e.g., $k=6$), 22 rounds, limited tokens.
  - Exact token numbers to be researched.

---

## 3. Token Algorithm Design

### DP Table Approach
- Compute separate DP tables for each edge type: taxi only, bus only, train only, and combined.
- Combine information from all tables to inform the final strategy.

### Robber Ticket Subproblem
- Introduce a subproblem where the **robber has limited tickets**.
- When the robber moves, they **must declare which ticket type** they are using.
- If no ticket type is declared, the default is **taxi (yellow)**.

---

## 4. Next Meeting
Friday 27th, 3:00–4:00pm, in person.
