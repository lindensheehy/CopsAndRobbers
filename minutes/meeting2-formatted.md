# Algorithm Optimization & Game Variant Notes

## 1. Propagation Optimization (Reverse Adjacency)
To speed up the marking process, avoid iterating through the entire state space blindly.
- **Structure:** Maintain a list of **incoming edges** for every node in the game graph.
- **Trigger:** When a state $(C, R)$ is marked as a win/loss:
  - Immediately trigger an update for all **parent states** (nodes that can transition *to* this state).
  - *Benefit:* Updates propagate instantly rather than waiting for the next full iteration loop.

## 2. Win Conditions & Combinatorics
- **Symmetry Rule:** If *any* combination of $k$ cops can force a win from a specific configuration, then *all* valid permutations of those cop positions (assuming indistinguishable cops) represent the same winning state.
- **Scotland Yard Constraint:** The game has a hard limit of **20 rounds**.
  - *Implication:* A "win" is only valid if `Moves_to_Win <= 20`.
  - If the algorithm finds a winning strategy that takes 50 rounds, it counts as a **Loss** for the cops in the context of Scotland Yard.

## 3. Move Counting (Depth of Win)
We must track not just *if* we win, but *how fast* (Minimax with depth).
- **Base Case:** If Cop and Robber share a node ($C = R$), rounds = $0$.
- **Cop Turn (Left Side / Maximizer):**
  - When marking a node, set its value to: `min(neighbor_values) + 1`.
  - *Logic:* The cops will choose the move that wins the fastest (Best Case).
- **Robber Turn (Right Side / Minimizer):**
  - When marking a node, set its value to: `max(neighbor_values) + 1`.
  - *Logic:* The robber will choose the move that delays the loss the longest (Worst Case).

## 4. Invisible Robber Variant (Blind Play)
Future goal: Solving for a robber whose position is unknown (or partially known).
- **Trivial Example:** A single cop on a simple Path graph.
  - *Strategy:* The cop can sweep from one end to the other and guarantee a win without ever "seeing" the robber.
- **Research Question:** Once the auxiliary graph (State Space) is fully built with move counts:
  - How do we identify if a graph allows for a "blind" win?
  - If a blind win is impossible, what specific information (e.g., "Robber is in Sector X") is required to guarantee a win?