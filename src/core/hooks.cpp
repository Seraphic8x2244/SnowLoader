#include <Windows.h>

#include "core/hooks.h"

#include <Psapi.h>
#include <cstring>
#include <sstream>

#include "core/logger.h"

PatternBytes::PatternBytes(const char* pattern)
{
    std::istringstream iss(pattern);
    std::string byteStr;

    while (iss >> byteStr)
    {
        if (byteStr == "??" || byteStr == "?")
        {
            bytes.push_back(-1);
        }
        else
        {
            try
            {
                bytes.push_back(std::stoi(byteStr, nullptr, 16));
            }
            catch (...)
            {
                Logger::Warning("Invalid byte in pattern: {}", byteStr);
            }
        }
    }
}

uintptr_t Hooks::PatternScan(const char* pattern)
{
    HMODULE gameModule = GetModuleHandleA(nullptr);
    if (!gameModule)
    {
        Logger::Error("Failed to get game module handle");
        return 0;
    }

    MODULEINFO modInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(MODULEINFO)))
    {
        Logger::Error("Failed to get module information");
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    const uintptr_t size = modInfo.SizeOfImage;

    PatternBytes patternBytes(pattern);
    if (patternBytes.bytes.empty())
    {
        Logger::Error("Invalid pattern");
        return 0;
    }

    for (uintptr_t i = 0; i < size - patternBytes.bytes.size(); i++)
    {
        bool found = true;
        for (size_t j = 0; j < patternBytes.bytes.size(); j++)
        {
            if (patternBytes.bytes[j] != -1)
            {
                const uint8_t byte = *(reinterpret_cast<uint8_t*>(base + i + j));
                if (byte != patternBytes.bytes[j])
                {
                    found = false;
                    break;
                }
            }
        }

        if (found)
        {
            Logger::Info("Pattern found at: 0x{:X}", base + i);
            return base + i;
        }
    }

    Logger::Warning("Pattern not found");
    return 0;
}

bool Hooks::ApplyPatch(uintptr_t address, const uint8_t* patch, size_t size)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Logger::Error("Failed to change memory protection");
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(address), patch, size);

    DWORD dummy = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &dummy))
    {
        Logger::Error("Failed to restore memory protection");
        return false;
    }

    Logger::Info("Patch applied at: 0x{:X} ({} bytes)", address, size);
    return true;
}

std::vector<uint8_t> Hooks::ReadMemory(uintptr_t address, size_t size)
{
    std::vector<uint8_t> buffer(size);
    std::memcpy(buffer.data(), reinterpret_cast<void*>(address), size);
    return buffer;
}

bool Hooks::WriteMemory(uintptr_t address, const uint8_t* data, size_t size)
{
    return ApplyPatch(address, data, size);
}
