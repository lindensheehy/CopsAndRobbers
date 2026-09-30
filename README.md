# Cops and Robbers: Scotland Yard Analysis

A solver for the *Cops and Robbers* pursuit-evasion game on graphs, extended toward the rules of the board game *Scotland Yard*. The main question it answers is:

> Given a graph, **k** cops, and a robber the cops can only see **once every p moves**, can the cops force a capture? If so, where should they start, and how long does it take?

## Team

* **Linden Sheehy**: Project Manager, Business Analyst, Lead Developer
* **Valeriy Popov**: Architect, QA & Build Manager, Lead Developer

## The game

* The graph is undirected, with up to 255 vertices.
* The cops choose their starting vertices first. The robber then chooses a start, knowing where the cops are.
* Play alternates. On their turn, each cop moves along an edge. Then the robber moves along an edge. In all current solvers, neither side may stay in place.
* The cops win if any cop ever occupies the robber's vertex.
* **1/p visibility:** the cops see where the robber is at the start of every *p*-move cycle. For the rest of the cycle they only know which vertices the robber could have reached. `p = 1` is the classic full-information game. *Scotland Yard*, where Mr. X surfaces every few moves, is the motivating case.

The solver reports **WIN** or **LOSS** for the cops. On a WIN, it also reports the best cop starting positions and the worst-case capture time in plys (half-turns).

## How the solver works

The solver uses **retrograde analysis** (backward induction) over an auxiliary state graph:

1. **Build the state space.** A state is a pair *(C, r)*. *C* is a cop configuration, meaning the multiset of *k* cop positions stored in sorted order so symmetric states aren't duplicated. *r* is the vertex where the robber was last seen. Cop configurations and the moves between them are generated once in `AuxGraph`.
2. **Seed the captures.** Every state where the robber is on a cop's vertex is a win at cost 0.
3. **Evaluate macro-turns.** Each unsolved state is expanded over a full *p*-step visibility cycle. A depth-first search tries every sequence of cop moves. Alongside it, the solver tracks a **robber cloud**: the set of vertices the robber could be on. At each step the cloud expands by one hop, then any vertex a cop occupies is removed. The state is a win for the cops if some sequence of moves either
   * empties the cloud partway through the cycle (every possible robber path was intercepted), or
   * reaches the end of the cycle with every remaining robber vertex landing in a state that is already solved as a win.

   Branch-and-bound pruning keeps the search tractable. Cop moves are tried nearest-first (using all-pairs shortest-path distances), and any branch that can't beat the best cost found so far is dropped.
4. **Repeat until nothing changes.** Sweeps continue until a full pass marks no new states. Once a state is solved, its cost is never changed, so the recorded capture times are exact.
5. **Report the verdict.** The cops win if some starting configuration is a win against *every* robber start. The best such configuration is the one with the lowest worst-case capture time.

Because the whole cycle is evaluated as one step, a robber can never "pass through" a cop partway through a cycle without being caught. Earlier designs that stored each intermediate step as a separate state had this problem.

The state space grows roughly as *n<sup>k+1</sup>*, so memory and speed are the main engineering constraints. The core is written in C++ with a custom arena allocator, bit-packed state entries, and bitset robber clouds. The search does not allocate memory while it runs. Solved state tables are cached to disk so a repeated run can skip the work.

### Solver versions

All versions in [solvers/](solvers/) solve the same problem. They differ in how they do it:

* `k_cops_visibility`: the first generalization to arbitrary *p*. It stores each intermediate step as its own state, which makes it too pessimistic for *p > 1*.
* `k_cops_visibility_v2`: the macro-turn design described above. This is the current reference solver.
* `k_cops_visibility_v3`: a rewrite of v2 to pay down technical debt and improve performance. It moves the bitset and adjacency logic into reusable `VertexBitField` / `PackedAdjMatrix` types and replaces the recursive search with an explicit stack. This version is still in progress.

Full specifications are in [docs/](docs/).

## Building

Requirements: `g++` with C++17 support and Python 3. Development is done on Windows with MSYS2 UCRT64.

```
python build.py
```

The build does the following:

* It compiles everything in [lib/src/](lib/src/) to object files in `build/obj/`.
* It builds one executable per file in [solvers/](solvers/), written to `build/bin/<solver>.exe`. Each solver is compiled into the shared driver [app/main.cpp](app/main.cpp) using the `-DALGORITHM_INCLUDE=...` compiler flag.
* It only rebuilds files that have changed.
* If one solver fails to compile, the build reports it and continues with the others.

### Adding a solver

