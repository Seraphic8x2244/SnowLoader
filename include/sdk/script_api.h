#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <type_traits>

enum HotkeyModifiers : unsigned int
{
    HotkeyModifier_None = 0,
    HotkeyModifier_Control = 1u << 0,
    HotkeyModifier_Shift = 1u << 1,
    HotkeyModifier_Alt = 1u << 2,
};

// Input state polling (real-time key/chord state)
class IInputManager
{
public:
    virtual ~IInputManager() = default;

    virtual bool IsDown(int virtualKey) const = 0;
    virtual bool WasPressed(int virtualKey) const = 0;
    virtual bool WasReleased(int virtualKey) const = 0;
    virtual bool IsChordDown(int virtualKey, unsigned int modifiers) const = 0;
    virtual bool WasChordPressed(int virtualKey, unsigned int modifiers) const = 0;
};

class IConfig
{
public:
    virtual ~IConfig() = default;

    virtual bool Load() = 0;
    virtual bool Save() = 0;

    virtual bool GetBool(const std::string& key, bool defaultValue = false) const = 0;
    virtual float GetFloat(const std::string& key, float defaultValue = 0.0f) const = 0;
    virtual int GetInt(const std::string& key, int defaultValue = 0) const = 0;
    virtual std::string GetString(const std::string& key, const std::string& defaultValue = "") const = 0;

    virtual void SetBool(const std::string& key, bool value) = 0;
    virtual void SetFloat(const std::string& key, float value) = 0;
    virtual void SetInt(const std::string& key, int value) = 0;
    virtual void SetString(const std::string& key, const std::string& value) = 0;
};

class IConfigService;

class ConfigHandle
{
public:
    ConfigHandle() = default;
    ConfigHandle(IConfigService* service, IConfig* config)
        : m_Service(service)
        , m_Config(config)
    {
    }

    ConfigHandle(const ConfigHandle&) = delete;
    ConfigHandle& operator=(const ConfigHandle&) = delete;

    ConfigHandle(ConfigHandle&& other) noexcept
        : m_Service(other.m_Service)
        , m_Config(other.m_Config)
    {
        other.m_Service = nullptr;
        other.m_Config = nullptr;
    }

    ConfigHandle& operator=(ConfigHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Service = other.m_Service;
            m_Config = other.m_Config;
            other.m_Service = nullptr;
            other.m_Config = nullptr;
        }

        return *this;
    }

    ~ConfigHandle();

    IConfig* Get() const
    {
        return m_Config;
    }

    IConfig* operator->() const
    {
        return m_Config;
    }

    operator IConfig*() const
    {
        return m_Config;
    }

    explicit operator bool() const
    {
        return m_Config != nullptr;
    }

    void Reset();

private:
    IConfigService* m_Service = nullptr;
    IConfig* m_Config = nullptr;
};

class IConfigService
{
public:
    virtual ~IConfigService() = default;

    virtual ConfigHandle OpenConfig(const char* relativePath,
        const char* section = "Settings") = 0;
    virtual void ReleaseConfig(IConfig* config) = 0;
};

inline ConfigHandle::~ConfigHandle()
{
    Reset();
}

inline void ConfigHandle::Reset()
{
    if (m_Config != nullptr && m_Service != nullptr)
    {
        m_Service->ReleaseConfig(m_Config);
    }

    m_Service = nullptr;
    m_Config = nullptr;
}

class IOverlayService
{
public:
    virtual ~IOverlayService() = default;

    virtual void ShowTransientMessage(const char* message, unsigned int durationMs = 6000) = 0;
};

enum class GameState : int
{
    Unknown = 0,
    MainMenu = 1,
    Loading = 2,
    InGame = 3,
};

class IGameStateService
{
public:
    virtual ~IGameStateService() = default;

    virtual GameState GetState() const = 0;
    virtual const char* GetLevelName() const = 0;
    virtual bool IsInGame() const = 0;
    virtual bool IsInMenu() const = 0;
    virtual bool IsPaused() const = 0;
};

class IKeybindingManager
{
public:
    virtual ~IKeybindingManager() = default;

    virtual void Clear() = 0;

    // Render a rebindable hotkey UI element with a label
    // Returns true if the binding was changed
    virtual bool RenderKeybindingOption(const char* label, const char* configKey,
        int& outVirtualKey, unsigned int& outModifiers) = 0;

    // Save all pending keybinding changes to config
    virtual void SaveKeybindings() = 0;

    // Register a keybinding action for documentation
    // Optional; used for displaying help/keybinds menu
    virtual void RegisterAction(const char* actionName, const char* description,
        int defaultVirtualKey, unsigned int defaultModifiers = HotkeyModifier_None) = 0;

