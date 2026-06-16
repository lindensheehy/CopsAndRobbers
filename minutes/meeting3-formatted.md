# Graph Theory & Scotland Yard Development Notes

## 1. Next Steps from Previous Meeting
- Add step count to win calculation
- Begin translating solver to C++
- Research existing results on how long it takes for a cop to catch a randomly-moving robber

---

## 2. $(d,g)$-Cage Graphs
A potential source of graphs requiring many cops.

**Relevant Theorem:**
> If $G$ has girth $\ge 5$, then $c(G) \ge \delta(G)$ (minimum degree of $G$).

**Definitions:**
- **Girth:** Length of the shortest cycle in the graph.
- **$(d,g)$-Cage:** A graph where every vertex has degree exactly $d$ and the girth is exactly $g$.

**Implication:**
A $(d, 5)$-cage has cop number at least $d$. These are promising candidates for finding graphs that require more cops. Exploring different values of $d$ with $g=5$ is a good avenue for testing.

---

## 3. Scotland Yard Graph Development
- Add bus and train edges to the Scotland Yard graph (no token constraints yet)
- Maintain **three separate versions** of the graph:
  1. Taxi only
  2. Taxi + Bus
  3. Taxi + Bus + Train

---

## 4. Future Research Directions

### Invisible Robber *(Far Future)*
Solving the game where the robber's position is unknown.

### Alternating Visibility *(Something to Think About)*
A more nuanced visibility model:
- The cop sees the robber's starting position.
- Afterwards, the cop only sees the robber's position **every other move**.
- This may require the game graph to track all *possible* robber positions, not just the actual one.
- Relevant to Scotland Yard, where the robber's position is revealed only every ~4 rounds.

---

## 5. Collaboration
- A shared Overleaf document will be available in 1–2 weeks.
- References from Overleaf can be used publicly in the project.

---

## 6. Next Meeting
Friday, 20th @ 10:00am (or Thursday afternoon as alternative).