Create a new `.cpp` file in `solvers/` that defines the six pipeline steps called by `app/main.cpp`: `loadGraphFile`, `buildAuxGraph`, `initializeCaptures`, `mainLoop`, `findFinalResult`, and `outputData`. Each returns `false` on success. The next build picks up the new file automatically.

## Running

From the command line:

```
build/bin/k_cops_visibility_v2.exe <graph_file.txt> <num_cops> [visibility_p]
build/bin/k_cops_visibility_v2.exe assets/matrices/peterson.txt 3 1
```

`p` defaults to 1 (full visibility). The output includes a profiler breakdown of time spent in each step. Solved tables are cached in `cache/` under names like `<solver>__<graph>__<k>-cops__<p>-vis__<flags>__auxgraph.bin`.

There is also a small Tkinter launcher:

```
python run.py
```

It lists the executables in `build/bin/` and lets you pick a graph file, `k`, and `p`. It remembers your last inputs for each tool in `assets/master_cache.json`.

If the executable fails to start because of missing `libstdc++-6.dll` / `libgcc_s_seh-1.dll`, put your MSYS2 `ucrt64/bin` directory on your `PATH`.

## Testing

```
python test.py                               # run every solver on every graph
python test.py --exe k_cops_visibility_v2    # run one solver
python test.py --graphs cycle,grid,peterson  # run a subset of graphs
python test.py --include-big                 # include scotlandyard, cycle100, etc.
python test.py --timeout 300
```

[test.py](test.py) runs each solver on every graph in `assets/matrices/`, with *k* = 1–3 and *p* = 1–3, and writes results to `console_outputs/`:

* `<solver>_test_results.csv`: raw results.
* `<solver>_test_results_formatted.csv`: a readable, tab-separated version laid out like `console_outputs/reference.csv`.

After the runs, it compares solvers against known correctness rules. For example, at `p = 1` all solvers must agree exactly.

The per-run harness is [util/test_k_cops_visibility.py](util/test_k_cops_visibility.py). You can also run it on its own. It estimates memory for each run, skips runs that exceed `--state-budget`, and stops runs that exceed a time limit. It looks for the MSYS2 runtime in `C:\msys64\ucrt64\bin` by default. To use a different location, set the `K_COPS_DLL_DIR` environment variable.

## Graph files

Graphs are stored in [assets/](assets/):

| Folder | Format | Used by |
|---|---|---|
| `matrices/` | Adjacency matrix: one line per vertex, each a string of `0`/`1` characters | Solvers (input) |
| `positions/` | One `x,y` pixel coordinate per line, one line per vertex | Visualization |
| `edges/` | One edge per line as `u v`, 1-indexed | Building matrices |
| `degrees/` | One vertex degree per line | Reference |

The graph library includes the following:

* Standard test families: cycles, lines, trees, and grids.
* Graphs with known cop numbers: Petersen, dodecahedron, Robertson, and rook's graphs.
* Dismantlable, planar, and outerplanar graphs.
* The *Scotland Yard* board, both in full (`scotlandyard-all`) and split by transport type (`-yellow`, `-green`, `-red`). The board image is in `assets/ScotlandYardBoard.png`.

## Repository layout

```
app/          main.cpp: the shared driver that runs each solver's pipeline and profiles it
solvers/      One solver per file; each is built into its own executable
lib/          Shared C++ library
  Graph, AdjacencyList, PackedAdjMatrix   graph loading and representations
  AuxGraph                                cop configurations, moves between them, and the state table
  VertexBitField                          fixed-size vertex bitset (robber clouds)
  Allocator, Profiler, CacheManager, fileio
docs/         Solver specifications and design notes
util/         Test harness and Python debugging/visualization tools
assets/       Graphs, positions, and older cached results
console_outputs/  Test results
minutes/      Meeting notes from each project meeting (raw and formatted)
legacy/       Earlier Python prototypes and C++ solvers, kept for reference
build.py / run.py / test.py
```

### Visualization and debugging tools

[util/](util/) contains Python tools for looking inside solver output. They require `numpy`, `networkx`, and `matplotlib`.

* `perfect_game_replay.py`: steps through an optimal game on the graph.
* `play_game_robber.py`: lets you play as the robber against the solved cop strategy.
* `perfect_game_extractor.py`: exports an optimal game to JSON (see `assets/cached_solutions/`).

> **Note:** these tools were written for an older cache file layout and have not been updated for the current `CacheManager` format. They are debugging aids rather than part of the solving pipeline, and will be updated later.

## References

* A. Berarducci and B. Intrigila, *On the cop number of a graph*, Advances in Applied Mathematics, 1993.
