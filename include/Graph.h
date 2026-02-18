#pragma once


class Graph {

    /*
        General purpose data structure for storing undirected graphs
        Uses an internal adjacency matrix for edge states
    */

    public: 

        /*   Instance Variables   */

        // Adjacency matrix
        bool** g;

        int nodeCount;
        int edgeCount;

        // Constructors
        Graph();
        Graph(const char* fileName);

        // Destructor
        ~Graph();


        /*   Instance Functions   */



    private:

        /*   Instance Variables   */

};
