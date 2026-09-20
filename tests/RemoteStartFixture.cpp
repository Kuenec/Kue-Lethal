#include "platform/Environment.h"

#include <cstdio>
#include <cstring>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

using kue::platform::EnvironmentStatus;
using kue::platform::EnvironmentStorage;

bool environmentIs(const char* name, const char* expected) {
    EnvironmentStorage storage;
    const kue::platform::EnvironmentValue value = kue::platform::readEnvironment(name, storage);
    if (!expected)
        return value.status == EnvironmentStatus::Unset;
    return value.status == EnvironmentStatus::Valid && value.text == expected;
}

bool readLine(char* buffer, std::size_t capacity) {
    if (!std::fgets(buffer, static_cast<int>(capacity), stdin))
        return false;
    const std::size_t length = std::strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n')
        buffer[length - 1] = '\0';
    return true;
}

}

int main(int argc, char** argv) {
    if (argc != 3)
        return 10;
    const std::string_view mode(argv[1]);
    const char* const moduleName = argv[2];
    const bool initiallySet = mode == "set";
    if (initiallySet) {
        if (kue::platform::writeEnvironment("KUE_CONFIG", "original-config") != 0 ||
            kue::platform::writeEnvironment("KUE_LOG", "original-log") != 0) {
            return 11;
        }
    } else if (mode != "unset" || kue::platform::writeEnvironment("KUE_CONFIG", nullptr) != 0 ||
               kue::platform::writeEnvironment("KUE_LOG", nullptr) != 0) {
        return 12;
    }
    std::puts("ready");
    std::fflush(stdout);
    char line[64] = {};
    if (!readLine(line, sizeof(line)) || std::string_view(line) != "release")
        return 14;
    if (initiallySet) {
        if (!environmentIs("KUE_CONFIG", "original-config") ||
            !environmentIs("KUE_LOG", "original-log")) {
            return 15;
        }
    } else if (!environmentIs("KUE_CONFIG", nullptr) || !environmentIs("KUE_LOG", nullptr)) {
        return 16;
    }
    if (::GetModuleHandleA(moduleName) != nullptr)
        return 17;
    return 0;
}
