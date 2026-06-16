#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <iostream>
#include <iomanip>

class Profiler {
    private:
        using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

        TimePoint globalStart;
        TimePoint lastMark;
        
        std::string currentSection;
        std::unordered_map<std::string, double> sectionTotals;
        std::vector<std::string> sectionOrder; // Preserves chronological execution order

        bool isRunning;

    public:
        // Constructor starts the global application clock
        Profiler();

        // Enters a new section, implicitly closing and tallying the previous one
        void enter(const std::string& name);

        // Closes the currently running section (called automatically by print, but available manually)
        void stop();

        // Prints the styled timing report
        void print();
};
