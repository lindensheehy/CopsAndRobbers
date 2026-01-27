# Project: CopsAndRobbers (Scotland Yard Analysis)

## 1. Team Members and Roles
* **Linden Sheehy:** Project Manager, Business Analyst, Lead Developer
* **Valeriy Popov:** Architect, QA & Build Manager, Lead Developer

## 2. Objectives
**Goal:** To analyze and generalize the "Cops and Robbers" graph theory problem, inspired by the board game *Scotland Yard*.

**Key Accomplishments & Success Criteria:**
* **Generalization:** Extend the classic problem to support $k$-cops, variable robber speeds, and limited information (e.g., robber visible only every $N$ moves).
* **Algorithmic Solver:** Implement a high-performance solver using backward induction to determine winning states for various graph types (Dismantlable, Chordal, Planar).
* **Visualization:** Provide visual tools to render graph states and strategies (using `networkx` and `matplotlib`).
* **Validation:** Verify theoretical "cop numbers" for known graphs (e.g., Petersen, Dodecahedron, Robertson) against our solver's output.

## 3. Expected Architecture
The project follows a hybrid Python/C++ architecture to balance rapid prototyping with computational efficiency:
* **Core Solver (C++):** A high-performance C++17 engine will handle the heavy computational logic (state space search and backward induction). This is necessary to handle the combinatorial explosion of states ($O(n^{k+1})$ complexity).
* **Prototyping & IO (Python):** Python scripts handle graph generation, adjacency matrix parsing, and initial algorithm verification.
* **Visualization:** Python (`matplotlib`, `networkx`) is used to render the game graphs and agent positions.
* **Data Storage:** Graphs are stored as adjacency matrices in plain text files (e.g., `assets/matrices/`) for easy interoperability between the C++ and Python components.

## 4. Anticipated Risks & Challenges
* **State Space Explosion:** As the number of cops ($k$) or nodes ($n$) increases, the state space grows exponentially. Optimizing the solver memory layout and pruning invalid states (e.g., lexicographic sorting of cop positions) is a critical engineering challenge.
* **Performance:** Python prototypes may be too slow for larger $k$; seamless migration of logic to optimized C++ is required.
* **Complex Variants:** Implementing "limited visibility" or "subgraph movement" rules adds significant logic complexity to the standard backward induction algorithm.

## 5. Legal and Social Issues
* **Intellectual Property:** This is an academic study of a mathematical problem and existing board game mechanics. No proprietary code from the original *Scotland Yard* game is used; all algorithms are original implementations of graph theory concepts.
* **Academic Integrity:** All theoretical references (e.g., *Berarducci & Intrigila, 1993*) will be properly cited in the code and documentation.

## 6. Initial Plans (First Release & Setup)
* **Week 2-3:** Finalize the generic $k$-cop solver in Python (currently `cops_k_and_robber_algo.py`).
* **Week 4:** Complete the C++ file I/O and basic matrix parsing (migrate `fileio.cpp` to full solver).
* **Tools:**
    * **Build System:** Makefile (MinGW/GCC on Windows).
    * **Version Control:** Git/GitHub.
    * **Documentation:** Meeting minutes and requirements stored in `notes/` directory (e.g., `meeting1.md`).   