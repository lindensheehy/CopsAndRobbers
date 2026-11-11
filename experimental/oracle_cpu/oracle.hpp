#pragma once
#include "core.hpp"
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace sy {
class Mapping {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
#else
    int fd = -1;
#endif
  public:
    const uint8_t *data = nullptr;
    uint64_t size = 0;
    explicit Mapping(const std::string &path) {
        static_assert(sizeof(size_t) >= 8, "Use a 64-bit compiler");
        size = std::filesystem::file_size(path);
        if (size < 64 || size > SIZE_MAX)
            throw std::runtime_error("Invalid oracle file size");
#ifdef _WIN32
        file = CreateFileW(std::filesystem::path(path).wstring().c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping)
            data = static_cast<const uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
#else
        fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            void *p = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED)
                data = static_cast<const uint8_t *>(p);
        }
#endif
        if (!data) {
            close();
            throw std::runtime_error("Cannot map oracle read-only");
        }
    }
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    void close() {
#ifdef _WIN32
        if (data)
            UnmapViewOfFile(data);
        if (mapping)
            CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        mapping = nullptr;
        file = INVALID_HANDLE_VALUE;
#else
        if (data)
            munmap(const_cast<uint8_t *>(data), size);
        if (fd >= 0)
            ::close(fd);
        fd = -1;
#endif
        data = nullptr;
    }
    ~Mapping() { close(); }
};
struct Oracle : Values {
    Mapping map;
    int n;
    uint32_t base, cap;
    uint64_t best;
    uint64_t get(int off, int bytes) const {
        uint64_t x = 0;
        for (int i = 0; i < bytes; ++i)
            x |= uint64_t(map.data[off + i]) << (8 * i);
        return x;
    }
    Oracle(const std::string &path, const Graph &g, const Index &ix, const std::string &original)
        : map(path), n(g.n) {
        if (original.empty())
            throw std::runtime_error(
                "Supply --oracle-graph: the original matrix used to build the oracle");
        Graph provenance(original);
        if (provenance.canonical != g.canonical)
            throw std::runtime_error("Oracle graph differs from search graph");
        base = uint32_t(get(52, 4));
        cap = uint32_t(get(56, 4));
        best = get(32, 8);
        if (g.degree > 125 || get(0, 4) != 0x35564643 || get(4, 4) != 3 ||
            get(8, 4) != uint64_t(g.n) || get(12, 4) != uint64_t(ix.k) ||
            get(16, 8) != ix.count() || get(24, 8) != ix.count() * g.n || get(44, 4) != 2 ||
            get(48, 4) != uint64_t(g.degree) || base != uint32_t(g.degree + 1) ||
            cap != 126 - base || get(60, 4) != 127 || map.size != 64 + ix.count() * g.n ||
            (best != UINT64_MAX && best >= ix.count()))
            throw std::runtime_error("V5 oracle header/size/rules mismatch");
    }
    uint32_t robberLower(uint64_t c, int r) const override {
        uint8_t p = map.data[64 + c * n + r] >> 1;
        if (p < base)
            return INF; // Only valid for a COMPLETED fixed-point oracle.
        return p == 127 ? cap + 1 : p - base;
    }
    bool saturated(uint64_t c, int r) const { return (map.data[64 + c * n + r] >> 1) == 127; }
};
inline void replaceFile(const std::string &from, const std::string &to) {
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::path(from).wstring().c_str(),
                     std::filesystem::path(to).wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Checkpoint replace failed");
#else
    std::filesystem::rename(from, to);
#endif
}
} // namespace sy
