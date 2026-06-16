#pragma once

#include "Graph.h"

#include <cstdint>

class AdjacencyList {

    /*
        General purpose Adjacency List
        This uses a flat contiguous array with a constant stride based on the max degree within the graph
        Intended for large, sparse graphs
    */

    public:

        /*   Instance Variables   */

        int nodeCount;
        int maxDegree;

        // Constructors

        AdjacencyList() : nodeCount(0), maxDegree(0), edges(nullptr) {}
        AdjacencyList(Graph* g);
        AdjacencyList(int nodeCount, int maxDegree);


        // Destructor
        ~AdjacencyList();


        /*   Instance Functions   */

        // Deferred constructor
        void constructFrom(Graph* g);

        // Returns a pointer to list of edges connected to node. This list will have length at most this->maxDegree. 
        // The value 255 serves as a terminator of the data (no more edges are connected), even if the index has not reached maxDegree-1
        uint8_t* getEdges(int node) const;

        // Adds the edge (u, v) to the internal array   
        void addEdge(uint8_t u, uint8_t v);

        // Returns the total memory footprint of the adjacency list in bytes
        size_t getMemoryFootprint() const;

    private:

        /*   Instance Variables    */

        uint8_t* edges;

};
