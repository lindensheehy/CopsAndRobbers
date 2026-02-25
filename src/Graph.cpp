#include "Graph.h"
#include "fileio.h"


Graph::Graph() {

    this->g = nullptr;
    this->nodeCount = 0;
    this->edgeCount = 0;

    return;

}

Graph::Graph(const char* fileName) {

    this->g = nullptr;
    this->nodeCount = 0;
    this->edgeCount = 0;

    std::uintmax_t fileLength = 0;
    uint8_t* buf = readFile(fileName, &fileLength);

    if (!buf) return;

    // 1. Determine nodeCount by scanning until the first newline or '-'
    uint32_t cols = 0;
    while (cols < fileLength && buf[cols] != '\n' && buf[cols] != '\r' && buf[cols] != '-') {
        cols++;
    }

    // Protect against empty or heavily malformed files
    if (cols == 0) {
        delete[] buf;
        return; 
    }

    this->nodeCount = cols;

    // 2. Allocate the flat 1D Adjacency Matrix (one single allocation!)
    this->g = new bool[this->nodeCount * this->nodeCount]{false};

    // 3. Parse the matrix
    int row = 0;
    int col = 0;
    int totalOnes = 0;

    for (size_t i = 0; i < fileLength; ++i) {
        char c = static_cast<char>(buf[i]);

        if (c == '-') break; // End marker hit

        if (c == '0' || c == '1') {
            if (row < this->nodeCount && col < this->nodeCount) {
                bool isEdge = (c == '1');
                
                // MATH: Convert 2D coordinates to 1D index
                this->g[row * this->nodeCount + col] = isEdge;
                
                if (isEdge) totalOnes++;
            }
            col++;
        } else if (c == '\n') {
            // Newline means jump to the next row safely
            if (col > 0) { 
                row++;
                col = 0;
            }
        }
    }

    // 4. Calculate edge count (every undirected edge is listed twice)
    this->edgeCount = totalOnes / 2;

    delete[] buf;

    return;

}

Graph::~Graph() {
    delete[] this->g;
}

bool Graph::getEdge(int node1, int node2) const {

    if (!this->g) return false;

    // Bounds check
    if (node1 < 0 || node1 >= this->nodeCount || node2 < 0 || node2 >= this->nodeCount) {
        return false;
    }

    // MATH: Convert 2D coordinates to 1D index
    return this->g[node1 * this->nodeCount + node2];

}
