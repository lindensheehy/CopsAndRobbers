// Completed subproblem snapshots. No active frame is serialized, and no ABORT is stored.
// Each entry is sampled with the same atomic sequence protocol as ttProbe.
// A snapshot is a set of independent proved facts; it need not represent one global instant.
uint64_t memoInterval=600, memoSaved=0, memoLoaded=0;
bool memoCheckpoint=true;
constexpr size_t MEMO_RECORD=42;
bool snapshotEntry(TTEntry* e,uint8_t* record) {
    const uint32_t seq=__atomic_load_n(&e->seq,__ATOMIC_ACQUIRE);
    if(seq&1) return false;
    const uint8_t round=__atomic_load_n(&e->round,__ATOMIC_RELAXED);
    if(!round) return false;
    auto put=[&](int offset,uint64_t value,int count){for(int i=0;i<count;++i) record[offset+i]=uint8_t(value>>(8*i));};
    put(0,__atomic_load_n(&e->cfg,__ATOMIC_RELAXED),4);
    for(int w=0;w<4;++w) put(4+8*w,__atomic_load_n(&e->cloud[w],__ATOMIC_RELAXED),8);
    put(36,__atomic_load_n(&e->move,__ATOMIC_RELAXED),4);
    record[40]=round;record[41]=__atomic_load_n(&e->result,__ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return __atomic_load_n(&e->seq,__ATOMIC_RELAXED)==seq;
}
bool saveMemo() {
    if(!memoCheckpoint || checkpointPath.empty() || !ttEnabled) return true;
    try {
        const std::string target=checkpointPath+".memo", temp=target+".tmp";
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out<<"SY_MEMO_V1\n"<<identity<<'\n';
        auto countAt=out.tellp();out<<std::string(20,'0')<<'\n';
        auto hashAt=out.tellp();out<<std::string(64,'0')<<'\n';
        sy_provenance::Sha256 hash;uint64_t count=0;uint8_t record[MEMO_RECORD];
        for(uint64_t i=0;i<(ttMask+1)*2;++i) {
            if(!snapshotEntry(tt+i,record)) continue;
            out.write(reinterpret_cast<const char*>(record),MEMO_RECORD);
            hash.update(record,MEMO_RECORD);++count;
        }
        out.seekp(countAt);out<<std::setw(20)<<std::setfill('0')<<count;
        out.seekp(hashAt);out<<hash.finish();out.flush();
        if(!out) return false;
        out.close();
        if(!replaceFile(temp,target)) return false;
        memoSaved=count;return true;
    } catch (...) {return false;}
}
bool loadMemo(std::string& error) {
    const std::string path=checkpointPath+".memo";
    if(!memoCheckpoint || !ttEnabled || !std::filesystem::exists(path)) return true;
    std::ifstream in(path,std::ios::binary);
    std::string magic,idText,countText,digest;
    std::getline(in,magic);std::getline(in,idText);std::getline(in,countText);std::getline(in,digest);
    uint64_t id=0,count=0;
    if(magic!="SY_MEMO_V1" || !parseUInt(idText,id) || id!=identity || !parseUInt(countText,count)) {
        error="memo checkpoint identity/header mismatch";return false;
    }
    const auto offset=in.tellg();
    if(offset<0 || count>configCount*uint64_t(N)*256 || std::filesystem::file_size(path)!=uint64_t(offset)+count*MEMO_RECORD) {
        error="memo checkpoint length invalid";return false;
    }
    // Validate completely BEFORE making any saved fact available to the search.
    sy_provenance::Sha256 hash;uint8_t record[MEMO_RECORD];
    auto get=[&](int off,int n){uint64_t x=0;for(int i=0;i<n;++i)x|=uint64_t(record[off+i])<<(8*i);return x;};
    for(uint64_t i=0;i<count;++i) {
        if(!in.read(reinterpret_cast<char*>(record),MEMO_RECORD)) {error="truncated memo";return false;}
        hash.update(record,MEMO_RECORD);
        if(get(0,4)>=configCount || !record[40] || record[40]>horizon || record[41]>1) {error="invalid memo state";return false;}
        for(int r=N;r<256;++r) if((get(4+(r/64)*8,8)>>(r%64))&1) {error="invalid memo cloud";return false;}
    }
    if(hash.finish()!=digest) {error="memo checksum mismatch";return false;}
    in.clear();in.seekg(offset);
    for(uint64_t i=0;i<count;++i) {
        if(!in.read(reinterpret_cast<char*>(record),MEMO_RECORD)) {error="memo changed while reading";return false;}
        Cloud b;for(int w=0;w<4;++w)b.words[w]=get(4+8*w,8);
        uint32_t cfg=uint32_t(get(0,4));int round=record[40];
        ttStore(cfg,b,round,ttHash(cfg,b,round),record[41],uint32_t(get(36,4)));
    }
    memoLoaded=count;
    std::cout<<"  restored "<<count<<" completed subproblems from memo snapshot\n";
    return true;
}
