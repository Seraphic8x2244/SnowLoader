#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <unordered_map>

#include "sdk/base_script.h"
#include "sdk/script_api.h"
#include "core/logger.h"
#include "core/MinHook.h"
#include <imgui.h>

// ============================================================================
//  Game structures -- from drafty46/SMT (game_data.h)
// ============================================================================
#pragma pack(push, 1)
struct combine_combineTruckAction
{
    uint8_t  pad_0000[0x30];   // 0x0000
    void*    Veh;              // 0x0030
    float    PowerCoef;        // 0x0038
    bool     IsInAutoMode;     // 0x003C
    uint8_t  pad_003D[3];      // 0x003D
    float    WheelTurn;        // 0x0040
    float    Accel;            // 0x0044
    uint8_t  pad_0048[0x28];   // 0x0048
    int32_t  Gear_1;           // 0x0070  current gear
    int32_t  Gear_2;           // 0x0074  target gear
    uint8_t  pad_0078[0x38];   // 0x0078
    int32_t  NextGear;         // 0x00E0
};

struct Vehicle
{
    uint8_t                      pad_0000[0x68]; // 0x0000
    combine_combineTruckAction*  TruckAction;    // 0x0068
};

struct combine_TRUCK_CONTROL
{
    uint8_t   pad_0000[0x08]; // 0x0000
    Vehicle*  CurVehicle;     // 0x0008
};
#pragma pack(pop)

// ============================================================================
//  Function pointer types -- from drafty46/SMT (memory.h)
// ============================================================================
using FnShiftGear           = bool(Vehicle* veh, int32_t gear);
using FnShiftToAutoGear     = void(Vehicle* veh);
using FnDisableAutoAndShift = bool(Vehicle* veh, int32_t gear);
using FnGetMaxGear          = int32_t(const Vehicle* veh);
using FnSetCurrentVehicle   = void(combine_TRUCK_CONTROL* ctrl, Vehicle* veh);

// ============================================================================
//  Hook globals
// ============================================================================
enum class ShiftMode : int
{
    Auto = 0,
    Manual = 1,
    Smart = 2,
};

static FnShiftGear*           g_ShiftGearO           = nullptr;
static FnShiftToAutoGear*     g_ShiftToAutoGearO     = nullptr;
static FnDisableAutoAndShift* g_DisableAutoAndShiftO = nullptr;
static FnGetMaxGear*          g_GetMaxGearO          = nullptr;
static FnSetCurrentVehicle*   g_SetCurrentVehicleO   = nullptr;

// Live state -- written from hooks, read from OnTick/rendering
static Vehicle* g_CurrentVehicle   = nullptr;
static ShiftMode g_ShiftMode = ShiftMode::Manual;
static bool     g_NoAutoReverseEnabled = true;

// Tracks whether each vehicle was originally in auto mode when first seen
static std::unordered_map<Vehicle*, bool> g_WasAutoMap;

static bool IsScriptControlledMode()
{
    return g_ShiftMode == ShiftMode::Manual || g_ShiftMode == ShiftMode::Smart;
}

static combine_combineTruckAction* GetTruckAction(Vehicle* veh)
{
    return veh ? veh->TruckAction : nullptr;
}

static const char* GetShiftModeName()
{
    switch (g_ShiftMode)
    {
    case ShiftMode::Auto:
        return "AUTO";
    case ShiftMode::Manual:
        return "MANUAL";
    case ShiftMode::Smart:
        return "SMART";
    default:
        return "UNKNOWN";
    }
}

// ============================================================================
//  Hook implementations -- mirroring drafty46/SMT (game_data.cpp)
// ============================================================================

// Called when player enters / switches vehicle
static void Hooked_SetCurrentVehicle(combine_TRUCK_CONTROL* ctrl, Vehicle* veh)
{
    if (g_SetCurrentVehicleO)
        g_SetCurrentVehicleO(ctrl, veh);

    g_CurrentVehicle = veh;
    if (!IsScriptControlledMode())
        return;

    combine_combineTruckAction* truckAction = GetTruckAction(veh);
    if (!truckAction)
        return;

    if (veh)
    {
        // Remember original auto state, force manual
        if (g_WasAutoMap.find(veh) == g_WasAutoMap.end())
            g_WasAutoMap[veh] = truckAction->IsInAutoMode;

        truckAction->IsInAutoMode = false;
        // Start in gear 1
        if (g_ShiftGearO)
            g_ShiftGearO(veh, 1);
    }
}

