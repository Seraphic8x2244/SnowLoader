#pragma once

#include <string>

#include "sdk/script_api.h"

class ConfigService final : public IConfigService
{
public:
    explicit ConfigService(std::string baseDirectory = ".");

    void SetBaseDirectory(std::string baseDirectory);

    ConfigHandle OpenConfig(const char* relativePath,
        const char* section = "Settings") override;
    void ReleaseConfig(IConfig* config) override;

private:
    std::string ResolvePath(const char* relativePath) const;

    std::string m_BaseDirectory;
};