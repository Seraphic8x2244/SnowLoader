#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Hooks
{
public:
    static uintptr_t PatternScan(const char* pattern);
    static bool ApplyPatch(uintptr_t address, const uint8_t* patch, size_t size);
    static std::vector<uint8_t> ReadMemory(uintptr_t address, size_t size);
    static bool WriteMemory(uintptr_t address, const uint8_t* data, size_t size);
};

struct PatternBytes
{
    std::vector<int> bytes;

    PatternBytes(const char* pattern);
};
