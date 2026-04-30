#include "runtime/script_manager.h"

#include <filesystem>
#include <exception>
#include <sstream>
#include <system_error>

#include <imgui.h>

#include "core/logger.h"

namespace fs = std::filesystem;

namespace
{
    bool IsScriptDll(const fs::directory_entry& entry)
    {
        return entry.is_regular_file() && entry.path().extension() == ".dll";
    }

    std::string DescribeCurrentException()
    {
        try
        {
            throw;
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unknown C++ exception";
        }
    }

    template <typename Func>
    bool InvokeScriptCallback(Func&& func, std::string& error)
    {
        try
        {
            func();
            return true;
        }
        catch (...)
        {
            error = DescribeCurrentException();
            return false;
        }
    }

    const char* GetScriptNameOrFallback(const std::string& name, const std::string& sourcePath)
    {
        return name.empty() ? sourcePath.c_str() : name.c_str();
    }

    std::string FormatWindowsErrorMessage(DWORD error)
    {
        if (error == 0)
        {
            return "no error";
        }

        LPSTR buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageA(flags, nullptr, error, 0,
            reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

        std::ostringstream stream;
        stream << "Win32 error " << error;
        if (length != 0 && buffer)
        {
            std::string message(buffer, length);
            while (!message.empty() &&
                (message.back() == '\r' || message.back() == '\n' || message.back() == ' ' || message.back() == '\t'))
            {
                message.pop_back();
            }

            if (!message.empty())
            {
                stream << ": " << message;
            }
        }

        if (buffer)
        {
            LocalFree(buffer);
        }

        return stream.str();
    }

    std::string DescribeSehCode(unsigned int code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        default:
            break;
        }

        std::ostringstream stream;
        stream << "SEH 0x" << std::hex << std::uppercase << code;
        return stream.str();
    }

    bool InvokeCreateScriptSeh(CreateScriptFn create, IScript*& outScript, unsigned int& outSehCode)
    {
        outSehCode = 0;
#if defined(_MSC_VER)
        __try
        {
            outScript = create();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outSehCode = static_cast<unsigned int>(GetExceptionCode());
            return false;
        }
#else
        outScript = create();
        return true;
#endif
    }

    bool InvokeGetNameSeh(IScript* script, const char*& outName, unsigned int& outSehCode)
    {
        outSehCode = 0;
#if defined(_MSC_VER)
        __try
        {
            outName = script->GetName();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outSehCode = static_cast<unsigned int>(GetExceptionCode());
            return false;
        }
#else
        outName = script->GetName();
        return true;
#endif
    }

    bool InvokeInitializeSeh(IScript* script, const ScriptContext& context,
        bool& outInitialized, unsigned int& outSehCode)
    {
        outSehCode = 0;
#if defined(_MSC_VER)
        __try
        {
            outInitialized = script->Initialize(context);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outSehCode = static_cast<unsigned int>(GetExceptionCode());
            return false;
        }
#else
        outInitialized = script->Initialize(context);
        return true;
#endif
    }

    HMODULE LoadLibraryWithDiagnostics(const std::string& dllPath,
        DWORD& outLastError, unsigned int& outSehCode)
    {
        outLastError = 0;
        outSehCode = 0;

        SetLastError(ERROR_SUCCESS);
#if defined(_MSC_VER)
        __try
        {
            HMODULE module = LoadLibraryA(dllPath.c_str());
            outLastError = GetLastError();
            return module;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outSehCode = static_cast<unsigned int>(GetExceptionCode());
            outLastError = GetLastError();
            return nullptr;
        }
#else
        HMODULE module = LoadLibraryA(dllPath.c_str());
        outLastError = GetLastError();
        return module;
#endif
    }
}

ScriptManager::~ScriptManager()
{
    Shutdown();
}

bool ScriptManager::LoadScripts(const ScriptContext& context, const std::string& scriptsDirectory)
{
    Shutdown();

    if (context.keybindings)
    {
        context.keybindings->SaveKeybindings();
        context.keybindings->Clear();
    }

    m_Context = context;
    m_ScriptsDirectory = scriptsDirectory;
    m_ShadowDirectory = (fs::path(scriptsDirectory) / ".shadow").string();
    m_ShadowGeneration = 0;

    if (!EnsureScriptDirectoryReady())
    {
        return false;
    }

    const std::size_t loadedCount = LoadAllScripts();
    Logger::Info("Loaded {} script(s)", loadedCount);
    LogStatusSummary("Load");
    return loadedCount > 0;
}