// Game calls this when shifting to auto/high gear from UI
static void Hooked_ShiftToAutoGear(Vehicle* veh)
{
    if (!IsScriptControlledMode())
    {
        if (g_ShiftToAutoGearO)
            g_ShiftToAutoGearO(veh);
        return;
    }

    if (combine_combineTruckAction* truckAction = GetTruckAction(veh))
        truckAction->IsInAutoMode = false;
}

// Game calls this through brake-to-reverse path
static bool Hooked_DisableAutoAndShift(Vehicle* veh, int32_t gear)
{
    if (!IsScriptControlledMode())
        return g_DisableAutoAndShiftO ? g_DisableAutoAndShiftO(veh, gear) : false;
    return false; // Block in manual mode
}

static bool Hooked_ShiftGear(Vehicle* veh, int32_t gear)
{
    if (!g_ShiftGearO)
        return false;

    if (g_NoAutoReverseEnabled && gear == -1 &&
        veh && GetTruckAction(veh) && GetTruckAction(veh)->IsInAutoMode)
    {
        if (GetTruckAction(veh)->Gear_1 == -1)
            return g_ShiftGearO(veh, 1);

        return false;
    }

    return g_ShiftGearO(veh, gear);
}

// ============================================================================
//  SEH wrapper (must be a free function — MSVC forbids __try in methods
//  of classes with non-trivial destructors)
// ============================================================================
#if defined(_MSC_VER)
static bool InvokeWithSEH(bool(*fn)(void*), void* ctx, unsigned int& exCode)
{
    __try
    {
        return fn(ctx);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        exCode = GetExceptionCode();
        return false;
    }
}
#endif

// ============================================================================
//  Script
// ============================================================================
namespace
{
    class ManualTransmissionScript final : public BaseScript
    {
    public:
        static constexpr uintptr_t SPEED_ADDRESS = 0x2A856CC;
        static constexpr ULONGLONG SMART_SHIFT_COOLDOWN_MS = 450;

        const char* GetName() const override { return "smt"; }

        bool Initialize(const ScriptContext& context) override
        {
            if (!LoadKeybindingContext(context))
                return false;

            m_Memory     = context.memory;
            m_GameModule = context.gameModule;
            m_GameState  = context.gameState;

            if (!m_Memory || !m_GameModule)
            {
                Logger::Error("SMT: memory or game module is null");
                return false;
            }

            m_Config = OpenConfig("smt.ini", "ManualTransmission");
            if (!m_Config)
                return false;

            const bool legacyManualMode = m_Config->GetBool("ManualMode", true);
            const int savedMode = m_Config->GetInt("ShiftMode",
                legacyManualMode ? static_cast<int>(ShiftMode::Manual) : static_cast<int>(ShiftMode::Auto));
            g_ShiftMode = static_cast<ShiftMode>(std::clamp(savedMode,
                static_cast<int>(ShiftMode::Auto), static_cast<int>(ShiftMode::Smart)));
            g_NoAutoReverseEnabled = m_Config->GetBool("NoAutoReverse", true);
            m_ShowHUD          = m_Config->GetBool("ShowHUD", true);

            static constexpr ScriptActionBinding bindings[] = {
                { "ShiftUp",       "Shift up",                 VK_UP,    HotkeyModifier_None },
                { "ShiftDown",     "Shift down",               VK_DOWN,  HotkeyModifier_None },
                { "ShiftReverse",  "Shift to reverse",         'R',      HotkeyModifier_None },
                { "ShiftNeutral",  "Shift to neutral",         'N',      HotkeyModifier_None },
                { "CycleShiftMode",  "Cycle shift mode",       VK_HOME,  HotkeyModifier_None },
                { "ToggleNoAutoReverse", "Toggle auto-reverse prevention", VK_F9, HotkeyModifier_None },
                { "ToggleHUD",     "Toggle transmission HUD",  VK_DELETE,HotkeyModifier_None },
            };
            RegisterKeybindingActions(bindings);
            SaveKeybindings();

            m_Initialized = true;
            Logger::Info("SMT initialized (mode={})", GetShiftModeName());
            return true;
        }

