#pragma once

#include <cstdio>
#include <cstring>

namespace U::PortalFileLog
{
    inline constexpr const char* kPath = "D:\\portal_l4d2_trace.log";
    inline bool g_Initialized = false;

    inline bool ShouldCapture(const char* text)
    {
        if (!text)
            return false;

        return std::strstr(text, "PortalStage1Probe")
            || std::strstr(text, "PortalBridge")
            || std::strstr(text, "PortalSim")
            || std::strstr(text, "PortalTransition");
    }

    inline void EnsureInitialized()
    {
        if (g_Initialized)
            return;

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "wb") == 0 && file)
        {
            std::fprintf(file, "[PortalFileLog] reset path=%s\n", kPath);
            std::fclose(file);
            g_Initialized = true;
        }
    }

    inline void AppendMarker(const char* marker)
    {
        EnsureInitialized();

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "ab") != 0 || !file)
            return;

        std::fprintf(file, "[PortalFileLog] %s path=%s\n", marker ? marker : "marker", kPath);
        std::fclose(file);
    }

    inline void Reset()
    {
        if (!g_Initialized)
        {
            EnsureInitialized();
            return;
        }

        AppendMarker("portal init marker");
    }

    inline void Write(const char* text)
    {
        if (!ShouldCapture(text))
            return;

        EnsureInitialized();

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "ab") != 0 || !file)
            return;

        std::fputs(text, file);
        const size_t len = std::strlen(text);
        if (len == 0 || text[len - 1] != '\n')
            std::fputc('\n', file);

        std::fclose(file);
    }
}
