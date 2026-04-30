#include "core/game_memory_service.h"

#include <Windows.h>

#include <cstdint>
#include <utility>

#include "core/hooks.h"
#include "core/logger.h"

namespace
{
    const char* GetMemoryLabel(const char* label)
    {
        return (label != nullptr && label[0] != '\0') ? label : nullptr;
    }

    template <typename... Args>
    void LogMemoryError(const char* label, const char* labeledMessage,
        const char* unlabeledMessage, Args&&... args)
    {
        if (const char* name = GetMemoryLabel(label))
        {
            Logger::Error(labeledMessage, name, std::forward<Args>(args)...);
            return;
        }

        Logger::Error(unlabeledMessage, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void LogMemoryWarning(const char* label, const char* labeledMessage,
        const char* unlabeledMessage, Args&&... args)
    {
        if (const char* name = GetMemoryLabel(label))
        {
            Logger::Warning(labeledMessage, name, std::forward<Args>(args)...);
            return;
        }

        Logger::Warning(unlabeledMessage, std::forward<Args>(args)...);
    }
}

uintptr_t GameMemoryService::PatternScan(const char* pattern) const
{
    return Hooks::PatternScan(pattern);
}

bool GameMemoryService::ApplyPatch(uintptr_t address, const void* patch, std::size_t size) const
{
    return Hooks::ApplyPatch(address, static_cast<const uint8_t*>(patch), size);
}

bool GameMemoryService::ReadMemory(uintptr_t address, void* buffer, std::size_t size) const
{
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
        buffer, size, &bytesRead)
        && bytesRead == size;
}

bool GameMemoryService::WriteMemory(uintptr_t address, const void* data, std::size_t size) const
{
    return Hooks::WriteMemory(address, static_cast<const uint8_t*>(data), size);
}

bool GameMemoryService::ApplyPatternPatch(const char* pattern, const void* patch, std::size_t size,
    const char* label) const
{
    const uintptr_t address = PatternScan(pattern);
    if (address == 0)
    {
        LogMemoryWarning(label, "GameMemoryService: {} pattern not found",
            "GameMemoryService: pattern not found");
        return false;
    }

    const bool ok = ApplyPatch(address, patch, size);
    if (!ok)
    {
        LogMemoryError(label, "GameMemoryService: Failed to apply {} patch",
            "GameMemoryService: Failed to apply patch");
    }

    return ok;
}

bool GameMemoryService::TryGetModuleAddress(HMODULE module, uintptr_t offset,
    uintptr_t& address, const char* label) const
{
    if (!module)
    {
        LogMemoryError(label, "GameMemoryService: {} module is null",
            "GameMemoryService: module is null");
        return false;
    }

    address = reinterpret_cast<uintptr_t>(module) + offset;
    return true;
}

bool GameMemoryService::TryGetModuleValueAddress(HMODULE module, uintptr_t pointerOffset,
    uintptr_t valueOffset, uintptr_t& address, const char* label) const
{
    uintptr_t pointerAddress = 0;
    if (!TryGetModuleAddress(module, pointerOffset, pointerAddress, label))
    {
        return false;
    }

    uintptr_t basePtr = 0;
    if (!ReadMemory(pointerAddress, &basePtr, sizeof(basePtr)))
    {
        LogMemoryError(label,
            "GameMemoryService: Failed to read {} base pointer at 0x{:X}",
            "GameMemoryService: Failed to read base pointer at 0x{:X}",
            pointerAddress);
        return false;
    }

    if (basePtr == 0)
    {
        LogMemoryError(label, "GameMemoryService: {} base pointer resolved to null",
            "GameMemoryService: base pointer resolved to null");
        return false;
    }

    address = basePtr + valueOffset;
    return true;
}

bool GameMemoryService::ReadModuleOffset(HMODULE module, uintptr_t offset, void* buffer,
    std::size_t size, const char* label) const
{
    uintptr_t address = 0;
    if (!TryGetModuleAddress(module, offset, address, label))
    {
        return false;
    }

    const bool ok = ReadMemory(address, buffer, size);
    if (!ok)
    {
        LogMemoryError(label, "GameMemoryService: Failed to read {} value at 0x{:X}",
            "GameMemoryService: Failed to read value at 0x{:X}", address);
    }

    return ok;
}

bool GameMemoryService::ReadModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
    uintptr_t valueOffset, void* buffer, std::size_t size, const char* label) const
{
    uintptr_t valueAddress = 0;
    if (!TryGetModuleValueAddress(module, pointerOffset, valueOffset, valueAddress, label))
    {
        return false;
    }

    const bool ok = ReadMemory(valueAddress, buffer, size);
    if (!ok)
    {
        LogMemoryError(label, "GameMemoryService: Failed to read {} value at 0x{:X}",
            "GameMemoryService: Failed to read value at 0x{:X}", valueAddress);
    }

    return ok;
}

bool GameMemoryService::WriteModuleOffset(HMODULE module, uintptr_t offset, const void* data,
    std::size_t size, const char* label) const
{
    uintptr_t address = 0;
    if (!TryGetModuleAddress(module, offset, address, label))
    {
        return false;
    }

    const bool ok = WriteMemory(address, data, size);
    if (!ok)
    {
        LogMemoryError(label, "GameMemoryService: Failed to write {} patch at 0x{:X}",
            "GameMemoryService: Failed to write patch at 0x{:X}", address);
    }

    return ok;
}

bool GameMemoryService::WriteModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
    uintptr_t valueOffset, const void* data, std::size_t size, const char* label) const
{
    uintptr_t valueAddress = 0;
    if (!TryGetModuleValueAddress(module, pointerOffset, valueOffset, valueAddress, label))
    {
        return false;
    }

    const bool ok = WriteMemory(valueAddress, data, size);
    if (!ok)
    {
        LogMemoryError(label, "GameMemoryService: Failed to write {} value at 0x{:X}",
            "GameMemoryService: Failed to write value at 0x{:X}", valueAddress);
    }

    return ok;
}

bool GameMemoryService::ReadPointer(uintptr_t address, uintptr_t& value) const
{
    if (!ReadMemory(address, &value, sizeof(value)))
    {
        value = 0;
        return false;
    }

    return true;
}

uintptr_t GameMemoryService::ResolveRelativeAddress(uintptr_t instructionAddress,
    std::size_t displacementOffset) const
{
    int32_t relativeOffset = 0;
    if (!ReadMemory(instructionAddress + displacementOffset, &relativeOffset, sizeof(relativeOffset)))
    {
        return 0;
    }

    return instructionAddress + displacementOffset + sizeof(relativeOffset) + relativeOffset;
}

uintptr_t GameMemoryService::ResolvePointerChain(uintptr_t baseAddress, const uintptr_t* offsets,
    std::size_t offsetCount) const
{
    if (baseAddress == 0 || offsets == nullptr || offsetCount == 0)
    {
        return 0;
    }

    uintptr_t currentPtr = 0;
    if (!ReadPointer(baseAddress, currentPtr) || currentPtr == 0)
    {
        return 0;
    }

    for (std::size_t index = 0; index + 1 < offsetCount; ++index)
    {
        uintptr_t nextPtr = 0;
        if (!ReadPointer(currentPtr + offsets[index], nextPtr) || nextPtr == 0)
        {
            return 0;
        }

        currentPtr = nextPtr;
    }

    return currentPtr + offsets[offsetCount - 1];
}