bool ScriptManager::EnsureScriptDirectoryReady()
{
    if (!fs::exists(m_ScriptsDirectory) || !fs::is_directory(m_ScriptsDirectory))
    {
        Logger::Warning("Scripts directory not found: {}", m_ScriptsDirectory);
        return false;
    }

    std::error_code ec;
    fs::create_directories(m_ShadowDirectory, ec);
    if (ec)
    {
        Logger::Error("Failed to create shadow directory {}: {}", m_ShadowDirectory, ec.message());
        return false;
    }

    SetFileAttributesA(m_ShadowDirectory.c_str(), FILE_ATTRIBUTE_HIDDEN);
    CleanupShadowCopies();
    return true;
}

std::size_t ScriptManager::LoadAllScripts()
{
    std::size_t loadedCount = 0;
    for (const auto& entry : fs::directory_iterator(m_ScriptsDirectory))
    {
        if (!IsScriptDll(entry))
        {
            continue;
        }

        if (LoadScript(entry.path()))
        {
            ++loadedCount;
        }
    }

    return loadedCount;
}

bool ScriptManager::ReloadAllScripts()
{
    if (m_ScriptsDirectory.empty())
    {
        Logger::Warning("Reload requested before scripts were initialized");
        return false;
    }

    Logger::Info("Reloading all script DLLs");

    for (auto& script : m_Scripts)
    {
        UnloadScript(script);
    }

    m_Scripts.clear();

    if (m_Context.keybindings)
    {
        m_Context.keybindings->SaveKeybindings();
        m_Context.keybindings->Clear();
    }

    CleanupShadowCopies();

    const std::size_t loadedCount = LoadAllScripts();

    Logger::Info("Reloaded {} script(s)", loadedCount);
    LogStatusSummary("Reload");
    return loadedCount > 0;
}

void ScriptManager::TickScripts(float deltaTime)
{
    for (auto& script : m_Scripts)
    {
        if (!script.instance || script.state != LoadedScript::State::Loaded)
        {
            continue;
        }

        std::string error;
        if (!InvokeScriptCallback([&]() { script.instance->OnTick(deltaTime); }, error))
        {
            script.state = LoadedScript::State::RuntimeError;
            script.lastError = error;
            Logger::Error("Script entered error state during OnTick: {} ({})",
                GetScriptNameOrFallback(script.name, script.sourcePath), error);
        }
    }
}

const char* ScriptManager::GetStateLabel(LoadedScript::State state)
{
    switch (state)
    {
    case LoadedScript::State::Loaded:
        return "Loaded";
    case LoadedScript::State::LoadFailed:
        return "Load failed";
    case LoadedScript::State::RuntimeError:
        return "Error";
    default:
        return "Unknown";
    }
}

void ScriptManager::RenderStatusOverlay() const
{
    const auto getScriptStateColor = [](LoadedScript::State state)
    {
        switch (state)
        {
        case LoadedScript::State::Loaded:
            return ImVec4(0.55f, 0.85f, 0.60f, 1.0f);
        case LoadedScript::State::LoadFailed:
            return ImVec4(1.0f, 0.55f, 0.35f, 1.0f);
        case LoadedScript::State::RuntimeError:
            return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    };

    std::size_t unhealthyCount = 0;
    for (const auto& script : m_Scripts)
    {
        if (script.state != LoadedScript::State::Loaded)
        {
            ++unhealthyCount;
        }
    }

    if (unhealthyCount == 0)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    if (ImGui::Begin("##host_script_status", nullptr,
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoMove))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f),
            "Script Status (%zu issue%s)", unhealthyCount, unhealthyCount == 1 ? "" : "s");
        ImGui::Separator();

        for (const auto& script : m_Scripts)
        {
            if (script.state == LoadedScript::State::Loaded)
            {
                continue;
            }

            ImGui::TextColored(getScriptStateColor(script.state), "%s",
                GetStateLabel(script.state));
            ImGui::SameLine();
            ImGui::Text("%s", GetScriptNameOrFallback(script.name, script.sourcePath));

            const fs::path sourcePath(script.sourcePath);
            const std::string fileName = sourcePath.empty()
                ? std::string{}
                : sourcePath.filename().string();
            if (!fileName.empty())
            {
                ImGui::TextDisabled("DLL: %s", fileName.c_str());
            }
            if (!script.sourcePath.empty())
            {
                ImGui::TextDisabled("Source: %s", script.sourcePath.c_str());
            }

            if (!script.lastError.empty())
            {
                ImGui::TextWrapped("%s", script.lastError.c_str());
            }
        }
    }
    ImGui::End();
}

void ScriptManager::Shutdown()
{
    for (auto& script : m_Scripts)
    {
        UnloadScript(script);
    }

    m_Scripts.clear();
    CleanupShadowCopies();
}

