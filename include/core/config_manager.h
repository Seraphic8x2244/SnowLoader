#pragma once

#include <string>

#include "sdk/script_api.h"

class ConfigManager final : public IConfig
{
public:
    explicit ConfigManager(std::string configPath, std::string section = "Settings");

    bool Load() override;
    bool Save() override;

    bool GetBool(const std::string& key, bool defaultValue = false) const override;
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const override;
    int GetInt(const std::string& key, int defaultValue = 0) const override;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const override;

    void SetBool(const std::string& key, bool value) override;
    void SetFloat(const std::string& key, float value) override;
    void SetInt(const std::string& key, int value) override;
    void SetString(const std::string& key, const std::string& value) override;

    const std::string& GetPath() const { return m_ConfigPath; }
    const std::string& GetSection() const { return m_Section; }

private:
    bool EnsureFileExists();
    std::string ReadKey(const std::string& key) const;
    void WriteKey(const std::string& key, const std::string& value) const;

    std::string m_ConfigPath;
    std::string m_Section;
};
