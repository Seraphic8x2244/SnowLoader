#include <Windows.h>

#include <cmath>

#include <imgui.h>

#include "sdk/base_script.h"
#include "sdk/script_api.h"
#include "core/logger.h"

namespace
{
    class TimeControlScript final : public BaseScript
    {
    public:
        const char* GetName() const override
        {
            return "timelapse";
        }

        bool Initialize(const ScriptContext& context) override
        {
            if (!LoadKeybindingContext(context))
            {
                return false;
            }

            if (!context.memory)
            {
                return false;
            }

            m_GameModule = context.gameModule;
            m_Memory = context.memory;
            m_GameState = context.gameState;

            if (!m_GameModule || !m_Memory)
            {
                Logger::Error("TimeControlScript: Game module or memory service is null");
                return false;
            }

            m_IsTimePassing = true;
            m_Mode = TimeMode::DefaultGame;
            m_ControlledTime = GetCurrentTime();
            m_RateMultiplier = 1.0f;
            m_IsRealClockSynced = false;
            m_Config = OpenConfig("timelapse.ini", "Overlay");
            if (m_Config)
            {
                m_HasSavedWindowPos = m_Config->GetBool("HasWindowPos", false);
                m_WindowPos = ImVec2(
                    m_Config->GetFloat("WindowPosX", 0.0f),
                    m_Config->GetFloat("WindowPosY", 0.0f));
            }

            static constexpr ScriptActionBinding bindings[] = {
                { "StopTime", "Stop time", VK_NUMPAD0, HotkeyModifier_Alt },
                { "DefaultRate", "Resume default game time", VK_NUMPAD1, HotkeyModifier_Alt },
                { "RateX2", "Set time rate x2", VK_NUMPAD2, HotkeyModifier_Alt },
                { "RateX3", "Set time rate x3", VK_NUMPAD3, HotkeyModifier_Alt },
                { "RateX4", "Set time rate x4", VK_NUMPAD4, HotkeyModifier_Alt },
                { "RateX5", "Set time rate x5", VK_NUMPAD5, HotkeyModifier_Alt },
                { "RateX6", "Set time rate x6", VK_NUMPAD6, HotkeyModifier_Alt },
                { "RateHalf", "Set time rate /2", VK_NUMPAD7, HotkeyModifier_Alt },
                { "RateThird", "Set time rate /3", VK_NUMPAD8, HotkeyModifier_Alt },
                { "RateQuarter", "Set time rate /4", VK_NUMPAD9, HotkeyModifier_Alt },
                { "RealClockSync", "Sync with real clock", VK_DIVIDE, HotkeyModifier_Alt },
                { "ShiftForward1", "Shift time forward by 1 hour", VK_ADD, HotkeyModifier_Control },
                { "ShiftForward2", "Shift time forward by 2 hours", VK_ADD, HotkeyModifier_Shift },
                { "ShiftForward3", "Shift time forward by 3 hours", VK_ADD, HotkeyModifier_Alt },
                { "ShiftBackward1", "Shift time backward by 1 hour", VK_SUBTRACT, HotkeyModifier_Control },
                { "ShiftBackward2", "Shift time backward by 2 hours", VK_SUBTRACT, HotkeyModifier_Shift },
                { "ShiftBackward3", "Shift time backward by 3 hours", VK_SUBTRACT, HotkeyModifier_Alt },
            };

            RegisterKeybindingActions(bindings);

            SaveKeybindings();

            Logger::Info("TimeControlScript initialized");
            return true;
        }

