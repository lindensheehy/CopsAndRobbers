/**
 * ============================================================================
 * FILE --- k_cops_limited.cpp
 * ============================================================================
 * * OVERVIEW
 * A generalized solver for the "Cops and Robbers" problem that introduces 
 * edge-use limitations (tickets). It uses a dense, mixed-radix 1D array to 
 * represent the entire state space, packing all positional and resource data 
 * into a single contiguous block of memory.
 * * KEY FEATURES
 * - Bitmasked Adjacency Matrix: Stores Yellow (1), Green (2), and Red (4) edges.
 * - On-The-Fly Transitions: Eliminates the massive precomputed transition vector.
 * - 2-Bit State Packing: Uses only 2 bits per state (Cop Win, Robber Win).
 * ============================================================================
 */

#include "Allocator.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iomanip>

// --- Edge Type Bitmasks ---
const uint8_t EDGE_NONE   = 0;
const uint8_t EDGE_YELLOW = 1 << 0; // Infinite
const uint8_t EDGE_GREEN  = 1 << 1; // Limited
const uint8_t EDGE_RED    = 1 << 2; // Limited

// --- DP Table Bitmasks ---
const uint8_t COP_WIN_BIT = 1 << 0;
const uint8_t ROB_WIN_BIT = 1 << 1;

// --- Maximum constraints for static arrays (Prototype) ---
const int MAX_COPS = 8;

struct GameState {
    int r;
    int cops[MAX_COPS];
    uint8_t r_g, r_r;
    uint8_t c_g[MAX_COPS], c_r[MAX_COPS];
};

class GraphLimited {
public:
    int N;
    uint8_t* adjMatrix;

    GraphLimited() {
        N = getLineCount("assets/matrices/scotlandyard-yellow.txt");
        if (N == 0) {
            std::cerr << "Failed to find scotlandyard matrices. Ensure you are running from the project root.\n";
            exit(1);
        }
        
        adjMatrix = new uint8_t[N * N]{0};
        
        loadEdges("assets/matrices/scotlandyard-yellow.txt", EDGE_YELLOW);
        loadEdges("assets/matrices/scotlandyard-green.txt", EDGE_GREEN);
        loadEdges("assets/matrices/scotlandyard-red.txt", EDGE_RED);
        
        // Ensure self-loops (staying put requires no tickets, so it's a Yellow edge)
        for(int i = 0; i < N; ++i) {
            adjMatrix[i * N + i] |= EDGE_YELLOW;
        }
    }

    ~GraphLimited() {
        delete[] adjMatrix;
    }

    uint8_t getEdge(int u, int v) const {
        return adjMatrix[u * N + v];
    }

private:
    int getLineCount(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return 0;
        int count = 0;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line[0] != '-') count++;
        }
        return count;
    }

    void loadEdges(const std::string& filename, uint8_t edgeMask) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open " << filename << "\n";
            return;
        }
        
        std::string line;
        int row = 0;
        while (std::getline(file, line) && row < N) {
            if (line[0] == '-') break;
            int col = 0;
            for (char c : line) {
                if (c == '1' && col < N) {
                    adjMatrix[row * N + col] |= edgeMask;
                }
                if (c == '0' || c == '1') col++;
            }
            row++;
        }
    }
};

class CopsAndRobbersSolver {
private:
    GraphLimited* g;
    int k; // Number of cops
    int n; // Max tickets
    int T; // Ticket states (n + 1)
    
    size_t totalStates;
    uint8_t* dpTable = nullptr; 
    
    // Allocator must be a member variable so its lifetime matches the solver
    Allocator mem;

    // Mixed-Radix Dense Encoding
    size_t encodeState(const GameState& s) {
        size_t id = s.r;
        size_t mult = g->N;
        
        for (int i = 0; i < k; ++i) { id += s.cops[i] * mult; mult *= g->N; }
        
        id += s.r_g * mult; mult *= T;
        id += s.r_r * mult; mult *= T;
        
        for (int i = 0; i < k; ++i) { id += s.c_g[i] * mult; mult *= T; }
        for (int i = 0; i < k; ++i) { id += s.c_r[i] * mult; mult *= T; }
        
        return id;
    }

    // Decode ID back into physical state
    GameState decodeState(size_t id) {
        GameState s;
        s.r = id % g->N; id /= g->N;
        for (int i = 0; i < k; ++i) { s.cops[i] = id % g->N; id /= g->N; }
        
        s.r_g = id % T; id /= T;
        s.r_r = id % T; id /= T;
        
        for (int i = 0; i < k; ++i) { s.c_g[i] = id % T; id /= T; }
        for (int i = 0; i < k; ++i) { s.c_r[i] = id % T; id /= T; }
        return s;
    }

