# Scotland Yard Solver

This doc outlines the idea for making a solver **specifically for the Scotland Yard graph**.

The plan is as follows.

## 1. Building the oracle

We first start by solving the graph with 4 cops and full visibility. This gives us an "oracle" which we can use moving forward.

### 1.1 Rewriting the full visibility solver

We will need to rewrite the full visibility solver to fit some constraints, as follows:

**1.1.1** The solver will need to match the 6 function pattern the other solvers use, so it can properly be compiled and run in our current framework.

**1.1.2** It will need to be lean on memory. The state space of this algorithm is $ {200 \choose 4} \times 200 = 12,936,990,000 $. Assuming 1 byte per state (1 bit for the `isMarked` bool + 7 bits for the ply count of the capture sequence), this requires 12.049GB of memory at runtime. This is a significant constraint, so we will need to be careful about what else we try to cache.

**1.1.3** We need a way to export and store the results. For each state $(c1, c2, c3, c4, r)$ we must store the best move for each robber as $(c1', c2', c3', c4')$.

### 1.2 Storing the best moves

We have some flexibility about how exactly we may store those best moves, as follows:

**1.2.1** We may be able to drop the key values $(c1, c2, c3, c4, r)$ from the output, using the implicit mapping through indexing like we use at runtime. This immediately cuts our footprint to 4/9 of the expected.

**1.2.2** In the worst case, we would store the actual node ids of each best move $(c1', c2', c3', c4')$. With 200 total nodes, this requires 8 bits per value, or 4 bytes per set. With 12,936,990,000 states at 4 bytes each, this requires $ 12.049GB \times 4 = 48.194GB $. This is not awful, but we may be able to do better, as the next section explains.

**1.2.3** The approach of storing the node ids is quite excessive. It gives 200 options per value, but in practice the max degree is about 7, which means any given cop position $C$ can only have at most 7 possible values of $C'$. We need to do a lot of precomputation to leverage this pattern, but in a case where we have an upper bound on the max degree of 8, we only need 3 bits per best cop move. Times 4, that's 12 bits per state, which would require only 18.073GB. Depending on how expensive the lookup of the edge mapping is, this may or may not be viable.

### 1.3 How would we do that, though?

**1.3.1** To accomplish this, we would need to create a mapping per node where each 3 bit value maps to its respective node. We would only need $ (E \times 2) $ bytes to store this, assuming we use a 1 byte variable to store each 3 bit value so the alignment is convenient for the CPU. That part is trivial.

**1.3.2** Next, we would need to find a way to reliably map these per node edge ids to their respective nodes. We can simply add this as a field onto the existing structure, which doubles our footprint but remains tiny. Even if we use some significant memory overhead to streamline access (ex. head pointers), this should not be a concern on memory.

## 2. Using the oracle

So now what do we do with that? Let's assume we ran the full visibility solver and have our oracle that we can reference as needed. Here comes the interesting part, because this can dramatically decrease the average branching factor for our DFS trees.

### 2.1 Some context first

To accurately solve Scotland Yard with the rules regarding the non-uniform $p$ (visibility parameter), we need to fundamentally overhaul the algorithm.

**2.1.1** The old algorithm **relies** on the uniform value of $p$. As soon as it's variable, the logic falls apart, because suddenly you have no sure way to know what visibility periods apply until you find the length of the capture.

**2.1.2** The solution: since all capture lengths have different rules regarding visibility, one possible option is to simply solve based on capture length. This means we loop forwards from capture length 0 all the way to 24 (the end of the game, after which captures cannot happen).

**2.1.3** Iteration 0 is trivial, as this is already part of our current algorithm. It consists of simply marking all states where the robber coincides with a cop.

**2.1.4** Iterations 1-24. This is the fun part. So what do our initial states represent in this context? They represent the **start** of the game. So we pick some state (c1, c2, c3, c4, r) that is currently unmarked, and we ask "can the cops win in $L$ moves?" for $1 \le L \le 24$. Let's run through some examples.

**2.1.5** Iteration 2. Visibility sequence as follows:

- Turn 1
    - Cops' turn (**visible**)
    - Robber's turn (**turns invisible**)
