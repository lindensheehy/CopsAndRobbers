#include "AdjacencyList.h"

#include <cstring> 

AdjacencyList::AdjacencyList(Graph* g) {

    nodeCount = g->nodeCount;

    // Step 1: Determine maxDegree
    maxDegree = 0;
    for (int i = 0; i < nodeCount; ++i) {
        int currentDegree = 0;
        for (int j = 0; j < nodeCount; ++j) {
            if (g->getEdge(i, j)) {
                currentDegree++;
            }
        }
        if (currentDegree > maxDegree) {
            maxDegree = currentDegree;
        }
    }

    // Step 2: Allocate memory and initialize terminators
    int totalSize = nodeCount * maxDegree;
    edges = new uint8_t[totalSize];
    std::memset(edges, 255, totalSize);

    // Step 3: Populate the flat array directly
    for (int i = 0; i < nodeCount; ++i) {
        int offset = i * maxDegree;
        int edgeIndex = 0;
        for (int j = 0; j < nodeCount; ++j) {
            if (g->getEdge(i, j)) {
                edges[offset + edgeIndex] = (uint8_t)j;
                edgeIndex++;
            }
        }
    }

    return;

}

AdjacencyList::AdjacencyList(int nodeCount, int maxDegree) : nodeCount(nodeCount), maxDegree(maxDegree) {
    
    int totalSize = nodeCount * maxDegree;
    this->edges = new uint8_t[totalSize];

    // Initialize the entire memory block to 255 (terminator)
    std::memset(this->edges, 255, totalSize);

    return;

}

AdjacencyList::~AdjacencyList() {
    delete[] this->edges;
}

void AdjacencyList::addEdge(uint8_t u, uint8_t v) {

    int offset = u * maxDegree;
    
    // Scan for the first open slot (marked by 255) and insert
    for (int i = 0; i < maxDegree; ++i) {
        if (this->edges[offset + i] == 255) {
            this->edges[offset + i] = v;
            return;
        }
    }

    return;

}

uint8_t* AdjacencyList::getEdges(int node) {
    return &(this->edges[node * maxDegree]);
}
