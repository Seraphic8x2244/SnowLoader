#pragma once

#include <Windows.h>

#include <array>
#include <map>
#include <string>
#include <vector>

#include "sdk/script_api.h"

class IConfig;

namespace HotkeyUtils
{
    std::string GetKeyName(int virtualKey);
    std::string FormatModifiers(unsigned int modifiers);

    inline bool IsValidString(const char* str)
    {
        return str != nullptr && str[0] != '\0';
    }

    constexpr int VK_ASYNC_STATE_FLAG = 0x8000;
    constexpr const char* KEY_SUFFIX = "Key";
    constexpr const char* MODIFIERS_SUFFIX = "Modifiers";
}

struct HotkeyBinding
{
    int virtualKey = 0;
    unsigned int modifiers = HotkeyModifier_None;
};

class HotkeyBindingUtils
{
public:
    static HotkeyBinding LoadBinding(IConfig* config, const char* actionName,
        int defaultVirtualKey, unsigned int defaultModifiers = HotkeyModifier_None);
    static bool WasPressed(const IInputManager* manager, const HotkeyBinding& binding);
    static std::string Format(const HotkeyBinding& binding);
};

class InputManager final : public IInputManager
{
public:
    void Update();
    void Reset();

    bool IsDown(int virtualKey) const override;
    bool WasPressed(int virtualKey) const override;
    bool WasReleased(int virtualKey) const override;
    bool IsChordDown(int virtualKey, unsigned int modifiers) const override;
    bool WasChordPressed(int virtualKey, unsigned int modifiers) const override;

private:
    static constexpr int KEY_COUNT = 256;

    bool IsValidKey(int virtualKey) const;
    bool AreModifiersDown(unsigned int modifiers) const;

    std::array<bool, KEY_COUNT> m_CurrentStates{};
    std::array<bool, KEY_COUNT> m_PreviousStates{};
};

class KeybindingManager final : public IKeybindingManager
{
public:
    // Configuration filename for keybindings
    static constexpr const char* KEYBINDINGS_CONFIG_FILE = "keybindings.ini";
    static constexpr const char* KEYBINDINGS_CONFIG_SECTION = "Keybindings";

    KeybindingManager();
    ~KeybindingManager() = default;

    void Initialize(IConfigService* configService);
    void Shutdown();

    bool RenderKeybindingOption(const char* label, const char* configKey,
        int& outVirtualKey, unsigned int& outModifiers) override;

    void SaveKeybindings() override;

    void RegisterAction(const char* actionName, const char* description,
        int defaultVirtualKey, unsigned int defaultModifiers = HotkeyModifier_None) override;

    bool TryGetBinding(const char* actionName, int& outVirtualKey,
        unsigned int& outModifiers) const override;

    std::string GetKeybindingString(int virtualKey, unsigned int modifiers) const override;

    // Render the central keybindings management UI
    void RenderKeybindingsUI(bool& showUI);

    // Called from WndProc on WM_KEYDOWN to deliver key events to any active rebind.
    // Returns true if the key was consumed by a rebind in progress.
    bool FeedKeyDown(int virtualKey, unsigned int modifiers);

    void Render();
    void Clear() override;

private:
    struct KeybindingAction
    {
        std::string scriptName;  // Which script registered this
        std::string name;
        std::string description;
        std::string configKey;   // Section key for saving
        int defaultVirtualKey = 0;
        unsigned int defaultModifiers = HotkeyModifier_None;
    };

    struct PendingRebind
    {
        std::string configKey;
        int virtualKey = 0;
        unsigned int modifiers = HotkeyModifier_None;
        bool isRebinding = false;
        bool waitingForRelease = false; // skip capture until triggering mouse click is released
    };

    void CaptureKeybinding(PendingRebind& rebind);
    void LoadKeybindingFromConfig(const std::string& configKey, int& virtualKey, unsigned int& modifiers);

    IConfigService* m_ConfigService = nullptr;
    ConfigHandle m_ConfigHandle;
    IConfig* m_Config = nullptr;
    std::map<std::string, PendingRebind> m_PendingRebinds;
    std::vector<KeybindingAction> m_RegisteredActions;
    std::string m_CurrentSearchFilter;
    bool m_IsDirty = false;
};

