// Original V5 producer, with reliable exit status and a completion receipt even
// when no universally winning cop start exists. No algorithm changes.
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include "../../solvers/k_cops_fullvis_v5.cpp"
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: v5_bench graph k receipt.json");
        if(loadGraphFile(argv[1],std::stoi(argv[2]),1) || buildAuxGraph() || initializeCaptures() || mainLoop() || findFinalResult() || outputData())return 2;
        std::ofstream result(argv[3]);
        result<<"{\"completed_fixed_point\":true,\"oracle_export_completed\":true,\"vertices\":"<<N<<",\"cops\":"<<k<<"}\n";
        result.flush();return result?0:2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
