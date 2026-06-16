# Algorithm Development & Strategy Tables

## 1. Action Items

- Find a reference for the Petersen graph having cop number 3
- Translate **both** versions of turn-counting into C++ for better testing
- Implement tables to determine the best move in all cases for both players

---

## 2. Strategy Tables (Dynamic Programming)

### 1-Cop Case
Generate two DP tables to verify correctness and test the algorithm:
- **Table 1:** Cop's turn
- **Table 2:** Robber's turn
- **Axes:** Y-axis = cop position, X-axis = robber position
- **Cell content:** Number of rounds remaining and the optimal move to play
- Together, these tables encode a **complete winning strategy for the cop**

### 2-Cop Case
- Instead of visualizing, output explicit move directions: where each cop should go

---

## 3. Algorithm Performance

- Ensure both versions of the algorithm produce the **same shortest-path result**
- If the algorithm is too slow: consider stopping as soon as the first winning group is found
  - Must first verify whether the first group found is always optimal

---

## 4. Development Notes

- Continue translating to C++ (also useful for performance benchmarking)
- Invisible robber to be addressed in a future meeting

---

## 5. Future Direction
- **Alternating visibility for cops** is the next major step after strategy tables are implemented

---

## 6. Next Meeting
Friday 27th, 3:00pm, in person.
