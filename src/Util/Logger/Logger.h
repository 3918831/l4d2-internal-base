#pragma once

#include <cstdarg>
#include <cstdio>
#include "../../SDK/L4D2/Interfaces/ICvar.h"
#include "../../SDK/L4D2/Includes/color.h"

// 日志前缀
#define LOG_PREFIX "[Portal of L4D2] "
#define LOG_DEBUG_PREFIX "[Portal of L4D2][DEBUG] "

// 颜色定义
#define COLOR_INFO    Color(255, 255, 255, 255)    // 白色 - 正常/调试信息
#define COLOR_WARNING Color(255, 200, 0, 255)      // 黄橙色 - 警告
#define COLOR_ERROR   Color(255, 50, 50, 255)      // 红色 - 错误

namespace U {

    // 正常信息 - 白色
    inline void LogInfo(const char* fmt, ...) {
        if (!I::Cvar) return;

        char buffer[2048];
        char finalBuffer[2048];

        va_list args;
        va_start(args, fmt);
        vsprintf_s(buffer, fmt, args);
        va_end(args);

        sprintf_s(finalBuffer, "%s%s", LOG_PREFIX, buffer);
        I::Cvar->ConsolePrintf("%s", finalBuffer);
    }

    // 调试信息 - 白色，带 [DEBUG] 前缀
    inline void LogDebug(const char* fmt, ...) {
        if (!I::Cvar) return;

        char buffer[2048];
        char finalBuffer[2048];

        va_list args;
        va_start(args, fmt);
        vsprintf_s(buffer, fmt, args);
        va_end(args);

        sprintf_s(finalBuffer, "%s%s", LOG_DEBUG_PREFIX, buffer);
        I::Cvar->ConsoleColorPrintf(COLOR_INFO, "%s", finalBuffer);
    }

    // 警告信息 - 黄橙色
    inline void LogWarning(const char* fmt, ...) {
        if (!I::Cvar) return;

        char buffer[2048];
        char finalBuffer[2048];

        va_list args;
        va_start(args, fmt);
        vsprintf_s(buffer, fmt, args);
        va_end(args);

        sprintf_s(finalBuffer, "%s%s", LOG_PREFIX, buffer);
        I::Cvar->ConsoleColorPrintf(COLOR_WARNING, "%s", finalBuffer);
    }

    // 错误信息 - 红色
    inline void LogError(const char* fmt, ...) {
        if (!I::Cvar) return;

        char buffer[2048];
        char finalBuffer[2048];

        va_list args;
        va_start(args, fmt);
        vsprintf_s(buffer, fmt, args);
        va_end(args);

        sprintf_s(finalBuffer, "%s%s", LOG_PREFIX, buffer);
        I::Cvar->ConsoleColorPrintf(COLOR_ERROR, "%s", finalBuffer);
    }

}