bool ScriptManager::LoadScript(const fs::path& scriptPath)
{
    std::error_code ec;
    const fs::file_time_type lastWriteTime = fs::last_write_time(scriptPath, ec);
    if (ec)
    {
        const std::string error = "Failed to read script timestamp: " + ec.message();
        Logger::Error("{} {}", error, scriptPath.string());
        RecordLoadFailure(scriptPath, fs::file_time_type{}, error);
        return false;
    }

    return LoadScriptFromShadow(scriptPath, lastWriteTime);
}

bool ScriptManager::LoadScriptFromShadow(const fs::path& sourcePath,
    fs::file_time_type lastWriteTime)
{
    const fs::path shadowDirectory(m_ShadowDirectory);
    const fs::path shadowPath = shadowDirectory /
        (sourcePath.stem().string() + "_" + std::to_string(m_ShadowGeneration++) + sourcePath.extension().string());

    std::error_code ec;
    fs::copy_file(sourcePath, shadowPath, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        const std::string error = "Failed to create shadow copy: " + ec.message();
        Logger::Error("{} for {}", error, sourcePath.string());
        RecordLoadFailure(sourcePath, lastWriteTime, error);
        return false;
    }

    Logger::Info("Loading script DLL: {} (shadow: {})", sourcePath.string(), shadowPath.string());

    std::error_code infoEc;
    const auto shadowSize = fs::file_size(shadowPath, infoEc);
    if (!infoEc)
    {
        Logger::Info("Shadow copy created: {} bytes at {}", shadowSize, shadowPath.string());
    }
    else
    {
        Logger::Warning("Unable to inspect shadow copy {}: {}", shadowPath.string(), infoEc.message());
    }

    if (m_Context.hostDirectory && m_Context.hostDirectory[0] != '\0')
    {
        const fs::path runtimeImgui = fs::path(m_Context.hostDirectory) / "imgui.dll";
        Logger::Info("Runtime imgui path {} exists={} ", runtimeImgui.string(), fs::exists(runtimeImgui));
    }

    DWORD loadError = 0;
    unsigned int sehCode = 0;
    HMODULE module = LoadLibraryWithDiagnostics(shadowPath.string(), loadError, sehCode);
    if (!module)
    {
        std::string error = "Failed to load shadow script DLL";
        if (sehCode != 0)
        {
            error += " (" + DescribeSehCode(sehCode) + ")";
        }

        if (loadError != 0)
        {
            error += " [" + FormatWindowsErrorMessage(loadError) + "]";
        }

        Logger::Error("{}: {}", error, shadowPath.string());
        RecordLoadFailure(sourcePath, lastWriteTime, error);
        fs::remove(shadowPath, ec);
        return false;
    }

    Logger::Info("LoadLibrary succeeded for {} (module=0x{:X})", shadowPath.string(),
        reinterpret_cast<uintptr_t>(module));

    auto create = reinterpret_cast<CreateScriptFn>(GetProcAddress(module, "CreateScript"));
    auto destroy = reinterpret_cast<DestroyScriptFn>(GetProcAddress(module, "DestroyScript"));
    Logger::Info("Resolved exports for {}: CreateScript={}, DestroyScript={}", sourcePath.string(),
        create != nullptr, destroy != nullptr);
    if (!create || !destroy)
    {
        const std::string error = "Script exports missing";
        Logger::Error("{} in {}", error, sourcePath.string());
        RecordLoadFailure(sourcePath, lastWriteTime, error);
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }

    IScript* rawScript = nullptr;
    std::string createError;
    Logger::Info("Invoking CreateScript for {}", sourcePath.string());
    unsigned int createSehCode = 0;
    if (!InvokeCreateScriptSeh(create, rawScript, createSehCode))
    {
        createError = DescribeSehCode(createSehCode);
        Logger::Error("Script factory faulted for {}: {}", sourcePath.string(), createError);
        RecordLoadFailure(sourcePath, lastWriteTime, createError);
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }
    if (!createError.empty())
    {
        Logger::Error("Script factory threw for {}: {}", sourcePath.string(), createError);
        RecordLoadFailure(sourcePath, lastWriteTime, createError);
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }

    Logger::Info("CreateScript returned {} for {}", rawScript ? "non-null" : "null", sourcePath.string());

    if (!rawScript)
    {
        const std::string error = "Script factory returned null";
        Logger::Error("{} for {}", error, sourcePath.string());
        RecordLoadFailure(sourcePath, lastWriteTime, error);
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }

    std::unique_ptr<IScript, DestroyScriptFn> script(rawScript, destroy);
    std::string scriptName = sourcePath.stem().string();
    std::string getNameError;
    Logger::Info("Querying script name for {}", sourcePath.string());
    const char* candidate = nullptr;
    unsigned int getNameSehCode = 0;
    if (!InvokeGetNameSeh(script.get(), candidate, getNameSehCode))
    {
        getNameError = DescribeSehCode(getNameSehCode);
    }
    else if (candidate && candidate[0] != '\0')
    {
        scriptName = candidate;
    }
    if (!getNameError.empty())
    {
        Logger::Warning("GetName failed for {}: {}", sourcePath.string(), getNameError);
    }

    bool initialized = false;
    std::string initializeError;
    Logger::Info("Initializing script instance for {}", scriptName);
    unsigned int initializeSehCode = 0;
    bool initializeCallOk = InvokeInitializeSeh(script.get(), m_Context, initialized, initializeSehCode);
    if (!initializeCallOk)
    {
        initializeError = DescribeSehCode(initializeSehCode);
    }

    if (!initializeCallOk)
    {
        Logger::Error("Script initialization threw for {}: {}", scriptName, initializeError);
        RecordLoadFailure(sourcePath, lastWriteTime, initializeError, scriptName);
        script.reset();
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }

    if (!initialized)
    {
        const std::string error = "Script initialization returned false";
        Logger::Error("{}: {}", error, scriptName);
        RecordLoadFailure(sourcePath, lastWriteTime, error, scriptName);
        script.reset();
        FreeLibrary(module);
        fs::remove(shadowPath, ec);
        return false;
    }

    Logger::Info("Script initialized: {}", scriptName);

    LoadedScript loaded;
    loaded.lastWriteTime = lastWriteTime;
    loaded.module = module;
    loaded.destroy = destroy;
    loaded.instance = std::move(script);
    loaded.name = scriptName;
    loaded.sourcePath = sourcePath.string();
    loaded.shadowPath = shadowPath.string();
    loaded.state = LoadedScript::State::Loaded;
    m_Scripts.push_back(std::move(loaded));
    return true;
}

