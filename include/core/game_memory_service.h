#pragma once

#include "sdk/script_api.h"

class GameMemoryService final : public IGameMemoryService
{
public:
    uintptr_t PatternScan(const char* pattern) const override;
    bool ApplyPatch(uintptr_t address, const void* patch, std::size_t size) const override;
    bool ReadMemory(uintptr_t address, void* buffer, std::size_t size) const override;
    bool WriteMemory(uintptr_t address, const void* data, std::size_t size) const override;
    bool ApplyPatternPatch(const char* pattern, const void* patch, std::size_t size,
        const char* label = nullptr) const override;
    bool ReadModuleOffset(HMODULE module, uintptr_t offset, void* buffer,
        std::size_t size, const char* label = nullptr) const override;
    bool ReadModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, void* buffer, std::size_t size,
        const char* label = nullptr) const override;
    bool WriteModuleOffset(HMODULE module, uintptr_t offset, const void* data,
        std::size_t size, const char* label = nullptr) const override;
    bool WriteModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, const void* data, std::size_t size,
        const char* label = nullptr) const override;
    bool ReadPointer(uintptr_t address, uintptr_t& value) const override;
    uintptr_t ResolveRelativeAddress(uintptr_t instructionAddress,
        std::size_t displacementOffset) const override;
    uintptr_t ResolvePointerChain(uintptr_t baseAddress, const uintptr_t* offsets,
        std::size_t offsetCount) const override;

private:
    bool TryGetModuleAddress(HMODULE module, uintptr_t offset, uintptr_t& address,
        const char* label) const;
    bool TryGetModuleValueAddress(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, uintptr_t& address, const char* label) const;
};