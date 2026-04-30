#include "core/config_manager.h"

#include <Windows.h>

#include <fstream>
#include <memory>

#include "core/logger.h"

ConfigManager::ConfigManager(std::string configPath, std::string section)
    : m_ConfigPath(std::move(configPath))
    , m_Section(std::move(section))
{
}

bool ConfigManager::Load()
{
    if (!EnsureFileExists())
    {
        Logger::Error("Failed to initialize configuration file: {}", m_ConfigPath);
        return false;
    }

    std::ifstream file(m_ConfigPath);
    if (!file.is_open())
    {
        Logger::Error("Failed to open configuration file: {}", m_ConfigPath);
        return false;
    }

    Logger::Info("Configuration loaded from {} [{}]", m_ConfigPath, m_Section);
    return true;
}

bool ConfigManager::Save()
{
    if (!EnsureFileExists())
    {
        Logger::Error("Failed to initialize configuration file for saving: {}", m_ConfigPath);
        return false;
    }

    if (WritePrivateProfileStringA(nullptr, nullptr, nullptr, m_ConfigPath.c_str()) == 0)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_SUCCESS)
        {
            Logger::Error("Failed to flush configuration file {}: {}", m_ConfigPath, error);
            return false;
        }
    }

    Logger::Info("Configuration saved to {} [{}]", m_ConfigPath, m_Section);
    return true;
}

bool ConfigManager::EnsureFileExists()
{
    std::ifstream file(m_ConfigPath);
    if (file.is_open())
    {
        return true;
    }

    std::ofstream create(m_ConfigPath, std::ios::app);
    const bool created = create.is_open();
    if (created)
    {
        Logger::Info("Created configuration file {}", m_ConfigPath);
    }
    return created;
}

bool ConfigManager::GetBool(const std::string& key, bool defaultValue) const
{
    const std::string value = ReadKey(key);
    if (value.empty())
    {
        return defaultValue;
    }

    return value == "1" || value == "true" || value == "yes" || value == "on";
}

float ConfigManager::GetFloat(const std::string& key, float defaultValue) const
{
    const std::string value = ReadKey(key);
    if (value.empty())
    {
        return defaultValue;
    }

    try
    {
        return std::stof(value);
    }
    catch (...)
    {
        return defaultValue;
    }
}

int ConfigManager::GetInt(const std::string& key, int defaultValue) const
{
    const std::string value = ReadKey(key);
    if (value.empty())
    {
        return defaultValue;
    }

    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return defaultValue;
    }
}

std::string ConfigManager::GetString(const std::string& key, const std::string& defaultValue) const
{
    const std::string value = ReadKey(key);
    return value.empty() ? defaultValue : value;
}

void ConfigManager::SetBool(const std::string& key, bool value)
{
    WriteKey(key, value ? "1" : "0");
}

void ConfigManager::SetFloat(const std::string& key, float value)
{
    WriteKey(key, std::to_string(value));
}

void ConfigManager::SetInt(const std::string& key, int value)
{
    WriteKey(key, std::to_string(value));
}

void ConfigManager::SetString(const std::string& key, const std::string& value)
{
    WriteKey(key, value);
}

std::string ConfigManager::ReadKey(const std::string& key) const
{
    // Use a reasonable buffer size for config values (4KB max)
    static constexpr size_t CONFIG_VALUE_MAX = 4096;
    std::unique_ptr<char[]> buffer(new char[CONFIG_VALUE_MAX]);
    
    GetPrivateProfileStringA(m_Section.c_str(), key.c_str(), "", buffer.get(), 
                            static_cast<DWORD>(CONFIG_VALUE_MAX), m_ConfigPath.c_str());
    
    return std::string(buffer.get());
}

void ConfigManager::WriteKey(const std::string& key, const std::string& value) const
{
    WritePrivateProfileStringA(m_Section.c_str(), key.c_str(), value.c_str(), m_ConfigPath.c_str());
}
