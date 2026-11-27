// Optional C ABI backend. The entire quantized oracle stays in device RAM.
// 0..13 exact, 14 = >=14 (host refines), 15 = full-visibility loss.
#include <cuda_runtime.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <memory>
#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif
struct Context {
    unsigned char* table = nullptr;
    uint32_t *ids = nullptr, *dest = nullptr;
    unsigned char* scores = nullptr;
    int n = 0, k = 0, capacity = 0;
    ~Context() { cudaFree(scores); cudaFree(dest); cudaFree(ids); cudaFree(table); }
};
static bool checked(cudaError_t e, char* error, size_t size) {
    if (e == cudaSuccess) return true;
    std::snprintf(error, size, "%s", cudaGetErrorString(e)); return false;
}
API void* sy_gpu_create(const uint8_t* raw, uint64_t states, int n, int k,
                        int base, int cap, uint64_t budget, char* error, size_t errorSize) {
    try {
        if (!raw || n < 1 || n > 200 || k < 1 || k > 4 || base < 1 || base > 126 || cap != 126-base) {
            std::snprintf(error,errorSize,"invalid GPU parameters"); return nullptr;
        }
        if (!checked(cudaSetDevice(0),error,errorSize)) return nullptr;
        size_t freeBytes=0,total=0;
        if (!checked(cudaMemGetInfo(&freeBytes,&total),error,errorSize)) return nullptr;
        const uint64_t needed = (states+1)/2;
        // Leave at least 512 MiB free for display/driver and bounded scratch.
        if (needed > budget || freeBytes < (512ull<<20) || needed > freeBytes-(512ull<<20)) {
            std::snprintf(error,errorSize,"packed oracle needs %.2f GiB; insufficient free VRAM/budget",double(needed)/(1ull<<30));
            return nullptr;
        }
        std::unique_ptr<Context> c(new Context); c->n=n; c->k=k;
        if (!checked(cudaMalloc(reinterpret_cast<void**>(&c->table),size_t(needed)),error,errorSize)) return nullptr;
        uint8_t lut[256];
        for(int b=0;b<256;++b) { int p=b>>1; lut[b]=p<base?15:uint8_t(std::min(14,p==127?cap+1:p-base)); }
        const size_t chunk = 16u<<20;
        std::vector<uint8_t> packed(size_t(std::min<uint64_t>(needed,chunk)));
        for(uint64_t off=0;off<needed;off+=chunk) {
            size_t count=size_t(std::min<uint64_t>(chunk,needed-off));
            for(size_t j=0;j<count;++j) {
                uint64_t index=2*(off+j);
                packed[j]=lut[raw[index]] | uint8_t((index+1<states?lut[raw[index+1]]:0)<<4);
            }
            if(!checked(cudaMemcpy(c->table+off,packed.data(),count,cudaMemcpyHostToDevice),error,errorSize)) return nullptr;
        }
        return c.release();
    } catch (...) { std::snprintf(error,errorSize,"GPU initialization host allocation failed"); return nullptr; }
}
__global__ void scoreKernel(const uint8_t* table, const uint32_t* ids, const uint32_t* dest,
                            uint8_t* scores, int count, int n, int k,
                            uint64_t b0,uint64_t b1,uint64_t b2,uint64_t b3) {
    int lane=threadIdx.x&31;
    int i=(blockIdx.x*blockDim.x+threadIdx.x)/32;
    if(i>=count) return; // uniform for the whole warp
    uint64_t bits[4]={b0,b1,b2,b3};
    uint32_t d=dest[i]; unsigned best=0;
    for(int r=lane;r<n;r+=32) {
        bool include=((bits[r/64]>>(r%64))&1)!=0;
        for(int j=0;j<k;++j) if(int((d>>(8*j))&255)==r) include=false;
        if(include) {
            uint64_t index=uint64_t(ids[i])*n+r;
            unsigned value=(table[index/2]>>(4*(index&1)))&15;
            best=max(best,value);
        }
    }
    for(int delta=16;delta;delta/=2) best=max(best,__shfl_down_sync(0xffffffffu,best,delta));
    if(lane==0) scores[i]=uint8_t(best==15?255:best);
}
API int sy_gpu_score(void* handle,const uint32_t* ids,const uint32_t* dest,int count,
                     const uint64_t* cloud,uint8_t* out,char* error,size_t errorSize) {
    auto* c=static_cast<Context*>(handle);
    if(!c || count<1 || count>2000000) { std::snprintf(error,errorSize,"invalid GPU batch");return 0; }
    if(!checked(cudaSetDevice(0),error,errorSize)) return 0;
    if(count>c->capacity) {
        cudaFree(c->ids);c->ids=nullptr;cudaFree(c->dest);c->dest=nullptr;cudaFree(c->scores);c->scores=nullptr;c->capacity=0;
        if(!checked(cudaMalloc(reinterpret_cast<void**>(&c->ids),size_t(count)*4),error,errorSize) ||
           !checked(cudaMalloc(reinterpret_cast<void**>(&c->dest),size_t(count)*4),error,errorSize) ||
           !checked(cudaMalloc(reinterpret_cast<void**>(&c->scores),size_t(count)),error,errorSize)) return 0;
        c->capacity=count;
    }
    if(!checked(cudaMemcpy(c->ids,ids,size_t(count)*4,cudaMemcpyHostToDevice),error,errorSize) ||
       !checked(cudaMemcpy(c->dest,dest,size_t(count)*4,cudaMemcpyHostToDevice),error,errorSize)) return 0;
    scoreKernel<<<(count+3)/4,128>>>(c->table,c->ids,c->dest,c->scores,count,c->n,c->k,cloud[0],cloud[1],cloud[2],cloud[3]);
    if(!checked(cudaGetLastError(),error,errorSize)) return 0;
    return checked(cudaMemcpy(out,c->scores,size_t(count),cudaMemcpyDeviceToHost),error,errorSize)?1:0;
}
API void sy_gpu_destroy(void* handle) { delete static_cast<Context*>(handle); }
