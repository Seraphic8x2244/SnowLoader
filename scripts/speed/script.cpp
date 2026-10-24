#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "sdk/base_script.h"
#include "sdk/script_api.h"
#include "core/logger.h"
#include <imgui.h>

namespace
{
    class SpeedHudScript final : public BaseScript
    {
    public:
        const char* GetName() const override
        {
            return "speed";
        }

        bool Initialize(const ScriptContext& context) override
        {
            if (!LoadKeybindingContext(context))
            {
                return false;
            }

            m_GameModule = context.gameModule;
            m_Memory = context.memory;
            m_GameState = context.gameState;

            if (!m_GameModule || !m_Memory)
            {
                Logger::Error("SpeedHudScript: Game module or memory service is null");
                return false;
            }

            m_Config = OpenConfig("speed.ini", "HUD");
            if (!m_Config)
            {
                return false;
            }

            m_ShowDistance = m_Config->GetBool("ShowDistance", true);
            m_ShowDecimalSpeed = m_Config->GetBool("ShowDecimalSpeed", false);
            m_HasSavedWindowPos = m_Config->GetBool("HasWindowPos", false);
            m_WindowPos = ImVec2(
                m_Config->GetFloat("WindowPosX", 0.0f),
                m_Config->GetFloat("WindowPosY", 0.0f));

            static constexpr ScriptActionBinding bindings[] = {
                { "ResetDistance", "Reset odometer", '8', HotkeyModifier_None },
            };

            RegisterKeybindingActions(bindings);
            SaveKeybindings();

            Logger::Info("SpeedHudScript initialized");
            return true;
        }

        void OnTick(float deltaTime) override
        {
            if (m_GameState && !m_GameState->IsInGame())
                return;

            UpdateHud(deltaTime);

            const ImGuiIO& io = ImGui::GetIO();
            const float margin = 10.0f;
            const float gaugeRadius = 80.0f;
            const float rightInset = io.DisplaySize.x * 0.15f;

            if (m_ApplyInitialWindowPos)
            {
                if (m_HasSavedWindowPos)
                {
                    ImGui::SetNextWindowPos(m_WindowPos, ImGuiCond_Always);
                }
                else
                {
                    ImGui::SetNextWindowPos(
                        ImVec2(io.DisplaySize.x - rightInset - gaugeRadius, io.DisplaySize.y - margin - gaugeRadius),
                        ImGuiCond_Always,
                        ImVec2(0.5f, 1.0f));
                }
                m_ApplyInitialWindowPos = false;
            }

            ImGui::SetNextWindowBgAlpha(0.0f);
            if (ImGui::Begin("##speed_hud_gauge", nullptr,
                ImGuiWindowFlags_NoDecoration       |
                ImGuiWindowFlags_AlwaysAutoResize   |
                ImGuiWindowFlags_NoSavedSettings    |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav))
            {
                HandleWindowDragging();
                m_WindowPos = ImGui::GetWindowPos();
                m_HasSavedWindowPos = true;

                const float speed = GetCurrentSpeed();
                const float distance = m_DistanceMeters / 1000.0f;
                RenderSpeedometerGauge(speed, distance, gaugeRadius);
            }
            ImGui::End();

            if (WasKeybindingPressed("ResetDistance"))
            {
                m_DistanceMeters = 0.0f;
            }
        }
        
        void RenderSpeedometerGauge(float speedKmh, float distanceKm, float radius)
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 center = ImGui::GetCursorScreenPos();
            center.x += radius;
            center.y += radius;
            
