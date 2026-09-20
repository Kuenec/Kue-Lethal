#include "platform/Process.h"

#include "platform/FileSystem.h"
#include "platform/WindowsSupport.h"

namespace kue::platform {

std::uint32_t currentProcessId() noexcept {
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
}

ModuleDirectory currentModuleDirectory(std::span<char> storage) noexcept {
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&currentModuleDirectory), &module)) {
        return {false, {}};
    }
    WideText<kMaximumPathBytes> wide;
    const DWORD length =
        ::GetModuleFileNameW(module, wide.units.data(), static_cast<DWORD>(wide.units.size()));
    if (length == 0 || length >= wide.units.size())
        return {false, {}};
    std::array<char, kMaximumPathBytes + 1> utf8{};
    const Utf16ToUtf8Result conversion =
        convertUtf16ToUtf8({reinterpret_cast<const std::uint16_t*>(wide.units.data()), length},
                           {utf8.data(), kMaximumPathBytes});
    if (conversion.status != Utf16ToUtf8Status::Success)
        return {false, {}};
    const std::string_view modulePath(utf8.data(), conversion.utf8Bytes);
    const std::size_t separator = modulePath.find_last_of("\\/");
    const std::string_view directory =
        separator == std::string_view::npos ? std::string_view{} : modulePath.substr(0, separator);
    if (storage.empty() || directory.size() >= storage.size())
        return {false, {}};
    std::memcpy(storage.data(), directory.data(), directory.size());
    storage[directory.size()] = '\0';
    return {true, {storage.data(), directory.size()}};
}

}
