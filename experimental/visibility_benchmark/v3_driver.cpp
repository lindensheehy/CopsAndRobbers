// Read-only instrumentation of the ORIGINAL repository solver. Search is unchanged.
#include <cstddef>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include "../../solvers/k_cops_visibility_v3.cpp"
int main(int argc,char** argv) {
    try {
        if(argc!=6) throw std::runtime_error("Usage: v3_bench graph k p states.csv receipt.json");
        auto clock=[] {return std::chrono::steady_clock::now();};
        auto begin=clock();
        if(loadGraphFile(argv[1],std::stoi(argv[2]),std::stoi(argv[3])) || buildAuxGraph() || initializeCaptures()) return 2;
        auto initialized=clock();if(mainLoop())return 2;auto solved=clock();
        std::ofstream csv(argv[4]);if(!csv)throw std::runtime_error("Cannot open state dump");
        csv<<"config,cops,robber,marked,reported_ply\n";
        size_t marked=0,universal=0,best=aux.configCount;int bestCost=127;
        for(size_t c=0;c<aux.configCount;++c) {
            bool all=true;int worst=0;
            for(int r=0;r<N;++r) {
                auto* state=aux.getState(c,r,0);
                csv<<c<<",\"";
                for(int j=0;j<k;++j)csv<<(j?",":"")<<int(aux.configs[c*k+j]);
                csv<<"\","<<r<<','<<int(state->marked)<<',';
                if(state->marked){csv<<int(state->markedRound);++marked;worst=std::max(worst,int(state->markedRound));}
                else all=false;
                csv<<'\n';
            }
            if(all){++universal;if(worst<bestCost){bestCost=worst;best=c;}}
        }
        csv.flush();if(!csv)throw std::runtime_error("Failed writing state dump");
        auto exported=clock();auto seconds=[](auto a,auto b){return std::chrono::duration<double>(b-a).count();};
        std::ofstream result(argv[5]);
        result<<std::setprecision(12)<<"{\"completed\":true,\"vertices\":"<<N<<",\"cops\":"<<k<<",\"period\":"<<p
              <<",\"configs\":"<<aux.configCount<<",\"marked\":"<<marked<<",\"universal_starts\":"<<universal
              <<",\"initialization_seconds\":"<<seconds(begin,initialized)<<",\"search_seconds\":"<<seconds(initialized,solved)
              <<",\"export_seconds\":"<<seconds(solved,exported)<<",\"best_reported_ply\":";
        if(best<aux.configCount)result<<bestCost;else result<<"null";
        result<<",\"best_start\":";
        if(best<aux.configCount){result<<'[';for(int j=0;j<k;++j)result<<(j?",":"")<<int(aux.configs[best*k+j]);result<<']';}
        else result<<"null";
        result<<"}\n";result.flush();return result?0:2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
