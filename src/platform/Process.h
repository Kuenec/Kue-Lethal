#ifndef KUE_PLATFORM_PROCESS_H
#define KUE_PLATFORM_PROCESS_H

#include <cstdint>
#include <span>
#include <string_view>

namespace kue::platform {

struct ModuleDirectory {
    bool available = false;
    std::string_view path;
};

[[nodiscard]] std::uint32_t currentProcessId() noexcept;
[[nodiscard]] ModuleDirectory currentModuleDirectory(std::span<char> storage) noexcept;

}

#endif
