/**
 * ============================================================================
 * FILE --- k_cops_poc.cpp
 * ============================================================================
 * * OVERVIEW
 * Proof of Concept solver for limited-edge Cops and Robbers.
 * Tracks a single resource (Red Tickets) to determine the minimum cost 
 * required for the Cops to guarantee a capture.
 * ============================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iomanip>

const uint8_t EDGE_NONE = 0;
const uint8_t EDGE_FREE = 1; // Merged Yellow and Green
const uint8_t EDGE_RED  = 2; // Costs 1 Red Ticket

const uint8_t INFINITY_COST = 255; // Robber escapes forever

struct GameState {
    int r;
    int c[2]; 
};

struct CopMove {
    int c[2];
    uint8_t redTicketsUsed;
};

class GraphPoC {
public:
    int N;
    uint8_t* adjMatrix;

    GraphPoC() {
        N = getLineCount("assets/matrices/scotlandyard-yellow.txt");
        if (N == 0) {
            std::cerr << "Failed to find matrices. Run from project root.\n";
            exit(1);
        }
        
        adjMatrix = new uint8_t[N * N]{0};
        
        // Load Yellow and Green as FREE edges
        loadEdges("assets/matrices/scotlandyard-yellow.txt", EDGE_FREE);
        loadEdges("assets/matrices/scotlandyard-green.txt", EDGE_FREE);
        
        // Load Red as Cost edges
        loadEdges("assets/matrices/scotlandyard-red.txt", EDGE_RED);
        
        // Self-loops cost nothing
        for(int i = 0; i < N; ++i) {
            adjMatrix[i * N + i] |= EDGE_FREE;
        }
    }

    ~GraphPoC() { delete[] adjMatrix; }

    uint8_t getEdge(int u, int v) const { return adjMatrix[u * N + v]; }

private:
    int getLineCount(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return 0;
        int count = 0; std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line[0] != '-') count++;
        }
        return count;
    }

    void loadEdges(const std::string& filename, uint8_t edgeType) {
        std::ifstream file(filename);
        if (!file.is_open()) return;
        
        std::string line; int row = 0;
        while (std::getline(file, line) && row < N) {
            if (line[0] == '-') break;
            int col = 0;
            for (char c : line) {
                if (c == '1' && col < N) {
                    // Prefer FREE over RED if both exist
                    if (adjMatrix[row * N + col] != EDGE_FREE) {
                        adjMatrix[row * N + col] = edgeType;
                    }
                }
                if (c == '0' || c == '1') col++;
            }
            row++;
        }
    }
};

class SolverPoC {
private:
    GraphPoC* g;
    size_t totalStates;
    uint8_t* dpTable; 

    size_t encodeState(const GameState& s) {
        return s.r + (s.c[0] * g->N) + (s.c[1] * g->N * g->N);
    }

    GameState decodeState(size_t id) {
        GameState s;
        s.r = id % g->N; id /= g->N;
        s.c[0] = id % g->N; id /= g->N;
        s.c[1] = id % g->N;
        return s;
    }

    // Generates all valid team moves and calculates tickets spent
    void generateCopMoves(const GameState& current, int copIdx, CopMove tempMove, std::vector<CopMove>& validMoves) {
        if (copIdx == 2) {
            validMoves.push_back(tempMove);
            return;
        }

        int u = current.c[copIdx];
        for (int v = 0; v < g->N; ++v) {
            uint8_t edge = g->getEdge(u, v);
            if (edge == EDGE_NONE) continue;

            CopMove nextMove = tempMove;
            nextMove.c[copIdx] = v;
            
            if (edge == EDGE_RED) {
                nextMove.redTicketsUsed++;
            }

            generateCopMoves(current, copIdx + 1, nextMove, validMoves);
        }
    }

public:
    SolverPoC(GraphPoC* graph) : g(graph) {
        totalStates = (size_t)g->N * g->N * g->N;
        std::cout << "Allocating " << totalStates << " states...\n";
        
        dpTable = new uint8_t[totalStates];
        std::memset(dpTable, INFINITY_COST, totalStates);
    }

    ~SolverPoC() { delete[] dpTable; }

    void solve() {
        std::cout << "Initializing Capture States...\n";
        int initialWins = 0;
        for (size_t id = 0; id < totalStates; ++id) {
            GameState s = decodeState(id);
            if (s.c[0] == s.r || s.c[1] == s.r) {
                dpTable[id] = 0; 
                initialWins++;
            }
        }
        std::cout << "Initialized " << initialWins << " capture states.\n";

        bool changed = true;
        int passes = 0;

        while (changed) {
            changed = false;
            passes++;
            int statesUpdated = 0;

            for (size_t id = 0; id < totalStates; ++id) {
                if (dpTable[id] == 0) continue; 

                GameState current = decodeState(id);
                uint8_t bestTeamCost = INFINITY_COST;

                std::vector<CopMove> teamMoves;
                CopMove initialMove = { {current.c[0], current.c[1]}, 0 };
                generateCopMoves(current, 0, initialMove, teamMoves);

                for (const auto& cMove : teamMoves) {
                    uint8_t worstRobberResponse = 0; 
                    
                    // --- THE FIX: INSTANT CAPTURE CHECK ---
                    // If a cop lands on the robber, the robber doesn't get a turn to run.
                    if (cMove.c[0] == current.r || cMove.c[1] == current.r) {
                        worstRobberResponse = 0; 
                    } else {
                        // Otherwise, the robber evaluates all possible escape routes
                        for (int r_next = 0; r_next < g->N; ++r_next) {
                            uint8_t rEdge = g->getEdge(current.r, r_next);
                            if (rEdge == EDGE_NONE) continue;

                            GameState nextState = {r_next, {cMove.c[0], cMove.c[1]}};
                            size_t nextId = encodeState(nextState);
                            
                            uint8_t resultingCost = dpTable[nextId];
                            
                            if (resultingCost > worstRobberResponse) {
                                worstRobberResponse = resultingCost;
                            }
                        }
                    }

                    if (worstRobberResponse != INFINITY_COST) {
                        uint8_t totalCost = worstRobberResponse + cMove.redTicketsUsed;
                        if (totalCost < bestTeamCost) {
                            bestTeamCost = totalCost;
                        }
                    }
                }

                if (bestTeamCost < dpTable[id]) {
                    dpTable[id] = bestTeamCost;
                    changed = true;
                    statesUpdated++;
                }
            }
            std::cout << "Pass " << passes << ": Updated " << statesUpdated << " states.\n";
        }
        std::cout << "Algorithm Converged. DP Table complete.\n";
        
        // Optional: Save the DP table here so Python can read it for the client meeting
    }
};

int main() {
    std::cout << "--- Scotland Yard Cops & Robbers (PoC) ---\n";
    GraphPoC g;
    SolverPoC solver(&g);
    solver.solve();
    return 0;
}