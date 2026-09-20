#include "platform/Process.h"

#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

namespace kue::platform {

std::uint32_t currentProcessId() noexcept {
    return static_cast<std::uint32_t>(::getpid());
}

ModuleDirectory currentModuleDirectory(std::span<char> storage) noexcept {
    Dl_info info{};
    if (::dladdr(reinterpret_cast<void*>(&currentModuleDirectory), &info) == 0 || !info.dli_fname)
        return {false, {}};
    const std::string_view modulePath(info.dli_fname);
    const std::size_t slash = modulePath.find_last_of('/');
    const std::string_view directory =
        slash == std::string_view::npos ? std::string_view{} : modulePath.substr(0, slash);
    if (storage.empty() || directory.size() >= storage.size())
        return {false, {}};
    std::memcpy(storage.data(), directory.data(), directory.size());
    storage[directory.size()] = '\0';
    return {true, {storage.data(), directory.size()}};
}

}
