#include <cstddef>
#include "Graph.h"
#include "AdjacencyList.h"
#include "AuxGraph.h"
#include "search.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef __CUDACC__
#include <cuda_runtime.h>
#endif
using namespace v32;
using Clock=std::chrono::steady_clock;
static double seconds(Clock::time_point t) { return std::chrono::duration<double>(Clock::now()-t).count(); }
struct Result { std::vector<Cost> values; size_t passes=0; double time=0; };
static void progress(size_t pass,size_t changes) {
    std::cout<<"Pass "<<pass<<": "<<changes<<" improved states"<<std::endl;
}
static Result cpuSolve(View g,const std::vector<Cost>& seed,size_t threads) {
    auto begin=Clock::now(); Result result{seed}; std::vector<Cost> next(seed.size());
    for(;;) {
        g.values=result.values.data();
        std::atomic<size_t> cursor{0}, changes{0};
        auto worker=[&]() {
            std::vector<Frame> stack(g.p);
            size_t localChanges=0;
            for(;;) {
                size_t first=cursor.fetch_add(32);
                if(first>=seed.size()) break;
                for(size_t root=first;root<std::min(first+32,seed.size());++root) {
                    Task t; start(g,root,t,stack.data());
                    while(t.depth) advance(g,t,stack.data(),4096);
                    next[root]=t.best;
                    localChanges+=t.best<g.values[root];
                }
            }
            changes.fetch_add(localChanges);
        };
        std::vector<std::thread> workers;
        for(size_t i=1;i<threads;++i) workers.emplace_back(worker);
        worker(); for(auto& t:workers) t.join();
        result.values.swap(next); ++result.passes;
        progress(result.passes,changes.load());
        if(!changes) break;
    }
    result.time=seconds(begin); return result;
}
#ifdef __CUDACC__
static void check(cudaError_t e) {
    if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e));
}
template<class T> struct Device {
    T* ptr=nullptr;
    explicit Device(size_t n) { check(cudaMalloc(reinterpret_cast<void**>(&ptr),n*sizeof(T))); }
    ~Device() { if(ptr) cudaFree(ptr); }
    Device(const Device&)=delete;
    void upload(const T* data,size_t n) { check(cudaMemcpy(ptr,data,n*sizeof(T),cudaMemcpyHostToDevice)); }
};
__global__ void initTasks(View g,size_t base,size_t count,Task* tasks,Frame* stacks) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i<count) start(g,base+i,tasks[i],stacks+i*g.p);
}
__global__ void resumeTasks(View g,size_t base,size_t count,Task* tasks,Frame* stacks,
                            int budget,unsigned* pending,unsigned* changed,Cost* output) {
    size_t i=size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i>=count) return;
    Task t=tasks[i];
    if(!t.depth) { output[base+i]=t.best; return; }
    advance(g,t,stacks+i*g.p,budget);
    tasks[i]=t;
    if(t.depth) atomicAdd(pending,1u);
    else { output[base+i]=t.best; if(t.best<g.values[base+i]) atomicExch(changed,1u); }
}
static Result gpuSolve(View host,const std::vector<Cost>& seed,size_t configs,size_t edgeCount,
                       size_t batch,int budget) {
    auto begin=Clock::now();
    check(cudaSetDevice(0)); cudaDeviceProp prop{}; check(cudaGetDeviceProperties(&prop,0));
    std::cout<<"GPU: "<<prop.name<<std::endl;
    batch=std::min(batch,seed.size());
    size_t freeBytes,total; check(cudaMemGetInfo(&freeBytes,&total));
    long double required=(long double)(configs+1+edgeCount)*sizeof(size_t)
        +(long double)(configs+host.n)*sizeof(Mask)+(long double)seed.size()*sizeof(Cost)*2
        +(long double)batch*(sizeof(Task)+(long double)host.p*sizeof(Frame))+2*sizeof(unsigned);
    std::cout<<"GPU buffers: "<<double(required/1048576)<<" MiB; free: "<<freeBytes/1048576<<" MiB\n";
    if(required>freeBytes*0.9L) throw std::runtime_error("GPU buffers exceed 90% of free VRAM. Reduce --batch or use a smaller graph/k; graph tables must fit.");
    Device<size_t> heads(configs+1),edges(std::max(size_t(1),edgeCount));
    Device<Mask> occupied(configs),neighbors(host.n);
    Device<Cost> a(seed.size()),b(seed.size());
    Device<Task> tasks(batch); Device<Frame> stacks(batch*host.p);
    Device<unsigned> pending(1),changed(1);
    heads.upload(host.heads,configs+1); edges.upload(host.edges,edgeCount);
    occupied.upload(host.occupied,configs); neighbors.upload(host.neighbors,host.n);
    a.upload(seed.data(),seed.size());
    Cost* current=a.ptr; Cost* output=b.ptr;
    View g{host.n,host.p,heads.ptr,edges.ptr,occupied.ptr,neighbors.ptr,current};
    Result result; size_t launches=0;
    for(;;) {
        g.values=current; check(cudaMemset(changed.ptr,0,sizeof(unsigned)));
        for(size_t base=0;base<seed.size();base+=batch) {
            size_t count=std::min(batch,seed.size()-base);
            unsigned blocks=unsigned((count+127)/128);
            initTasks<<<blocks,128>>>(g,base,count,tasks.ptr,stacks.ptr);
            check(cudaGetLastError());
            unsigned remaining;
            do {
                check(cudaMemset(pending.ptr,0,sizeof(unsigned)));
                resumeTasks<<<blocks,128>>>(g,base,count,tasks.ptr,stacks.ptr,budget,pending.ptr,changed.ptr,output);
                check(cudaGetLastError());
                check(cudaMemcpy(&remaining,pending.ptr,sizeof(unsigned),cudaMemcpyDeviceToHost));
                ++launches;
                if(launches%1000==0) std::cout<<"Launch "<<launches<<", pass "<<result.passes+1
                    <<", batch root "<<base<<", unfinished "<<remaining<<std::endl;
            } while(remaining);
        }
        unsigned any; check(cudaMemcpy(&any,changed.ptr,sizeof(unsigned),cudaMemcpyDeviceToHost));
        std::swap(current,output); ++result.passes;
        std::cout<<"GPU pass "<<result.passes<<": "<<(any?"improvements":"converged")<<std::endl;
        if(!any) break;
    }
    result.values.resize(seed.size());
    check(cudaMemcpy(result.values.data(),current,seed.size()*sizeof(Cost),cudaMemcpyDeviceToHost));
    result.time=seconds(begin);
    std::cout<<"Resumable search launches: "<<launches<<'\n';
    return result;
}
#endif
static size_t positive(const char* s) {
    std::string v(s); size_t used=0;
    if(v.empty() || v[0]=='-') throw std::runtime_error("Expected a positive integer");
    unsigned long long n=std::stoull(v,&used);
    if(used!=v.size() || !n || n>std::numeric_limits<size_t>::max()) throw std::runtime_error("Invalid positive integer");
    return size_t(n);
}
int main(int argc,char** argv) {
    try {
        if(argc<4) { std::cerr<<"Usage: v3_2 graph k p [--backend cpu|gpu|compare] [--threads N] [--batch N] [--budget N] [--dump file]\n"; return 1; }
        const size_t cops=positive(argv[2]),visibility=positive(argv[3]);
        if(cops>256 || visibility>1000000) throw std::runtime_error("k must be <=256; p must be <=1000000");
        size_t threads=std::max(1u,std::thread::hardware_concurrency()),batch=4096,budget=128;
        std::string backend="cpu",dump;
        for(int i=4;i<argc;i+=2) {
            if(i+1>=argc) throw std::runtime_error("Missing option value");
            std::string arg=argv[i];
            if(arg=="--backend") backend=argv[i+1];
            else if(arg=="--threads") threads=positive(argv[i+1]);
            else if(arg=="--batch") batch=positive(argv[i+1]);
            else if(arg=="--budget") budget=positive(argv[i+1]);
            else if(arg=="--dump") dump=argv[i+1];
            else throw std::runtime_error("Unknown option: "+arg);
        }
        if(backend!="cpu" && backend!="gpu" && backend!="compare") throw std::runtime_error("Invalid backend");
        if(threads>1024 || batch>1048576 || budget>1000000) throw std::runtime_error("threads<=1024, batch<=1048576, budget<=1000000 required");
#ifndef __CUDACC__
        if(backend!="cpu") throw std::runtime_error("This executable was built without CUDA");
#endif
        auto setup=Clock::now(); Graph graph(argv[1]);
        if(graph.nodeCount<1 || graph.nodeCount>200) throw std::runtime_error("Graph must contain 1..200 vertices");
        const int n=graph.nodeCount;
        std::vector<Mask> neighbors(n);
        for(int u=0;u<n;++u) {
            for(int v=0;v<n;++v) {
                if(graph.getEdge(u,v)!=graph.getEdge(v,u)) throw std::runtime_error("Only undirected graphs supported");
                if(graph.getEdge(u,v)) neighbors[u].set(v);
            }
            // Existing AuxGraph cannot safely generate transitions for a zero-degree cop.
            if(neighbors[u].empty()) throw std::runtime_error("Isolated vertex: no-move rules are undefined; add an explicit self-loop if passing is intended");
        }
        // Bound arithmetic before using the legacy AuxGraph combinatorial generator.
        size_t configs=1; size_t choose=std::min(cops,size_t(n-1));
        for(size_t i=1;i<=choose;++i) {
            size_t factor=n+cops-choose-1+i;
            if(configs>std::numeric_limits<size_t>::max()/factor) throw std::runtime_error("Configuration count overflow");
            configs=configs*factor/i;
        }
        if(configs>std::numeric_limits<size_t>::max()/size_t(n)/sizeof(Cost)/2)
            throw std::runtime_error("State table size overflow");
        if(configs*size_t(n)>(INF-1)/(2*visibility)) throw std::runtime_error("Capture-time bound exceeds numeric range");
        std::cout<<"Configurations: "<<configs<<"; states: "<<configs*n
            <<"; two value tables: "<<double(configs)*n*sizeof(Cost)*2/1048576<<" MiB\n";
        AdjacencyList adj; adj.constructFrom(&graph); Allocator memory; AuxGraph<Cost> aux;
        aux.setSelfEdges(SelfEdgeCop::FALSE,SelfEdgeRobber::FALSE);
        aux.constructFrom(int(cops),1,&adj,&memory);
        if(aux.configCount!=configs) throw std::runtime_error("AuxGraph configuration count mismatch");
        std::vector<Mask> occupied(configs);
        for(size_t c=0;c<configs;++c) for(size_t j=0;j<cops;++j) occupied[c].set(aux.configs[c*cops+j]);
        std::vector<Cost> seed(aux.numStates,INF);
        for(size_t c=0;c<configs;++c) for(int r=0;r<n;++r) if(aux.isInstantCatch(c,r)) seed[c*n+r]=0;
        View g{n,int(visibility),aux.transitionHeads,aux.transitions.data(),occupied.data(),neighbors.data(),nullptr};
        std::cout<<"Shared graph setup seconds: "<<seconds(setup)<<"\nCPU threads: "<<threads<<std::endl;
        Result result;
        if(backend=="cpu" || backend=="compare") {
            result=cpuSolve(g,seed,threads);
            std::cout<<"CPU solve seconds: "<<result.time<<std::endl;
        }
#ifdef __CUDACC__
        if(backend=="gpu" || backend=="compare") {
            Result gpu=gpuSolve(g,seed,configs,aux.transitions.size(),batch,int(budget));
            std::cout<<"GPU solve seconds (allocation + transfers + search): "<<gpu.time<<std::endl;
            if(backend=="compare") {
                for(size_t s=0;s<seed.size();++s) if(result.values[s]!=gpu.values[s])
                    throw std::runtime_error("CPU/GPU mismatch at flat state "+std::to_string(s));
                std::cout<<"PASS: all "<<seed.size()<<" CPU/GPU state costs match. Speedup: "<<result.time/gpu.time<<"x\n";
            }
            result=std::move(gpu);
        }
#endif
        Cost best=INF; size_t bestC=0;
        for(size_t c=0;c<configs;++c) {
            Cost worst=0;
            for(int r=0;r<n;++r) worst=std::max(worst,result.values[c*n+r]);
            if(worst<best) { best=worst; bestC=c; }
        }
        if(best==INF) std::cout<<"RESULT: LOSS under the stated movement/visibility rules.\n";
        else {
            std::cout<<"RESULT: WIN. Capture Time: "<<best<<" plies. Cop positions:";
            for(size_t j=0;j<cops;++j) std::cout<<' '<<int(aux.configs[bestC*cops+j]);
            std::cout<<'\n';
        }
        if(!dump.empty()) {
            std::ofstream file(dump); if(!file) throw std::runtime_error("Cannot open dump");
            file<<"config,robber,cost\n";
            for(size_t s=0;s<seed.size();++s) {
                file<<s/n<<','<<s%n<<',';
                if(result.values[s]==INF) file<<"INF"; else file<<result.values[s];
                file<<'\n';
            }
            if(!file) throw std::runtime_error("Failed writing dump");
        }
        return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
