#pragma once

#include <cstddef>
#include <string>

#include "script_api.h"

struct ScriptActionBinding
{
    const char* name = nullptr;
    const char* description = nullptr;
    int virtualKey = 0;
    unsigned int modifiers = HotkeyModifier_None;
};

class BaseScript : public IScript
{
public:
    virtual ~BaseScript() = default;

protected:
    std::string GetFullActionName(const char* actionName) const
    {
        if (!actionName || actionName[0] == '\0')
        {
            return {};
        }

        return std::string(GetName()) + "." + actionName;
    }

    bool LoadKeybindingContext(const ScriptContext& context)
    {
        m_ConfigService = context.configs;
        m_KeybindingManager = context.keybindings;
        m_InputManager = context.input;
        m_OverlayService = context.overlays;

        return m_ConfigService != nullptr && m_KeybindingManager != nullptr && m_InputManager != nullptr;
    }

    ConfigHandle OpenConfig(const char* relativePath, const char* section = "Settings") const
    {
        if (!m_ConfigService)
        {
            return {};
        }

        return m_ConfigService->OpenConfig(relativePath, section);
    }

    void ShowTransientMessage(IOverlayService* overlays, const char* message,
        unsigned int durationMs = 6000) const
    {
        if (overlays && message && message[0] != '\0')
        {
            overlays->ShowTransientMessage(message, durationMs);
        }
    }

    IKeybindingManager* GetKeybindingManager() const { return m_KeybindingManager; }
    IInputManager* GetInputManager() const { return m_InputManager; }
    IOverlayService* GetOverlayService() const { return m_OverlayService; }

    bool IsKeybindingDown(const char* actionName) const
    {
        if (!m_KeybindingManager || !m_InputManager)
        {
            return false;
        }

        const std::string fullActionName = GetFullActionName(actionName);
        int virtualKey = 0;
        unsigned int modifiers = HotkeyModifier_None;
        if (!m_KeybindingManager->TryGetBinding(fullActionName.c_str(), virtualKey, modifiers) ||
            virtualKey == 0)
        {
            return false;
        }

        return modifiers == HotkeyModifier_None
            ? m_InputManager->IsDown(virtualKey)
            : m_InputManager->IsChordDown(virtualKey, modifiers);
    }

    bool WasKeybindingPressed(const char* actionName) const
    {
        if (!m_KeybindingManager || !m_InputManager)
        {
            return false;
        }

        const std::string fullActionName = GetFullActionName(actionName);
        int virtualKey = 0;
        unsigned int modifiers = HotkeyModifier_None;
        if (!m_KeybindingManager->TryGetBinding(fullActionName.c_str(), virtualKey, modifiers) ||
            virtualKey == 0)
        {
            return false;
        }

        return modifiers == HotkeyModifier_None
            ? m_InputManager->WasPressed(virtualKey)
            : m_InputManager->WasChordPressed(virtualKey, modifiers);
    }

    bool RenderKeybindingOption(const char* label, const char* configKey,
        int& outVirtualKey, unsigned int& outModifiers) const
    {
        if (!m_KeybindingManager)
        {
            return false;
        }
        return m_KeybindingManager->RenderKeybindingOption(label, configKey,
            outVirtualKey, outModifiers);
    }

    void SaveKeybindings() const
    {
        if (m_KeybindingManager)
        {
            m_KeybindingManager->SaveKeybindings();
        }
    }

    std::string GetKeybindingString(int virtualKey, unsigned int modifiers) const
    {
        if (!m_KeybindingManager)
        {
            return "[No Manager]";
        }
        return m_KeybindingManager->GetKeybindingString(virtualKey, modifiers);
    }

    void RegisterKeybindingAction(const char* actionName, const char* description,
        int defaultVirtualKey, unsigned int defaultModifiers = HotkeyModifier_None) const
    {
        if (!m_KeybindingManager)
        {
            return;
        }

        // Format as "scriptname.actionname" for central UI grouping
        std::string fullActionName = GetFullActionName(actionName);
        m_KeybindingManager->RegisterAction(fullActionName.c_str(), description,
            defaultVirtualKey, defaultModifiers);
    }

    template <std::size_t Count>
    void RegisterKeybindingActions(const ScriptActionBinding (&bindings)[Count]) const
    {
        for (const ScriptActionBinding& binding : bindings)
        {
            RegisterKeybindingAction(binding.name, binding.description,
                binding.virtualKey, binding.modifiers);
        }
    }

private:
    IConfigService* m_ConfigService = nullptr;
    IKeybindingManager* m_KeybindingManager = nullptr;
    IInputManager* m_InputManager = nullptr;
    IOverlayService* m_OverlayService = nullptr;
};
