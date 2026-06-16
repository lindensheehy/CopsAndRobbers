#include "Profiler.h"
#include <algorithm>

Profiler::Profiler() {
    this->globalStart = std::chrono::high_resolution_clock::now();
    this->lastMark = this->globalStart;
    this->currentSection = "";
    this->isRunning = true;
}

void Profiler::enter(const std::string& name) {
    if (!this->isRunning) return;

    auto now = std::chrono::high_resolution_clock::now();

    // If we are currently in a section, calculate its delta and add to its total
    if (!this->currentSection.empty()) {
        std::chrono::duration<double> elapsed = now - this->lastMark;
        this->sectionTotals[this->currentSection] += elapsed.count();
    }

    // If this is the first time seeing this section, record its chronological order
    if (this->sectionTotals.find(name) == this->sectionTotals.end()) {
        this->sectionOrder.push_back(name);
        this->sectionTotals[name] = 0.0; // Initialize
    }

    this->currentSection = name;
    this->lastMark = now;
}

void Profiler::stop() {
    if (!this->isRunning || this->currentSection.empty()) return;

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - this->lastMark;
    this->sectionTotals[this->currentSection] += elapsed.count();
    
    this->currentSection = "";
    this->isRunning = false; // Prevents further recording unless reset
}

void Profiler::print() {
    // Implicitly close the final running section to ensure its time is captured
    this->stop();

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> totalAppTimeDuration = now - this->globalStart;
    double totalAppTime = totalAppTimeDuration.count();

    double totalTrackedTime = 0.0;
    for (const auto& pair : this->sectionTotals) {
        totalTrackedTime += pair.second;
    }

    double untrackedTime = totalAppTime - totalTrackedTime;
    if (untrackedTime < 0.0) untrackedTime = 0.0; // Safety clamp for floating point inaccuracies

    // --- ALIGNMENT CALCULATIONS ---
    size_t maxTier2 = 0;
    for (const std::string& name : this->sectionOrder) {
        // Reduced indent to 2 spaces before the arrow
        maxTier2 = std::max(maxTier2, std::string("  -> " + name + " ").length());
    }

    // Determine baseline alignment for the drill down
    size_t A2 = maxTier2 + 3; 

    // --- LAMBDA FOR PRINTING ROWS ---
    auto printLine = [&](int tier, const std::string& prefixStr, double seconds, bool showPercent = false) {
        size_t L = prefixStr.length();
        
        size_t targetAlign;
        if (tier == 2) {
            targetAlign = A2; // Dynamic alignment for the drill down
        } else if (tier == 1) {
            targetAlign = 32; // Hardcoded static padding for Tier 1
        } else {
            targetAlign = 37; // Hardcoded static padding for Tier 0 (+5 over Tier 1)
        }
        
        size_t numHyphens = (targetAlign > L + 2) ? (targetAlign - L - 2) : 1;
        
        std::cout << "||" << prefixStr << std::string(numHyphens, '-') << "=> "
                  << std::right << std::fixed << std::setprecision(4) << std::setw(12) << seconds << " s";
                  
        if (showPercent) {
            if (totalAppTime > 0) {
                double pct = (seconds / totalAppTime) * 100.0;
                std::cout << " (" << std::fixed << std::setprecision(2) << std::setw(6) << pct << "%)";
            } else {
                std::cout << " (  0.00%)";
            }
        }
        std::cout << "\n";
    };

    // --- RENDER REPORT ---
    std::cout << "\n||>>>>>=====-----=====<<<<<     Timing Profiler Report     >>>>>=====-----=====<<<<<\n";
    std::cout << "||\n";
    
    printLine(0, "   Total App Uptime ", totalAppTime, true);
    if (totalTrackedTime > 0) {
        printLine(1, "    -> Tracked Execution ", totalTrackedTime, true);
    }
    if (untrackedTime > 0.0001) { // Only show if untracked overhead is meaningful
        printLine(1, "    -> Untracked Overhead ", untrackedTime, true);
    }
    
    std::cout << "||\n||\n";
    
    if (!this->sectionOrder.empty()) {
        std::cout << "||  ---===<<<>>>===---   Drill Down   ---===<<<>>>===---\n";
        std::cout << "||\n";
        
        for (const std::string& name : this->sectionOrder) {
            // Apply the reduced 2-space indentation here as well
            printLine(2, "  -> " + name + " ", this->sectionTotals[name], true);
        }
    }
    
    std::cout << "||\n||>>>>>>>>>>>>>>>>================------------------================<<<<<<<<<<<<<<<<\n\n";
}
