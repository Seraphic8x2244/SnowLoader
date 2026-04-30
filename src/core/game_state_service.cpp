#include "core/game_state_service.h"
#include "core/logger.h"

#include <Windows.h>

#include <cstring>

void GameStateService::Initialize(IGameMemoryService* memory)
{
    m_Memory = memory;
    m_State = GameState::Unknown;
    m_Paused = false;
    m_LevelName.clear();
    m_GameManagerPtrAddr = 0;
    m_WorldManagerPtrAddr = 0;

    if (m_Memory)
    {
        ScanPatterns();
    }
}

bool GameStateService::ScanPatterns()
{
    // Resolve game manager pointer address
    auto match = m_Memory->PatternScan(GAME_MANAGER_SIG);
    if (match == 0)
    {
        Logger::Error("GameStateService: failed to find game manager signature");
        return false;
    }
    // mov rax, [rip+disp32] — displacement at offset 3
    m_GameManagerPtrAddr = m_Memory->ResolveRelativeAddress(match, 3);
    Logger::Info("GameStateService: game manager ptr at {:X}", m_GameManagerPtrAddr);

    // Resolve world manager pointer address (two-level: call target → lea target)
    match = m_Memory->PatternScan(WORLD_MANAGER_SIG);
    if (match == 0)
    {
        Logger::Error("GameStateService: failed to find world manager signature");
        return false;
    }
    // E8 <rel32> is at offset 4 in the pattern — resolve the CALL target
    uintptr_t getterFunc = m_Memory->ResolveRelativeAddress(match + 4, 1);
    // Getter is: lea rax, [rip+disp32]; ret — displacement at offset 3
    m_WorldManagerPtrAddr = m_Memory->ResolveRelativeAddress(getterFunc, 3);
    Logger::Info("GameStateService: world manager ptr at {:X}", m_WorldManagerPtrAddr);

    return true;
}

void GameStateService::Update()
{
    if (!m_Memory || m_GameManagerPtrAddr == 0)
    {
        m_State = GameState::Unknown;
        m_LevelName.clear();
        return;
    }

    // Read the game manager pointer
    uintptr_t managerObj = 0;
    if (!m_Memory->ReadPointer(m_GameManagerPtrAddr, managerObj) || managerObj == 0)
    {
        m_State = GameState::Unknown;
        m_Paused = false;
        m_LevelName.clear();
        return;
    }

    // Read the level name engine string at object + LEVEL_NAME_OFFSET
    std::string levelName;
    if (!ReadEngineString(managerObj + LEVEL_NAME_OFFSET, levelName))
    {
        m_State = GameState::Unknown;
        m_Paused = false;
        m_LevelName.clear();
        return;
    }

    m_LevelName = std::move(levelName);

    if (m_LevelName.empty() || m_LevelName == "level_main_menu")
    {
        m_State = GameState::MainMenu;
        m_Paused = false;
    }
    else
    {
        // The game manager can retain the old level name after returning
        // to the main menu. Cross-check with the world manager — if no
        // world is loaded, we're not actually in-game.
        uintptr_t worldObj = 0;
        if (m_WorldManagerPtrAddr != 0)
            m_Memory->ReadPointer(m_WorldManagerPtrAddr, worldObj);

        if (worldObj == 0)
        {
            m_State = GameState::MainMenu;
            m_Paused = false;
        }
        else
        {
            m_State = GameState::InGame;
            m_Paused = ReadPauseFlag();
        }
    }
}

GameState GameStateService::GetState() const
{
    return m_State;
}

const char* GameStateService::GetLevelName() const
{
    return m_LevelName.c_str();
}

bool GameStateService::IsInGame() const
{
    return m_State == GameState::InGame;
}

bool GameStateService::IsInMenu() const
{
    return m_State == GameState::MainMenu;
}

bool GameStateService::IsPaused() const
{
    return m_Paused;
}

bool GameStateService::ReadPauseFlag() const
{
    if (!m_Memory || m_WorldManagerPtrAddr == 0)
        return false;

    // Read the world manager pointer
    uintptr_t worldObj = 0;
    if (!m_Memory->ReadPointer(m_WorldManagerPtrAddr, worldObj) || worldObj == 0)
        return false;

    int32_t pauseCount = 0;
    if (!m_Memory->ReadValue(worldObj + PAUSE_FLAG_OFFSET, pauseCount))
        return false;

    return pauseCount > 0;
}

bool GameStateService::ReadEngineString(uintptr_t address, std::string& out) const
{
    // Engine string layout (24 bytes):
    //   offset 0: int32_t length
    //   offset 4: int32_t sso_flag (1 = inline up to 15 chars, 0 = heap pointer)
    //   offset 8: char[16] inline data  -OR-  char* heap pointer (8 bytes)

    struct EngineStringHeader
    {
        int32_t length;
        int32_t ssoFlag;
        union
        {
            char inlineData[16];
            uintptr_t heapPtr;
        };
    };

    EngineStringHeader header{};
    if (!m_Memory->ReadMemory(address, &header, sizeof(header)))
    {
        out.clear();
        return false;
    }

    if (header.length <= 0)
    {
        out.clear();
        return true;
    }

    // Sanity check — level names are short (< 256 chars)
    if (header.length > 255)
    {
        out.clear();
        return false;
    }

    if (header.ssoFlag == 1)
    {
        // Inline: data is at offset 8, max 15 chars + null
        const int safeLen = (header.length < 15) ? header.length : 15;
        out.assign(header.inlineData, safeLen);
    }
    else
    {
        // Heap-allocated: read from the pointer
        if (header.heapPtr == 0)
        {
            out.clear();
            return false;
        }

        char buf[256]{};
        if (!m_Memory->ReadMemory(header.heapPtr, buf, header.length))
        {
            out.clear();
            return false;
        }
        buf[header.length] = '\0';
        out.assign(buf, header.length);
    }

    return true;
}
