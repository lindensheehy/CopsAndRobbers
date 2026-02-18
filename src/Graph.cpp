#include "Graph.h"

#include "fileio.h"

Graph::Graph() {

    this->g = nullptr;
    this->nodeCount = 0;
    this->edgeCount = 0;

    return;

}

Graph::Graph(const char* fileName) {
    
    std::intmax_t fileLength;
    uint8_t* file = readFile(fileName, &fileLength);

    this->g = nullptr;
    this->nodeCount = 0;
    this->edgeCount = 0;

    return;

}

Graph::~Graph() {
    delete this->g;
}
