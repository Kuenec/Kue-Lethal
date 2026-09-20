#define _CRT_RAND_S
#include "platform/FileSystem.h"

#include "platform/WindowsSupport.h"

#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

namespace kue::platform {

namespace {

using WidePath = WideText<kMaximumTemporaryPathBytes>;

constexpr int kNoInherit = _O_NOINHERIT | _O_BINARY;
constexpr int kOwnerReadWrite = _S_IREAD | _S_IWRITE;
constexpr int kTemporaryNameAttempts = 64;
constexpr std::string_view kTemporaryNameAlphabet =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

int currentErrorOr(int fallback) noexcept {
    return errno != 0 ? errno : fallback;
}

DescriptorResult openWide(const char* utf8Path, int flags, int permissions) noexcept {
    WidePath wide;
    const WideConversionStatus conversion = toWide(utf8Path, wide);
    if (conversion != WideConversionStatus::Converted)
        return {-1, errnoFromWideConversion(conversion)};
    errno = 0;
    const int descriptor = ::_wopen(wide.units.data(), flags, permissions);
    if (descriptor < 0)
        return {-1, currentErrorOr(EIO)};
    return {descriptor, 0};
}

bool randomTemporarySuffix(char* suffix) noexcept {
    for (std::size_t index = 0; index < kTemporaryNameSuffixBytes; ++index) {
        unsigned int value = 0;
        if (::rand_s(&value) != 0)
            return false;
        suffix[index] = kTemporaryNameAlphabet[value % kTemporaryNameAlphabet.size()];
    }
    return true;
}

}

DescriptorResult openForAppend(const char* utf8Path) noexcept {
    return openWide(utf8Path, _O_WRONLY | _O_CREAT | _O_APPEND | kNoInherit, kOwnerReadWrite);
}

DescriptorResult openForRead(const char* utf8Path) noexcept {
    return openWide(utf8Path, _O_RDONLY | kNoInherit, 0);
}

DescriptorResult createExclusiveTemporary(char* utf8PathTemplate) noexcept {
    const std::size_t length = ::strnlen(utf8PathTemplate, kMaximumTemporaryPathBytes + 1);
    if (length > kMaximumTemporaryPathBytes || length < kTemporaryNameSuffixBytes)
        return {-1, EINVAL};
    char* const suffix = utf8PathTemplate + length - kTemporaryNameSuffixBytes;
    if (std::string_view(suffix, kTemporaryNameSuffixBytes) != "XXXXXX")
        return {-1, EINVAL};
    for (int attempt = 0; attempt < kTemporaryNameAttempts; ++attempt) {
        if (!randomTemporarySuffix(suffix))
            return {-1, EIO};
        const DescriptorResult result =
            openWide(utf8PathTemplate, _O_RDWR | _O_CREAT | _O_EXCL | kNoInherit, kOwnerReadWrite);
        if (result.descriptor >= 0 || result.errorCode != EEXIST)
            return result;
    }
    return {-1, EEXIST};
}

FileInspection inspectFile(int descriptor) noexcept {
    struct _stat64 metadata{};
    errno = 0;
    if (::_fstat64(descriptor, &metadata) != 0)
        return {false, currentErrorOr(EIO), FileKind::Other, 0};
    const FileKind kind =
        (metadata.st_mode & _S_IFMT) == _S_IFREG ? FileKind::Regular : FileKind::Other;
    const std::uint64_t bytes =
        metadata.st_size > 0 ? static_cast<std::uint64_t>(metadata.st_size) : 0;
    return {true, 0, kind, bytes};
}

InheritanceInspection inspectInheritance(int descriptor) noexcept {
    errno = 0;
    const intptr_t rawHandle = ::_get_osfhandle(descriptor);
    if (rawHandle == -1)
        return {false, currentErrorOr(EBADF), true};
    DWORD flags = 0;
    if (!::GetHandleInformation(reinterpret_cast<HANDLE>(rawHandle), &flags))
        return {false, errnoFromWin32(::GetLastError()), true};
    return {true, 0, (flags & HANDLE_FLAG_INHERIT) != 0};
}

ReadResult readSome(int descriptor, std::span<char> destination) noexcept {
    const unsigned int requested = destination.size() > static_cast<std::size_t>(INT_MAX)
                                       ? static_cast<unsigned int>(INT_MAX)
                                       : static_cast<unsigned int>(destination.size());
    errno = 0;
    const int result = ::_read(descriptor, destination.data(), requested);
    if (result < 0)
        return {0, currentErrorOr(EIO)};
    return {static_cast<std::size_t>(result), 0};
}

int closeDescriptor(int descriptor) noexcept {
    errno = 0;
    if (::_close(descriptor) == 0)
        return 0;
    return currentErrorOr(EIO);
}

StreamResult associateStream(int descriptor, const char* mode) noexcept {
    errno = 0;
    std::FILE* const stream = ::_fdopen(descriptor, mode);
    if (!stream)
        return {nullptr, currentErrorOr(EIO)};
    return {stream, 0};
}

int synchronizeDescriptor(int descriptor) noexcept {
    errno = 0;
    if (::_commit(descriptor) == 0)
        return 0;
    return currentErrorOr(EIO);
}

int removeFile(const char* utf8Path) noexcept {
    WidePath wide;
    const WideConversionStatus conversion = toWide(utf8Path, wide);
    if (conversion != WideConversionStatus::Converted)
        return errnoFromWideConversion(conversion);
    errno = 0;
    if (::_wunlink(wide.units.data()) == 0)
        return 0;
    return currentErrorOr(EIO);
}

ReplacementResult replaceFile(const char* utf8TemporaryPath,
                              const char* utf8DestinationPath) noexcept {
    WidePath temporary;
    WidePath destination;
    const WideConversionStatus temporaryConversion = toWide(utf8TemporaryPath, temporary);
    if (temporaryConversion != WideConversionStatus::Converted) {
        return {ReplacementStatus::NotCommitted,
                {ReplacementOperation::Rename, errnoFromWideConversion(temporaryConversion)},
                {}};
    }
    const WideConversionStatus destinationConversion = toWide(utf8DestinationPath, destination);
    if (destinationConversion != WideConversionStatus::Converted) {
        return {ReplacementStatus::NotCommitted,
                {ReplacementOperation::Rename, errnoFromWideConversion(destinationConversion)},
                {}};
    }
    if (!::MoveFileExW(temporary.units.data(), destination.units.data(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return {ReplacementStatus::NotCommitted,
                {ReplacementOperation::Rename, errnoFromWin32(::GetLastError())},
                {}};
    }
    return {ReplacementStatus::Durable, {}, {}};
}

ConsoleInspection inspectConsole(std::FILE* stream) noexcept {
    errno = 0;
    const int descriptor = ::_fileno(stream);
    if (descriptor < 0)
        return {false, currentErrorOr(EBADF), false};
    if (::_isatty(descriptor) == 0)
        return {true, 0, false};
    const intptr_t rawHandle = ::_get_osfhandle(descriptor);
    if (rawHandle == -1)
        return {true, 0, false};
    DWORD mode = 0;
    if (!::GetConsoleMode(reinterpret_cast<HANDLE>(rawHandle), &mode))
        return {true, 0, false};
    return {true, 0, (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0};
}

}
