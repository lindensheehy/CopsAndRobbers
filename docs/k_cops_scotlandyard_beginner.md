# Solving Scotland Yard (beginner rules), explained

This document explains, without assuming any background in algorithms, how
`solvers/k_cops_scotlandyard_beginner.cpp` works out whether the cops can always catch the robber
in the beginner version of Scotland Yard, and why we can trust its answer.

## 1. The question

The rules we use are in [beginner_rules/beginner_rules_brief.md](beginner_rules/beginner_rules_brief.md).
In short:

- The board is the Scotland Yard map: 199 stations, connected by taxi and bus routes
  (the underground is not used).
- The robber (Mister X) always starts on station 82. With 3 cops, they start on 41, 46 and 124.
  With 4 cops, also 142.
- The game lasts 13 rounds. In each round the robber moves first, then every cop moves.
  Nobody may stay where they are.
- Two cops may never share a station. Cops move one at a time, in whatever order they like,
  so a cop can move into a station another cop just left, but two cops can never swap places.
- The robber may never step onto a cop. If he has nowhere free to go, the cops win.
- The cops win the moment one of them lands on the robber's station.
- The robber is visible, except in rounds 3, 8 and 13. In those rounds his move is secret,
  so the cops have to make their move without knowing where he went. He is revealed again
  before the cops' next move.
- If all 13 rounds pass without a capture, the robber wins.

The question the solver answers is:

> **If both sides play perfectly, can the cops guarantee a capture?**

