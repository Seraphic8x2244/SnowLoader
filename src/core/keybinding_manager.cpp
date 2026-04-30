#include "core/keybinding_manager.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include "sdk/script_api.h"
#include <imgui.h>

namespace
{
    constexpr int KEYBINDING_BUTTON_WIDTH = 100;
    constexpr int KEYBINDING_MANAGER_BUTTON_WIDTH = 120;

    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    }

    bool IsModifierVirtualKey(int virtualKey)
    {
        return virtualKey == VK_CONTROL || virtualKey == VK_SHIFT || virtualKey == VK_MENU ||
            virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
            virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
            virtualKey == VK_LMENU || virtualKey == VK_RMENU;
    }
}

namespace HotkeyUtils
{
    std::string GetKeyName(int virtualKey)
    {
        if (virtualKey >= '0' && virtualKey <= '9')
        {
            return std::string(1, static_cast<char>(virtualKey));
        }

        if (virtualKey >= 'A' && virtualKey <= 'Z')
        {
            return std::string(1, static_cast<char>(virtualKey));
        }

        if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
        {
            return "F" + std::to_string(virtualKey - VK_F1 + 1);
        }

        switch (virtualKey)
        {
        case VK_SPACE: return "Space";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Esc";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_LCONTROL: return "LCtrl";
        case VK_RCONTROL: return "RCtrl";
        case VK_LSHIFT: return "LShift";
        case VK_RSHIFT: return "RShift";
        case VK_LMENU: return "LAlt";
        case VK_RMENU: return "RAlt";
        case VK_LBUTTON: return "LMouseBtn";
        case VK_RBUTTON: return "RMouseBtn";
        case VK_MBUTTON: return "MMouseBtn";
        case VK_OEM_1: return ";";
        case VK_OEM_2: return "/";
        case VK_OEM_3: return "`";
        case VK_OEM_4: return "[";
        case VK_OEM_5: return "\\";
        case VK_OEM_6: return "]";
        case VK_OEM_7: return "'";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PLUS: return "=";
        default:
            return "VK_" + std::to_string(virtualKey);
        }
    }

    std::string FormatModifiers(unsigned int modifiers)
    {
        std::ostringstream oss;

        if ((modifiers & HotkeyModifier_Control) != 0)
        {
            oss << "Ctrl+";
        }
        if ((modifiers & HotkeyModifier_Shift) != 0)
        {
            oss << "Shift+";
        }
        if ((modifiers & HotkeyModifier_Alt) != 0)
        {
            oss << "Alt+";
        }

        return oss.str();
    }
}

void InputManager::Update()
{
    m_PreviousStates = m_CurrentStates;

    for (int virtualKey = 0; virtualKey < KEY_COUNT; ++virtualKey)
    {
        m_CurrentStates[virtualKey] =
            (GetAsyncKeyState(virtualKey) & HotkeyUtils::VK_ASYNC_STATE_FLAG) != 0;
    }
}

void InputManager::Reset()
{
    m_CurrentStates.fill(false);
    m_PreviousStates.fill(false);
}

bool InputManager::IsDown(int virtualKey) const
{
    return IsValidKey(virtualKey) ? m_CurrentStates[virtualKey] : false;
}

bool InputManager::WasPressed(int virtualKey) const
{
    return IsValidKey(virtualKey)
        && m_CurrentStates[virtualKey]
        && !m_PreviousStates[virtualKey];
}

bool InputManager::WasReleased(int virtualKey) const
{
    return IsValidKey(virtualKey)
        && !m_CurrentStates[virtualKey]
        && m_PreviousStates[virtualKey];
}

bool InputManager::IsChordDown(int virtualKey, unsigned int modifiers) const
{
    return IsDown(virtualKey) && AreModifiersDown(modifiers);
}

bool InputManager::WasChordPressed(int virtualKey, unsigned int modifiers) const
{
    return WasPressed(virtualKey) && AreModifiersDown(modifiers);
}

bool InputManager::IsValidKey(int virtualKey) const
{
    return virtualKey >= 0 && virtualKey < KEY_COUNT;
}

bool InputManager::AreModifiersDown(unsigned int modifiers) const
{
    if ((modifiers & HotkeyModifier_Control) != 0 && !IsDown(VK_CONTROL))
    {
        return false;
    }

    if ((modifiers & HotkeyModifier_Shift) != 0 && !IsDown(VK_SHIFT))
    {
        return false;
    }

    if ((modifiers & HotkeyModifier_Alt) != 0 && !IsDown(VK_MENU))
    {
        return false;
    }

    return true;
}