            const float maxSpeed = 150.0f;
            const ImU32 bgColor = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
            const ImU32 rimColor = ImGui::GetColorU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            const ImU32 needleColor = ImGui::GetColorU32(ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            const ImU32 textColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            
            draw->AddCircleFilled(center, radius, bgColor, 32);
            draw->AddCircle(center, radius, rimColor, 32, 3.0f);
            
            for (int i = 0; i <= 10; ++i)
            {
                const float angle = 3.14159f * 0.2f + (3.14159f * 1.6f * i / 10.0f);
                const float outerX = center.x + cosf(angle) * (radius - 5.0f);
                const float outerY = center.y + sinf(angle) * (radius - 5.0f);
                const float innerX = center.x + cosf(angle) * (radius - 15.0f);
                const float innerY = center.y + sinf(angle) * (radius - 15.0f);
                draw->AddLine(ImVec2(outerX, outerY), ImVec2(innerX, innerY), rimColor, 2.0f);
            }
            
            const float speedRatio = (speedKmh < maxSpeed) ? (speedKmh / maxSpeed) : 1.0f;
            const float needleAngle = 3.14159f * 0.2f + (3.14159f * 1.6f * speedRatio);
            const float needleLength = radius - 20.0f;
            const float needleTipX = center.x + cosf(needleAngle) * needleLength;
            const float needleTipY = center.y + sinf(needleAngle) * needleLength;
            draw->AddLine(center, ImVec2(needleTipX, needleTipY), needleColor, 3.0f);
            draw->AddCircleFilled(center, 5.0f, needleColor, 8);
            draw->AddCircle(center, 4.0f, rimColor, 8);
            
            char speedStr[32];
            if (m_ShowDecimalSpeed)
                snprintf(speedStr, sizeof(speedStr), "%.1f", speedKmh);
            else
                snprintf(speedStr, sizeof(speedStr), "%.0f", speedKmh);
            ImVec2 speedSize = ImGui::CalcTextSize(speedStr);
            draw->AddText(ImVec2(center.x - speedSize.x * 0.5f, center.y - 10.0f), textColor, speedStr);
            draw->AddText(ImVec2(center.x - 15.0f, center.y + 5.0f), textColor, "km/h");
            
            if (m_ShowDistance)
            {
                char odoStr[32];
                snprintf(odoStr, sizeof(odoStr), "ODO: %.1f km", distanceKm);
                ImVec2 odoSize = ImGui::CalcTextSize(odoStr);
                draw->AddText(ImVec2(center.x - odoSize.x * 0.5f, center.y + radius + 10.0f), textColor, odoStr);
            }
            
            ImGui::Dummy(ImVec2(radius * 2, radius * 2 + 30));
        }

        void Shutdown() override
        {
            if (m_Config)
            {
                m_Config->SetBool("ShowDistance", m_ShowDistance);
                m_Config->SetBool("ShowDecimalSpeed", m_ShowDecimalSpeed);
                m_Config->SetBool("HasWindowPos", m_HasSavedWindowPos);
                m_Config->SetFloat("WindowPosX", m_WindowPos.x);
                m_Config->SetFloat("WindowPosY", m_WindowPos.y);
                m_Config->Save();
                m_Config.Reset();
            }
        }

    private:
        static constexpr uintptr_t SPEED_ADDRESS = 0x2A8FA2C;

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

        void UpdateHud(float deltaTime)
        {
            if (!m_GameModule || !m_Memory)
            {
                return;
            }

            const float speedMps = GetCurrentSpeed() * 0.277778f;
            m_DistanceMeters += speedMps * deltaTime;
        }

        float GetCurrentSpeed() const
        {
            if (!m_GameModule || !m_Memory)
            {
                return 0.0f;
            }

            float speed = 0.0f;
            return m_Memory->ReadModuleValue(m_GameModule, SPEED_ADDRESS, speed, "VehicleSpeed")
                ? speed
                : 0.0f;
        }

        HMODULE m_GameModule = nullptr;
        IGameMemoryService* m_Memory = nullptr;
        IGameStateService* m_GameState = nullptr;
        ConfigHandle m_Config;
        bool m_ShowDistance = true;
        bool m_ShowDecimalSpeed = false;
        float m_DistanceMeters = 0.0f;
        ImVec2 m_WindowPos = ImVec2(0.0f, 0.0f);
        bool m_HasSavedWindowPos = false;
        bool m_ApplyInitialWindowPos = true;
    };
}

SNOWLOADER_DEFINE_SCRIPT(SpeedHudScript)
