#pragma once

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "sdk/script_api.h"

class ScriptManager
{
public:
    ScriptManager() = default;
    ~ScriptManager();

    bool LoadScripts(const ScriptContext& context, const std::string& scriptsDirectory);
    bool ReloadAllScripts();
    void TickScripts(float deltaTime);
    void RenderStatusOverlay() const;
    void Shutdown();

private:
    struct LoadedScript
    {
        enum class State
        {
            Loaded,
            LoadFailed,
            RuntimeError,
        };

        std::filesystem::file_time_type lastWriteTime{};
        HMODULE module = nullptr;
        DestroyScriptFn destroy = nullptr;
        std::unique_ptr<IScript, DestroyScriptFn> instance{ nullptr, nullptr };
        std::string name;
        std::string sourcePath;
        std::string shadowPath;
        State state = State::Loaded;
        std::string lastError;
    };

    bool EnsureScriptDirectoryReady();
    std::size_t LoadAllScripts();
    static const char* GetStateLabel(LoadedScript::State state);

    bool LoadScript(const std::filesystem::path& scriptPath);
    bool LoadScriptFromShadow(const std::filesystem::path& sourcePath,
        std::filesystem::file_time_type lastWriteTime);
    void RecordLoadFailure(const std::filesystem::path& sourcePath,
        std::filesystem::file_time_type lastWriteTime, const std::string& error,
        const std::string& name = {});
    void LogStatusSummary(const char* actionLabel) const;
    void UnloadScript(LoadedScript& script);
    void CleanupShadowCopies() const;

    ScriptContext m_Context{};
    std::string m_ScriptsDirectory;
    std::string m_ShadowDirectory;
    unsigned long long m_ShadowGeneration = 0;
    std::vector<LoadedScript> m_Scripts;
};
