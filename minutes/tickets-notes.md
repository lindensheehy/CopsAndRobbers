## Option 1: Tracking Token Use During Iteration

* **Infeasible due to state space explosion:** The combinatorial growth makes this approach unscalable.
* **Mathematical Bottleneck:** For every positional state (robber, cop_1, ..., cop_k), tracking ticket usage requires adding ticket_count^(k+1) states *per configuration*. This acts as a massive multiplier on top of the already massive n^(k+1) positional state space. 
* **Conclusion:** This method is fundamentally not computable within our target performance and memory constraints.

---

## Option 2: Backtracking from the DP Table

This approach involves computing the game normally (ignoring ticket constraints) to produce the standard DP table, and then utilizing that information to derive a path that catches the robber within the ticket limits.

* **The Trivial Case:** This is easily solved **if and only if** a strictly optimal solution naturally exists within the DP table that falls within the acceptable ticket range.
* **The Complex Case:** If the strictly optimal paths exceed the ticket limit, the problem becomes significantly harder. We must work backwards from those optimal solutions to determine where ticket usage can be safely pruned.

### Potential Backtracking Approaches

#### Approach 2A: Trimming from the End (Tracing Backwards)
* **Mechanism:** From a given optimal solution, trace backwards to find the **most recent** ticket use. Branch off and attempt to solve the path again while explicitly *forbidding* that specific ticket use.
* **Outcomes:**
    * *Success:* We find an alternate solution (it will almost certainly require more turns, which is acceptable). We can stop here.
    * *Failure:* We do not find any solution, meaning we must scrap this logical thread.
* **The Complication:** If we fail and must backtrack to the *next* most recent ticket use, the logic gets extremely tricky. We eventually end up in the exact same position as Option 1, forcing us to solve the game while actively tracking resources in the state memory.

#### Approach 2B: Trimming from the Start (Tracing Forwards)
* **Mechanism:** From a given optimal solution, trace forwards to find the **first** ticket use. We skip/forbid that specific use (locking out that solution space) and evaluate the remaining path options.
* **Validation:** We check the usual way to see if a valid solution still exists where that initial ticket use was skipped.
* **Recursion:** If no solution exists, we revert to where we initially skipped the ticket. From that point, we split the threads as we did initially, but this time we target the *next* ticket use in each thread. 
* **Summary:** This represents a recursive approach where we attempt to trim ticket dependencies from the start of the timeline rather than from the end.

---

## Option 3: Iterative Ticket Scaling

This approach uses the foundational logic of Option 1 but mitigates the upfront combinatorial explosion by scaling the permitted resources progressively.

* **Mechanism:** We initially forbid all ticket use (setting the ticket limit to 0) and compute the game to see if a solution exists. If no solution is found, we increment the allowed ticket count to 1 and recompute. We repeat this process, adding one ticket each iteration, until a valid solution is discovered.
* **Best Case Scenario:** If a solution exists that requires zero or very few tickets, the state space remains tight. The algorithm resolves quickly, performing identically to a version of the algorithm that does not track tickets at all.
* **Worst Case Scenario:** If the optimal solution requires a high number of tickets (or if no solution exists), the iterative additions eventually expand the state space to its maximum size. In this scenario, it hits the exact same computational wall as Option 1, making it infeasible.


### Plain text (pre AI formatting)
```
option 1: tracking token use during iteration
- infeasible because state space explodes dramatically
- for each state (robber, cop_1, ..., cop_k), we need to add information for tickets used. this requires ticket_count^(k+1) states PER configuration (which is already n^(k+1))
- simply not computable for the constraints we are aiming to work within

option 2: backtracking from the dp table
- we compute the game normally and produce the dp table as weve done before, then we use that information to derive a solution that can catch the robber within the ticket constraints set.
- this is trivial IF AND ONLY IF a primary (strictly optimal) solution exists in this dp table where the tickets used are within the acceptable range
- this gets much harder if that is not the case, as we then have to work BACKWARDS from those strictly optimal solutions to see where ticket uses can be cut
- potential approaches:
    - from a given optimal solution, we trace backwards to find the MOST RECENT ticket use. from there, we branch off (solve again) while FORBIDDING ticket uses. in one case, we find a solution (almost certainly in more turns, which is okay) and we can stop. in the other case, we do not find any solution and we scrap this thread (metaphorically speaking). continuing from there gets very tricky, as if we backtrack to the NEXT most recent ticket use, we are in the same position we were in for option 1, requiring us to solve the game with resources in mind.
    - from a given optimal solution, we trace FORWARDS to find the FIRST ticket use. we will skip this use (forbid that specific solution space), and check the other options. from those options, we check as usual to see if a solution exists where we skipped that ticket use. if no solution exists, we can go back to where we skipped initially. from that point, we split as we did initially, only this time we look for the next ticker use in each thread. this is a recursive approach where we trim ticket uses from the start rather than from the end.

option 3:
- the same as option 1, but we scale up ticket use as necessary. first, we forbid all ticket use to see if a solution exists. if none does, we then allow ONE ticket, and recompute. repeat, adding one ticket each time until a solution is found. in the worst case, this performs identically to option 1 (infeasible), but in best case it performs identically to the version of the algorithm that does not track tickets. 
```