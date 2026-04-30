#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <string>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "core/config_service.h"
#include "core/game_memory_service.h"
#include "core/game_state_service.h"
#include "core/keybinding_manager.h"
#include "core/logger.h"
#include "core/overlay_service.h"
#include "runtime/script_manager.h"

namespace
{
    using PresentFn       = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    PresentFn       g_OriginalPresent       = nullptr;
    ResizeBuffersFn g_OriginalResizeBuffers = nullptr;
    ID3D11Device*            g_Device       = nullptr;
    ID3D11DeviceContext*     g_Context      = nullptr;
    ID3D11RenderTargetView*  g_Rtv          = nullptr;
    IDXGISwapChain*          g_ActiveSwapChain = nullptr; // swap chain we initialized against
    void**                   g_PatchedVtable = nullptr;   // vtable to restore on shutdown
    std::atomic<bool> g_Initialized{ false };
    std::atomic<bool> g_InPresent{ false };   // re-entrancy guard
    bool g_ImGuiInitialized = false;
    bool g_ScriptsLoaded    = false;          // scripts survive swap-chain recreation
    HMODULE g_GameModule = nullptr;
    HWND g_GameWindow = nullptr;
    WNDPROC g_OriginalWndProc = nullptr;
    std::string g_DllDirectory;
    std::string g_RuntimeDirectory;
    ConfigService g_ConfigService;
    GameMemoryService g_GameMemoryService;
    GameStateService g_GameStateService;
    InputManager g_InputManager;
    OverlayService g_OverlayService;
    KeybindingManager g_KeybindingManager;
    ScriptManager g_ScriptManager;
    ConfigHandle g_HotkeyConfig;
    bool g_ShowKeybindingsUI = false;
    LONGLONG g_LastPresentTime = 0;
    LONGLONG g_PerfFrequency   = 1;
    int      g_SkipFrames      = 5;   // warm-up frames before ImGui touches D3D11

    LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (g_ImGuiInitialized)
        {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

            // Deliver keyboard events to any active keybind capture (event-driven, not polled)
            if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
            {
                unsigned int mods = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= HotkeyModifier_Control;
                if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= HotkeyModifier_Shift;
                if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= HotkeyModifier_Alt;

                const bool keybindConsumed =
                    g_KeybindingManager.FeedKeyDown(static_cast<int>(wParam), mods);

                if (!keybindConsumed && g_ShowKeybindingsUI && wParam == VK_ESCAPE)
                {
                    g_ShowKeybindingsUI = false;
                    return 1;
                }
            }

            const ImGuiIO& io = ImGui::GetIO();
            if ((g_ShowKeybindingsUI || io.WantCaptureMouse || io.WantCaptureKeyboard) &&
                (msg == WM_MOUSEMOVE ||
                 msg == WM_MOUSEWHEEL ||
                 msg == WM_MOUSEHWHEEL ||
                 msg == WM_LBUTTONDOWN ||
                 msg == WM_LBUTTONUP ||
                 msg == WM_LBUTTONDBLCLK ||
                 msg == WM_RBUTTONDOWN ||
                 msg == WM_RBUTTONUP ||
                 msg == WM_RBUTTONDBLCLK ||
                 msg == WM_MBUTTONDOWN ||
                 msg == WM_MBUTTONUP ||
                 msg == WM_MBUTTONDBLCLK ||
                 msg == WM_XBUTTONDOWN ||
                 msg == WM_XBUTTONUP ||
                 msg == WM_XBUTTONDBLCLK ||
                 msg == WM_KEYDOWN ||
                 msg == WM_KEYUP ||
                 msg == WM_SYSKEYDOWN ||
                 msg == WM_SYSKEYUP ||
                 msg == WM_CHAR))
            {
                return 1;
            }
        }

        return g_OriginalWndProc
            ? CallWindowProcA(g_OriginalWndProc, hwnd, msg, wParam, lParam)
            : DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    void HookGameWindow(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }

        if (g_GameWindow == hwnd && g_OriginalWndProc)
        {
            return;
        }

