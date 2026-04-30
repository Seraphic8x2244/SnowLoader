#include "core/overlay_service.h"

#include <algorithm>

#include "core/core_constants.h"
#include <imgui.h>

void OverlayService::ShowTransientMessage(const char* message, unsigned int durationMs)
{
    if (!message || message[0] == '\0')
    {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    m_Messages.push_back(TransientMessage{ message, now + durationMs });
}

void OverlayService::Render()
{
    PruneExpiredMessages();
    if (m_Messages.empty())
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float maxWindowWidth = (io.DisplaySize.x > 0.0f)
        ? (io.DisplaySize.x * CoreConstants::UI::MESSAGE_WINDOW_WIDTH_RATIO)
        : CoreConstants::UI::MESSAGE_WINDOW_FALLBACK_WIDTH;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - CoreConstants::UI::MESSAGE_WINDOW_OFFSET_X, CoreConstants::UI::MESSAGE_WINDOW_OFFSET_Y), ImGuiCond_Always,
        ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(CoreConstants::UI::MESSAGE_WINDOW_MIN_WIDTH, 0.0f), ImVec2(maxWindowWidth, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(CoreConstants::UI::MESSAGE_WINDOW_BG_ALPHA);
    if (ImGui::Begin("##host_transient_messages", nullptr,
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoMove))
    {
        for (auto it = m_Messages.rbegin(); it != m_Messages.rend(); ++it)
        {
            const ULONGLONG now = GetTickCount64();
            const ULONGLONG remainingMs = it->expiresAt > now ? (it->expiresAt - now) : 0;
            float alpha = 1.0f;
            if (remainingMs < CoreConstants::Timing::MESSAGE_FADE_DURATION_MS)
            {
                alpha = static_cast<float>(remainingMs) / static_cast<float>(CoreConstants::Timing::MESSAGE_FADE_DURATION_MS);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxWindowWidth - CoreConstants::UI::MESSAGE_WINDOW_TEXT_WRAP_MARGIN);
            ImGui::TextUnformatted(it->text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleVar();

            if (it + 1 != m_Messages.rend())
            {
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}

void OverlayService::Clear()
{
    m_Messages.clear();
}

void OverlayService::PruneExpiredMessages()
{
    const ULONGLONG now = GetTickCount64();
    std::erase_if(m_Messages, [now](const TransientMessage& message)
    {
        return message.expiresAt <= now;
    });
}