"Guarantee" is the important word. We are not asking whether the cops *usually* win, or win
against a careless robber. We are asking whether there is a way for the cops to play that wins
**no matter what the robber does**. If there is, the answer is WIN. If the robber has some way
to survive no matter what the cops do, the answer is LOSS. (For a game like this, where both
sides can see everything except the robber's secret moves, one of the two is always true.)

## 2. Why "just try every possible game" doesn't work

The obvious approach is to play out every possible game: try every robber move, then every cop
reply, then every robber move after that, and so on to the end.

The problem is how fast that grows. Each round the robber has about 4 choices and the 3 cops
together have about 70 (each cop has around 4 places to go, and the combinations multiply).
That is about 280 possibilities per round. Over 13 rounds that is 280 multiplied by itself 13
times, which is roughly 10^32 games. A computer checking a billion games per second would need
far longer than the age of the universe.

The saving grace is that **most of those games pass through the same positions**. The cops can
reach the same three stations in round 6 by thousands of different routes, and from that point
on it doesn't matter how they got there. What matters is only:

- where the cops are,
- where the robber is, and
- which round it is.

We call that combination a **position**. There are far fewer positions than games: about
1.3 million ways to place 3 cops, times 199 places for the robber, times 13 rounds, which is a
few billion. A computer can handle that.

## 3. The key idea: work backwards from the end

Instead of starting at the beginning and asking "where can the game go from here?", the solver
starts at the **end** of the game and works backwards one round at a time.

This is the same trick people use for puzzles like mazes (start from the exit and trace back) or
for planning backwards from a deadline ("to be at the airport at 9, I need to leave at 8, so I
need to be up by 7..."). The end of the game is easy to reason about, and each earlier round
only depends on the round after it.

### What the solver writes down

For each round, the solver fills in a table. The table has one row for every possible way to
place the cops (about 1.3 million rows for 3 cops). Each row holds a **checklist of the 199
stations**, with a tick next to every station where, if the robber stood there at the start of
that round, the cops could force a capture before the game ends.

So the table for round 10 answers questions like: "It's the start of round 10. The cops are on
stations 13, 94 and 138. If the robber is on station 107, can the cops still guarantee a win?"
The answer is just whether station 107 has a tick in that row.

### The last step: after round 13

After round 13 the game is over. If the robber hasn't been caught, he has won. So the
"after round 13" table has **no ticks at all**. This is the starting point for working backwards.

### A normal (visible) round

Suppose we already have the finished table for round 11 and want to fill in round 10. Take one
row: the cops are on some three stations, call them C. For each station r the robber might be
on, we ask:

1. The robber moves first. He can go to any neighbouring station that doesn't have a cop on it.
2. Then the cops, who can see where he went, pick their reply.

The cops win from r if **every** move the robber can make has **some** cop reply that either:

- lands a cop on the robber (an immediate capture), or
- leads to a position that the round-11 table already says is a win.

If the robber has even one move with no good reply, he will take it, and r gets no tick.

The order matters. The robber chooses first, so he will pick whatever is worst for the cops.
The cops reply knowing his choice, so they only need *one* good answer to each of his moves.

### A hidden round (rounds 3, 8 and 13)

In a hidden round the cops don't see where the robber went. They have to choose one move that
works **whichever** of his possible moves he made. So the rule becomes:

The cops win from r if there is **one single** cop move that, **for every** station the robber
could have moved to, either lands a cop on him or leads to a winning position in the next
round's table.

That is a much stronger requirement. In a visible round the cops can tailor their reply to the
robber's move; in a hidden round one reply has to cover every possibility at once. This is
exactly why hidden rounds help the robber.

It also explains why the hidden rounds don't make the problem much harder to solve. Because the
robber is shown again straight after, the cops are only ever unsure about **one** move. Where
he might be is always just "one of the free neighbours of the station he was last seen on".

### Repeating all the way back to round 1

The solver fills in round 13 from the empty "after the game" table, then round 12 from round 13,
then round 11 from round 12, and so on down to round 1. Each round only needs the table for the
round after it.

Once round 1 is done, the answer is a single lookup: in the row for the cops' starting stations
{41, 46, 124}, is station 82 ticked? If yes, the cops can guarantee a capture. The solver also
reports, for each possible first move of the robber, a cop reply that keeps the win.

## 4. "But there is only one starting position. Isn't that wasteful?"

It's a fair question. Working backwards fills in the table for *every* placement of the cops and
the robber, while we only care about one starting position.

The answer is that you can't know whether the starting position is a win without knowing about
the positions it leads to, and those spread out very fast. Here is how many different placements
of the 3 cops are actually possible at the start of each round, starting from {41, 46, 124}
(out of 1,293,699 in total):

| Round | Possible cop placements |
|------:|------------------------:|
| 1     | 1                       |
| 2     | 392                     |
| 3     | 17,950                  |
| 4     | 162,746                 |
| 5     | 453,939                 |
| 6     | 806,054                 |
| 7     | 1,121,338               |
| 8     | 1,242,402               |
| 9     | 1,287,729               |
| 10–13 | essentially all of them |

From round 9 onwards, practically every placement of the cops can happen. It's the same story
with 4 cops: 1 placement in round 1, about 7.6 million in round 4, and all 63.4 million from
round 11. Any method that
answers the question correctly has to deal with nearly all of them in the second half of the
game, whether it works forwards or backwards.

Only the early rounds would be wasted, so the solver avoids that waste with a cheap first step:
before working backwards, it works *forwards* once, simply marking which cop placements (and which
robber stations) can occur in each round. The backwards pass then skips everything that can never
happen. This changes how long the solver takes, never the answer, because the skipped positions
are never needed.

## 5. Why it fits in memory

Each round's table only depends on the next round's table. So the solver never keeps more than
**two** tables at once: the one it is reading from and the one it is filling in. Once round 10 is
done, round 11's table is no longer needed and its memory is reused for round 9.

Each row is a checklist of 199 ticks, which a computer stores in 32 bytes. So:

|        | Cop placements | One table | Memory used (two tables) |
|--------|---------------:|----------:|-------------------------:|
| 3 cops | 1.3 million    | 40 MB     | about 80 MB              |
| 4 cops | 63.4 million   | 2 GB      | about 4 GB               |

A forwards search would have to remember results from every round at once, because it jumps
back and forth between rounds as it explores. For 4 cops that would be around 40 GB.

The checklists also make each step fast. Instead of asking "can the cops win if the robber is on
station 1? on station 2? ..." 199 separate times, the solver works with all 199 ticks in one go,
using the computer's ability to combine 64 yes/no values in a single operation.

## 6. How cop moves are worked out

The cops move one at a time, in any order, and can never share a station. That rules out some
combinations that look fine at first glance. The solver works out the legal joint moves like this:

1. Each cop picks a neighbouring station.
2. No two cops may pick the same station.
3. If cop A wants to move onto the station cop B is standing on, then B has to move first. If
   B also wants A's station, they would each be waiting for the other. That's a swap, and it's
   impossible. The same goes for three cops going round in a circle. Any other combination can
   be done in some order, so it is legal.

If the cops have no legal joint move at all, the brief says they lose. (On the real board this
never happens, with 3 or 4 cops: the solver counts it, and the count is zero.)

One small simplification: the solver treats the cops' turn as one complete joint move, and counts
a capture if any cop ends up on the robber. In the real game, the moment a cop lands on the robber
the game ends and the remaining cops don't need to move. These two only differ if a cop could step
onto a station but no complete legal joint move includes that step. The solver counts how often
that happens on the real board. With 3 cops the count is zero, so the simplification does not
affect that answer. (The 4-cop run was done before this count was added, so it hasn't been
measured for 4 cops yet; it is printed on every run.)

## 7. How we know it's right

A clever method is only useful if it gives the right answer, so the solver is checked against a
completely separate program, `util/test_k_cops_scotlandyard_beginner.py`. That checker:

- is written directly from the rules, in the most straightforward way possible: it plays the game
  forwards by trying every move, and remembers positions it has already seen;
- works out cop moves differently too, by literally trying every order the cops could move in,
  one cop at a time;
- shares no code with the solver.

It is far too slow for the full game, but it can solve small ones. The test runs both programs on
hundreds of games and checks that they agree:

- 400 games on small random maps (4 to 10 stations, 1 to 4 cops, 1 to 7 rounds, with random rounds
  hidden, and random starting positions). Many of these maps are deliberately cramped, so the
  "cops are stuck" rule really gets exercised.
- 40 short games (1 to 4 rounds) on the real Scotland Yard board.

Every run so far agrees on every game (three runs with different random choices, 1,320 games in
total, with both cop wins and robber wins well represented).

To run the test:

```
python util/test_k_cops_scotlandyard_beginner.py
```

## 8. Results

| Cops | Result | Time |
|------|--------|------|
| 3 (on 41, 46, 124) | **WIN**: the cops can guarantee a capture | about 11 seconds, 80 MB |
| 4 (on 41, 46, 124, 142) | **WIN**: the cops can guarantee a capture | about 27 minutes, 4 GB |

(Times are on a 20-thread desktop. For 4 cops, about 7 minutes is the forwards "what can happen"
pass and 20 minutes the backwards pass; the hidden rounds are the slowest, because one cop move
has to be checked against every place the robber might be.)

A 4th cop is not automatically an advantage: it must move every turn and can get in the other
cops' way. So the 4-cop result is a separate answer, not something that follows from the 3-cop one.

With 3 cops, we also asked how short the game could be and still be a guaranteed win, by
re-running with fewer rounds. With 7 rounds the robber can escape; with 8, the cops win. So 3
perfect cops can always catch the robber **by the end of round 8**. (A win in an 8-round game is
automatically a win in any longer game, because the extra rounds come after the capture has
already happened.)

## 9. Running it

Build it the usual way (`python build.py`), then:

```
build/bin/k_cops_scotlandyard_beginner.exe assets/matrices/scotlandyard-beginner.txt 3
```

Replace `3` with `4` for the four-cop game. `assets/matrices/scotlandyard-beginner.txt` is the
board with taxi and bus routes only.

Everything else defaults to the beginner rules, but can be changed with environment variables.
Station numbers are the ones printed on the board:

| Variable | Meaning | Default |
|----------|---------|---------|
| `SYB_ROBBER` | robber's starting station | `82` |
| `SYB_COPS` | cops' starting stations, e.g. `41,46,124` | `41,46,124` (plus `142` for 4 cops) |
| `SYB_ROUNDS` | number of rounds | `13` |
| `SYB_HIDDEN` | hidden rounds, e.g. `3,8,13`, or `none` | `3,8,13` |
| `SYB_THREADS` | how many processor cores to use | all of them |

The result is printed and also saved to
`cache/k_cops_scotlandyard_beginner__<graph>__<k>-cops__result.txt`.

## 10. What is not modelled

- The robber's black ticket and double-move ticket (out of scope in the brief).
- The rule for stuck cops is our own addition (the official rules don't say what happens).
- The answer is about perfect play on both sides. It says nothing about how a human robber or
  human cops will actually play.