        if (g_GameWindow && g_OriginalWndProc)
        {
            SetWindowLongPtrA(g_GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_OriginalWndProc));
            g_GameWindow = nullptr;
            g_OriginalWndProc = nullptr;
        }

        g_OriginalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
        if (g_OriginalWndProc)
        {
            g_GameWindow = hwnd;
            Logger::Info("Hooked game window proc: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd));
        }
        else
        {
            Logger::Warning("Failed to hook game window proc for hwnd=0x{:X} (error={})",
                reinterpret_cast<uintptr_t>(hwnd), GetLastError());
        }
    }

    void RestoreGameWindowHook()
    {
        if (g_GameWindow && g_OriginalWndProc)
        {
            SetWindowLongPtrA(g_GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_OriginalWndProc));
        }

        g_GameWindow = nullptr;
        g_OriginalWndProc = nullptr;
    }

    void CreateRenderTarget(IDXGISwapChain* swapChain)
    {
        if (g_Rtv)
        {
            g_Rtv->Release();
            g_Rtv = nullptr;
        }
        if (!g_Device) {
            Logger::Warning("CreateRenderTarget: g_Device is null, skipping");
            return;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            Logger::Error("CreateRenderTarget: GetBuffer failed (0x{:X})", static_cast<unsigned long>(hr));
            return;
        }

        hr = g_Device->CreateRenderTargetView(backBuffer, nullptr, &g_Rtv);
        if (FAILED(hr))
        {
            Logger::Error("CreateRenderTarget: CreateRenderTargetView failed (0x{:X})", static_cast<unsigned long>(hr));
            g_Rtv = nullptr;
        }
        else
        {
            Logger::Info("CreateRenderTarget: success (RTV=0x{:X})", reinterpret_cast<uintptr_t>(g_Rtv));
        }
        backBuffer->Release();
    }

    // Tears down only the D3D11 / ImGui resources without touching the script DLLs.
    // Called when the game recreates its swap chain (e.g. loading screen → gameplay).
    void TeardownD3D()
    {
        Logger::Info("TeardownD3D: releasing D3D/ImGui resources for swap-chain change");
        if (g_Rtv)    { g_Rtv->Release();    g_Rtv     = nullptr; }
        if (g_ImGuiInitialized)
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
        }
        RestoreGameWindowHook();
        if (g_Context) { g_Context->Release(); g_Context = nullptr; }
        if (g_Device)  { g_Device->Release();  g_Device  = nullptr; }
        g_ActiveSwapChain = nullptr;
        g_LastPresentTime = 0;
        g_SkipFrames      = 5;
        g_Initialized.store(false, std::memory_order_release);
        g_KeybindingManager.Shutdown();
    }

    void InitializeRuntime(IDXGISwapChain* swapChain)
    {
        if (g_Initialized.load(std::memory_order_acquire))
        {
            return;
        }

        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_Device))))
        {
            return;
        }

        g_Device->GetImmediateContext(&g_Context);
        CreateRenderTarget(swapChain);
        if (!g_GameModule) g_GameModule = GetModuleHandleA(nullptr);

        // Keybindings are now managed by KeybindingManager
        // No need to manually load host keybindings here

        // Initialize the keybinding manager for use by scripts
        g_KeybindingManager.Initialize(&g_ConfigService);

        // Initialize ImGui against the game's swap-chain window and D3D11 device.
        // ImGui is a shared DLL so any script that links imgui.dll shares this context.
        DXGI_SWAP_CHAIN_DESC scDesc{};
        swapChain->GetDesc(&scDesc);
        HookGameWindow(scDesc.OutputWindow);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // don't steal the game's cursor
        io.IniFilename = nullptr;   // don't write imgui.ini into the game directory
        ImGui_ImplWin32_Init(scDesc.OutputWindow);
        ImGui_ImplDX11_Init(g_Device, g_Context);
        g_ImGuiInitialized = true;
        Logger::Info("ImGui initialized (swap chain: 0x{:X})",
            reinterpret_cast<uintptr_t>(swapChain));

        // Load scripts only once — they survive swap-chain recreation.
        if (!g_ScriptsLoaded)
        {
            const std::string scriptsDirectory = g_RuntimeDirectory + "\\scripts";
            ScriptContext context{};
            context.gameModule    = g_GameModule;
            context.hostDirectory = g_RuntimeDirectory.c_str();
            context.input        = &g_InputManager;
            context.configs       = &g_ConfigService;
            context.overlays      = &g_OverlayService;
            context.memory        = &g_GameMemoryService;
            context.keybindings   = &g_KeybindingManager;
            context.gameState     = &g_GameStateService;
            g_GameStateService.Initialize(&g_GameMemoryService);
            g_ScriptManager.LoadScripts(context, scriptsDirectory);
            g_ScriptsLoaded = true;
            Logger::Info("Game module: 0x{:X}", reinterpret_cast<uintptr_t>(g_GameModule));
            Logger::Info("Scripts directory: {}", scriptsDirectory);
        }

        g_ActiveSwapChain = swapChain;
        Logger::Info("ASI host runtime initialized");
        g_Initialized.store(true, std::memory_order_release);
    }

    // Called by the game when it resizes the swap chain (e.g. during startup or
    // resolution changes).  Our g_Rtv would reference the old, released buffer —
    // we must tear it down before the resize and rebuild it after.
    HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* swapChain,
        UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        Logger::Info("ResizeBuffers: {}x{} (swapChain=0x{:X}, active=0x{:X})", width, height,
            reinterpret_cast<uintptr_t>(swapChain),
            reinterpret_cast<uintptr_t>(g_ActiveSwapChain));

        // If we're currently inside a Present call on another thread, wait for it.
        // Spin briefly — ResizeBuffers is rare so this won't burn CPU.
        while (g_InPresent.load(std::memory_order_acquire)) { Sleep(1); }

        if (g_ImGuiInitialized)
            ImGui_ImplDX11_InvalidateDeviceObjects();

        if (g_Rtv) { g_Rtv->Release(); g_Rtv = nullptr; }

        const HRESULT hr = g_OriginalResizeBuffers(
            swapChain, bufferCount, width, height, format, flags);

        if (SUCCEEDED(hr) && g_Device)
        {
            CreateRenderTarget(swapChain);
            if (g_ImGuiInitialized)
                ImGui_ImplDX11_CreateDeviceObjects();
        }

        // Reset the warm-up counter so we skip a few frames while D3D stabilises.
        g_SkipFrames      = 3;
        g_LastPresentTime = 0;
        return hr;
    }

    HRESULT __stdcall HookedPresent(IDXGISwapChain* swapChain, UINT sync, UINT flags)
    {
        // Log every Present (but limit spam after first 10)
        static int presentCount = 0;
        if (++presentCount <= 10 || presentCount % 100 == 0) {
            Logger::Info("HookedPresent #{} (sc=0x{:X}, active=0x{:X}, init={})", presentCount,
                reinterpret_cast<uintptr_t>(swapChain),
                reinterpret_cast<uintptr_t>(g_ActiveSwapChain),
                g_Initialized.load(std::memory_order_acquire));
        }

        // Detect swap-chain recreation (loading screen → gameplay switches swap chains)
        if (g_Initialized.load(std::memory_order_acquire) && swapChain != g_ActiveSwapChain)
        {
            Logger::Info("Swap chain changed (0x{:X} -> 0x{:X}), reinitializing D3D",
                reinterpret_cast<uintptr_t>(g_ActiveSwapChain),
                reinterpret_cast<uintptr_t>(swapChain));
            TeardownD3D();
        }

        InitializeRuntime(swapChain);

        if (g_Initialized.load(std::memory_order_acquire) && g_Rtv)
        {
            // Reject re-entrant calls (game calling Present from within Present —
            // common for splash screens, software cursors, internal render passes).
            bool expected = false;
            if (g_InPresent.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                if (g_SkipFrames > 0)
                {
                    --g_SkipFrames;
                    g_LastPresentTime = 0;
                }
                else
                {
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);

                    const LONGLONG lastTime = g_LastPresentTime;
                    g_LastPresentTime = now.QuadPart;

                    if (lastTime != 0)
                    {
                        float dt = static_cast<float>(now.QuadPart - lastTime)
                                   / static_cast<float>(g_PerfFrequency);
                        if (dt > 0.1f) dt = 0.1f;

                        g_InputManager.Update();
                        g_GameStateService.Update();

                        // Check for host actions (reload scripts, show keybindings menu)
                        // These are managed by KeybindingManager now
                        if (g_InputManager.WasPressed(VK_F6))
                        {
                            Logger::Info("F6 pressed, reloading script DLLs");
                            g_ScriptManager.ReloadAllScripts();
                        }

                        if (g_InputManager.WasPressed(VK_F7))
                        {
                            g_ShowKeybindingsUI = !g_ShowKeybindingsUI;
                            Logger::Info("F7 pressed, keybindings UI = {}", g_ShowKeybindingsUI ? "shown" : "hidden");
                        }

                        // ALWAYS set up ImGui frame — scripts may call ImGui functions in OnTick.
                        try {
                            ImGui_ImplDX11_NewFrame();
                            ImGui_ImplWin32_NewFrame();
                            ImGui::NewFrame();

                            g_ScriptManager.TickScripts(dt);
                            g_ScriptManager.RenderStatusOverlay();
                            g_OverlayService.Render();
                            g_KeybindingManager.RenderKeybindingsUI(g_ShowKeybindingsUI);

                            ImGui::Render();
                            
                            if (g_Rtv && g_Context)
                            {
                                ID3D11RenderTargetView* savedRtv = nullptr;
                                ID3D11DepthStencilView* savedDsv = nullptr;
                                g_Context->OMGetRenderTargets(1, &savedRtv, &savedDsv);
                                g_Context->OMSetRenderTargets(1, &g_Rtv, nullptr);
                                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                                g_Context->OMSetRenderTargets(1, &savedRtv, savedDsv);
                                if (savedRtv) savedRtv->Release();
                                if (savedDsv) savedDsv->Release();
                            }
                        }
                        catch (const std::exception& e) {
                            Logger::Error("ImGui frame exception: {}", e.what());
                        }
                        catch (...) {
                            Logger::Error("ImGui frame crashed (unknown exception)");
                        }
                    }
                }
                g_InPresent.store(false, std::memory_order_release);
            }
        }

        return g_OriginalPresent(swapChain, sync, flags);
    }

    DWORD WINAPI InitHook(LPVOID)
    {
        // Register a throwaway window class that we own, so D3D11 can create a swap
        // chain against it without hitting UIPI/UAC access-denied on system windows.
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(WNDCLASSEXA);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = DefWindowProcA;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.lpszClassName = "SnowrunnerASIDummy";
        RegisterClassExA(&wc);

        HWND dummyHwnd = CreateWindowExA(
            0, wc.lpszClassName, "Dummy",
            WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,   // 8x8, never made visible
            nullptr, nullptr, wc.hInstance, nullptr);

        if (!dummyHwnd)
        {
            Logger::Error("Failed to create dummy window: {}", GetLastError());
            UnregisterClassA(wc.lpszClassName, wc.hInstance);
            return 1;
        }

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count  = 1;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount       = 1;
        sd.OutputWindow      = dummyHwnd;
        sd.Windowed          = TRUE;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain*       dummySwapChain = nullptr;
        ID3D11Device*         dummyDevice    = nullptr;
        ID3D11DeviceContext*  dummyContext   = nullptr;

        const HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &sd, &dummySwapChain, &dummyDevice, nullptr, &dummyContext);

        if (SUCCEEDED(hr) && dummySwapChain)
        {
            void** vtable = *reinterpret_cast<void***>(dummySwapChain);
            g_PatchedVtable = vtable;  // Save for restoration on shutdown

            g_OriginalPresent       = reinterpret_cast<PresentFn>(vtable[8]);
            g_OriginalResizeBuffers = reinterpret_cast<ResizeBuffersFn>(vtable[13]);

            // Patch vtable[8]=Present and vtable[13]=ResizeBuffers in one protection window.
            DWORD oldProtect = 0;
            if (VirtualProtect(&vtable[8], sizeof(void*) * 6, PAGE_READWRITE, &oldProtect))
            {
                vtable[8]  = reinterpret_cast<void*>(&HookedPresent);
                vtable[13] = reinterpret_cast<void*>(&HookedResizeBuffers);
                VirtualProtect(&vtable[8], sizeof(void*) * 6, oldProtect, &oldProtect);
                Logger::Info("DirectX Present + ResizeBuffers hooks installed");
            }
            else
            {
                Logger::Error("VirtualProtect failed — hooks not installed");
            }
        }
        else
        {
            Logger::Error("Failed to create dummy D3D11 device: 0x{:X}", static_cast<unsigned long>(hr));
        }

        if (dummyContext)   dummyContext->Release();
        if (dummyDevice)    dummyDevice->Release();
        if (dummySwapChain) dummySwapChain->Release();
        DestroyWindow(dummyHwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // Resolve DLL directory early so the logger and script loader both use it.
        char dllPath[MAX_PATH] = {};
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
        std::string fullPath(dllPath);
        const size_t sep = fullPath.find_last_of("\\/");
        g_DllDirectory = (sep != std::string::npos) ? fullPath.substr(0, sep) : ".";
        g_RuntimeDirectory = g_DllDirectory + "\\SnowLoader";

        std::error_code ec;
        std::filesystem::create_directories(g_RuntimeDirectory, ec);
        if (ec)
        {
            return FALSE;
        }

        SetDllDirectoryA(g_RuntimeDirectory.c_str());
        const HMODULE imguiModule = LoadLibraryA((g_RuntimeDirectory + "\\imgui.dll").c_str());

        g_ConfigService.SetBaseDirectory(g_RuntimeDirectory);

        Logger::Initialize(g_RuntimeDirectory + "\\SnowLoader.log");
        Logger::Info("=== SnowLoader Host v1.0 ===");
        Logger::Info("DLL directory: {}", g_DllDirectory);
        Logger::Info("Runtime directory: {}", g_RuntimeDirectory);
        if (!imguiModule)
        {
            Logger::Error("Failed to preload imgui.dll from {}", g_RuntimeDirectory);
        }

        // Cache the perf-counter frequency once (denominator for all delta-time math).
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        g_PerfFrequency = freq.QuadPart;
        g_InputManager.Reset();

        const HANDLE thread = CreateThread(nullptr, 0, InitHook, nullptr, 0, nullptr);
        if (thread)
        {
            CloseHandle(thread);
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Logger::Info("=== SnowLoader Host Shutting Down ===");
        g_ScriptManager.Shutdown();
        g_OverlayService.Clear();

        if (g_HotkeyConfig)
        {
            g_HotkeyConfig.Reset();
        }

        if (g_ImGuiInitialized)
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
        }
        RestoreGameWindowHook();

        if (g_Rtv) g_Rtv->Release();
        if (g_Context) g_Context->Release();
        if (g_Device) g_Device->Release();

        // Restore the original vtable entries before unloading to prevent the game
        // from calling into freed memory on the next Present/ResizeBuffers call.
        if (g_PatchedVtable && g_OriginalPresent && g_OriginalResizeBuffers)
        {
            Logger::Info("Restoring original vtable entries before unload");
            DWORD oldProtect = 0;
            if (VirtualProtect(&g_PatchedVtable[8], sizeof(void*) * 6, PAGE_READWRITE, &oldProtect))
            {
                g_PatchedVtable[8]  = reinterpret_cast<void*>(g_OriginalPresent);
                g_PatchedVtable[13] = reinterpret_cast<void*>(g_OriginalResizeBuffers);
                VirtualProtect(&g_PatchedVtable[8], sizeof(void*) * 6, oldProtect, &oldProtect);
                Logger::Info("Vtable restored successfully");
            }
            else
            {
                Logger::Error("Failed to restore vtable on shutdown");
            }
        }

        Logger::Shutdown();
    }

    return TRUE;
}
