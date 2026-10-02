# Experimental v3_2: CPU/CUDA macro-turn solver

Based on repository commit f7891e03344552e95c590be9d67759ff437f94a0.
This is a complete experimental solver path, not a demonstrated optimal GPU implementation.
It leaves v1/v2/v3 and the normal build.py unchanged. No GPU speedup is claimed before measurements.

## Build and run on the RTX 3060 Ti (Windows cmd)

Extract the supplied archive into the repository root; it adds experimental/v3_2/.
The existing lib/ and assets/ folders are required. From D:\capstone\CopsAndRobbers:

```bat
call experimental\v3_2\build_windows.cmd
build\bin\k_cops_visibility_v3_2.exe assets/matrices/peterson.txt 3 1 --backend compare
build\bin\k_cops_visibility_v3_2.exe assets/matrices/grid9.txt 2 2 --backend compare
build\bin\k_cops_visibility_v3_2.exe assets/matrices/dismantleable48.txt 1 1 --backend compare
```

The script restores the Windows SDK paths fixed during setup, uses x64 MSVC,
and compiles for sm_86. It currently names your installed SDK 10.0.26100.0 and
VS2022 BuildTools location explicitly. If these change, edit the script.
CUDA compilation builds ALL sources with the same toolchain; it does not link
previously built MinGW/MSYS2 object files into the MSVC executable.

Already in a correctly configured developer prompt:

```bat
python experimental\v3_2\build.py
```

CPU-only build with g++ (Windows MSYS2 or Linux):

```bat
python experimental\v3_2\build.py --cpu-only
build\bin\k_cops_visibility_v3_2_cpu.exe assets/matrices/grid9.txt 2 2 --backend cpu
```

## What each mode measures

- `--backend cpu`: multithreaded CPU solver, defaults to hardware thread count.
- `--backend gpu`: GPU solver; CPU builds the existing AuxGraph and schedules launches.
- `--backend compare`: runs CPU then GPU from identical seeds; compares EVERY final
  state cost, not just the starting positions or the final WIN/LOSS.
- `--threads 1`: single-thread CPU baseline. Default uses all reported hardware threads.
- `--batch 4096`: maximum simultaneous GPU root searches (default 4096).
- `--budget 128`: maximum DFS loop iterations per root per launch (default 128).
- `--dump filename.csv`: export config ID, robber vertex, and cost (`INF` if losing).

CPU solve time includes worker startup and all sweeps. GPU solve time includes CUDA
initialization, allocations, uploads, all sweeps, synchronization, and final download;
allocation cleanup is outside the reported timer. Shared graph/transition construction
is reported separately. CPU/GPU speedup is CPU solve time divided by GPU solve time;
add shared setup time to BOTH to compare full application runtime. This is an honest
cold-run baseline; repeat runs and compare medians on a quiet PC (close WoW).
Tiny cases will often favor CPU because startup dominates.

After small cases pass, try:

```bat
build\bin\k_cops_visibility_v3_2.exe assets/matrices/grid36.txt 2 2 --backend compare --batch 4096 --budget 128
```

Only compare different batch/budget settings on the SAME graph/k/p and CPU thread count.
For example, try batches 1024, 4096, 16384. Do not start with the full Scotland Yard
case: existing cop-transition precomputation can already exhaust host RAM before
GPU work begins. Ctrl+C cancels; there is no disk checkpoint/restart yet.

## Algorithm and mapping to v3

1. Reuse Graph, AdjacencyList, AuxGraph, Allocator and existing canonical cop IDs.
   AuxGraph targets are already multiplied by N; search preserves that layout.
   Existing capture-state allocation is retained, although values for this solver
   live in independent tables. This adds host memory overhead.
2. Seed (cop configuration, visible robber vertex) at cost zero for collisions;
   all other states start at infinity. A cost counts individual plies.
3. For each sweep, freeze the previous cost table. Every root search reads it and
   writes only its own new value. The next sweep starts after all roots finish.
