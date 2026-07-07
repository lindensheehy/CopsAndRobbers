#include "AdjacencyList.h"

#include <cstring> 

AdjacencyList::AdjacencyList(Graph* g) {

    this->constructFrom(g);

    return;

}

AdjacencyList::AdjacencyList(int nodeCount, int maxDegree) : nodeCount(nodeCount), maxDegree(maxDegree) {

    // Stride is maxDegree + 1: the extra slot guarantees that a node whose degree
    // equals maxDegree still has a trailing 255 terminator. Without it, consumers
    // that scan `while (edges[e] != 255)` run off the end of the row (and, for the
    // last node, off the end of the whole allocation) -> heap corruption.
    int totalSize = nodeCount * (maxDegree + 1);
    this->edges = new uint8_t[totalSize];

    // Initialize the entire memory block to 255 (terminator)
    std::memset(this->edges, 255, totalSize);

    return;

}

AdjacencyList::~AdjacencyList() {
    delete[] this->edges;
}

void AdjacencyList::constructFrom(Graph* g) {

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

    // Step 2: Allocate memory and initialize terminators.
    // Stride is maxDegree + 1 so every row keeps a trailing 255 terminator even
    // when its degree equals maxDegree (see the (int, int) constructor for why).
    int totalSize = nodeCount * (maxDegree + 1);
    edges = new uint8_t[totalSize];
    std::memset(edges, 255, totalSize);

    // Step 3: Populate the flat array directly
    for (int i = 0; i < nodeCount; ++i) {
        int offset = i * (maxDegree + 1);
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

uint8_t* AdjacencyList::getEdges(int node) const {
    return &(this->edges[node * (maxDegree + 1)]);
}

void AdjacencyList::addEdge(uint8_t u, uint8_t v) {

    int offset = u * (maxDegree + 1);

    // Scan for the first open slot (marked by 255) and insert. Bound is maxDegree
    // (not maxDegree + 1) so the trailing terminator slot is never overwritten.
    for (int i = 0; i < maxDegree; ++i) {
        if (this->edges[offset + i] == 255) {
            this->edges[offset + i] = v;
            return;
        }
    }

    return;

}

size_t AdjacencyList::getMemoryFootprint() const {
    return sizeof(*this) + (this->nodeCount * (this->maxDegree + 1) * sizeof(uint8_t));
}
