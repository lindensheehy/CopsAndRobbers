# CPU oracle-guided Scotland Yard tree solver

This implements the finite-horizon tree in `docs/scotlandyard_solver_tidy.md`, using the robber-cloud capture/expansion mechanics of `experimental/v3_2/search.hpp` and a read-only V5 full-visibility value oracle. It is a new standalone executable; it does not modify the existing solvers or regenerate the oracle.

The important correction to the proposal is that full-visibility recommendations guide the search; they do not restrict it to one preferred action. A move that is optimal with full information can be wrong when the robber will disappear. Even if every possible robber position is individually winning, the hidden cloud may be losing because the cops must choose one common action.

## Build on Windows

Extract this package into the repository root so this file is at `experimental/oracle_cpu/README.md`. Requires Python 3 and a 64-bit C++17 g++ compiler (the existing MinGW setup is suitable if it supports `std::thread`).

```powershell
cd D:\capstone\CopsAndRobbers
python .\experimental\oracle_cpu\build.py
python .\experimental\oracle_cpu\test.py
```

The executable is `build/bin/scotlandyard_oracle_cpu.exe`. The standalone build intentionally does not rebuild every solver. It also works on Linux; the `.exe` filename is retained there for consistent commands. No CUDA or Python packages are required.

## First check your existing oracle

```powershell
$solver = '.\build\bin\scotlandyard_oracle_cpu.exe'
$graph = '.\assets\matrices\scotlandyard-all.txt'
$oracle = '.\cache\fullvis_v5_strategy__scotlandyard-all__4-cops.bin'

& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph --preflight
& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph --query 5
```

Your recorded oracle has 199 vertices, 67,331,650 cop configurations, and exactly 13,398,998,414 bytes including its header. Its default start should decode to `[0,3,66,184]`. The query for robber vertex 5 should return an exact full-visibility value of 9 plies; ties can produce a different equally good first move from the logged trace. IDs are zero-based throughout.

`--oracle-graph` must identify the authentic original matrix used to generate the binary. V5 stores dimensions, encoding parameters and maximum degree, but **no graph hash, pass-rule flags, completion flag, or payload checksum**. The reader checks its 64-byte header and exact size, and compares the supplied original matrix with the search matrix. This cannot prove provenance or detect all corruption. Use the completed fixed-point file from the successful V5 save, not a partial/frontier file. Your pasted log reports that completion and save. Do not modify/replace the oracle while it is mapped.

## First bounded search

```powershell
& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph `
    --rounds 24 --reveals 3,8,13,18,24 --threads 1 --max-seconds 60 `
    --checkpoint '.\cache\sy_cpu_24rounds.checkpoint'
```

This tests the oracle's best full-visibility cop start against every initially visible robber start. It does **not** assume that this cop start is optimal under partial visibility. To resume with a longer budget:

```powershell
& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph `
    --rounds 24 --reveals 3,8,13,18,24 --threads 1 --max-seconds 600 `
    --checkpoint '.\cache\sy_cpu_24rounds.checkpoint' --resume
```

Checkpoints save **completed initial roots**, not the active DFS stack. An unfinished root restarts on resume; increase its budget to make further progress. If a root takes longer than the entire budget, repeatedly resuming with the same budget will not help. Changing game inputs invalidates the checkpoint. Changing thread count, time/node budget, or memo capacity is allowed. An existing checkpoint is never silently overwritten without `--resume`.

Begin with one thread. `--threads 4` parallelizes different initial roots; it does not parallelize one tree. Each thread gets its own bounded memo table. All threads share the read-only mapping. Mapping avoids explicitly copying the 12.479 GiB table into a heap buffer, but touched pages still consume RAM/file cache and random disk faults can dominate runtime. More threads are not automatically faster.

Other useful modes:

```powershell
# One initially visible robber, for profiling
& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph --robber 5 --max-seconds 60

# One initially hidden cloud: all positions must share one strategy until a reveal
& $solver $graph --cops 4 --oracle $oracle --oracle-graph $graph --hidden-start 5,6,7 --max-seconds 60

# Compare exact same problem without oracle guidance (explicit start required without oracle)
& $solver $graph --cops 4 --start 0,3,66,184 --oracle-mode off --max-seconds 60
```

Use separate checkpoint filenames for different oracle modes or game settings. `--oracle-mode order` orders moves using the oracle without pruning them; `bound` is the default when an oracle is supplied. `--max-nodes` gives a reproducible shared node budget with one thread. `--max-seconds 0` removes the time limit. Ctrl+C interrupts the search and preserves completed roots when the platform delivers SIGINT to the process.

