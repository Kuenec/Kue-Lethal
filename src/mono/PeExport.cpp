#include "mono/PeExport.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include "platform/ProcessMemory.h"
#endif

namespace kue::mono::pe {
namespace {

const uint8_t* rvaToPtr(const uint8_t* base, uint32_t rva) {
    return base + rva;
}

}

#if defined(_WIN32)
void* moduleBase(const char* moduleName) {
    if (!moduleName)
        return nullptr;
    return static_cast<void*>(::GetModuleHandleA(moduleName));
}
#else
void* moduleBase(const char* moduleSubstring) {
    if (!moduleSubstring)
        return nullptr;
    platform::MemoryRegionScan scan;
    platform::MemoryRegion region;
    while (scan.next(region)) {
        if (region.readable && region.fileOffset == 0 &&
            region.path.find(moduleSubstring) != std::string_view::npos) {
            return std::bit_cast<void*>(region.begin);
        }
    }
    return nullptr;
}
#endif

void* getExport(void* basePtr, const char* name) {
    if (!basePtr || !name)
        return nullptr;
    const auto* base = static_cast<const uint8_t*>(basePtr);
    if (base[0] != 'M' || base[1] != 'Z')
        return nullptr;

    const uint32_t e_lfanew = *reinterpret_cast<const uint32_t*>(base + 0x3C);
    const uint8_t* pe = base + e_lfanew;
    if (pe[0] != 'P' || pe[1] != 'E')
        return nullptr;

    const uint8_t* opt = pe + 24;
    const uint16_t magic = *reinterpret_cast<const uint16_t*>(opt);
    if (magic != 0x20b)
        return nullptr;

    const uint32_t exportRva = *reinterpret_cast<const uint32_t*>(opt + 112);
    if (!exportRva)
        return nullptr;

    const uint8_t* exp = rvaToPtr(base, exportRva);
    const uint32_t numNames = *reinterpret_cast<const uint32_t*>(exp + 24);
    const uint32_t addrFuncs = *reinterpret_cast<const uint32_t*>(exp + 28);
    const uint32_t addrNames = *reinterpret_cast<const uint32_t*>(exp + 32);
    const uint32_t addrOrds = *reinterpret_cast<const uint32_t*>(exp + 36);

    const auto* names = reinterpret_cast<const uint32_t*>(rvaToPtr(base, addrNames));
    const auto* ords = reinterpret_cast<const uint16_t*>(rvaToPtr(base, addrOrds));
    const auto* funcs = reinterpret_cast<const uint32_t*>(rvaToPtr(base, addrFuncs));

    for (uint32_t i = 0; i < numNames; i++) {
        const char* exportName = reinterpret_cast<const char*>(rvaToPtr(base, names[i]));
        if (std::strcmp(exportName, name) != 0)
            continue;
        const uint16_t ord = ords[i];
        const uint32_t funcRva = funcs[ord];

        return const_cast<uint8_t*>(base + funcRva);
    }
    return nullptr;
}

}
