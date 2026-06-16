# Token Solving: Simplification & Development Priority

## 1. Simplified Token Model
- Assumption: **taxi is free**; track only **1 resource type** to start.
- Possibly cut the auxiliary graph off at **22 rounds**.

---

## 2. Dijkstra-Like Approach
- From the DP table, find whether a shortest path is unique (similar to Dijkstra).
- **Path length = time (steps); token usage = optimization criteria.**
- Main challenge: can't simply discard "bad" moves (where robber survives fewer rounds) — they may be necessary for minimizing token use.
- All game states must be considered, which risks graph explosion.

---

## 3. Tractable Starting Point
To make the problem manageable:
- Track **only one cop** (all other cops become bobbies without token constraints).
- Find the winning node in the DP table and record how many tokens were used to reach it.
- If a path with fewer tokens is found during propagation, **update** the count.
- Continue until a path satisfying the token constraint is found.
- Entire search still bounded at 22 rounds.

---

## 4. Development Priority
1. **Finalize 1/2 visibility:** Must be clean — no bugs, marked states stored with timestamps.
2. **Generalize to 1/5 visibility.**
3. **Token work** to follow only after the above are clean.

---

## 5. Next Meeting
Wednesday, April 29th, 1:00pm, in person.