        void OnTick(float) override
        {
            if (!m_Initialized)
                return;

            if (m_GameState && !m_GameState->IsInGame())
                return;

            if (!m_HooksInstalled && !m_HookInstallFailed)
            {
                if (!TryInstallHooksSafe())
                    return;
            }

            if (WasKeybindingPressed("CycleShiftMode"))
                CycleShiftMode();

            if (WasKeybindingPressed("ToggleNoAutoReverse"))
                ToggleNoAutoReverse();

            if (WasKeybindingPressed("ToggleHUD"))
            {
                m_ShowHUD = !m_ShowHUD;
                ShowTransientMessage(GetOverlayService(),
                    m_ShowHUD ? "SMT: HUD shown" : "SMT: HUD hidden");
            }

            if (g_ShiftMode == ShiftMode::Manual && g_CurrentVehicle)
            {
                if (WasKeybindingPressed("ShiftUp"))
                    ShiftRelative(+1);
                if (WasKeybindingPressed("ShiftDown"))
                    ShiftRelative(-1);
                if (WasKeybindingPressed("ShiftReverse"))
                    DoShift(-1);
                if (WasKeybindingPressed("ShiftNeutral"))
                    DoShift(0);
            }

            if (g_ShiftMode == ShiftMode::Smart)
                UpdateSmartShifting();

            if (m_ShowHUD)
                RenderHUD();
        }

        void Shutdown() override
        {
            // Restore auto mode on all vehicles we touched
            if (IsScriptControlledMode())
            {
                for (auto& [veh, wasAuto] : g_WasAutoMap)
                {
                    if (wasAuto && veh && veh->TruckAction)
                        veh->TruckAction->IsInAutoMode = true;
                }
            }
            g_WasAutoMap.clear();
            g_CurrentVehicle = nullptr;

            if (m_HooksInstalled)
                RemoveHooks();

            if (m_Config)
            {
                m_Config->SetBool("ManualMode", g_ShiftMode != ShiftMode::Auto);
                m_Config->SetInt("ShiftMode", static_cast<int>(g_ShiftMode));
                m_Config->SetBool("NoAutoReverse", g_NoAutoReverseEnabled);
                m_Config->SetBool("ShowHUD", m_ShowHUD);
                m_Config->Save();
                m_Config.Reset();
            }

            m_Initialized = false;
        }

    private:
        // Patterns from drafty46/SMT (memory.cpp)
        static constexpr const char* PAT_SHIFT_GEAR =
            "48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 "
            "48 8B 81 48 01 00 00 48 8B F1 48 B9 FF FF FF FF "
            "FF FF 00 FF 8B FA 48 23 C1 74";
        static constexpr const char* PAT_SHIFT_TO_AUTO =
            "40 57 48 81 EC 80 00 00 00 48 8B 41 68 48 8B F9 "
            "48 89 9C 24 90 00 00 00 0F 29 7C 24 60 C6 40 3C 01";
        static constexpr const char* PAT_DISABLE_AUTO_SHIFT =
            "48 8B 41 68 C6 40 3C 00 E9";
        static constexpr const char* PAT_GET_MAX_GEAR =
            "48 8B 41 68 48 8B 50 58 48 8B 48 60 48 3B D1 75 "
            "? 33 C0 C3 48 2B CA 48 C1 F9 02 8D 41 FE";
        static constexpr const char* PAT_SET_CURRENT_VEHICLE =
            "48 8B C4 53 57 48 81 EC 98 00 00 00 48 8B FA 48 "
            "8B D9 48 39 51 08 0F 84 ? ? ? ? 48 89 68 E8 48 "
            "83 C1 70 48 89 70 E0 4C 89 70 D8 4C 89 78 D0";

        struct HookEntry
        {
            const char*  pattern;
            void**       originalOut;
            void*        detour;
            uintptr_t    foundAddr = 0;
        };