void ScriptManager::RecordLoadFailure(const fs::path& sourcePath,
    fs::file_time_type lastWriteTime, const std::string& error, const std::string& name)
{
    LoadedScript failed;
    failed.lastWriteTime = lastWriteTime;
    failed.name = name.empty() ? sourcePath.stem().string() : name;
    failed.sourcePath = sourcePath.string();
    failed.state = LoadedScript::State::LoadFailed;
    failed.lastError = error;
    m_Scripts.push_back(std::move(failed));
}

void ScriptManager::LogStatusSummary(const char* actionLabel) const
{
    std::size_t loadedCount = 0;
    std::size_t issueCount = 0;
    for (const auto& script : m_Scripts)
    {
        if (script.state == LoadedScript::State::Loaded)
        {
            ++loadedCount;
        }
        else
        {
            ++issueCount;
        }
    }

    Logger::Info("{} summary: {} loaded, {} issue(s)", actionLabel, loadedCount, issueCount);
    for (const auto& script : m_Scripts)
    {
        if (script.state == LoadedScript::State::Loaded)
        {
            continue;
        }

        Logger::Warning("{}: {} - {}", GetStateLabel(script.state),
            GetScriptNameOrFallback(script.name, script.sourcePath), script.lastError);
    }
}

void ScriptManager::UnloadScript(LoadedScript& script)
{
    if (script.instance)
    {
        Logger::Info("Shutting down script: {}",
            GetScriptNameOrFallback(script.name, script.sourcePath));

        std::string error;
        if (!InvokeScriptCallback([&]() { script.instance->Shutdown(); }, error))
        {
            script.state = LoadedScript::State::RuntimeError;
            script.lastError = error;
            Logger::Error("Script shutdown threw for {}: {}",
                GetScriptNameOrFallback(script.name, script.sourcePath), error);
        }

        script.instance.reset();
    }

    if (script.module)
    {
        FreeLibrary(script.module);
        script.module = nullptr;
    }

    if (!script.shadowPath.empty())
    {
        std::error_code ec;
        fs::remove(script.shadowPath, ec);
    }

    script.destroy = nullptr;
    script.sourcePath.clear();
    script.shadowPath.clear();
}

void ScriptManager::CleanupShadowCopies() const
{
    if (m_ShadowDirectory.empty())
    {
        return;
    }

    std::error_code ec;
    if (!fs::exists(m_ShadowDirectory, ec))
    {
        return;
    }

    for (const auto& entry : fs::directory_iterator(m_ShadowDirectory, ec))
    {
        if (ec)
        {
            return;
        }

        fs::remove_all(entry.path(), ec);
        if (ec)
        {
            Logger::Warning("Failed to remove stale shadow copy {}: {}",
                entry.path().string(), ec.message());
            ec.clear();
        }
    }
}