    // Resolve the current keybinding for a registered action.
    virtual bool TryGetBinding(const char* actionName, int& outVirtualKey,
        unsigned int& outModifiers) const = 0;

    // Get formatted keybinding string (e.g., "Ctrl+Shift+Q")
    virtual std::string GetKeybindingString(int virtualKey, unsigned int modifiers) const = 0;
};

class IGameMemoryService
{
public:
    virtual ~IGameMemoryService() = default;

    virtual uintptr_t PatternScan(const char* pattern) const = 0;
    virtual bool ApplyPatch(uintptr_t address, const void* patch, std::size_t size) const = 0;
    virtual bool ReadMemory(uintptr_t address, void* buffer, std::size_t size) const = 0;
    virtual bool WriteMemory(uintptr_t address, const void* data, std::size_t size) const = 0;
    virtual bool ApplyPatternPatch(const char* pattern, const void* patch, std::size_t size,
        const char* label = nullptr) const = 0;
    virtual bool ReadModuleOffset(HMODULE module, uintptr_t offset, void* buffer,
        std::size_t size, const char* label = nullptr) const = 0;
    virtual bool ReadModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, void* buffer, std::size_t size,
        const char* label = nullptr) const = 0;
    virtual bool WriteModuleOffset(HMODULE module, uintptr_t offset, const void* data,
        std::size_t size, const char* label = nullptr) const = 0;
    virtual bool WriteModulePointerOffset(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, const void* data, std::size_t size,
        const char* label = nullptr) const = 0;
    virtual bool ReadPointer(uintptr_t address, uintptr_t& value) const = 0;
    virtual uintptr_t ResolveRelativeAddress(uintptr_t instructionAddress,
        std::size_t displacementOffset) const = 0;
    virtual uintptr_t ResolvePointerChain(uintptr_t baseAddress, const uintptr_t* offsets,
        std::size_t offsetCount) const = 0;

    template <typename T>
    bool ReadValue(uintptr_t address, T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::ReadValue requires a trivially copyable type");
        return ReadMemory(address, &value, sizeof(T));
    }

    template <typename T>
    bool WriteValue(uintptr_t address, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::WriteValue requires a trivially copyable type");
        return WriteMemory(address, &value, sizeof(T));
    }

    template <typename T>
    bool ReadModuleValue(HMODULE module, uintptr_t offset, T& value,
        const char* label = nullptr) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::ReadModuleValue requires a trivially copyable type");
        return ReadModuleOffset(module, offset, &value, sizeof(T), label);
    }

    template <typename T>
    bool WriteModuleValue(HMODULE module, uintptr_t offset, const T& value,
        const char* label = nullptr) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::WriteModuleValue requires a trivially copyable type");
        return WriteModuleOffset(module, offset, &value, sizeof(T), label);
    }

    template <typename T>
    bool ReadModulePointerValue(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, T& value, const char* label = nullptr) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::ReadModulePointerValue requires a trivially copyable type");
        return ReadModulePointerOffset(module, pointerOffset, valueOffset,
            &value, sizeof(T), label);
    }

    template <typename T>
    bool WriteModulePointerValue(HMODULE module, uintptr_t pointerOffset,
        uintptr_t valueOffset, const T& value, const char* label = nullptr) const
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "IGameMemoryService::WriteModulePointerValue requires a trivially copyable type");
        return WriteModulePointerOffset(module, pointerOffset, valueOffset,
            &value, sizeof(T), label);
    }
};

struct ScriptContext
{
    HMODULE gameModule = nullptr;
    const char* hostDirectory = nullptr;
    IInputManager* input = nullptr;
    IConfigService* configs = nullptr;
    IOverlayService* overlays = nullptr;
    IGameMemoryService* memory = nullptr;
    IKeybindingManager* keybindings = nullptr;
    IGameStateService* gameState = nullptr;
};

class IScript
{
public:
    virtual ~IScript() = default;

    virtual const char* GetName() const = 0;
    virtual bool Initialize(const ScriptContext& context) = 0;
    virtual void OnTick(float deltaTime) = 0;
    virtual void Shutdown() = 0;
};

using CreateScriptFn = IScript* (*)();
using DestroyScriptFn = void (*)(IScript* script);

#define SNOWLOADER_DEFINE_SCRIPT(ScriptType)                                 \
    extern "C" __declspec(dllexport) IScript* CreateScript()               \
    {                                                                       \
        return new ScriptType();                                            \
    }                                                                       \
    extern "C" __declspec(dllexport) void DestroyScript(IScript* script)  \
    {                                                                       \
        delete script;                                                      \
    }
