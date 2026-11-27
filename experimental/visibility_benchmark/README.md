# Original v3 versus hardened visibility benchmark

Extract this update into the CopsAndRobbers repository root. It adds only
experimental/visibility_benchmark; no solver source, graph or existing oracle is replaced.
Requires the previously installed hardened update and repository v3/V5 sources.

Build from PowerShell in the repository root:

    python .\experimental\visibility_benchmark\build.py

Start small:

    python .\experimental\visibility_benchmark\run.py --sizes 4,6 --threads 16

Increase coverage:

    python .\experimental\visibility_benchmark\run.py --sizes 8,12,16 --cops 1,2,3,4 --periods 1,2,3,5 --horizons 4,8,12,24 --threads 16 --seconds 30 --out .\console_outputs\v3_race_extended

This is intentionally a lengthy suite. Stop with Ctrl+C if needed. Resume with the
same command and settings plus --resume. Completed records, including timeouts, are
skipped on resume; use a fresh output directory to retry them with a larger budget.
Do not run multiple processes into the same output directory.

Further sizes can be requested with --sizes 24,32,48. --repeats 3 repeats searches
while reusing each oracle. --families path,cycle,grid,random controls graph families;
random graphs are seeded and connected. Paths use one cop, cycles at most two, and
all cases require n >= 2*k. Thus four cops are not gratuitously tested on a path.
Grid sizes need not be perfect squares: incomplete final rows are allowed.

## What it runs

1. Build a full-visibility oracle with the ORIGINAL k_cops_fullvis_v5.cpp for each
   graph/cop pair, in an isolated case directory. Verify its completion receipt,
   dimensions and file size, then bind its payload and graph with SHA-256 metadata.
   This works even when no universal full-visibility winning start exists.
2. Run ORIGINAL solvers/k_cops_visibility_v3.cpp unchanged through a read-only
   instrumentation driver. Export every state, marking and reported capture time.
   The driver skips its normal cache export and separately times state CSV export.
3. Run the hardened solver with the generated oracle, one thread and the requested
   multicore count, at every requested periodic visibility and horizon. Select the
   full-visibility best start (or spread start if none) and v3's best start when it
   differs; otherwise use the spread start. The same starts are read from v3's table.
4. For n <= 8 and <=4000 full-visibility states, independently validate each bounded
   answer and winning first move using the exhaustive Python reference, in a separate
   time-limited process. A reference timeout is explicitly marked unchecked.

The hardened solver does NOT generate missing oracles itself. This runner generates
and explicitly selects them automatically. Existing Scotland Yard oracles are not used.
An oracle is reused across periods, horizons, thread counts and repetitions within
this output directory. A new output directory builds its own oracles.

## Interpretation

v3 computes an entire reachability table. Hardened solves a fixed cop start by a
fixed deadline. Their raw elapsed times are NOT a like-for-like speedup ratio.
The CSV separates oracle build, v3 whole-table work, and hardened queries. It includes
process wall time and available internal search/setup/export times. Oracle building
is paid once and its cost is never silently included in only one competing query.
Runs are sequential: solvers do not compete simultaneously for CPU, RAM or disk.
Order is v3 then hardened cpu1 then multicore; OS caches are not forcibly cleared.

Original v3 freezes first-found costs and has a 7-bit cost limit. Its cost is not
necessarily the minimum; its unmarked states are treated as UNPROVEN here.
- v3 bound <= 2H but hardened LOSS: bound contradiction needing investigation.
- hardened WIN earlier than v3's recorded bound: possible improved bound, not by
  itself a wrong answer.
- v3 unmarked but hardened WIN: flagged separately.
- Independent-reference mismatch: a correctness failure needing investigation.

WIN/LOSS of the hardened solver concern the selected start and deadline, not the
graph's cop number. UNKNOWN/timeouts are unfinished work, never a fabricated loss.
Changing p=2 to p=3 also changes reveal timing; these schedules are not nested.

## Outputs and limits

runs.jsonl is flushed after each completed process record. runs.csv and REPORT.md
are written at completion or Ctrl+C. Each case retains graphs, oracle and receipt,
v3 full tables, hardened result JSON, and console logs. settings.json binds resume
to settings and executable hashes. Binaries and original source hashes are recorded
by the build script (build_sources.json).

Defaults: 20 seconds per v3 search; hardened gets a 20-second internal budget plus
10 seconds external setup/stop allowance; oracle build 180 seconds. An external
 timeout kills the child and its unfinished work is not considered a verdict.
The memory gate is 500,000 (C,r) states and an estimated 256 MiB of v3 transition
storage, adjustable via --max-states and --max-transition-mib. These are estimates,
NOT hard RAM caps. v3 and V5 have no parent-enforced RAM limit. Timed-out V5 runs may
leave diagnostic frontier files in their isolated case/cache directory. Check disk
space for a very long suite. No claim of a rigorous runtime upper bound is made.

Optional --gpu adds hardened GPU-auto runs AFTER your CUDA build/device tests pass.
This uses the full-visibility oracle just generated; inspect gpu_rows because small
cases often do no GPU work. GPU auto can fall back to CPU. It is not a guaranteed
GPU performance measurement. CPU is the default.

Validation performed here: smallest path/cycle suite, k=1/2, p=1/2, H=2 completed
with no independent-reference mismatches or v3-bound contradictions. Larger suites
are supplied for measurement on your PC, not claimed executed here. Windows build
of these new instrumentation drivers still needs your local check.
