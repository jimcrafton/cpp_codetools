#include "Logging.h"
#include "TextEncoding.h"

namespace CodeToolsVsix
{
    namespace
    {
        LogSinkCallback g_managedSink = nullptr;

        const char* SeverityLabel(cpptools::Severity severity)
        {
            switch (severity)
            {
            case cpptools::Severity::Note: return "NOTE";
            case cpptools::Severity::Warning: return "WARNING";
            case cpptools::Severity::Error: return "ERROR";
            case cpptools::Severity::Fatal: return "FATAL";
            default: return "UNKNOWN";
            }
        }
    }

    void SetManagedLogSink(LogSinkCallback sink)
    {
        g_managedSink = sink;
    }

    void Log(cpptools::Severity severity, const std::string& message)
    {
        std::string line = std::string("[codetools++] [") + SeverityLabel(severity) + "] " + message;

        OutputDebugStringA((line + "\r\n").c_str());

        if (g_managedSink)
        {
            std::wstring wide = Utf8ToWide(line);
            g_managedSink(severity, wide.c_str(), wide.size());
        }
    }
}
