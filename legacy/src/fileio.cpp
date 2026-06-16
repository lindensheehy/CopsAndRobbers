#include "fileio.h"
#include <cstdio>
#include <filesystem>
#include <system_error>

uint8_t* readFile(const char* fileName, std::uintmax_t* fileLengthOut) {

    std::uintmax_t fileLength;
    bool fileLengthFound = getFileLength(fileName, &fileLength);
    if (fileLengthFound) return nullptr;
    if (fileLength == 0) return nullptr;

    if (fileLengthOut == nullptr) return nullptr;
    *fileLengthOut = fileLength;

    std::FILE* file = std::fopen(fileName, "rb");
    if (!file) return nullptr;

    uint8_t* buf = new uint8_t[fileLength];
    size_t n = std::fread(buf, 1, fileLength, file);
    std::fclose(file);

    if (n != fileLength || n == 0) {
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