        bool InstallOneHook(HookEntry& e)
        {
            e.foundAddr = m_Memory->PatternScan(e.pattern);
            if (!e.foundAddr)
            {
                Logger::Error("SMT: pattern not found");
                return false;
            }
            MH_STATUS s = MH_CreateHook(
                reinterpret_cast<void*>(e.foundAddr), e.detour, e.originalOut);
            if (s != MH_OK)
            {
                Logger::Error("SMT: MH_CreateHook failed ({})", static_cast<int>(s));
                return false;
            }
            s = MH_EnableHook(reinterpret_cast<void*>(e.foundAddr));
            if (s != MH_OK)
            {
                Logger::Error("SMT: MH_EnableHook failed ({})", static_cast<int>(s));
                MH_RemoveHook(reinterpret_cast<void*>(e.foundAddr));
                e.foundAddr = 0;
                return false;
            }
            return true;
        }

        bool InstallHooks()
        {
            const MH_STATUS init = MH_Initialize();
            if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
            {
                Logger::Error("SMT: MH_Initialize failed ({})", static_cast<int>(init));
                return false;
            }

            m_Hooks[0] = { PAT_SHIFT_GEAR,           reinterpret_cast<void**>(&g_ShiftGearO),           reinterpret_cast<void*>(&Hooked_ShiftGear) };
            m_Hooks[1] = { PAT_SHIFT_TO_AUTO,         reinterpret_cast<void**>(&g_ShiftToAutoGearO),     reinterpret_cast<void*>(&Hooked_ShiftToAutoGear) };
            m_Hooks[2] = { PAT_DISABLE_AUTO_SHIFT,    reinterpret_cast<void**>(&g_DisableAutoAndShiftO), reinterpret_cast<void*>(&Hooked_DisableAutoAndShift) };
            m_Hooks[3] = { PAT_GET_MAX_GEAR,          reinterpret_cast<void**>(&g_GetMaxGearO),          nullptr }; // no detour, just resolved for DoShift
            m_Hooks[4] = { PAT_SET_CURRENT_VEHICLE,   reinterpret_cast<void**>(&g_SetCurrentVehicleO),   reinterpret_cast<void*>(&Hooked_SetCurrentVehicle) };

            // Resolve GetMaxGear address (no hook needed, called directly via g_GetMaxGearO)
            {
                uintptr_t addr = m_Memory->PatternScan(PAT_GET_MAX_GEAR);
                if (addr)
                    g_GetMaxGearO = reinterpret_cast<FnGetMaxGear*>(addr);
                else
                    Logger::Warning("SMT: GetMaxGear pattern not found; gear clamping disabled");
            }

            bool ok = true;
            for (int i = 0; i < 5; ++i)
            {
                if (m_Hooks[i].detour == nullptr)
                    continue;
                if (!InstallOneHook(m_Hooks[i]))
                {
                    Logger::Error("SMT: hook #{} failed", i);
                    ok = false;
                }
                else
                {
                    Logger::Info("SMT: hook #{} at 0x{:X}", i, m_Hooks[i].foundAddr);
                }
            }
            m_HooksInstalled = ok;
            return ok;
        }

        static bool InstallHooksThunk(void* ctx)
        {
            return static_cast<ManualTransmissionScript*>(ctx)->InstallHooks();
        }

        bool TryInstallHooksSafe()
        {
#if defined(_MSC_VER)
            unsigned int exCode = 0;
            if (!InvokeWithSEH(&InstallHooksThunk, this, exCode))
            {
                m_HookInstallFailed = true;
                if (exCode != 0)
                    Logger::Error("SMT: hook installation faulted with SEH 0x{:X}", exCode);
                else
                    Logger::Error("SMT: hook installation failed during runtime setup");
                return false;
            }
#else
            if (!InstallHooks())
            {
                m_HookInstallFailed = true;
                Logger::Error("SMT: hook installation failed during runtime setup");
                return false;
            }
#endif
            return true;
        }

