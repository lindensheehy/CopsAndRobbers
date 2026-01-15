#include "fileio.h"
#include <cstdio>
#include <filesystem>
#include <system_error>

uint8_t* readFile(const char* fileName) {

    std::uintmax_t file_length;
    bool file_length_found = getFileLength(fileName, &file_length);
    if (file_length_found) return nullptr;
    if (file_length == 0) return nullptr;

    std::FILE* file = std::fopen(fileName, "rb");
    if (!file) return nullptr;

    uint8_t* buf = new uint8_t[file_length];
    size_t n = std::fread(buf, 1, file_length, file);
    std::fclose(file);

    if (n != file_length || n == 0) {
        delete[] buf;
        return nullptr;
    }

    return buf;

}

bool getFileLength(const char* fileName, std::uintmax_t * size) {

    std::error_code ec;
    std::uintmax_t temp = std::filesystem::file_size(fileName, ec);
    if (ec) return true;
    
    *size = temp;
    return false;

}
