#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "Allocator.h"
#include "Profiler.h"
#include <iostream>
#include <vector>
#include <string>

bool loadGraphFile(const char* filename, int k);
bool buildAuxGraph();
bool initializeCaptures();
bool mainLoop();
bool findFinalResult();
bool outputData();

#include ALGORITHM_INCLUDE

// --- MAIN ALGORITHM ---
void solveCopsAndRobbers(const char* filename, int k) {

    Profiler p;


    /* --- Load Graph File --- */

    p.enter("Load Graph File");
    if (loadGraphFile(filename, k)) {
        std::cout << "[FAILED STEP]: 'Load Graph File' was unsuccessful!\n";
        return;
    }
    p.enter("Idle");


    /* --- Build Aux Graph --- */

    p.enter("Build Aux Graph");
    if (buildAuxGraph()) {
        std::cout << "[FAILED STEP]: 'Build Aux Graph' was unsuccessful!\n";
        return;
    }
    p.enter("Idle");


    /* --- Initialize Captures --- */

    p.enter("Initialize Captures");
    if (initializeCaptures()) {
        std::cout << "[FAILED STEP]: 'Initialize Captures' was unsuccessful!\n";
        return;
    }
    p.enter("Idle");


    /* --- Main Loop --- */
    
    p.enter("Main Loop");
    if (mainLoop()) {
        std::cout << "[FAILED STEP]: 'Main Loop' was unsuccessful!\n";
        return;
    }
    p.enter("Idle");


    /* --- Find Final Result --- */

    p.enter("Find Final Result");
    if (findFinalResult()) {
        std::cout << "[FAILED STEP]: 'Find Final Result' was unsuccessful!\n";
        return;
    }
    p.enter("Idle");


    /* --- Output Data --- */

    p.enter("Output Data");
    if (outputData()) {
        std::cout << "[FAILED STEP]: 'Output Data' was unsuccessful!\n";
        return;
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
