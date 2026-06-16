#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include <iostream>
#include <vector>
#include <string>

// Notice the updated signature
bool loadGraphFile(const char* filename, int k, int p);
bool buildAuxGraph();
bool initializeCaptures();
bool mainLoop();
bool findFinalResult();
bool outputData();

#include ALGORITHM_INCLUDE

// --- MAIN ALGORITHM ---
void solveCopsAndRobbers(const char* filename, int k, int p) {

    Profiler prof;


    /* --- Load Graph File --- */

    prof.enter("Load Graph File");
    if (loadGraphFile(filename, k, p)) {
        std::cout << "[FAILED STEP]: 'Load Graph File' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");


    /* --- Build Aux Graph --- */

    prof.enter("Build Aux Graph");
    if (buildAuxGraph()) {
        std::cout << "[FAILED STEP]: 'Build Aux Graph' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");


    /* --- Initialize Captures --- */

    prof.enter("Initialize Captures");
    if (initializeCaptures()) {
        std::cout << "[FAILED STEP]: 'Initialize Captures' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");


    /* --- Main Loop --- */
    
    prof.enter("Main Loop");
    if (mainLoop()) {
        std::cout << "[FAILED STEP]: 'Main Loop' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");


    /* --- Find Final Result --- */

    prof.enter("Find Final Result");
    if (findFinalResult()) {
        std::cout << "[FAILED STEP]: 'Find Final Result' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");


    /* --- Output Data --- */

    prof.enter("Output Data");
    if (outputData()) {
        std::cout << "[FAILED STEP]: 'Output Data' was unsuccessful!\n";
        return;
    }
    prof.enter("Idle");

    
    prof.print();

    return;

}

// --- ENTRY POINT ---
int main(int argc, char* argv[]) {

    if (argc < 3 || argc > 4) {
        std::cout << "Usage: " << argv[0] << " <graph_file.txt> <num_cops> [visibility_p]\n";
        std::cout << "Example (1/2 Vis): " << argv[0] << " graph3.txt 4 2\n";
        std::cout << "Example (Standard): " << argv[0] << " graph3.txt 4\n";
        return 1;
    }

    const char* filename = argv[1];
    int k = std::stoi(argv[2]);
    
    int p = (argc == 4) ? std::stoi(argv[3]) : 1;
    
    solveCopsAndRobbers(filename, k, p);

    return 0;

}
