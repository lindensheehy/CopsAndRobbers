# Scotland Yard hardened CPU solver with optional batched CUDA scoring

Based on Linden's `3c1ae57939dd95d4bd361fb5c3597cbb27ea5f5b` solver. This is a
fixed-start, finite-horizon partial-visibility solver, not a new cop-number proof.
Unlimited movement tokens; no tickets or added passes. Existing matrix self-edges count.
The default positional visibility argument is now a real period: `2` means reveal
after robber moves 2, 4, 6, ... . `SY_REVEALS` can specify a different schedule.

## Install and build

The update ZIP contains every changed/new source, test and script. Apply it to the
existing repository; the existing `lib/`, matrices, and `experimental/oracle_cpu/`
remain dependencies. No cache or existing oracle is replaced. The optional installer
backs up changed files and refuses unexpected local source edits.

From the repository root in PowerShell:

```powershell
python .\experimental\scotlandyard_hardened\build.py
python .\experimental\oracle_cpu\build.py
python .\util\test_k_cops_scotlandyard.py --exe .\build\bin\scotlandyard_hardened.exe
python .\experimental\scotlandyard_hardened\test_extensions.py
```

Requires Python 3.10+ and a C++17 GCC toolchain (your MSYS2 g++ is suitable).
The dedicated driver returns nonzero on setup errors and worker failures; it does
not inherit app/main.cpp's successful exit code after a failed stage.

## Oracle binding (once)

Use the authentic original matrix and the completed V5 result you already have:

```powershell
python .\experimental\scotlandyard_hardened\make_manifest.py `
  --graph .\assets\matrices\scotlandyard-all.txt `
  --oracle .\cache\fullvis_v5_strategy__scotlandyard-all__4-cops.bin `
  --result .\cache\fullvis_v5_result__scotlandyard-all__4-cops.txt `
  --attest-completed-same-rules
```

This hashes the original matrix and entire oracle; it records your provenance
attestation, not an independent proof of the full-visibility solver. Every load
checks those hashes. The original V5 binary has no graph hash or completion flag.
`SY_TRUST_LEGACY_ORACLE=1` is an explicit escape hatch for known completed legacy
oracles and reference-generated test fixtures; the overnight runner uses manifests.
Hashing 12.48 GiB takes time and is logged. Setup time is outside the search budget.
The producing solver is `solvers/k_cops_fullvis_v5.cpp`, with
`COPS_SAVE_FULLVIS_V5=1`; this update does not rebuild that oracle.

## First bounded run, then overnight

```powershell
python .\experimental\scotlandyard_hardened\run.py --period 2 --hours 0.02 --threads 16
python .\experimental\scotlandyard_hardened\run.py --period 2 --hours 8 --threads 16 --resume
```

The first command creates a new run directory. If one already exists, use `--resume`
or supply a different `--run-dir`. Period 3 or 5 uses a separate default directory.
The default deadline is 24 cop+robber rounds, the default cop start is the full-visibility
oracle's best start, and the robber is initially visible at every possible vertex.
A WIN covers every requested initial position. LOSS means at least one escapes this
deadline from this cop start; UNKNOWN means unfinished. Neither LOSS nor UNKNOWN
establishes that four cops can never win with partial visibility.

Defaults: all logical CPUs, 1024 MiB shared TT, 2048 MiB aggregate search-arena cap,
24576 MiB soft resident-memory stop. RSS is sampled, so this is not a hard total
allocation guarantee. Split-task bookkeeping and other memory remain outside the
arena cap. A controlled allocation failure produces diagnostics and incomplete work,
not a fabricated LOSS. `--tt-mb`, `--arena-mb`, `--rss-mb` adjust these settings.

Checkpoints:
- `roots.chk`: completed initial positions, plus SHA-256 checksum.
- `roots.chk.memo`: proved subproblems from the shared transposition table, checksummed,
  every 600 seconds and on normal completion/controlled stop.
- Console logs, launch settings, result JSON and counters are saved in the run directory.

Resume validates graph, oracle digest, cop start, horizon, schedule and roots. CPU/GPU
mode and thread count can change. Active recursion frames restart; saved proved
subproblems can cut that repeated work. Entries evicted before a snapshot are lost.
Snapshots can approach 42/64 of the TT capacity and take disk time. Hard power loss
may lose recent work; checksums reject damaged files. Old prototype/Linden checkpoint
formats are deliberately not silently imported. Avoid two processes sharing a run directory.
Ctrl+C requests a controlled stop; do not forcibly kill it while it saves.

## GPU: useful batching, optional and locally testable

The CUDA backend keeps the **whole** oracle in device memory at four bits per state:

- 0..13 represent the exact decoded lower bound.
- 14 means the decoded bound is at least 14; the CPU refines these candidates exactly.
- 15 means full-visibility loss; the host represents it as 255.

For your 13,398,998,350 states this is 6,699,499,175 bytes (~6.24 GiB), rather than
12.48 GiB. It checks actual free VRAM and leaves at least 512 MiB headroom. No managed
memory oversubscription and no 13 GiB transfer per search node. Packing/upload is paid
once per process; host staging is 16 MiB. The unchanged host oracle remains available.

