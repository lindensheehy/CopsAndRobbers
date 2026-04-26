#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include <iostream>
#include <vector>
#include <string>

bool loadGraphFile(const char* filename);
bool buildAuxGraph(int k);
bool initializeCaptures();
bool mainLoop();
bool findFinalResult(int k);

#include ALGORITHM_INCLUDE

// --- MAIN ALGORITHM ---
void solveCopsAndRobbers(const char* filename, int k) {

    Profiler p;


    /* --- Load Graph File --- */

    p.enter("Load Graph");
    if (loadGraphFile(filename)) {
        // throw error
    }
    p.enter("Idle");


    /* --- Build Aux Graph --- */

    p.enter("Build Aux Graph");
    if (buildAuxGraph(k)) {
        // throw error
    }
    p.enter("Idle");


    /* --- Initialize Captures --- */

    p.enter("Initialize Captures");
    if (initializeCaptures()) {
        // throw error
    }
    p.enter("Idle");


    /* --- Main Loop --- */
    
    p.enter("Main Loop");
    if (mainLoop()) {
        // throw error
    }
    p.enter("Idle");


    /* --- Find Final Result --- */

    p.enter("Final Verdict Evaluation");
    if (findFinalResult(k)) {
        // throw error
    }
    p.enter("Idle");

    
    p.print();

    return;

}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops>\n";
        std::cout << "Example: " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    const char* filename = argv[1];
    int k = std::stoi(argv[2]);
    
    solveCopsAndRobbers(filename, k);

    return 0;

}
