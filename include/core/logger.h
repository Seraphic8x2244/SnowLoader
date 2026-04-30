#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

class Logger
{
public:
    static void Initialize(const std::string& logPath)
    {
        Logger& instance = GetInstance();
        instance.m_LogPath = logPath;
        if (!logPath.empty())
        {
            instance.m_LogFile.open(logPath, std::ios::app);
            if (!instance.m_LogFile.is_open())
            {
                std::cerr << "Warning: Failed to open log file: " << logPath << std::endl;
            }
        }
        Info("Logger initialized");
    }

    static void Shutdown()
    {
        Logger& instance = GetInstance();
        if (instance.m_LogFile.is_open())
        {
            instance.m_LogFile.close();
        }
    }

    template<typename... Args>
    static void Info(const std::string& format, Args... args)
    {
        GetInstance().Log("[INFO] ", format, args...);
    }

    template<typename... Args>
    static void Warning(const std::string& format, Args... args)
    {
        GetInstance().Log("[WARN] ", format, args...);
    }

    template<typename... Args>
    static void Error(const std::string& format, Args... args)
    {
        GetInstance().Log("[ERROR] ", format, args...);
    }

    template<typename... Args>
    static void Debug(const std::string& format, Args... args)
    {
        GetInstance().Log("[DEBUG] ", format, args...);
    }

private:
    Logger() = default;

    static Logger& GetInstance()
    {
        static Logger instance;
        return instance;
    }

    template<typename... Args>
    void Log(const std::string& prefix, const std::string& format, Args... args)
    {
        std::ostringstream oss;
        oss << prefix;

        int argIndex = 0;
        for (size_t i = 0; i < format.length(); i++)
        {
            if (format[i] == '{' && i + 1 < format.length() && format[i + 1] == '}')
            {
                if (argIndex < sizeof...(args))
                {
                    FormatArg(oss, argIndex, args...);
                }
                i++;
                argIndex++;
            }
            else if (format[i] == '{' && i + 2 < format.length() && format[i + 1] == ':' && format[i + 2] == 'X')
            {
                if (i + 3 < format.length() && format[i + 3] == '}')
                {
                    if (argIndex < sizeof...(args))
                    {
                        oss << std::hex;
                        FormatArg(oss, argIndex, args...);
                        oss << std::dec;
                    }
                    i += 3;
                    argIndex++;
                }
                else
                {
                    oss << format[i];
                }
            }
            else
            {
                oss << format[i];
            }
        }

        WriteMessage(oss.str());
    }

    void Log(const std::string& prefix, const std::string& format)
    {
        WriteMessage(prefix + format);
    }

    template<typename T, typename... Args>
    void FormatArg(std::ostringstream& oss, int index, T arg, Args... args)
    {
        if (index == 0)
        {
            oss << arg;
        }
        else
        {
            FormatArg(oss, index - 1, args...);
        }
    }

    template<typename T>
    void FormatArg(std::ostringstream& oss, int index, T arg)
    {
        if (index == 0)
        {
            oss << arg;
        }
    }

    void WriteMessage(const std::string& message)
    {
        std::cout << message << std::endl;

        if (m_LogFile.is_open())
        {
            m_LogFile << message << std::endl;
            m_LogFile.flush();
        }
    }

    std::string m_LogPath;
    std::ofstream m_LogFile;
};
