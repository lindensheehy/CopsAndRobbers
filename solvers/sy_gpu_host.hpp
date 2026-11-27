// Included inside namespace sy after the CPU scorer. CPU and CUDA share no C++ objects.
using GpuCreate = void* (*)(const uint8_t*,uint64_t,int,int,int,int,uint64_t,char*,size_t);
using GpuScore = int (*)(void*,const uint32_t*,const uint32_t*,int,const uint64_t*,uint8_t*,char*,size_t);
using GpuDestroy = void (*)(void*);
struct GpuBackend {
    void* context=nullptr;
#ifdef _WIN32
    HMODULE library=nullptr;
#else
    void* library=nullptr;
#endif
    GpuScore score=nullptr; GpuDestroy destroy=nullptr;
    bool verify=false, disabled=false, autoTune=false;
    int samples=0;double sampledGpu=0,sampledCpu=0;
    uint64_t minBatch=2048, minWork=131072;
    std::mutex mutex;
    std::vector<uint32_t> ids,dest;
    std::vector<uint8_t> result;
    std::atomic<uint64_t> batches{0},rows{0},refined{0},busy{0},errors{0};
    double setupSeconds=0, scoreSeconds=0;
    ~GpuBackend() {
        if(context && destroy) destroy(context);
#ifdef _WIN32
        if(library) FreeLibrary(library);
#else
        if(library) dlclose(library);
#endif
    }
    void* symbol(const char* name) {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(library,name));
#else
        return dlsym(library,name);
#endif
    }
} gpu;

bool initializeGpu(std::string& error) {
    const std::string requested=env("SY_GPU")?env("SY_GPU"):"off";
    if(requested=="off" || requested=="0") return true;
    if(requested!="auto" && requested!="verify" && requested!="required") {
        error="SY_GPU must be off, auto, verify or required";return false;
    }
    gpu.verify=requested=="verify";gpu.autoTune=requested=="auto";
    const bool optional=requested=="auto";
    auto unavailable=[&](const std::string& why){
        std::cerr<<"GPU unavailable: "<<why<<(optional?"; using CPU.\n":"\n");
        error=why;return optional;
    };
    if(mode==Mode::Off) return unavailable("oracle scoring is off");
    uint64_t budget=7168;
    if(env("SY_GPU_MB") && (!parseUInt(env("SY_GPU_MB"),budget) || budget>1048576)) {error="bad SY_GPU_MB";return false;}
    if(env("SY_GPU_MIN_BATCH") && (!parseUInt(env("SY_GPU_MIN_BATCH"),gpu.minBatch) || !gpu.minBatch)) {error="bad SY_GPU_MIN_BATCH";return false;}
    if(env("SY_GPU_MIN_WORK") && !parseUInt(env("SY_GPU_MIN_WORK"),gpu.minWork)) {error="bad SY_GPU_MIN_WORK";return false;}
    const std::string path=env("SY_GPU_LIBRARY")?env("SY_GPU_LIBRARY"):
#ifdef _WIN32
        "build/bin/sy_gpu.dll";
    gpu.library=LoadLibraryW(std::filesystem::absolute(path).wstring().c_str());
#else
        "build/bin/libsy_gpu.so";
    gpu.library=dlopen(std::filesystem::absolute(path).c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
    if(!gpu.library) return unavailable("cannot load "+path);
    auto create=reinterpret_cast<GpuCreate>(gpu.symbol("sy_gpu_create"));
    gpu.score=reinterpret_cast<GpuScore>(gpu.symbol("sy_gpu_score"));
    gpu.destroy=reinterpret_cast<GpuDestroy>(gpu.symbol("sy_gpu_destroy"));
    if(!create || !gpu.score || !gpu.destroy) return unavailable("GPU plugin ABI incomplete");
    std::cout<<"  packing/uploading GPU oracle: "<<double((configCount*N+1)/2)/(1ull<<30)<<" GiB (one-time setup)\n"<<std::flush;
    char err[512]{};
    auto begin=std::chrono::steady_clock::now();
    gpu.context=create(oracleRows,configCount*N,N,K,int(oracleBase),int(oracleCap),budget<<20,err,sizeof(err));
    gpu.setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(!gpu.context) return unavailable(err);
    std::cout<<"  GPU oracle ready; batches >= "<<gpu.minBatch<<", position checks >= "<<gpu.minWork
             <<(gpu.verify?"; every result CPU-verified":"")<<"\n";
    return true;
}

void scoreCandidates(Cand* cands,int n,const Cloud& cloud) {
    if(!gpu.context || uint64_t(n)<gpu.minBatch || uint64_t(n)*popcount(cloud)<gpu.minWork) {
        scoreCandidatesCPU(cands,n,cloud);return;
    }
    std::unique_lock<std::mutex> lock(gpu.mutex,std::try_to_lock);
    if(!lock.owns_lock()) { ++gpu.busy;scoreCandidatesCPU(cands,n,cloud);return; }
    if(gpu.disabled) {lock.unlock();scoreCandidatesCPU(cands,n,cloud);return;}
    gpu.ids.resize(n);gpu.dest.resize(n);gpu.result.resize(n);
    for(int i=0;i<n;++i){gpu.ids[i]=cands[i].id;gpu.dest[i]=cands[i].dest;}
    char error[512]{};
    const auto begin=std::chrono::steady_clock::now();
    if(!gpu.score(gpu.context,gpu.ids.data(),gpu.dest.data(),n,cloud.words,gpu.result.data(),error,sizeof(error))) {
        ++gpu.errors;gpu.disabled=true;
        std::cerr<<"GPU disabled after runtime error: "<<error<<"; recomputing batch on CPU.\n";
        lock.unlock();scoreCandidatesCPU(cands,n,cloud);return;
    }
    ++gpu.batches;gpu.rows+=uint64_t(n);
    for(int i=0;i<n;++i) {
        if(gpu.result[i]==14) {scoreCandidatesCPU(cands+i,1,cloud);++gpu.refined;}
        else cands[i].score=gpu.result[i];
    }
    const double batchGpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(gpu.verify || (gpu.autoTune && gpu.samples<8)) {
        std::vector<Cand> cpu(cands,cands+n);
        const auto cpuBegin=std::chrono::steady_clock::now();
        scoreCandidatesCPU(cpu.data(),n,cloud);
        const double batchCpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-cpuBegin).count();
        for(int i=0;i<n;++i) if(cands[i].score!=cpu[i].score)
            throw std::runtime_error("GPU/CPU score mismatch; no result from this batch is trusted");
        if(gpu.autoTune) {
            ++gpu.samples;gpu.sampledGpu+=batchGpu;gpu.sampledCpu+=batchCpu;
            if(gpu.samples==8 && gpu.sampledGpu>gpu.sampledCpu*1.10) {
                gpu.disabled=true;
                std::cerr<<"GPU auto calibration: CPU faster on sampled batches; remaining work uses CPU.\n";
            }
        }
    }
    gpu.scoreSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
}
