# Validation for v3_2

Source base: f7891e03344552e95c590be9d67759ff437f94a0.

Executed in the assistant Linux workspace (not on the user Windows desktop):

- g++ CPU build succeeded.
- CUDA 13.0.48 compiled main.cu for sm_86. The resulting object linked with the
  repository libraries and CUDA runtime using g++. The Linux pip toolkit lacks
  nvcc link.stub, so this check used separate compilation and host linking.
- The CUDA-compiled executable also passed all 336 cases in CPU mode.
- GPU execution here is unavailable: the runtime reports insufficient driver.
  Windows/MSVC compilation and GPU results are not claimed as tested.
- 336 graph/parameter cases passed: 334 complete state tables compared with an
  independent Python recursive set-based reference, plus two capture-time regressions.
- 14,400 root searches matched for work budgets 1 and 1,000,000.
- The resume test also passed AddressSanitizer and UndefinedBehaviorSanitizer.
  LeakSanitizer was disabled because this execution environment blocks its /proc access.
- Petersen, k=3,p=1: WIN, 1 ply, cop positions 0 2 6.
- dismantleable48, k=1,p=1: WIN, 13 plies (old v3 reported 19).
- grid36, k=2,p=2: WIN, 11 plies, positions 6 20 (old v3 reported 13).
- Synthetic 100-vertex path with self-loops: cop at 0, robber at 99 has cost 197.

GPU runtime comparison and Windows/MSVC build must still be run on the user's PC.
No GPU speedup has been measured. The supplied --backend compare checks every
state cost and reports solve times including GPU uploads/downloads and allocations.
