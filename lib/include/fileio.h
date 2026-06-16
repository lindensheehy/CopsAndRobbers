#pragma once

#include <cstddef>
#include <cstdint>


// Reads from fileName and returns a new heap allocated buffer containing the contents
uint8_t* readFile(const char* fileName, uintmax_t* fileLengthOut);

// Returns the length of fileName
bool getFileLength(const char* fileName, uintmax_t* fileLengthOut);

// Creates a file and puts size bytes from data into it
bool createFile(const char* fileName, uint8_t* data, uintmax_t size);
