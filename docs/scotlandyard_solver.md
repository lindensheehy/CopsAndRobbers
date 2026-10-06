# Scotlandyard Solver

This doc outlines the idea for making a solver **specifically for the scotlandyard graph**.

the plan is as follows:
1. we first start by solving the graph with 4 cops and full visibility. this gives us an "oracle" which we can use moving forward.

1.1 we will need to rewrite the full visibility solver to fit some contraints, as follows:

1.1.1 the solver will need to match the 6 function pattern the other solvers use, so it can properly be compiled and run in our current framework

1.1.2 it will need to be lean on memory. the state space of this algorithm is: $ {200 \choose 4} \times 200 = 12,936,990,000 $. assuming 1 byte per state - 1 bit for the `isMarked` bool + 7 bits for the ply count of the capture sequence - this requires 12.049GB of memory at runtime. this is a significant constraint, so we will need to be careful about what else we try to cache.

1.1.3 we need a way to export and store the results. for each state $(c1, c2, c3, c4, r)$ we must store the best move for each robber as $(c1', c2', c3', c4')$.

1.2 we have some flexibility about how exactly we may store those best moves, as follows:

1.2.1 we may be able to drop the key values $(c1, c2, c3, c4, r)$ from the output, using the implicit mapping through indexing as we use at runtime. this immediate cuts out footprint to 4/9 of the expected.

1.2.2 in worst case, we would store the actual node ids of each best move $(c1', c2', c3', c4')$. with 200 total nodes, this requires 8 bits per value or 4 bytes per set. with 12,936,990,000 states at 4 bytes each, this requires $ 12.049GB \times 4 = 48.194GB $. this is not awful, but we may be able to do better as the next section explains.

1.2.3 the approach of storing the node ids is quite excessive. this gives 200 options per value, but in practice the max degree is about 7, which means any given cop position $C$ can only have at most 7 possible values of $C'$. we need to do a lot of precomputation to leverage this pattern, but in a case where we have an upper bound on the max degree of 8, we only need 3 bits per best cop move. times 4 this 12 bits per state, which would require only 18.073GB. depending on how the expensive the lookup of the edge mapping is, this may or may not be viable.

1.3 how would we do that though?

1.3.1 how would we do that though? to accomplish this we would need to create a mapping per node where each 3 bit value per node maps to its respective node. we would only need $ (E \times 2) $ bytes to store this, assuming we use an 1 byte variable to store each 3 bit value so the alignment is conveinent for the cpu. that part is trivial.

1.3.2 next we would need to find a way to reliably map these per node edge ids to their respective nodes. we can simply add this as a field onto the existing structure, which doubles our footprint but remains tiny. even if we use some significant memory overhead to streamline access (ex. head pointers), this should not be a concern on memory.

2. so now what do we do with that? lets assume we ran the full visibility solver and have our oracle that we can reference as needed. here comes the interesting part, because this can dramatically decrease the average brancing factor for our dfs trees.

2.1 first some context. to accurately solve scotlandyard with the rules regarding the non uniform p (visibility parameter), we need to fundamentally overhaul the algorithm

2.1.1 the old algorithm **relies** on the uniform value of p. as soon as its variable, the logic falls apart because suddenly you have no sure way to know what visibility periods apply until you find the length of the capture.

2.1.2 the solution. since all capture lengths have different rules regarding visibility, one possible option is to simply solve based on capture length. this means we loop forwards from capture length 0, all the way to 24 (the end of the game, after which captures cannot happen). 

2.1.3 iteration 0 is trivial, as this is already part of our current algorithm. it composes of simply marking all states where the robber coincides with a cop.

2.1.4 iterations 1-24. this is the fun part. so what do our initial states represent in this context? they represent the **start** of the game. so we pick some state (c1, c2, c3, c4, r) that is currenly unmarked, and we ask "can the cops win in $L$ moves?" for $1 \le L \le 24$. lets run through some examples:

2.1.5 iteration 2. visibility sequence as follows:

- Turn 1
    - cops turn (**visible**)
    - robbers turn (**turns invisible**)
- Turn 2
    - cops turn (invisible)
    - robbers turn (invisible)

and all were asking is "can the cops win in those 2 turns?" if yes, we mark it. if no, we skip and we will revisit it on the next iteration for $L' = L + 1$.

2.1.6 iteration 10. visibility sequence:

- Turn 1
    - cops turn (**visible**)
    - robbers turn (**turns invisible**)
- Turn 2
    - cops turn (invisible)
    - robbers turn (invisible)
- Turn 3
    - cops turn (invisible)
    - robbers turn (**turns visible**)
- Turn 4
    - cops turn (**visible**)
    - robbers turn (**turns invisible**)
- Turns 5-7
    - cops turn (invisible)
    - robbers turn (invisible)
- Turn 8
    - cops turn (invisible)
    - robbers turn (**turns visible**)
- Turn 9
    - cops turn (**visible**)
    - robbers turn (**turns invisible**)
- Turn 10
    - cops turn (invisible)
    - robbers turn (invisible)

Same as last time, all were asking is "can the cops win in those 10 turns?" and we will move on if not.

2.1.7 Notice something? the sequence never changes from the root for this reason, the entire pattern of iterating over increasing values $L$ is somewhat unnecessary, though its a helpful mental model for a first version. regardless though, the issue is not the outer loop, but rather the trees we will produce through this dfs...

2.2 so whats our plan here? first its worth looking at what the depth 24 dfs can produce in worst case, so lets find a reasonable upper bound on the number of leaf nodes. in the full 24 round interval, we have the following:

- of the 24 rounds, we have 19 robber turns where the robber is invisible or turns invisible. these always have a branching factor of 1, because the expansion happens to the robber set rather than the dfs tree itself. this is good news, but we pay this back elsewhere. this results in a factor of 1 to the result.
- of the 24 rounds, we have 5 robber turns where the robber was insivible and becomes visible. this produces a branching factor on each of those ply, as follows. these happen on turns 3, 8, 13, 18, and 24, with p values of 3, 5, 5, 5, and 6 respectively. assuming an average degree on the graph of 4, we can give a reasonable estimate by using $4^p$ for each of those turns. this is a somewhat lazy bound because it assumes overlapping nodes are counted, but its functional for an estimate. this gives $4^3 \times 4^5 \times 4^5 \times 4^5 \times 4^6 = 4^{24} = 281,474,976,710,656$ as the total branching factor of these 5 robber turns. thats $2.8 \times 10^{14}$, but lets call that $10^{14}$ for simplicity.
- the really ridiculous part, **the cops moves**. we have 24 turns of cop moves. we can estimate this the same way we did for the robbers, using an average degree of 4 to the power of the number of cops - $4^4 = 256$. thats per move, so we put that to the power of 24 - $256^{24}$. that equals $10^{57}$. 

This gives us a total leaf node estimate of roughly $10^{71}$. not great...

2.3 **why we arent completely screwed (maybe)**. of those cops moves, a **lot** of them are going to be dumb, and even one single extra branch based on a dumb move can compound to a functionally infinite amount of leaf nodes.

2.3.1 we used that average degree to find the branching factor. thats helpful for an estimate, and mostly accurate for an exhaustive search, but we dont **need** to be exhaustive. leveraging that oracle outlined above, we have a fantastic base set of which moves are good.

2.3.2 situation 1. on a turn where the robber is visible, we dont need to branch at all. the oracle tells us exactly which moves are best for each cop. the brancing factor collapses to 1 on these turns, which make up 5 of the 24 total cop turns, dropping our partial factor from $256^{24}$ to $256^{19}$. thats about $10^{45}$ for a total of roughly $10^{59}$.