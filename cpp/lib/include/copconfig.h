#pragma once

#include "Allocator.h"
#include <cstddef>
#include <cstdint>

// Maximum supported number of cops to prevent stack overflow during generation
constexpr size_t MAX_COPS = 256;

// Generates all unique, sorted cop configurations in lexicographical order.
// Returns a flat heap-allocated array. The caller is responsible for calling delete[].
uint8_t* generateCopConfigs(uint32_t k, int N, size_t* outNumConfigs, Allocator* allocator);