        void OnTick(float deltaTime) override
        {
            if (m_GameState && !m_GameState->IsInGame())
                return;

            Update(deltaTime);

            if (!GetInputManager())
            {
                RenderStatusOverlay();
                return;
            }

            if (WasKeybindingPressed("StopTime"))
            {
                FreezeTime();
            }
            if (WasKeybindingPressed("DefaultRate"))
            {
                ResumeDefaultTime();
            }
            if (WasKeybindingPressed("RateX2"))
            {
                SetTimeScale(2.0f);
            }
            if (WasKeybindingPressed("RateX3"))
            {
                SetTimeScale(3.0f);
            }
            if (WasKeybindingPressed("RateX4"))
            {
                SetTimeScale(4.0f);
            }
            if (WasKeybindingPressed("RateX5"))
            {
                SetTimeScale(5.0f);
            }
            if (WasKeybindingPressed("RateX6"))
            {
                SetTimeScale(6.0f);
            }
            if (WasKeybindingPressed("RateHalf"))
            {
                SetTimeScale(0.5f);
            }
            if (WasKeybindingPressed("RateThird"))
            {
                SetTimeScale(1.0f / 3.0f);
            }
            if (WasKeybindingPressed("RateQuarter"))
            {
                SetTimeScale(0.25f);
            }
            if (WasKeybindingPressed("RealClockSync"))
            {
                SyncWithRealClock();
            }

            if (WasKeybindingPressed("ShiftForward1"))
            {
                ShiftTime(1.0f);
            }
            if (WasKeybindingPressed("ShiftForward2"))
            {
                ShiftTime(2.0f);
            }
            if (WasKeybindingPressed("ShiftForward3"))
            {
                ShiftTime(3.0f);
            }
            if (WasKeybindingPressed("ShiftBackward1"))
            {
                ShiftTime(-1.0f);
            }
            if (WasKeybindingPressed("ShiftBackward2"))
            {
                ShiftTime(-2.0f);
            }
            if (WasKeybindingPressed("ShiftBackward3"))
            {
                ShiftTime(-3.0f);
            }

            RenderStatusOverlay();
        }

        void Shutdown() override
        {
            if (m_Config)
            {
                m_Config->SetBool("HasWindowPos", m_HasSavedWindowPos);
                m_Config->SetFloat("WindowPosX", m_WindowPos.x);
                m_Config->SetFloat("WindowPosY", m_WindowPos.y);
                m_Config->Save();
                m_Config.Reset();
            }

            if (!m_IsTimePassing)
            {
                ResumeTODTicker();
            }

            m_Mode = TimeMode::DefaultGame;
            m_ControlledTime = 12.0f;
            m_RateMultiplier = 1.0f;
            m_IsRealClockSynced = false;
            m_Memory = nullptr;
            m_GameModule = nullptr;
            Logger::Info("TimeControlScript shutdown");
        }

    private:
        enum class TimeMode
        {
            DefaultGame,
            Frozen,
            CustomRate,
        };