A warp scores one joint action over the robber cloud. The default gate is >=2048
candidate actions AND >=131072 candidate-position checks. Small jobs stay on CPU.
One worker can submit a batch; other workers do CPU work instead of waiting for that
GPU mutex. The host refines ambiguous values, so final scores match CPU scoring.
`auto` checks its first eight eligible batches against CPU and disables further GPU
scoring if those measured batches are >10% slower. That is a local calibration, not
a guarantee about all future workloads. VRAM stays allocated until process exit.

Build CUDA from an **x64 Native Tools Command Prompt for Visual Studio**, with g++
and Python on PATH. CUDA's Windows compiler needs MSVC even though the main executable
uses g++. The backend has a C ABI and links the CUDA runtime statically.

```powershell
python .\experimental\scotlandyard_hardened\build.py --cuda
python .\experimental\scotlandyard_hardened\test_gpu.py
```

Default target is `sm_86` for the 3060 Ti; `--arch` changes it. Then a short end-to-end
verification run (use a new directory):

```powershell
python .\experimental\scotlandyard_hardened\run.py --period 2 --hours 0.02 --threads 16 --gpu verify --run-dir .\cache\gpu_verify_p2
```

`verify` compares every GPU batch with CPU scoring. Check `gpu_batches`/`gpu_rows`:
zero means this run exercised no eligible GPU work. `test_gpu.py` directly exercises
the CUDA scorer regardless of the game's batch sizes. Only after both checks pass:

```powershell
python .\experimental\scotlandyard_hardened\run.py --period 2 --hours 8 --threads 16 --gpu auto --resume
```

`auto` falls back if no device/plugin/VRAM is available. `required`/`verify` fail setup
instead of silently claiming GPU coverage. A runtime CUDA failure disables the GPU
and recomputes the affected batch on CPU. A CPU/GPU value mismatch aborts the task.
A fully checked, independent sibling proof may still finish despite another task's
failure; the process reports the worker error separately with exit code 3.

Controls: `SY_GPU_LIBRARY`, `SY_GPU_MB` (default 7168), `SY_GPU_MIN_BATCH`,
`SY_GPU_MIN_WORK`; low thresholds are for experiments, not recommended overnight defaults.
No measured GPU speedup or successful Windows/CUDA build is claimed by the supplied
Linux validation. CUDA source and local hardware tests are provided; CPU execution is
the tested path. See VALIDATION.md.

## Correct oracle rules and notes audit

For a candidate joint cop destination C', remove captured robber positions first.
For each surviving possible robber position r, read the **robber-turn** full-visibility
lower bound R(C',r). With t the current round and H the deadline, reject C' if any
bound is INF or exceeds `2*(H-t+1)-1`. Equivalently, use the maximum over positions.
More information cannot make catching the robber harder; therefore failure even
with full information rules out that action under partial information.

Passing the bound is not a proof. The AND/OR tree must still prove one common joint
cop action works for the whole hidden cloud. At a reveal, every singleton continuation
must win, and subsequent decisions may depend on the observation. Captures are removed
both before and after robber expansion. Only fully solved WIN/LOSS subproblems enter
shared caches. Cancelled/failed/budget-limited work never enters as LOSS.

There is NO independent per-cop pruning, no restriction to full-visibility-optimal
moves, and no shortcut saying all singleton wins imply a hidden-cloud win. The bound
is safe at visible and invisible states, with the correct turn and deadline.

`minutes/meeting20_1.txt` explicitly records doubt about copying full-visibility
choices when the robber will become invisible. `docs/scotlandyard_solver.md` proposes
collapsing visible branching to one and trimming each cop's moves; those proposals
are not proofs and are not used. `docs/k_cops_visibility_v2.md` supplies the important
capture-before/after-expansion rules. Its best-known-cost cutoff targets a different
optimization problem; this solver uses a fixed deadline rather than claiming exact
minimum capture time from the first winning search.

## Benchmarks

```powershell
python .\experimental\scotlandyard_hardened\benchmark.py --suite smoke --threads 16
python .\experimental\scotlandyard_hardened\benchmark.py --suite full --threads 16 --repeats 3 --oracle-modes bound,order,off
```

After the GPU tests, add `--gpu` to include a required-GPU variant. Small graph runs
may not reach the default GPU gate; inspect the counters. The full suite uses paths
with one cop, cycles with one/two, a grid, seeded connected graphs, self-loops and cube
fixtures with three/four cops. It covers p=1,2,3,5, visible and hidden initial states.
It is a curated suite, not enumeration of all small graphs or all repository assets.

The paired baseline is `experimental/oracle_cpu`, using identical starts, H, schedules,
roots, rules and oracle modes. Every definitive verdict and returned first move is
checked against the independent set-based Python reference. Budgets/UNKNOWN/timeouts
are recorded, not converted to losses or speedups. Process wall time includes setup;
solver search time is separate; oracle generation time is also recorded. OS cache is
not forcibly cold. Memory policies differ and are part of the configuration being compared.

JSONL is flushed after each run; CSV and per-process stdout/stderr are retained. Tiny
fixtures test correctness, not meaningful speedup. No formula extrapolating from n
alone is supplied: degree^k branching, horizon, cloud growth, pruning and reuse dominate.
`experimental/v3_2` is the separate periodic whole-state fixed-point reference; its
whole-map runtime is not an apples-to-apples speed comparison against this bounded search.

CUDA API reference used for implementation:
https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html
