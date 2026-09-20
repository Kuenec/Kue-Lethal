#ifndef KUE_PLATFORM_WINDOWS_SUPPORT_H
#define KUE_PLATFORM_WINDOWS_SUPPORT_H

#include "core/Utf8.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kue::platform {

inline int errnoFromWin32(DWORD code) noexcept {
    switch (code) {
    case ERROR_SUCCESS:
        return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_ENVVAR_NOT_FOUND:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EACCES;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        return EINVAL;
    case ERROR_INVALID_HANDLE:
        return EBADF;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BUFFER_OVERFLOW:
        return ENAMETOOLONG;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_DISK_FULL:
        return ENOSPC;
    default:
        return EIO;
    }
}

template <std::size_t Capacity> struct WideText {
    std::array<wchar_t, Capacity + 1> units{};
    std::size_t size = 0;
};

enum class WideConversionStatus : std::uint8_t { Converted, Empty, TooLong, InvalidEncoding };

template <std::size_t Capacity>
WideConversionStatus toWide(std::string_view utf8, WideText<Capacity>& wide) noexcept {
    static_assert(sizeof(wchar_t) == sizeof(std::uint16_t));
    wide.size = 0;
    wide.units[0] = L'\0';
    if (utf8.empty())
        return WideConversionStatus::Empty;
    if (utf8.size() > Capacity)
        return WideConversionStatus::TooLong;
    const Utf8ToUtf16Result result =
        convertUtf8ToUtf16({utf8.data(), utf8.size()},
                           {reinterpret_cast<std::uint16_t*>(wide.units.data()), Capacity});
    switch (result.status) {
    case Utf8ToUtf16Status::Success:
        wide.size = result.utf16CodeUnits;
        wide.units[wide.size] = L'\0';
        return WideConversionStatus::Converted;
    case Utf8ToUtf16Status::OutputCapacityExceeded:
        return WideConversionStatus::TooLong;
    case Utf8ToUtf16Status::EmptyInput:
        return WideConversionStatus::Empty;
    case Utf8ToUtf16Status::NullInput:
    case Utf8ToUtf16Status::EmbeddedNull:
    case Utf8ToUtf16Status::InvalidUtf8:
        return WideConversionStatus::InvalidEncoding;
    }
    return WideConversionStatus::InvalidEncoding;
}

template <std::size_t Capacity>
WideConversionStatus toWide(const char* utf8, WideText<Capacity>& wide) noexcept {
    if (!utf8)
        return WideConversionStatus::InvalidEncoding;
    return toWide(std::string_view(utf8, ::strnlen(utf8, Capacity + 1)), wide);
}

inline int errnoFromWideConversion(WideConversionStatus status) noexcept {
    switch (status) {
    case WideConversionStatus::Converted:
        return 0;
    case WideConversionStatus::Empty:
    case WideConversionStatus::InvalidEncoding:
        return EINVAL;
    case WideConversionStatus::TooLong:
        return ENAMETOOLONG;
    }
    return EINVAL;
}

}

#endif
