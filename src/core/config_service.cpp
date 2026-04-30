#include "core/config_service.h"

#include <filesystem>

#include "core/config_manager.h"

namespace fs = std::filesystem;

ConfigService::ConfigService(std::string baseDirectory)
    : m_BaseDirectory(std::move(baseDirectory))
{
}

void ConfigService::SetBaseDirectory(std::string baseDirectory)
{
    m_BaseDirectory = std::move(baseDirectory);
}

ConfigHandle ConfigService::OpenConfig(const char* relativePath, const char* section)
{
    const std::string resolvedPath = ResolvePath(relativePath);
    const std::string resolvedSection = (section != nullptr && section[0] != '\0')
        ? section
        : "Settings";

    ConfigManager* config = new ConfigManager(resolvedPath, resolvedSection);
    config->Load();
    return ConfigHandle(this, config);
}

void ConfigService::ReleaseConfig(IConfig* config)
{
    delete config;
}

std::string ConfigService::ResolvePath(const char* relativePath) const
{
    if (relativePath == nullptr || relativePath[0] == '\0')
    {
        return (fs::path(m_BaseDirectory) / "default.ini").string();
    }

    fs::path path(relativePath);
    if (path.is_absolute())
    {
        return path.string();
    }

    return (fs::path(m_BaseDirectory) / path).string();
}