4. DFS goes down one cop trajectory, keeping the surviving robber cloud in each
   stack frame. Move cops; remove captures; empty cloud wins at 2*d-1. Otherwise
   expand robber moves and remove cop-occupied destinations; empty wins at 2*d.
5. At depth p, the robber becomes visible. ALL surviving positions must have finite
   costs in the previous table. Candidate cost = 2*p + their maximum cost. Cops
   choose the minimum over trajectories. A failed leaf means try other trajectories.
6. Bound: a frame at depth d cannot improve a known cost <=2*d-1. A deeper frame
   cannot improve a known cost <=2*d+1. Continue other ancestors' alternatives.
7. Re-evaluate previously winning states too: their costs may decrease. Stop only
   when NO value changes, not merely when no new winning states appear.

The old docs' claim that freezing the first finite cost guarantees the optimum is
incorrect: an unexplored/unproved alternative can later become faster. Decreasing
value iteration resolves that. Every finite update witnesses a valid strategy;
nonnegative integer costs decrease and the finite state space eventually stabilizes.
Under these finite-game rules, a winning reachability strategy has a finite bound;
repeated macro-turn evaluation discovers it. Values still at infinity at convergence
are losing. uint64_t values replace the 7-bit horizon; a conservative 2*p*stateCount
bound is checked for numeric overflow before solving. A resource error is an ERROR,
not LOSS.

Do not delete invisible robber positions just because their visible-state costs are
finite: the strategies for different positions can require incompatible cop moves.
Only actual captures remove positions inside the blind window.

Movement matches v3's actual input semantics: no extra passing move is injected,
but diagonal 1s in an input matrix ARE legal self-loops. Undirected graphs with
1..200 vertices are supported. Isolated vertices are rejected before calling legacy
AuxGraph (its transition generator assumes a move exists); decide the no-move rule
before extending this. Input files use the existing repository matrix format.

## GPU execution

A kernel runs many root searches concurrently, one thread per root, using the same
host/device search.hpp routine as the CPU. Each thread owns a global-memory stack.
Graph masks, occupied masks, transition heads/edges and both value tables stay in
VRAM. No cloud transfers are performed at each cop/robber step.

Long searches resume across bounded-work kernels. A tiny pending counter returns
to the CPU after each launch; this preserves DFS state in VRAM while allowing
shorter launches on a Windows display GPU. Budget limits WORK, not wall time:
if Windows resets the GPU or a launch is too long, lower --budget and --batch.
Do not disable Windows timeout protection. Completed threads in a batch remain idle
until the batch finishes; dynamic work refill and warp-cooperative expansion are
future tuning options if profiling shows imbalance. CPU and GPU are alternative
search backends here, not simultaneous workers on the same pass.

Current bottlenecks remain: exponential cop trajectory branching, branch divergence,
random DP lookups, global stack traffic, and precomputed cop transitions. The graph
and both DP tables must fit VRAM; lowering batch only shrinks stack/task memory.
We check planned GPU buffers against 90% of currently free VRAM before allocation.

NVIDIA recommends batching transfers and keeping intermediate data on-device:
https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device

## Verification and files

```bat
python experimental\v3_2\test_reference.py
python experimental\v3_2\test_reference.py --exe build/bin/k_cops_visibility_v3_2.exe --backend compare
```

The first uses the CPU-only executable; the second runs both backends on your GPU
with batch 17 and budget 1 to stress resumability and partial final batches. It
spawns many processes, so it is a correctness test rather than a speed benchmark.

- search.hpp: portable bit masks, DFS frames, initialization, bounded advance.
- main.cu: existing graph infrastructure, CPU scheduling, CUDA allocation/kernels,
  convergence, comparisons and verdict.
- build.py / build_windows.cmd: build and Windows environment.
- test_reference.py: independent Python sets + recursive minimax, all four-vertex
  undirected graphs without isolates, with/without self-loops, k=1/2,p=1/2;
  additional five-vertex p=3 cases and capture-time regression tests.
- test_resume.cpp: 14,400 root searches with tiny vs large work budgets.

These tests support correctness on the stated rules; they do not validate the full
research specification, token/resource constraints, or performance on Scotland Yard.
