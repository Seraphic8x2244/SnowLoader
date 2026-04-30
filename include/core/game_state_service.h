#pragma once

#include "sdk/script_api.h"

#include <string>

class GameStateService final : public IGameStateService
{
public:
    void Initialize(IGameMemoryService* memory);
    void Update();

    GameState GetState() const override;
    const char* GetLevelName() const override;
    bool IsInGame() const override;
    bool IsInMenu() const override;
    bool IsPaused() const override;

private:
    bool ReadEngineString(uintptr_t address, std::string& out) const;
    bool ReadPauseFlag() const;
    bool ScanPatterns();

    IGameMemoryService* m_Memory = nullptr;
    GameState m_State = GameState::Unknown;
    bool m_Paused = false;
    std::string m_LevelName;

    // Addresses resolved at init via pattern scanning (0 = unresolved)
    uintptr_t m_GameManagerPtrAddr = 0;
    uintptr_t m_WorldManagerPtrAddr = 0;

    // Object-relative offsets (stable across builds)
    static constexpr uintptr_t LEVEL_NAME_OFFSET = 0x10;
    static constexpr uintptr_t PAUSE_FLAG_OFFSET = 0x30;

    // Byte patterns for signature scanning
    //
    // Game manager getter (checks +0x50 flag, returns setz):
    //   mov rax,[rip+X]; test rax,rax; jnz +1; ret; cmp byte [rax+0x50],0; setz al; ret
    static constexpr const char* GAME_MANAGER_SIG =
        "48 8B 05 ?? ?? ?? ?? 48 85 C0 75 01 C3 80 78 50 00 0F 94 C0 C3";

    // World manager IsPaused (calls getter, checks +0x30 counter):
    //   sub rsp,0x28; call <getter>; mov rcx,[rax]; cmp dword [rcx+0x30],0; seta al; add rsp,0x28; ret
    static constexpr const char* WORLD_MANAGER_SIG =
        "48 83 EC 28 E8 ?? ?? ?? ?? 48 8B 08 83 79 30 00 0F 97 C0 48 83 C4 28 C3";
};
