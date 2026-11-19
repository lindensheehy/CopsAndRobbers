#include <cstddef>
#include <random>
#include "../../solvers/k_cops_scotlandyard.cpp"
static int simulatedGpu(void*,const uint32_t* ids,const uint32_t* dest,int count,const uint64_t* words,uint8_t* out,char*,size_t) {
    using namespace sy;
    Cloud b;for(int w=0;w<4;++w)b.words[w]=words[w];
    for(int i=0;i<count;++i) {
        uint8_t best=0;Cloud mid=Cloud::binaryAndNot(b,copMask(dest[i]));
        eachVertex(mid,[&](int r){best=std::max(best,decodeLut[oracleRows[uint64_t(ids[i])*N+r]]);});
        out[i]=best==255?255:std::min<uint8_t>(14,best);
    }
    return 1;
}
int main() {
    using namespace sy;
    if(sy_provenance::sha256(reinterpret_cast<const uint8_t*>("abc"),3)!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 1;
    std::mt19937 rng(881);
    uint64_t checks=0;
    for(int n:{13,31,32,33,63,64,65,126,127,128,129,199,200}) for(int k=1;k<=4;++k) {
        N=n;K=k;oracleBase=(n==126?126:13);oracleCap=126-oracleBase;
        for(int b=0;b<256;++b) {int p=b>>1;decodeLut[b]=p<int(oracleBase)?255:p==127?uint8_t(oracleCap+1):uint8_t(p-oracleBase);}
        std::vector<uint8_t> raw(size_t(n)*513);for(auto& v:raw)v=uint8_t(rng());oracleRows=raw.data();
        for(int trial=0;trial<20;++trial) {
            for(auto& value:raw) {
                int payload=trial%3==0 ? int(oracleBase)+int(rng()%(128-oracleBase)) : trial%3==1 ? int(oracleBase)+int(rng()%std::min<uint32_t>(14,128-oracleBase)) : int(rng()%128);
                value=uint8_t(payload*2+(rng()%2));
            }
            Cloud cloud;for(int r=0;r<n;++r)if(trial==0 || rng()%4)cloud.set(r);
            std::vector<Cand> a(513),b;
            for(auto& c:a){c.id=rng()%513;c.dest=0;uint8_t pos[4]{};for(int j=0;j<k;++j)pos[j]=uint8_t(rng()%n);if(trial%3==0)for(int j=1;j<k;++j)pos[j]=pos[0];c.dest=packCops(pos);}
            b=a;haveAvx2=false;scoreCandidatesCPU(a.data(),int(a.size()),cloud);
#ifdef SY_X86
            haveAvx2=__builtin_cpu_supports("avx2");
#endif
            scoreCandidatesCPU(b.data(),int(b.size()),cloud);
            for(size_t i=0;i<a.size();++i){if(a[i].score!=b[i].score)return 2;++checks;}
            gpu.context=reinterpret_cast<void*>(1);gpu.score=simulatedGpu;gpu.minBatch=1;gpu.minWork=0;
            gpu.verify=(trial%2==0);
            scoreCandidates(b.data(),int(b.size()),cloud);
            for(size_t i=0;i<a.size();++i)if(a[i].score!=b[i].score)return 3;
            gpu.context=nullptr;
        }
    }
    std::cout<<"PASS: "<<checks<<" scalar/AVX and simulated-GPU host refinement scores; SHA-256 vector; AVX2="<<haveAvx2<<"\n";
}
