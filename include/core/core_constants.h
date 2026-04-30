#pragma once

namespace CoreConstants
{
    // UI Layout Constants — Transient Message Window
    namespace UI
    {
        // Width as a fraction of screen width for transient messages
        constexpr float MESSAGE_WINDOW_WIDTH_RATIO = 0.35f;
        // Fallback width if screen width is invalid
        constexpr float MESSAGE_WINDOW_FALLBACK_WIDTH = 420.0f;
        // Offset from right edge of screen (pixels)
        constexpr float MESSAGE_WINDOW_OFFSET_X = 16.0f;
        // Offset from top edge of screen (pixels)
        constexpr float MESSAGE_WINDOW_OFFSET_Y = 16.0f;
        // Minimum window width (pixels)
        constexpr float MESSAGE_WINDOW_MIN_WIDTH = 220.0f;
        // Window background transparency (0.0 = transparent, 1.0 = opaque)
        constexpr float MESSAGE_WINDOW_BG_ALPHA = 0.75f;
        // Text wrap margin from window edge (pixels)
        constexpr float MESSAGE_WINDOW_TEXT_WRAP_MARGIN = 48.0f;
    }

    // Timing Constants — Transient Message Fade-Out
    namespace Timing
    {
        // Duration for message fade-out effect (milliseconds)
        constexpr unsigned int MESSAGE_FADE_DURATION_MS = 500u;
    }
}
