# SnowLoader — Script Hook for SnowRunner

A Script Hook-style ASI host for SnowRunner. One native host DLL, many independent script DLLs.

## Features

- **ASI Host**: Hooks D3D11 Present, boots the runtime, renders ImGui overlays
- **Script SDK**: Stable C++ interface — scripts are independent DLLs discovered at runtime
- **Game State Detection**: Pattern-scanned game state service (in-game / main menu / paused)
- **Memory Services**: Pattern scanning, RIP-relative resolution, pointer chains, typed read/write
- **Configuration**: Per-script INI files with typed get/set and auto-save
- **Input & Keybindings**: Real-time key/chord polling with rebindable hotkeys and ImGui UI
- **Overlay System**: Host-rendered transient messages and per-script ImGui windows
- **MinHook Integration**: Function hooking via auto-fetched MinHook dependency

## Included Scripts

| Script | Description |
|--------|-------------|
| **speed** | Speedometer gauge overlay with odometer |
| **timelapse** | Time-of-day control — freeze, scale, shift, real-clock sync |
| **smt** | Manual transmission — manual, smart-auto, and auto shift modes |
| **freecam** | Free camera control |

All script overlays automatically hide when not in-game (main menu, loading screens).

## Project Structure

```
snowrunner-asiloader/
├── include/
│   ├── sdk/               # Public script-facing SDK (script_api.h, base_script.h)
│   ├── core/              # Internal services (hooks, memory, config, logger, game state, …)
│   └── runtime/           # Script manager
├── src/
│   ├── host/              # ASI entry point (dllmain.cpp — D3D11 hook, frame loop)
│   ├── core/              # Service implementations
│   └── runtime/           # Script loading/ticking
├── scripts/
│   ├── speed/             # Speedometer script
│   ├── timelapse/         # Time control script
│   ├── smt/               # Manual transmission script
│   └── freecam/           # Free camera script
├── assets/                # Build output (SnowLoader.asi + SnowLoader/ runtime folder)
├── CMakeLists.txt
└── README.md
```

## Building

### Prerequisites

- Visual Studio 2022+ with C++ desktop workload
- CMake 3.20+

### Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

Output lands in `assets/` — `SnowLoader.asi` and `SnowLoader/` folder with script DLLs and ImGui.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) into the SnowRunner game directory
2. Copy `assets/SnowLoader.asi` next to `SnowRunner.exe`
3. Copy the `assets/SnowLoader/` folder next to `SnowLoader.asi`
4. Launch SnowRunner — scripts load automatically

Check `SnowLoader/SnowLoader.log` to verify initialization.

## Writing a Script

Create `scripts/<name>/script.cpp`. CMake auto-discovers it via the `add_script_library()` macro.

```cpp
#include "sdk/base_script.h"

namespace
{
    class MyScript final : public BaseScript
    {
    public:
        const char* GetName() const override { return "my_script"; }

        bool Initialize(const ScriptContext& context) override
        {
            if (!LoadKeybindingContext(context))
                return false;

            // Access services:
            // context.memory    — IGameMemoryService (pattern scan, read/write)
            // context.gameState — IGameStateService  (IsInGame, IsPaused, GetLevelName)
            // context.input     — IInputManager      (key polling)
            // context.configs   — IConfigService     (INI files)
            // context.overlays  — IOverlayService    (transient messages)

            return true;
        }

        void OnTick(float deltaTime) override
        {
            // Called every frame. Render ImGui, process input, etc.
        }

        void Shutdown() override {}
    };
}

SNOWLOADER_DEFINE_SCRIPT(MyScript)
```

### SDK Services

| Service | Interface | Key Methods |
|---------|-----------|-------------|
| Memory | `IGameMemoryService` | `PatternScan`, `ReadMemory`, `WriteMemory`, `ApplyPatternPatch`, `ResolveRelativeAddress`, `ResolvePointerChain`, `ReadValue<T>`, `WriteValue<T>` |
| Game State | `IGameStateService` | `GetState`, `IsInGame`, `IsInMenu`, `IsPaused`, `GetLevelName` |
| Input | `IInputManager` | `IsDown`, `WasPressed`, `WasReleased`, `IsChordDown`, `WasChordPressed` |
| Config | `IConfigService` | `OpenConfig` → `IConfig` (Get/Set Bool, Float, Int, String) |
| Keybindings | `IKeybindingManager` | `RegisterAction`, `TryGetBinding`, `RenderKeybindingOption` |
| Overlay | `IOverlayService` | `ShowTransientMessage` |

### BaseScript Helpers

`BaseScript` (in `base_script.h`) provides convenience wrappers:
- `LoadKeybindingContext(context)` — captures all services
- `OpenConfig(path, section)` — returns a scoped `ConfigHandle`
- `RegisterKeybindingActions(bindings)` / `WasKeybindingPressed(name)` / `IsKeybindingDown(name)`
- `ShowTransientMessage(overlays, message)`

## Runtime Model

- One native ASI host DLL (`SnowLoader.asi`)
- One shared static library (`snowloader_core`) with all services
- Many independent script DLLs discovered from `SnowLoader/scripts/` at runtime
- Scripts are hot-reloadable (F5 default)
- Script status overlay (F7 default) shows loaded/failed scripts

## Common Issues

| Problem | Solution |
|---------|----------|
| DLL not loading | Check `SnowLoader.log`; verify Ultimate ASI Loader is installed; must be x64 |
| Pattern scan fails | Game was updated — find new bytes with a pattern scanner and update the signature |
| Overlays not showing | Verify D3D11 hook initialized in log; check ImGui DLL is in `SnowLoader/` |

## Reference Projects

- [drafty46/SMT](https://github.com/drafty46/SMT) — Manual transmission mod
- [dazza-bo/SR-NoAutoReverse](https://github.com/dazza-bo/SR-NoAutoReverse)
- [equdevel/SnowRunner_time_control](https://github.com/equdevel/SnowRunner_time_control)
- [NakedDevA/SnowFlyer2](https://github.com/NakedDevA/SnowFlyer2)
- [crzknya/SpeedHud](https://github.com/crzknya/SpeedHud)

## License

This project is provided as-is for educational and modding purposes.