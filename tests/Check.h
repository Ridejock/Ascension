#pragma once

#include <cstdio>
#include <string>

#include "Emerald/Core/Defines.h"

// A minimal test helper: prints every check and counts the failures (main returns non-zero if
// there were any, which is what ctest looks at).
inline i32 g_Failures = 0;

inline void Check(bool condition, const char* what)
{
    std::printf("[%s] %s\n", condition ? " OK " : "FAIL", what);
    if (!condition)
        ++g_Failures;
}

inline void CheckHas(const std::string& message, const char* part, const char* what)
{
    const bool ok = message.find(part) != std::string::npos;
    Check(ok, what);
    if (!ok)
        std::printf("    message was: %s\n", message.c_str());
}
