#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

void check(cudaError_t error)
{
    if (error != cudaSuccess) {
        std::fprintf(stderr, "CUDA error: %s\n",
                     cudaGetErrorString(error));
        std::exit(1);
    }
}

__global__ void doubleValues(int* values, int count)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < count)
        values[i] *= 2;
}

int main()
{
    check(cudaSetDevice(0));

    cudaDeviceProp gpu;
    check(cudaGetDeviceProperties(&gpu, 0));
    std::printf("GPU: %s\n", gpu.name);

    const int count = 1000000;
    const size_t bytes = count * sizeof(int);

    std::vector<int> values(count);
    for (int i = 0; i < count; ++i)
        values[i] = i;

    int* deviceValues = nullptr;
    check(cudaMalloc(reinterpret_cast<void**>(&deviceValues), bytes));

    // Upload the entire array once.
    check(cudaMemcpy(deviceValues, values.data(), bytes,
                     cudaMemcpyHostToDevice));

    // One launch processes one million values.
    doubleValues<<<(count + 255) / 256, 256>>>(deviceValues, count);
    check(cudaGetLastError());
    check(cudaDeviceSynchronize());

    // Download the entire result once.
    check(cudaMemcpy(values.data(), deviceValues, bytes,
                     cudaMemcpyDeviceToHost));
    check(cudaFree(deviceValues));

    for (int i = 0; i < count; ++i) {
        if (values[i] != 2 * i) {
            std::fprintf(stderr, "FAIL at index %d\n", i);
            return 1;
        }
    }

    std::puts("PASS: all 1,000,000 GPU results verified.");
    return 0;
}