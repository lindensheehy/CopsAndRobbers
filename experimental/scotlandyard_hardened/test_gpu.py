"""Run on a CUDA machine after build.py --cuda. Fails if CUDA is absent; never silently passes."""
import argparse, ctypes as C, os, random
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,default=ROOT/'build/bin'/('sy_gpu.dll' if os.name=='nt' else 'libsy_gpu.so'));a=p.parse_args()
lib=C.CDLL(str(a.library.resolve()))
u8=C.POINTER(C.c_uint8);u32=C.POINTER(C.c_uint32);u64=C.POINTER(C.c_uint64)
lib.sy_gpu_create.argtypes=[u8,C.c_uint64,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint64,C.c_char_p,C.c_size_t];lib.sy_gpu_create.restype=C.c_void_p
lib.sy_gpu_score.argtypes=[C.c_void_p,u32,u32,C.c_int,u64,u8,C.c_char_p,C.c_size_t];lib.sy_gpu_score.restype=C.c_int
lib.sy_gpu_destroy.argtypes=[C.c_void_p];lib.sy_gpu_destroy.restype=None
rng=random.Random(771);checks=0
for n,kind in [(n,kind) for n in (13,31,32,33,126,199,200) for kind in range(3)]:
    configs=4097;base=126 if n==126 else 13;cap=126-base
    raw=bytes(2*(rng.randrange(128) if kind==0 else rng.randrange(base,128) if kind==1 else rng.randrange(base,min(128,base+14)))+rng.randrange(2) for _ in range(n*configs));array=(C.c_uint8*len(raw)).from_buffer_copy(raw)
    error=C.create_string_buffer(512);handle=lib.sy_gpu_create(array,len(raw),n,4,base,cap,256<<20,error,512)
    assert handle,error.value
    try:
        for count in (1,3,4,5,2048,4097):
            cloud=set(range(n)) if count%2 else {r for r in range(n) if rng.randrange(3)}
            words=(C.c_uint64*4)(*[sum(1<<(r%64) for r in cloud if r//64==w) for w in range(4)])
            ids=(C.c_uint32*count)(*[rng.randrange(configs) for _ in range(count)])
            positions=[([rng.randrange(n)]*4 if i%3==0 else [rng.randrange(n) for _ in range(4)]) for i in range(count)]
            dest=(C.c_uint32*count)(*[sum(r<<(8*j) for j,r in enumerate(pos)) for pos in positions]);out=(C.c_uint8*count)()
            assert lib.sy_gpu_score(handle,ids,dest,count,words,out,error,512),error.value
            for i,pos in enumerate(positions):
                values=[]
                for r in cloud-set(pos):
                    payload=raw[ids[i]*n+r]>>1
                    values.append(255 if payload<base else min(14,cap+1 if payload==127 else payload-base))
                assert out[i]==max(values,default=0),(n,count,i,out[i],max(values,default=0))
                checks+=1
    finally:lib.sy_gpu_destroy(handle)
print(f'PASS: {checks} CUDA/reference scores (odd/even rows, tails, duplicate cops, saturation, INF)')