- Turn 2
    - Cops' turn (invisible)
    - Robber's turn (invisible)

And all we're asking is "can the cops win in those 2 turns?" If yes, we mark it. If no, we skip it and revisit it on the next iteration for $L' = L + 1$.

**2.1.6** Iteration 10. Visibility sequence:

- Turn 1
    - Cops' turn (**visible**)
    - Robber's turn (**turns invisible**)
- Turn 2
    - Cops' turn (invisible)
    - Robber's turn (invisible)
- Turn 3
    - Cops' turn (invisible)
    - Robber's turn (**turns visible**)
- Turn 4
    - Cops' turn (**visible**)
    - Robber's turn (**turns invisible**)
- Turns 5-7
    - Cops' turn (invisible)
    - Robber's turn (invisible)
- Turn 8
    - Cops' turn (invisible)
    - Robber's turn (**turns visible**)
- Turn 9
    - Cops' turn (**visible**)
    - Robber's turn (**turns invisible**)
- Turn 10
    - Cops' turn (invisible)
    - Robber's turn (invisible)

Same as last time, all we're asking is "can the cops win in those 10 turns?" and we move on if not.

**2.1.7** Notice something? The sequence never changes from the root. For this reason, the entire pattern of iterating over increasing values of $L$ is somewhat unnecessary, though it's a helpful mental model for a first version. Regardless, the issue is not the outer loop, but rather the trees we will produce through this DFS...

### 2.2 Worst case leaf count

So what's our plan here? First, it's worth looking at what the depth 24 DFS can produce in the worst case, so let's find a reasonable upper bound on the number of leaf nodes. In the full 24 round interval, we have the following:

- **Invisible robber turns.** Of the 24 rounds, we have 19 robber turns where the robber is invisible or turns invisible. These always have a branching factor of 1, because the expansion happens to the robber set rather than the DFS tree itself. This is good news, but we pay it back elsewhere. This results in a factor of 1 to the result.
- **Reveal turns.** Of the 24 rounds, we have 5 robber turns where the robber was invisible and becomes visible. These produce a branching factor on each of those plies, as follows. They happen on turns 3, 8, 13, 18, and 24, with $p$ values of 3, 5, 5, 5, and 6 respectively. Assuming an average degree on the graph of 4, we can give a reasonable estimate by using $4^p$ for each of those turns. This is a somewhat lazy bound because it assumes overlapping nodes are counted, but it's functional for an estimate. This gives $4^3 \times 4^5 \times 4^5 \times 4^5 \times 4^6 = 4^{24} = 281,474,976,710,656$ as the total branching factor of these 5 robber turns. That's $2.8 \times 10^{14}$, but let's call it $10^{14}$ for simplicity.
- **The really ridiculous part: the cops' moves.** We have 24 turns of cop moves. We can estimate this the same way we did for the robber, using an average degree of 4 to the power of the number of cops: $4^4 = 256$. That's per move, so we raise that to the power of 24: $256^{24}$. That equals $10^{57}$.

This gives us a total leaf node estimate of roughly $10^{71}$. Not great, but let's see what we can do with it...

### 2.3 Why we aren't completely screwed (maybe)

Of those cop moves, a **lot** of them are going to be dumb, and even one single extra branch based on a dumb move can compound into a massive number of leaf nodes.

**2.3.1** We used that average degree to find the branching factor. That's helpful for an estimate, and mostly accurate for an exhaustive search, but we don't **need** to be exhaustive. Leveraging the oracle outlined above, we have a fantastic base set of which moves are good.

**2.3.2** Situation 1: on a turn where the robber is visible, we don't need to branch at all. The oracle tells us exactly which moves are best for each cop. The branching factor collapses to 1 on these turns, which make up 5 of the 24 total cop turns, dropping our partial factor from $256^{24}$ to $256^{19}$. That's about $10^{45}$, for a total of roughly $10^{59}$.