## Exact game being searched

- Fixed sorted multiset of 1–4 cop positions. Multiple cops may occupy the same vertex.
- Each cop follows one adjacency edge each cop turn; the robber follows one edge each robber turn. Input self-edges are honored. No extra stay moves are inserted.
- Capture occurs at shared vertices, both on the cop move and on the robber move. Captured possibilities are removed **before** expanding the surviving robber cloud, then again afterward. A robber cannot escape after already being caught.
- The initial robber position is visible by default. Each initial singleton gets its own strategy. `--hidden-start` instead requires one strategy for the supplied set.
- A round is one joint cop turn followed by one robber turn. The cops move first. Reveals occur **after** robber moves 3, 8, 13, 18 and 24 by default. Once revealed, the next cop action can depend on the observed position.
- At hidden turns the surviving robber positions are unioned into one belief set. At reveals every surviving singleton continuation must win. Within a node at least one legal joint cop action must win.
- Capture must occur by the end of round H. No additional cop move is granted after a final-round reveal.
- No transport tickets, double moves, black tickets, or other board-game rules. Graphs must be undirected, have 1–255 vertices, and have no isolated vertices. Unsupported inputs are rejected rather than assigned invented no-move rules.

## Why oracle pruning is safe

After a proposed cop action to C', remove immediately captured possibilities from B. For every remaining robber vertex r, read the full-visibility **robber-turn** distance R(C',r). The maximum across these vertices is an optimistic lower bound for the partially observed continuation: revealing more information cannot make capture harder for the cops.

If even that optimistic bound exceeds the remaining ply budget, discard the action. If a remaining position is losing even under full visibility, discard the action as well. Otherwise keep the action. Sort surviving actions by this bound and search them until one succeeds. No per-cop move intersection/union, common-best-move shortcut, or preference alone is used as a proof.

V5 stores one byte per `(cop configuration, robber vertex)`: bit 0 is the cop-turn win flag; the upper seven bits encode the robber-turn value/counter. Payload 127 means solved but saturated, **not losing**. It contributes the conservative bound `max_exact_robber_ply + 1`. Exact cop-turn values for `--query` are reconstructed by examining legal successor configurations. The query marks saturated uncertainty with `exact:false`; such a recommendation need not be an optimal move.

The search memoizes `(cop configuration, robber cloud, round)` within each initial root. Its cache evictions affect speed only. The finite deadline makes the tree acyclic. This is a decision search for a given deadline, not V2's first-discovered-distance propagation and not a new full-state oracle builder.

## Output and limits

JSON is written to stdout; progress and errors go to stderr.

- `WIN`: every requested initial root has a guaranteed capture strategy by this deadline.
- `LOSS`: at least one requested root has been exhaustively proved unwinnable by this deadline. It does not mean cops can never win, or that no other cop start works.
- `UNKNOWN`: no losing root was proved, but one or more roots remain unfinished. A timeout is never reported as a loss.

Exit codes are 0 for a definitive result, 2 for UNKNOWN, and 1 for invalid input or operational errors. Output includes each initial cloud, its result, and its first winning cop action when one is needed. This version does not export the entire contingent policy for a visualizer. Checkpoints are trusted local files, not proof certificates.

The default per-thread memo capacity is 100,000 states. Joint moves are generated on demand and deduplicated. A node requiring more than 1,000,000 raw joint products is rejected with an explicit error; it is not silently truncated. The Scotland Yard maximum degree 12 means at most 12^4 = 20,736 products per four-cop node. This guard bounds one generation step, not total runtime or memory.

This implementation does not promise that the full 24-round Scotland Yard search is tractable. It establishes a correctness-tested CPU baseline with an existing oracle, measurable pruning and honest interruption behavior. GPU work should compare against this baseline on identical problems before any speed claims.

## Validation

```powershell
python .\experimental\oracle_cpu\test.py
# Optional: compile the original V5 producer and check its actual small saved binaries
python .\experimental\oracle_cpu\test_v5_integration.py
```

The Python reference independently uses sets and exhaustive finite-horizon search. It also solves full-visibility fixtures by Bellman iteration to generate test oracles. Tests compare all three oracle modes, visible/hidden roots, multiple reveal schedules, repeated cop positions, self-edges, cache eviction and parallel roots, and independently verify each returned first action. Further checks cover ranks, saturation, malformed headers, wrong graphs, checkpoints and resource exhaustion. See `VALIDATION.md` for the checks actually run for this delivery.
