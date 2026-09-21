#include "MemoryUsage.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif
std::size_t memoryBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX c{}; c.cb=sizeof(c);
    if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c))) return c.PrivateUsage;
#endif
    return 0;
}