HotkeyBinding HotkeyBindingUtils::LoadBinding(IConfig* config, const char* actionName,
    int defaultVirtualKey, unsigned int defaultModifiers)
{
    HotkeyBinding binding{};
    binding.virtualKey = defaultVirtualKey;
    binding.modifiers = defaultModifiers;

    if (config == nullptr || !HotkeyUtils::IsValidString(actionName))
    {
        return binding;
    }

    const std::string keyKey = std::string(actionName) + HotkeyUtils::KEY_SUFFIX;
    const std::string modifiersKey = std::string(actionName) + HotkeyUtils::MODIFIERS_SUFFIX;

    const int configuredKey = config->GetInt(keyKey, defaultVirtualKey);
    const int configuredModifiers = config->GetInt(modifiersKey, static_cast<int>(defaultModifiers));

    config->SetInt(keyKey, configuredKey);
    config->SetInt(modifiersKey, configuredModifiers);

    binding.virtualKey = configuredKey;
    binding.modifiers = static_cast<unsigned int>(configuredModifiers);
    return binding;
}

bool HotkeyBindingUtils::WasPressed(const IInputManager* manager, const HotkeyBinding& binding)
{
    if (manager == nullptr)
    {
        return false;
    }

    if (binding.modifiers == HotkeyModifier_None)
    {
        return manager->WasPressed(binding.virtualKey);
    }

    return manager->WasChordPressed(binding.virtualKey, binding.modifiers);
}

std::string HotkeyBindingUtils::Format(const HotkeyBinding& binding)
{
    std::string result = HotkeyUtils::FormatModifiers(binding.modifiers);
    result += HotkeyUtils::GetKeyName(binding.virtualKey);
    return result;
}

KeybindingManager::KeybindingManager()
{
}

void KeybindingManager::Initialize(IConfigService* configService)
{
    m_ConfigService = configService;
    m_ConfigHandle.Reset();
    m_Config = nullptr;

    if (m_ConfigService)
    {
        m_ConfigHandle = m_ConfigService->OpenConfig(KEYBINDINGS_CONFIG_FILE, KEYBINDINGS_CONFIG_SECTION);
        m_Config = m_ConfigHandle.Get();
    }
}

void KeybindingManager::Shutdown()
{
    SaveKeybindings();
    m_Config = nullptr;
    m_ConfigHandle.Reset();
    m_ConfigService = nullptr;
    m_PendingRebinds.clear();
    m_RegisteredActions.clear();
    m_IsDirty = false;
}

bool KeybindingManager::RenderKeybindingOption(const char* label, const char* configKey,
    int& outVirtualKey, unsigned int& outModifiers)
{
    if (!HotkeyUtils::IsValidString(label) || !HotkeyUtils::IsValidString(configKey))
    {
        return false;
    }

    // Initialize pending rebind if not already present
    if (m_PendingRebinds.find(configKey) == m_PendingRebinds.end())
    {
        PendingRebind& rebind = m_PendingRebinds[configKey];
        rebind.configKey = configKey;
        rebind.virtualKey = outVirtualKey;
        rebind.modifiers = outModifiers;
        rebind.isRebinding = false;
    }

    PendingRebind& rebind = m_PendingRebinds[configKey];
    bool changed = false;

    // Render label
    ImGui::Text("%s", label);
    ImGui::SameLine();

    // Current keybinding display (as a button)
    ImGui::PushID(configKey);
    if (rebind.isRebinding)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.0f, 0.8f));
        ImGui::Button("...", ImVec2(KEYBINDING_BUTTON_WIDTH, 0));
        ImGui::PopStyleColor();

        CaptureKeybinding(rebind);

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
        {
            // Right-click to cancel
            rebind.isRebinding = false;
        }
    }
    else
    {
        std::string bindingStr = GetKeybindingString(rebind.virtualKey, rebind.modifiers);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 0.8f));
        if (ImGui::Button(bindingStr.c_str(), ImVec2(KEYBINDING_BUTTON_WIDTH, 0)))
        {
            rebind.isRebinding = true;
            rebind.waitingForRelease = true;
        }
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click to rebind\nRight-click to clear");
            if (ImGui::IsMouseClicked(1))
            {
                rebind.virtualKey = 0;
                rebind.modifiers = HotkeyModifier_None;
                m_IsDirty = true;
                changed = true;
            }
        }
    }

    ImGui::PopID();

    outVirtualKey = rebind.virtualKey;
    outModifiers = rebind.modifiers;

    return changed;
}