        void RemoveHooks()
        {
            for (auto& h : m_Hooks)
            {
                if (h.foundAddr)
                {
                    MH_DisableHook(reinterpret_cast<void*>(h.foundAddr));
                    MH_RemoveHook(reinterpret_cast<void*>(h.foundAddr));
                    h.foundAddr = 0;
                }
            }
            g_ShiftGearO           = nullptr;
            g_ShiftToAutoGearO     = nullptr;
            g_DisableAutoAndShiftO = nullptr;
            g_GetMaxGearO          = nullptr;
            g_SetCurrentVehicleO   = nullptr;
            m_HooksInstalled       = false;
        }

        void CycleShiftMode()
        {
            switch (g_ShiftMode)
            {
            case ShiftMode::Auto:
                g_ShiftMode = ShiftMode::Manual;
                break;
            case ShiftMode::Manual:
                g_ShiftMode = ShiftMode::Smart;
                break;
            case ShiftMode::Smart:
            default:
                g_ShiftMode = ShiftMode::Auto;
                break;
            }

            if (g_CurrentVehicle && g_CurrentVehicle->TruckAction)
            {
                g_CurrentVehicle->TruckAction->IsInAutoMode = !IsScriptControlledMode();

                if (IsScriptControlledMode() && g_CurrentVehicle->TruckAction->Gear_1 <= 0)
                    DoShift(1);
            }

            m_LastSmartShiftAt = 0;

            ShowTransientMessage(GetOverlayService(),
                g_ShiftMode == ShiftMode::Auto
                    ? "SMT: AUTO mode"
                    : (g_ShiftMode == ShiftMode::Manual ? "SMT: MANUAL mode" : "SMT: SMART mode"));
        }

        void ToggleNoAutoReverse()
        {
            g_NoAutoReverseEnabled = !g_NoAutoReverseEnabled;
            ShowTransientMessage(GetOverlayService(),
                g_NoAutoReverseEnabled
                    ? "SMT: auto-reverse prevention ON"
                    : "SMT: auto-reverse prevention OFF");
        }

        void DoShift(int32_t gear)
        {
            if (!g_CurrentVehicle || !g_ShiftGearO)
                return;

            if (g_GetMaxGearO)
            {
                int32_t maxGear = g_GetMaxGearO(g_CurrentVehicle);
                gear = std::clamp(gear, -1, maxGear);
            }

            g_ShiftGearO(g_CurrentVehicle, gear);
        }

        float GetCurrentSpeedKmh() const
        {
            if (!m_GameModule || !m_Memory)
                return 0.0f;

            float speed = 0.0f;
            return m_Memory->ReadModuleValue(m_GameModule, SPEED_ADDRESS, speed, "VehicleSpeed")
                ? std::max(speed, 0.0f)
                : 0.0f;
        }

        void UpdateSmartShifting()
        {
            if (!g_CurrentVehicle || !g_CurrentVehicle->TruckAction || !g_ShiftGearO)
                return;

            combine_combineTruckAction* truckAction = g_CurrentVehicle->TruckAction;
            const ULONGLONG now = GetTickCount64();
            if (m_LastSmartShiftAt != 0 && now - m_LastSmartShiftAt < SMART_SHIFT_COOLDOWN_MS)
                return;

            const float speedKmh = GetCurrentSpeedKmh();
            const float throttle = std::clamp(truckAction->Accel, 0.0f, 1.0f);
            const float steeringLoad = std::min(std::abs(truckAction->WheelTurn), 1.0f);
            const int32_t currentGear = truckAction->Gear_1;
            const int32_t maxGear = g_GetMaxGearO ? std::max(1, g_GetMaxGearO(g_CurrentVehicle)) : 8;

            if (currentGear <= 0)
            {
                if (throttle > 0.15f)
                {
                    DoShift(1);
                    m_LastSmartShiftAt = now;
                }
                return;
            }

            static constexpr std::array<float, 9> UPSHIFT_SPEEDS = {
                0.0f, 12.0f, 24.0f, 36.0f, 48.0f, 62.0f, 76.0f, 90.0f, 104.0f
            };
            static constexpr std::array<float, 9> DOWNSHIFT_SPEEDS = {
                0.0f, 0.0f, 10.0f, 18.0f, 29.0f, 41.0f, 54.0f, 68.0f, 82.0f
            };
            static constexpr std::array<float, 9> KICKDOWN_SPEEDS = {
                0.0f, 0.0f, 16.0f, 28.0f, 42.0f, 57.0f, 73.0f, 89.0f, 106.0f
            };

            const std::size_t index = static_cast<std::size_t>(std::clamp(currentGear, 1, 8));
            const float holdBias = (throttle * 16.0f) + (steeringLoad * 5.0f);
            const float economyBias = (1.0f - throttle) * 5.0f;
            const float upshiftAt = UPSHIFT_SPEEDS[index] + holdBias - economyBias;
            const float downshiftAt = DOWNSHIFT_SPEEDS[index] - (throttle * 2.5f);

            if (currentGear > 1 && throttle > 0.9f && speedKmh < KICKDOWN_SPEEDS[index])
            {
                DoShift(currentGear - 1);
                m_LastSmartShiftAt = now;
                return;
            }

            if (currentGear < maxGear && speedKmh >= upshiftAt)
            {
                DoShift(currentGear + 1);
                m_LastSmartShiftAt = now;
                return;
            }

            if (currentGear > 1 && speedKmh <= downshiftAt && throttle > 0.2f)
            {
                DoShift(currentGear - 1);
                m_LastSmartShiftAt = now;
            }
        }

