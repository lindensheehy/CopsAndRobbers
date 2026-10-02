#pragma once
#include <cstddef>
#include <cstdint>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef __CUDACC__
#define HD __host__ __device__
#else
#define HD
#endif
namespace v32 {
using Cost = uint64_t;
constexpr Cost INF = UINT64_MAX;
struct Mask {
    uint64_t w[4];
    HD bool empty() const { return !(w[0] | w[1] | w[2] | w[3]); }
    HD void set(int r) { w[r / 64] |= uint64_t(1) << (r % 64); }
    HD void remove(const Mask& occupied) { for (int j=0;j<4;++j) w[j] &= ~occupied.w[j]; }
};
HD inline int lowbit(uint64_t x) {
#ifdef __CUDA_ARCH__
    return __ffsll(static_cast<long long>(x))-1;
#elif defined(_MSC_VER)
    unsigned long bit; _BitScanForward64(&bit,x); return int(bit);
#else
    return __builtin_ctzll(x);
#endif
}
struct View {
    int n, p;
    const size_t* heads;
    const size_t* edges; // Existing AuxGraph targets are cId * N, NOT cId.
    const Mask* occupied;
    const Mask* neighbors;
    const Cost* values; // Immutable for the entire sweep.
};
struct Frame { Mask cloud; size_t next, end; };
struct Task { Cost best; int depth; };
HD inline Frame frame(View g,size_t c,Mask cloud) {
    return {cloud,g.heads[c],g.heads[c+1]};
}
HD inline void start(View g,size_t root,Task& task,Frame* stack) {
    task.best=g.values[root];
    task.depth=task.best==0 ? 0 : 1;
    if(task.depth) { Mask cloud{}; cloud.set(int(root%g.n)); stack[0]=frame(g,root/g.n,cloud); }
}
// Resume at most `budget` DFS loop iterations. Stacks persist across launches.
// Each root owns its task, stack and output: no shared DP writes during search.
HD inline void advance(View g,Task& task,Frame* stack,int budget) {
    for(int work=0;work<budget && task.depth; ++work) {
        const int d=task.depth;
        Frame& f=stack[d-1];
        const Cost copCost=Cost(2)*d-1;
        if(f.next==f.end || copCost>=task.best) { --task.depth; continue; }
        size_t base=g.edges[f.next++];
        size_t c=base/g.n;
        Mask cloud=f.cloud;
        cloud.remove(g.occupied[c]);
        if(cloud.empty()) { task.best=copCost; --task.depth; continue; }
        Mask expanded{};
        for(int w=0;w<4;++w) {
            uint64_t bits=cloud.w[w];
            while(bits) {
                int r=w*64+lowbit(bits); bits&=bits-1;
                for(int j=0;j<4;++j) expanded.w[j]|=g.neighbors[r].w[j];
            }
        }
        expanded.remove(g.occupied[c]);
        const Cost elapsed=Cost(2)*d;
        if(expanded.empty()) {
            if(elapsed<task.best) task.best=elapsed;
            continue;
        }
        if(d==g.p) {
            Cost worst=0;
            for(int w=0;w<4 && worst!=INF;++w) {
                uint64_t bits=expanded.w[w];
                while(bits) {
                    int r=w*64+lowbit(bits); bits&=bits-1;
                    Cost child=g.values[base+r];
                    if(child==INF) { worst=INF; break; }
                    if(child>worst) worst=child;
                }
            }
            if(worst!=INF && worst<INF-elapsed && worst+elapsed<task.best)
                task.best=worst+elapsed;
            continue;
        }
        if(elapsed+1>=task.best) continue;
        stack[d]=frame(g,c,expanded);
        ++task.depth;
    }
}
}
