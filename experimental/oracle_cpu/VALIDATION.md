# Validation record

Validated in the Linux workspace using g++ 14, C++17, on 2026-11-10.

- Optimized standalone build: passed with `-Wall -Wextra -Wpedantic`, no warnings.
- `python experimental/oracle_cpu/test.py`: passed 1,489 solver/reference and boundary checks. Oracle-bound searches performed 682 prunes across these fixtures without changing the independently computed results.
- Those fixtures included 13 cases where all individual robber positions were winning but the combined hidden cloud was losing. All three oracle modes correctly preserved that distinction.
- Rank test: exhaustive multiset rank/unrank roundtrips for 1–12 vertices and 1–4 cops, additional samples at 199 vertices, and the recorded Scotland Yard mapping `(0,3,66,184) -> 69617` with 67,331,650 configurations.
- `python experimental/oracle_cpu/test_v5_integration.py`: built the repository's existing V5 producer, wrote three tiny real oracle binaries, and checked all 58 cop-turn queries against independent Python full-visibility values. Included self-edges and repeated two-cop positions.
- UndefinedBehaviorSanitizer build (`-fsanitize=undefined -fno-sanitize-recover=all`): the reference/boundary suite and rank checks passed with no reported UB. Subsequent changes removed a redundant query pass and rejected empty initial robber sets; the final optimized suite was rerun afterward.
- Manual one-second time-budget check on a 12-cycle, hidden robber, no memoization: returned UNKNOWN with exit code 2 rather than LOSS.
- Checkpoint tests: interrupted search resumed to the same initial-root answers as a fresh search; changed horizon rejected; accidental overwrite rejected.

No full-size Scotland Yard tree search was run. The user's 13,398,998,414-byte oracle was not available here, so its actual payload was not tested. The recorded header/count/rank were used for compatibility checks, and the original V5 writer was tested on small inputs. No Windows runtime or CUDA performance claim is made: the Win32 read-only mapping/checkpoint paths are implemented but still require a local Windows run.

These tests establish a meaningful CPU baseline, not a formal verification proof or a performance guarantee. Correct oracle pruning assumes an authentic, completed V5 oracle generated for the same adjacency matrix and movement rules. Full-game policy export, within-root DFS checkpointing, global cop-start optimization, tickets and GPU execution are not implemented.