        // Time patterns
        static constexpr const char* TOD_TICK_DISABLE_PATTERN = "F3 41 0F 11 95 38 01 00 00";
        static constexpr const uint8_t TOD_TICK_DISABLE_PATCH[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        static constexpr const char* TOD_TICK_REVERT_PATTERN = "90 90 90 90 90 90 90 90 90 F3 0F 10 05";
        static constexpr const uint8_t TOD_TICK_REVERT_PATCH[] = { 0xF3, 0x41, 0x0F, 0x11, 0x95, 0x38, 0x01, 0x00, 0x00 };
        static constexpr uintptr_t TOD_OFFSET = 0x138;
        static constexpr uintptr_t TOD_POINTER = 0x02A87678;

        void HandleWindowDragging()
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) &&
                !ImGui::IsAnyItemActive())
            {
                const ImVec2 windowPos = ImGui::GetWindowPos();
                const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                ImGui::SetWindowPos(ImVec2(windowPos.x + mouseDelta.x, windowPos.y + mouseDelta.y));
            }
        }

        void Update(float deltaTime)
        {
            if (!m_GameModule || !m_Memory || deltaTime <= 0.0f)
            {
                return;
            }

            if (m_Mode == TimeMode::CustomRate)
            {
                AdvanceControlledTime(deltaTime);
            }
        }

        bool StopTODTicker()
        {
            if (m_Memory && m_Memory->ApplyPatternPatch(TOD_TICK_DISABLE_PATTERN,
                    TOD_TICK_DISABLE_PATCH, sizeof(TOD_TICK_DISABLE_PATCH)))
            {
                m_IsTimePassing = false;
                Logger::Info("Time frozen");
                return true;
            }

            Logger::Warning("TOD ticker disable pattern not found");
            return false;
        }

        bool ResumeTODTicker()
        {
            if (m_Memory && m_Memory->ApplyPatternPatch(TOD_TICK_REVERT_PATTERN,
                    TOD_TICK_REVERT_PATCH, sizeof(TOD_TICK_REVERT_PATCH)))
            {
                m_IsTimePassing = true;
                Logger::Info("Time resumed");
                return true;
            }

            Logger::Warning("TOD ticker revert pattern not found");
            return false;
        }

        bool CaptureControlledTime()
        {
            const float currentTime = GetCurrentTime();
            if (currentTime < 0.0f || currentTime >= 24.0f)
            {
                return false;
            }

            m_ControlledTime = currentTime;
            return true;
        }

        static float NormalizeTime(float hours)
        {
            float normalized = std::fmod(hours, 24.0f);
            if (normalized < 0.0f)
            {
                normalized += 24.0f;
            }

            return normalized;
        }

        static float GetLocalTime()
        {
            SYSTEMTIME localTime{};
            ::GetLocalTime(&localTime);
            return localTime.wHour + (localTime.wMinute / 60.0f) + (localTime.wSecond / 3600.0f);
        }

        float GetCurrentTime() const
        {
            if (!m_GameModule || !m_Memory) return 0.0f;

            float time = 0.0f;
            return m_Memory->ReadModulePointerValue(m_GameModule, TOD_POINTER,
                TOD_OFFSET, time, "TODTime")
                ? time
                : 0.0f;
        }

        void SetTime(float hours)
        {
            if (!m_GameModule || !m_Memory) return;

            const float normalizedTime = NormalizeTime(hours);

            if (!m_Memory->WriteModulePointerValue(m_GameModule, TOD_POINTER,
                TOD_OFFSET, normalizedTime, "TODTime"))
            {
                Logger::Warning("TimeControlScript: Failed to write time value");
            }
        }

        void AdvanceControlledTime(float deltaTime)
        {
            if (m_Mode != TimeMode::CustomRate)
            {
                return;
            }

            static constexpr float DEFAULT_GAME_HOURS_PER_REAL_SECOND = 1.0f / 150.0f;
            m_ControlledTime = NormalizeTime(m_ControlledTime +
                (deltaTime * DEFAULT_GAME_HOURS_PER_REAL_SECOND * m_RateMultiplier));
            SetTime(m_ControlledTime);
        }

        bool FreezeTime()
        {
            if (m_IsTimePassing && !StopTODTicker())
            {
                return false;
            }

            CaptureControlledTime();
            m_Mode = TimeMode::Frozen;
            m_RateMultiplier = 1.0f;
            m_IsRealClockSynced = false;
            return true;
        }

        bool ResumeDefaultTime()
        {
            if (!m_IsTimePassing && !ResumeTODTicker())
            {
                return false;
            }

            m_Mode = TimeMode::DefaultGame;
            m_RateMultiplier = 1.0f;
            m_IsRealClockSynced = false;
            CaptureControlledTime();
            return true;
        }

        bool SetTimeScale(float multiplier)
        {
            if (multiplier <= 0.0f)
            {
                Logger::Warning("TimeControlScript: Invalid time scale {}", multiplier);
                return false;
            }

            if (std::fabs(multiplier - 1.0f) < 0.001f)
            {
                return ResumeDefaultTime();
            }

            CaptureControlledTime();
            if (m_IsTimePassing && !StopTODTicker())
            {
                return false;
            }

            m_Mode = TimeMode::CustomRate;
            m_RateMultiplier = multiplier;
            m_IsRealClockSynced = false;
            Logger::Info("Custom time scale set to {}x", multiplier);
            return true;
        }

        bool SyncWithRealClock()
        {
            m_ControlledTime = GetLocalTime();
            SetTime(m_ControlledTime);

            if (m_IsTimePassing && !StopTODTicker())
            {
                return false;
            }

            m_Mode = TimeMode::CustomRate;
            m_RateMultiplier = 1.0f / 24.0f;
            m_IsRealClockSynced = true;
            Logger::Info("Time synchronized with real clock");
            return true;
        }

        bool ShiftTime(float hours)
        {
            const float baseTime = (m_Mode == TimeMode::DefaultGame)
                ? GetCurrentTime()
                : m_ControlledTime;
            const float shiftedTime = NormalizeTime(baseTime + hours);

            m_ControlledTime = shiftedTime;
            SetTime(shiftedTime);
            Logger::Info("Time shifted by {} hours", hours);
            return true;
        }

        bool SetFrozenTime(float hours)
        {
            if (m_IsTimePassing && !StopTODTicker())
            {
                return false;
            }

            m_ControlledTime = NormalizeTime(hours);
            SetTime(m_ControlledTime);
            m_Mode = TimeMode::Frozen;
            m_RateMultiplier = 1.0f;
            m_IsRealClockSynced = false;
            Logger::Info("Time set to {} hours and frozen", m_ControlledTime);
            return true;
        }

        const char* GetModeName() const
        {
            switch (m_Mode)
            {
            case TimeMode::DefaultGame:
                return "Default";
            case TimeMode::Frozen:
                return "Frozen";
            case TimeMode::CustomRate:
                return m_IsRealClockSynced ? "Real Clock" : "Custom";
            default:
                return "Unknown";
            }
        }

        void RenderStatusOverlay()
        {
            const float currentTime = GetCurrentTime();
            int hour = static_cast<int>(std::floor(currentTime));
            int minute = static_cast<int>(std::round((currentTime - hour) * 60.0f));
            if (minute >= 60)
            {
                minute -= 60;
                hour = (hour + 1) % 24;
            }

            if (m_ApplyInitialWindowPos)
            {
                ImGui::SetNextWindowPos(
                    m_HasSavedWindowPos ? m_WindowPos : ImVec2(16.0f, 120.0f),
                    ImGuiCond_Always);
                m_ApplyInitialWindowPos = false;
            }

            ImGui::SetNextWindowBgAlpha(0.75f);
            if (ImGui::Begin("##timelapse_status", nullptr,
                ImGuiWindowFlags_NoDecoration       |
                ImGuiWindowFlags_AlwaysAutoResize   |
                ImGuiWindowFlags_NoSavedSettings    |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav))
            {
                HandleWindowDragging();
                m_WindowPos = ImGui::GetWindowPos();
                m_HasSavedWindowPos = true;

                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "Time Control");
                ImGui::Separator();
                ImGui::Text("Time: %02d:%02d", hour, minute);
                ImGui::Text("Mode: %s", GetModeName());
                if (m_Mode == TimeMode::CustomRate)
                {
                    if (m_IsRealClockSynced)
                    {
                        ImGui::Text("Rate: real clock");
                    }
                    else
                    {
                        ImGui::Text("Rate: x%.3g", m_RateMultiplier);
                    }
                }
                ImGui::TextDisabled("F7 for full keybindings");
            }
            ImGui::End();
        }

        HMODULE m_GameModule = nullptr;
        IGameMemoryService* m_Memory = nullptr;
        IGameStateService* m_GameState = nullptr;
        ConfigHandle m_Config;
        bool m_IsTimePassing = true;
        TimeMode m_Mode = TimeMode::DefaultGame;
        float m_ControlledTime = 12.0f;
        float m_RateMultiplier = 1.0f;
        bool m_IsRealClockSynced = false;
        ImVec2 m_WindowPos = ImVec2(0.0f, 0.0f);
        bool m_HasSavedWindowPos = false;
        bool m_ApplyInitialWindowPos = true;
    };
}

SNOWLOADER_DEFINE_SCRIPT(TimeControlScript)