        void ShiftRelative(int delta)
        {
            if (!g_CurrentVehicle || !g_CurrentVehicle->TruckAction)
                return;

            int32_t current = g_CurrentVehicle->TruckAction->Gear_1;
            DoShift(current + delta);
        }

        void RenderHUD()
        {
            if (!g_CurrentVehicle || !g_CurrentVehicle->TruckAction)
                return;

            const combine_combineTruckAction* ta = g_CurrentVehicle->TruckAction;
            const int32_t gear = ta->Gear_1;

            ImGui::SetNextWindowPos(ImVec2(10.0f, 250.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.75f);
            if (ImGui::Begin("##smt_hud", nullptr,
                ImGuiWindowFlags_NoDecoration    |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav))
            {
                HandleWindowDragging();

                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "SMT");
                ImGui::Separator();

                const char* modeStr = GetShiftModeName();
                ImVec4 modeColor = g_ShiftMode == ShiftMode::Auto
                    ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                    : (g_ShiftMode == ShiftMode::Manual
                        ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                        : ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
                ImGui::TextColored(modeColor, "%s", modeStr);

                ImGui::Text("Reverse auto-shift: %s", g_NoAutoReverseEnabled ? "BLOCKED" : "ALLOWED");
                if (g_ShiftMode == ShiftMode::Smart)
                    ImGui::Text("Speed: %.0f km/h", GetCurrentSpeedKmh());

                ImGui::Text("Gear: ");
                ImGui::SameLine();
                if (gear == -1)       ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "R");
                else if (gear == 0)   ImGui::TextColored(ImVec4(0.8f,0.8f,0.8f,1), "N");
                else                  ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "%d", gear);

                ImGui::TextDisabled("DEL: hide  HOME: mode cycle  F9: reverse lock");
            }
            ImGui::End();
        }

        void HandleWindowDragging()
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) &&
                !ImGui::IsAnyItemActive())
            {
                const ImVec2 pos = ImGui::GetWindowPos();
                const ImVec2 d   = ImGui::GetIO().MouseDelta;
                ImGui::SetWindowPos(ImVec2(pos.x + d.x, pos.y + d.y));
            }
        }

        static constexpr int HOOK_COUNT = 5;
        HookEntry           m_Hooks[HOOK_COUNT] = {};

        HMODULE             m_GameModule  = nullptr;
        IGameMemoryService* m_Memory      = nullptr;
        IGameStateService*  m_GameState   = nullptr;
        ConfigHandle        m_Config;
        bool                m_ShowHUD     = true;
        bool                m_Initialized = false;
        bool                m_HooksInstalled = false;
        bool                m_HookInstallFailed = false;
        ULONGLONG           m_LastSmartShiftAt = 0;
    };
}

SNOWLOADER_DEFINE_SCRIPT(ManualTransmissionScript)
