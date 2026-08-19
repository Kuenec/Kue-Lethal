#include "mono/PeExport.h"

#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

namespace kue::mono::pe {
namespace {

bool readMapsBase(const char* needle, uintptr_t& out) {
    std::ifstream maps("/proc/self/maps");
    if (!maps)
        return false;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(needle) == std::string::npos)
            continue;

        const std::string_view row(line);
        std::size_t cursor = 0;
        auto nextField = [&]() {
            cursor = row.find_first_not_of(' ', cursor);
            if (cursor == std::string_view::npos)
                return std::string_view{};
            const std::size_t end = row.find(' ', cursor);
            const std::string_view field = row.substr(cursor, end - cursor);
            cursor = end;
            return field;
        };
        const std::string_view range = nextField();
        const std::string_view permissions = nextField();
        const std::string_view offsetText = nextField();
        const std::size_t dash = range.find('-');
        if (dash == std::string_view::npos || permissions.find('r') == std::string_view::npos)
            continue;
        uintptr_t start = 0;
        uintptr_t offset = 0;
        const auto startResult = std::from_chars(range.begin(), range.begin() + dash, start, 16);
        const auto offsetResult =
            std::from_chars(offsetText.data(), offsetText.data() + offsetText.size(), offset, 16);
        if (startResult.ec == std::errc{} && offsetResult.ec == std::errc{} && offset == 0) {
            out = start;
            return true;
        }
    }
    return false;
}

const uint8_t* rvaToPtr(const uint8_t* base, uint32_t rva) {
    return base + rva;
}

}

void* moduleBase(const char* moduleSubstring) {
    uintptr_t base = 0;
    if (!readMapsBase(moduleSubstring, base))
        return nullptr;
    return std::bit_cast<void*>(base);
}

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
