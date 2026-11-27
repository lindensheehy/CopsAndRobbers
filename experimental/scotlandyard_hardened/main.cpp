// Dedicated driver: preserve nonzero failure codes without changing other solvers.
#include <cstddef>
#include "../../solvers/k_cops_scotlandyard.cpp"
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: scotlandyard_hardened.exe graph cops [period=1]\n";
            return 2;
        }
        if (loadGraphFile(argv[1], std::stoi(argv[2]), argc == 4 ? std::stoi(argv[3]) : 1)) return 2;
        if (buildAuxGraph() || initializeCaptures() || mainLoop() || findFinalResult() || outputData()) return 2;
        return sy::stopReason.find("worker error") != std::string::npos ? 3 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "; no completed verdict produced.\n";
        return 2;
    } catch (...) { std::cerr << "ERROR: unknown exception; no completed verdict produced.\n"; return 2; }
}
