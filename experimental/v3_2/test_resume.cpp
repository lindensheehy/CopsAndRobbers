#include "search.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace v32;
int main() {
    std::mt19937 rng(32);
    for(int trial=0;trial<400;++trial) {
        const int n=6,p=1+trial%5;
        std::vector<Mask> neighbors(n),occupied(n);
        std::vector<size_t> heads{0},edges;
        for(int u=0;u<n;++u) {
            occupied[u].set(u);
            for(int v=0;v<n;++v) if(v==(u+1)%n || rng()%3==0) {
                neighbors[u].set(v); edges.push_back(v*n);
            }
            heads.push_back(edges.size());
        }
        std::vector<Cost> costs(n*n);
        for(int s=0;s<n*n;++s) costs[s]=s/n==s%n ? 0 : (rng()%3 ? Cost(rng()%40+1) : INF);
        View g{n,p,heads.data(),edges.data(),occupied.data(),neighbors.data(),costs.data()};
        for(int root=0;root<n*n;++root) {
            std::vector<Frame> a(p),b(p); Task x,y;
            start(g,root,x,a.data()); start(g,root,y,b.data());
            while(x.depth) advance(g,x,a.data(),1000000);
            while(y.depth) advance(g,y,b.data(),1);
            assert(x.best==y.best);
        }
    }
    std::cout<<"PASS: 14,400 root searches match with budget 1 vs 1,000,000\n";
}
