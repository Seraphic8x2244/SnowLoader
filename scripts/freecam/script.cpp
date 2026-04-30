#include <Windows.h>

#include <string>

#include "core/logger.h"
#include "sdk/base_script.h"
#include "sdk/script_api.h"

namespace
{
    class FreeCamScript final : public BaseScript
    {
    public:
        const char* GetName() const override
        {
            return "freecam";
        }

        bool Initialize(const ScriptContext& context) override
        {
            if (!LoadKeybindingContext(context))
            {
                return false;
            }

            m_GameModule = context.gameModule;
            m_Memory = context.memory;

            if (!m_GameModule || !m_Memory)
            {
                Logger::Error("FreeCamScript: Game module or memory service is null");
                return false;
            }

            m_IsFreeCamActive = false;

            static constexpr ScriptActionBinding bindings[] = {
                { "ToggleFreecam", "Toggle freecam mode", VK_F5, HotkeyModifier_Control },
            };

            RegisterKeybindingActions(bindings);
            SaveKeybindings();
            Logger::Info("FreeCam initialized");
            return true;
        }

        void OnTick(float) override
        {
            if (WasKeybindingPressed("ToggleFreecam"))
            {
                ToggleFreeCam();
                Logger::Info("FreeCam: {}", m_IsFreeCamActive ? "enabled" : "disabled");
            }
        }

        void Shutdown() override
        {
            if (m_IsFreeCamActive)
            {
                DisableFreeCam();
            }

            m_Memory = nullptr;
            m_GameModule = nullptr;
            Logger::Info("FreeCamScript shutdown");
        }

    private:
        // Device mode patterns
        static constexpr const char* DEV_CHECK_PATTERN_A = "80 B8 C8 00 00 00 00 75";
        static constexpr uint8_t DEV_CHECK_PATCH_A[] = { 0x80, 0xB8, 0xC8, 0x00, 0x00, 0x00, 0x01 };
        static constexpr const char* DEV_CHECK_REVERT_PATTERN_A = "80 B8 C8 00 00 00 01 75";
        static constexpr uint8_t DEV_CHECK_REVERT_PATCH_A[] = { 0x80, 0xB8, 0xC8, 0x00, 0x00, 0x00, 0x00 };

        static constexpr const char* DEV_CHECK_PATTERN_B = "80 B8 C8 00 00 00 00 0F 85";
        static constexpr uint8_t DEV_CHECK_PATCH_B[] = { 0x80, 0xB8, 0xC8, 0x00, 0x00, 0x00, 0x01 };
        static constexpr const char* DEV_CHECK_REVERT_PATTERN_B = "80 B8 C8 00 00 00 01 0F 85";
        static constexpr uint8_t DEV_CHECK_REVERT_PATCH_B[] = { 0x80, 0xB8, 0xC8, 0x00, 0x00, 0x00, 0x00 };

        static constexpr uintptr_t FLY_MODE_FLAG_OFFSET = 0x2A4E018;
        static constexpr uint8_t FLY_MODE_ON_PATCH[] = { 0x01 };
        static constexpr uint8_t FLY_MODE_OFF_PATCH[] = { 0x00 };

        void ToggleFreeCam()
        {
            if (!m_Memory || !m_GameModule)
            {
                return;
            }

            if (m_IsFreeCamActive)
            {
                DisableFreeCam();
            }
            else
            {
                EnableFreeCam();
            }
        }

        bool EnableFreeCam()
        {
            if (!m_Memory || !m_GameModule)
            {
                Logger::Warning("FreeCamScript: Enable requested without memory service or game module");
                return false;
            }

            const bool patchedA = m_Memory->ApplyPatternPatch(DEV_CHECK_PATTERN_A, DEV_CHECK_PATCH_A,
                sizeof(DEV_CHECK_PATCH_A), "DevModeA");
            const bool patchedB = m_Memory->ApplyPatternPatch(DEV_CHECK_PATTERN_B, DEV_CHECK_PATCH_B,
                sizeof(DEV_CHECK_PATCH_B), "DevModeB");
            const bool flyMode = m_Memory->WriteModuleOffset(m_GameModule, FLY_MODE_FLAG_OFFSET,
                FLY_MODE_ON_PATCH, sizeof(FLY_MODE_ON_PATCH), "FlyMode");

            if (!(patchedA && patchedB && flyMode))
            {
                Logger::Warning("FreeCamScript: enable failed (DevModeA={}, DevModeB={}, FlyMode={})",
                    patchedA, patchedB, flyMode);

                // Attempt to leave the game in a consistent state if one of the patches failed.
                m_Memory->ApplyPatternPatch(DEV_CHECK_REVERT_PATTERN_A, DEV_CHECK_REVERT_PATCH_A,
                    sizeof(DEV_CHECK_REVERT_PATCH_A), "DevModeA");
                m_Memory->ApplyPatternPatch(DEV_CHECK_REVERT_PATTERN_B, DEV_CHECK_REVERT_PATCH_B,
                    sizeof(DEV_CHECK_REVERT_PATCH_B), "DevModeB");
                m_Memory->WriteModuleOffset(m_GameModule, FLY_MODE_FLAG_OFFSET,
                    FLY_MODE_OFF_PATCH, sizeof(FLY_MODE_OFF_PATCH), "FlyMode");
                ShowTransientMessage(GetOverlayService(), "FreeCam: enable failed");
                m_IsFreeCamActive = false;
                return false;
            }

            m_IsFreeCamActive = true;
            ShowTransientMessage(GetOverlayService(), "FreeCam: enabled");
            Logger::Info("FreeCamScript: FreeCam enabled");
            return true;
        }

        bool DisableFreeCam()
        {
            if (!m_Memory || !m_GameModule)
            {
                Logger::Warning("FreeCamScript: Disable requested without memory service or game module");
                m_IsFreeCamActive = false;
                return false;
            }

            const bool revertedA = m_Memory->ApplyPatternPatch(DEV_CHECK_REVERT_PATTERN_A, DEV_CHECK_REVERT_PATCH_A,
                sizeof(DEV_CHECK_REVERT_PATCH_A), "DevModeA");
            const bool revertedB = m_Memory->ApplyPatternPatch(DEV_CHECK_REVERT_PATTERN_B, DEV_CHECK_REVERT_PATCH_B,
                sizeof(DEV_CHECK_REVERT_PATCH_B), "DevModeB");
            const bool flyMode = m_Memory->WriteModuleOffset(m_GameModule, FLY_MODE_FLAG_OFFSET,
                FLY_MODE_OFF_PATCH, sizeof(FLY_MODE_OFF_PATCH), "FlyMode");

            m_IsFreeCamActive = false;
            if (!(revertedA && revertedB && flyMode))
            {
                Logger::Warning("FreeCamScript: disable failed (DevModeA={}, DevModeB={}, FlyMode={})",
                    revertedA, revertedB, flyMode);
                ShowTransientMessage(GetOverlayService(), "FreeCam: disable incomplete");
                return false;
            }

            ShowTransientMessage(GetOverlayService(), "FreeCam: disabled");
            Logger::Info("FreeCamScript: FreeCam disabled");
            return true;
        }

        HMODULE m_GameModule = nullptr;
        IGameMemoryService* m_Memory = nullptr;
        bool m_IsFreeCamActive = false;
    };
}

SNOWLOADER_DEFINE_SCRIPT(FreeCamScript)