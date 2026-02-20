#pragma once


class Graph {

    /*
        General purpose data structure for storing undirected graphs
        Uses an internal adjacency matrix for edge states
    */

    public: 

        /*   Instance Variables   */

        int nodeCount;
        int edgeCount;

        // Constructors
        Graph();
        Graph(const char* fileName);

        // Destructor
        ~Graph();


        /*   Instance Functions   */

        // Returns true if an edge exists between the two passed nodes
        bool getEdge(int node1, int node2);

    private:

        /*   Instance Variables   */

        // Adjacency matrix
        bool* g;

};