void KeybindingManager::SaveKeybindings()
{
    if (!m_Config || !m_ConfigService || !m_IsDirty)
    {
        return;
    }

    for (const auto& pair : m_PendingRebinds)
    {
        const std::string& configKey = pair.first;
        const PendingRebind& rebind = pair.second;

        std::string keyKey = configKey + HotkeyUtils::KEY_SUFFIX;
        std::string modKey = configKey + HotkeyUtils::MODIFIERS_SUFFIX;

        m_Config->SetInt(keyKey, rebind.virtualKey);
        m_Config->SetInt(modKey, static_cast<int>(rebind.modifiers));
    }

    m_Config->Save();
    m_IsDirty = false;
}

void KeybindingManager::RegisterAction(const char* actionName, const char* description,
    int defaultVirtualKey, unsigned int defaultModifiers)
{
    if (!HotkeyUtils::IsValidString(actionName))
    {
        return;
    }

    // Parse script.action format to extract script name
    std::string actionStr(actionName);
    std::string scriptName;
    std::string baseName = actionStr;

    size_t dotPos = actionStr.find('.');
    if (dotPos != std::string::npos)
    {
        scriptName = actionStr.substr(0, dotPos);
        baseName = actionStr.substr(dotPos + 1);
    }

    KeybindingAction action;
    action.scriptName = scriptName;
    action.name = baseName;
    action.description = description ? description : "";
    action.defaultVirtualKey = defaultVirtualKey;
    action.defaultModifiers = defaultModifiers;
    action.configKey = actionStr;

    m_RegisteredActions.push_back(action);

    // Auto-initialize pending rebind if not already present
    if (m_PendingRebinds.find(action.configKey) == m_PendingRebinds.end())
    {
        PendingRebind& rebind = m_PendingRebinds[action.configKey];
        rebind.configKey = action.configKey;
        rebind.virtualKey = defaultVirtualKey;
        rebind.modifiers = defaultModifiers;
        rebind.isRebinding = false;

        // Try to load from config
        LoadKeybindingFromConfig(action.configKey, rebind.virtualKey, rebind.modifiers);
    }
}

bool KeybindingManager::TryGetBinding(const char* actionName, int& outVirtualKey,
    unsigned int& outModifiers) const
{
    if (!HotkeyUtils::IsValidString(actionName))
    {
        return false;
    }

    const auto it = m_PendingRebinds.find(actionName);
    if (it == m_PendingRebinds.end())
    {
        return false;
    }

    outVirtualKey = it->second.virtualKey;
    outModifiers = it->second.modifiers;
    return true;
}

std::string KeybindingManager::GetKeybindingString(int virtualKey, unsigned int modifiers) const
{
    if (virtualKey == 0)
    {
        return "[Not Set]";
    }

    std::string result = HotkeyUtils::FormatModifiers(modifiers);
    result += HotkeyUtils::GetKeyName(virtualKey);
    return result;
}

void KeybindingManager::Render()
{
    // Optional: Render a central keybindings help menu if needed
}

void KeybindingManager::Clear()
{
    m_PendingRebinds.clear();
    m_RegisteredActions.clear();
    m_IsDirty = false;
}

void KeybindingManager::CaptureKeybinding(PendingRebind& rebind)
{
    // Just track mouse release so FeedKeyDown() becomes active.
    // Key capture is event-driven via FeedKeyDown() called from HookedWndProc.
    if (rebind.waitingForRelease)
    {
        const bool anyMouseDown =
            (GetAsyncKeyState(VK_LBUTTON) & HotkeyUtils::VK_ASYNC_STATE_FLAG) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & HotkeyUtils::VK_ASYNC_STATE_FLAG) != 0 ||
            (GetAsyncKeyState(VK_MBUTTON) & HotkeyUtils::VK_ASYNC_STATE_FLAG) != 0;
        if (!anyMouseDown)
        {
            rebind.waitingForRelease = false;
        }
    }
}

bool KeybindingManager::FeedKeyDown(int vk, unsigned int modifiers)
{
    // Called from WndProc (message thread) on WM_KEYDOWN / WM_SYSKEYDOWN.
    // Routes the key press to whichever rebind is currently waiting for input.
    bool consumed = false;
    for (auto& pair : m_PendingRebinds)
    {
        PendingRebind& rebind = pair.second;
        if (!rebind.isRebinding || rebind.waitingForRelease)
        {
            continue;
        }

        if (vk == VK_ESCAPE)
        {
            rebind.isRebinding = false;
        }
        else if (!IsModifierVirtualKey(vk))
        {
            rebind.virtualKey = vk;
            rebind.modifiers = modifiers;
            rebind.isRebinding = false;
            m_IsDirty = true;
        }
        consumed = true;
    }
    return consumed;
}