    // Helper to generate all valid simultaneous cop team moves
    void generateCopMoves(const GameState& current, int copIdx, GameState tempState, std::vector<GameState>& validMoves) {
        if (copIdx == k) {
            validMoves.push_back(tempState);
            return;
        }

        int u = current.cops[copIdx];
        for (int v = 0; v < g->N; ++v) {
            uint8_t edge = g->getEdge(u, v);
            if (edge == EDGE_NONE) continue;

            GameState next = tempState;
            next.cops[copIdx] = v;
            
            bool canMove = false;
            if (edge & EDGE_YELLOW) {
                canMove = true;
            } else if ((edge & EDGE_GREEN) && next.c_g[copIdx] > 0) {
                next.c_g[copIdx]--;
                canMove = true;
            } else if ((edge & EDGE_RED) && next.c_r[copIdx] > 0) {
                next.c_r[copIdx]--;
                canMove = true;
            }

            if (canMove) {
                generateCopMoves(current, copIdx + 1, next, validMoves);
            }
        }
    }

public:
    CopsAndRobbersSolver(GraphLimited* graph, int numCops, int numTickets) 
        : g(graph), k(numCops), n(numTickets) {
        
        T = n + 1;
        
        // Calculate dimensions
        size_t posStates = g->N;
        for (int i = 0; i < k; ++i) posStates *= g->N;
        
        size_t ticketStates = T * T; // Robber
        for (int i = 0; i < k; ++i) ticketStates *= (T * T); // Cops
        
        totalStates = posStates * ticketStates;
        
        double memGB = static_cast<double>(totalStates) / (1024.0 * 1024.0 * 1024.0);
        std::cout << "Required States: " << totalStates << " (" << std::fixed << std::setprecision(2) << memGB << " GB)\n";
        
        if (memGB > 10.0) {
            std::cerr << "FATAL ERROR: Memory requirement exceeds 10 GB limit for prototype safety.\n";
            std::cerr << "Try a smaller graph or fewer tickets.\n";
            exit(1);
        }

        // Drop in the Allocator for the monolithic array
        mem.requestAlloc("Mixed-Radix DP Table", totalStates, &dpTable);
        mem.allocate();
        mem.print();
    }

    ~CopsAndRobbersSolver() {
        // Allocator mem automatically destroys dpTable, no explicit delete[] needed!
    }

    void solve() {
        // --- STEP 1: INITIALIZATION ---
        std::cout << "Initializing capture states...\n";
        int initialWins = 0;
        for (size_t id = 0; id < totalStates; ++id) {
            GameState s = decodeState(id);
            bool caught = false;
            for (int i = 0; i < k; ++i) {
                if (s.cops[i] == s.r) caught = true;
            }
            if (caught) {
                dpTable[id] |= COP_WIN_BIT;
                dpTable[id] |= ROB_WIN_BIT;
                initialWins++;
            }
        }
        std::cout << "Initialized " << initialWins << " capture states.\n";

        // --- STEP 2: SYNCHRONOUS BACKWARD INDUCTION ---
        bool changed = true;
        int passes = 0;

        while (changed) {
            changed = false;
            passes++;
            int newWins = 0;

            std::cout << "Starting Pass " << passes << "...\n";

            for (size_t id = 0; id < totalStates; ++id) {
                
                // --- PROGRESS TRACKER ---
                if (id % 10000000 == 0 && id > 0) {
                    double percent = (static_cast<double>(id) / totalStates) * 100.0;
                    std::cout << "  -> Evaluated " << id << " / " << totalStates 
                              << " states (" << std::fixed << std::setprecision(1) << percent << "%)\r" << std::flush;
                }

                // Skip fully locked wins
                if ((dpTable[id] & COP_WIN_BIT) && (dpTable[id] & ROB_WIN_BIT)) continue;

                GameState current = decodeState(id);

                // --- ROBBER'S TURN ---
                if (!(dpTable[id] & ROB_WIN_BIT)) {
                    int safeCount = 0;
                    int u = current.r;

                    for (int v = 0; v < g->N; ++v) {
                        uint8_t edge = g->getEdge(u, v);
                        if (edge == EDGE_NONE) continue;

                        GameState next = current;
                        next.r = v;
                        bool validMove = false;

                        if (edge & EDGE_YELLOW) {
                            validMove = true;
                        } else if ((edge & EDGE_GREEN) && next.r_g > 0) {
                            next.r_g--; validMove = true;
                        } else if ((edge & EDGE_RED) && next.r_r > 0) {
                            next.r_r--; validMove = true;
                        }

                        if (validMove) {
                            size_t nextId = encodeState(next);
                            if (!(dpTable[nextId] & COP_WIN_BIT)) {
                                safeCount++;
                            }
                        }
                    }

                    if (safeCount == 0) {
                        dpTable[id] |= ROB_WIN_BIT;
                        changed = true;
                        newWins++;
                    }
                }

                // --- COPS' TURN ---
                if (!(dpTable[id] & COP_WIN_BIT)) {
                    std::vector<GameState> validTeamMoves;
                    generateCopMoves(current, 0, current, validTeamMoves);

                    bool canWin = false;
                    for (const auto& nextState : validTeamMoves) {
                        size_t nextId = encodeState(nextState);
                        if (dpTable[nextId] & ROB_WIN_BIT) {
                            canWin = true;
                            break;
                        }
                    }

                    if (canWin) {
                        dpTable[id] |= COP_WIN_BIT;
                        changed = true;
                        newWins++;
                    }
                }
            }
            
            // Clear the progress bar line before printing the pass results
            std::cout << "                                                                              \r";
            std::cout << "Pass " << passes << ": Found " << newWins << " new winning states.\n";
        }
        std::cout << "Algorithm Converged.\n";
    }
};

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <num_tickets>\n";
        std::cout << "Example: " << argv[0] << " 5\n";
        return 1;
    }

    int k = 2; // Hardcoded cop count for prototype
    int n = std::stoi(argv[1]);

    GraphLimited g;
    
    CopsAndRobbersSolver solver(&g, k, n);
    solver.solve();

    return 0;
}