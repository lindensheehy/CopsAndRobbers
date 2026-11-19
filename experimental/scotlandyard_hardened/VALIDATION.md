# Validation record

Environment: Linux x86-64, GCC, AVX2 available. Source base: Linden commit
`3c1ae57939dd95d4bd361fb5c3597cbb27ea5f5b`. No Scotland Yard four-cop search was run here.

Executed:

- Dedicated CPU build with `-std=c++17 -O3 -Wall -Wextra -Wpedantic`.
- Existing independent-reference suite: 1,795 solver/reference and boundary checks,
  including 18 cases where individual singleton wins do not establish a hidden-cloud win.
  Parallel pruning counters vary with scheduling; they are not unique-state counts.
- 533,520 scalar/AVX score comparisons and simulated-GPU host-refinement comparisons.
  Cases cover n=13,31,32,33,63,64,65,126,127,128,129,199,200, k=1..4,
  finite distances, saturation, INF, repeated destinations, masks and row tails.
  The simulated backend tests host integration only, not CUDA execution.
- Extension suite: 21 process checks covering periodic configuration, manifest/payload
  mismatch, explicit legacy trust, injected node/queue/thread-startup failures, arena
  exhaustion, completed-subproblem resume, damaged checkpoints and missing-GPU fallback.
- AddressSanitizer + UndefinedBehaviorSanitizer extension suite (leak checking disabled
  for this sandbox; UBSan configured to halt on error).
- Benchmark smoke: 32 paired runs checked against the independent reference.
- Benchmark full: 512 paired runs, eight fixture/cop-count combinations, periods
  1,2,3,5, horizons 4/6, visible/hidden starts, baseline/new solver, 1/4 threads.
  Every definitive verdict and returned first move is checked, not merely compared
  between two implementations. These tiny fixtures do not establish a speedup on
  Scotland Yard. Per-process records are included under `validation/`.

Not executed:

- Windows/MSYS2 build.
- CUDA compilation or device execution: no nvcc/device available in this environment.
- 12.48 GiB oracle load, full-size GPU upload, or a four-cop Scotland Yard search.
- Reliable large-graph runtime extrapolation.

GPU backend must pass `test_gpu.py` on the target machine, followed by a short
`SY_GPU=verify` game run with nonzero `gpu_batches`, before its results/performance
are treated as hardware-validated. CPU remains the default. No GPU speedup is claimed.

Code changes preserve the finite-horizon recurrence and joint oracle bound. Execution
changes include nonthrowing task boundaries, pre-reserved queue publication, worker
startup cleanup, RAII arena restoration, C++17 aligned TT allocation, arena/RSS budgets,
periodic p handling, full-file oracle provenance binding, and checksummed root/memo
snapshots. Cached entries always represent completed WIN/LOSS results, never ABORT.