void KeybindingManager::LoadKeybindingFromConfig(const std::string& configKey, int& virtualKey, unsigned int& modifiers)
{
    if (!m_Config)
    {
        return;
    }

    std::string keyKey = configKey + HotkeyUtils::KEY_SUFFIX;
    std::string modKey = configKey + HotkeyUtils::MODIFIERS_SUFFIX;

    virtualKey = m_Config->GetInt(keyKey, virtualKey);
    modifiers = static_cast<unsigned int>(m_Config->GetInt(modKey, static_cast<int>(modifiers)));
}

void KeybindingManager::RenderKeybindingsUI(bool& showUI)
{
    if (!showUI)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(700.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Keybindings Manager", &showUI, ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        ImGui::TextWrapped("Configure keybindings for all installed scripts. Click a keybinding to rebind it.");
        ImGui::Separator();

        // Search/filter box
        ImGui::Text("Search:");
        ImGui::SameLine();
        char filterBuffer[256] = {};
        strncpy_s(filterBuffer, sizeof(filterBuffer), m_CurrentSearchFilter.c_str(), _TRUNCATE);
        if (ImGui::InputText("##filter", filterBuffer, sizeof(filterBuffer)))
        {
            m_CurrentSearchFilter = filterBuffer;
        }
        ImGui::Separator();

        // Group actions by script
        std::map<std::string, std::vector<size_t>> actionsByScript;
        for (size_t i = 0; i < m_RegisteredActions.size(); ++i)
        {
            const auto& action = m_RegisteredActions[i];
            actionsByScript[action.scriptName].push_back(i);
        }

        // Render each script's section
        for (const auto& pair : actionsByScript)
        {
            const std::string& scriptName = pair.first;
            const auto& indices = pair.second;

            if (ImGui::CollapsingHeader(scriptName.empty() ? "Host" : scriptName.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (size_t idx : indices)
                {
                    const auto& action = m_RegisteredActions[idx];

                    // Apply search filter
                    if (!m_CurrentSearchFilter.empty())
                    {
                        const std::string lowerName = ToLowerCopy(action.name);
                        const std::string lowerDesc = ToLowerCopy(action.description);
                        const std::string lowerFilter = ToLowerCopy(m_CurrentSearchFilter);

                        if (lowerName.find(lowerFilter) == std::string::npos &&
                            lowerDesc.find(lowerFilter) == std::string::npos)
                        {
                            continue;
                        }
                    }

                    // Render action label and description
                    ImGui::Text("%s", action.name.c_str());
                    if (!action.description.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("[%s]", action.description.c_str());
                    }

                    ImGui::SameLine();
                    ImGui::Text("       ");
                    ImGui::SameLine();

                    // Render keybinding control
                    auto it = m_PendingRebinds.find(action.configKey);
                    if (it == m_PendingRebinds.end())
                    {
                        PendingRebind& rebind = m_PendingRebinds[action.configKey];
                        rebind.configKey = action.configKey;
                        rebind.virtualKey = action.defaultVirtualKey;
                        rebind.modifiers = action.defaultModifiers;
                        it = m_PendingRebinds.find(action.configKey);
                    }

                    PendingRebind& rebind = it->second;

                    ImGui::PushID(action.configKey.c_str());
                    if (rebind.isRebinding)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.0f, 0.8f));
                        ImGui::Button("...", ImVec2(KEYBINDING_MANAGER_BUTTON_WIDTH, 0));
                        ImGui::PopStyleColor();

                        CaptureKeybinding(rebind);

                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
                        {
                            rebind.isRebinding = false;
                        }
                    }
                    else
                    {
                        std::string bindingStr = GetKeybindingString(rebind.virtualKey, rebind.modifiers);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 0.8f));
                        if (ImGui::Button(bindingStr.c_str(), ImVec2(KEYBINDING_MANAGER_BUTTON_WIDTH, 0)))
                        {
                            rebind.isRebinding = true;
                            rebind.waitingForRelease = true;
                        }
                        ImGui::PopStyleColor();

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Left-click to rebind\nRight-click to clear");
                            if (ImGui::IsMouseClicked(1))
                            {
                                rebind.virtualKey = 0;
                                rebind.modifiers = HotkeyModifier_None;
                                m_IsDirty = true;
                            }
                        }
                    }
                    ImGui::PopID();
                }

                ImGui::Spacing();
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Save All Keybindings", ImVec2(-1, 0)))
        {
            SaveKeybindings();
            ImGui::OpenPopup("SaveSuccess");
        }

        if (ImGui::BeginPopupModal("SaveSuccess", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Keybindings saved successfully!");
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}