**2.3.3** Situation 2: on a turn where the robber is invisible, we have a set of nodes where the robber may be. This is where the real optimization lies, because there's a significant chance that our oracle will allow us to trim some of the options for the cops. And this is completely conceptually valid, because it strictly does not prune any branches that may be useful. Let's play out some example numbers to see what this earns us. We used an average degree of 4 before, but let's change that to 2 for this example. That represents that of the 4 possible options for each cop, only 2 of them are "best" for the set of positions the robber may occupy, so we prune the other 2. This results in $2^4 = 16$ options per move across the 19 relevant moves, so $16^{19} = 10^{22}$. Putting that together with our robber factor of $10^{14}$, we are left with $10^{36}$ leaf nodes.

**2.3.4** What does that number mean for us? Well, let's make an estimate. With $10^{36}$ leaf nodes, we will have to visit twice that many nodes in total. For our purposes we will leave the number as is, since a factor of 2 is pretty negligible in this context. We will consider a CPU with a clock speed of 3GHz. That's 3,000,000,000 clock cycles per second, per core. Let's assume we spend 100 clock cycles per node, which is somewhere between optimistic and realistic. That means we can compute 30,000,000, or about $10^7$, nodes per second. Dividing our node count by this, we get $10^{29}$ seconds, or about $10^{21}$ years. Needless to say, this is not sufficient, but let's work with this as our current baseline.

### 2.4 That bound is generous

We should consider a major caveat: that leaf node count is a pretty generous upper bound. It assumes that every single starting node always requires the full depth of 24.

In practice, we might expect any of the following:

- **Case 1: the worst case.** This is unlikely, but technically possible. In this case, a significant number of the root nodes would require us to search the full depth 24 tree, and we find that our runtime is comparable to that upper bound. In other words, there's not much we can do about this, and we will not be able to solve the graph with this approach.
- **Case 2: we get super lucky.** It might happen that the cops can always catch the robber in only, say, 13 moves. In this case, our estimate gets absolutely slashed. We take that estimate and divide it by $4^5 \times 4^6$ for the robber's moves on turns 18 and 24, then divide that by $16^9$ for the cop moves past round 13. This gives us about $10^{11}$ seconds of expected runtime, compared to the baseline $10^{29}$.
- **Case 3: the likely reality.** I think we can reasonably expect that the worst case capture takes, say, 18 moves. In this case, we still find some significant savings. Specifically, we gain back $4^6$ for the last robber turn and $16^5$ for the last 5 cop turns, giving us an estimate of about $10^{19}$ seconds of runtime. We will work with this number moving forward.

### 2.5 The pool shrinks as we go

We do have another piece of help up our sleeve, due to the structure of how the algorithm executes. We would mark many nodes immediately, then more at depth 1, and so on. By the time we need to do a search at our highest depth, our pool of nodes has shrunk massively. Of the $10^{10}$ starting nodes, we might expect that only $10^4$ of them require the worst depth. This immediately wins a factor of $10^6$, dropping us down to $10^{13}$ seconds of runtime. There's no way to accurately predict how significant this would be, but it's definitely worth noting.

### 2.6 Optimizing

For our purposes, we're going to anchor on a runtime of $10^{13}$ seconds. That's still about 300,000 years, so we're not done yet. How do we optimize? Well, we have some options.

**2.6.1** Most obviously, the CPU does not have only one core. I mentioned above that we get 3,000,000,000 clock cycles per second **per core**. Immediately, the CPU can become 10x more powerful by adding multithreading, which in this context is fairly straightforward. This doesn't make the problem easy, but it's a step.

**2.6.2** What's going to have to be our saviour: the GPU. Based on our experimental version using the GPU, there are significant gains to be found here. The previous version was very much a proof of concept, and I'm optimistic that we can find much better performance with further development. I think we may reasonably find a speedup on the order of $10^3$. Again, it's a step. This is sort of independent of the gains from using multiple CPU cores, but they may or may not be able to compound on each other.

**2.6.3** Cloud computing. We could feasibly purchase several hours or days of compute on a massively powerful instance. This could buy us a speedup perhaps somewhere on the order of 10-100x.

**2.6.4** Let's assume all of those work in our favor, and we get the estimated $10 \times 10^3 \times 10^3 = 10^7$ improvement to runtime. On our $10^{13}$ second estimate, that leaves us at just $10^6$ seconds, or a bit under 300 hours (about 12 days) of runtime. That is completely feasible, so we just need to hope that our estimate is realistic.
