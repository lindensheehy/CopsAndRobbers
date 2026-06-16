# ML Training Strategy & Token Constraint Solving

## 1. ML Training Strategy
- **Avoid training cops to prioritize distance** to the robber.
  - This leads to "zombie" behavior — mindlessly closing in without a capture strategy (see: Zombies and Survivors problem).
- **Recommended approach:** Train cops on the DP table for alternating visibility first, then train the robber against those cops.
- **Reward structure:**
  - Robber: small reward for surviving one more round.
  - Cop: slight reward for getting closer to the robber.

---

## 2. Token Constraint Approach

### Setup
- Start with robber tokens only, half-visible scenario.
- From the DP table, identify paths that satisfy token constraints.
- If no valid path is found, add more cops and recompute.

### Cop Token Maximization (Robber's Goal)
- The robber should try to **force cops to use as many tokens as possible**.
- If the robber cannot force excessive token use, the cops can win *even under the token constraint*.

---

## 3. Graph Construction from DP Table
- Build a new graph where each node = a combination of game positions from the DP table.
- Deduce valid moves → creates an additional graph layer.
- The constraint is a **triplet of resources** — this type of problem has likely been studied in the literature.
- Compute connected nodes on-the-fly (**implicit graph**) to avoid memory blowup.

---

## 4. Simplification Plan
To establish a clear starting point:
- Cops share tokens (combined pool).
- Robber has **infinite** tokens (no constraint on robber).
- Find the **shortest path** under this simplified model first.

---

## 5. Next Meeting
Thursday, April 2nd, 9:30am, in person.
