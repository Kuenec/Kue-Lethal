#include "mono/PeExport.h"

#include <iostream>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "FAIL: pass the Mono module fixture path\n";
        return 1;
    }
    const HMODULE loaded = ::LoadLibraryA(argv[1]);
    if (!loaded) {
        std::cerr << "FAIL: the Mono module fixture loads\n";
        return 1;
    }
    int failures = 0;
    void* const base = kue::mono::pe::moduleBase(kue::mono::pe::kMonoModuleName);
    if (base != static_cast<void*>(loaded)) {
        std::cerr << "FAIL: the Mono module is found by its dotted file name\n";
        ++failures;
    }
    if (!kue::mono::pe::getExport(base, "mono_get_root_domain")) {
        std::cerr << "FAIL: the Mono module exports are readable\n";
        ++failures;
    }
    if (failures == 0)
        std::cout << "2 assertions passed\n";
    return failures == 0 ? 0 : 1;
}
