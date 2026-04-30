#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "sdk/script_api.h"

class OverlayService final : public IOverlayService
{
public:
    void ShowTransientMessage(const char* message, unsigned int durationMs = 6000) override;
    void Render();
    void Clear();

private:
    struct TransientMessage
    {
        std::string text;
        ULONGLONG expiresAt = 0;
    };

    void PruneExpiredMessages();

    std::vector<TransientMessage> m_Messages;
};