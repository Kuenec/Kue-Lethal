#include "platform/FileSystem.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace kue::platform {

namespace {

int currentErrorOr(int fallback) noexcept {
    return errno != 0 ? errno : fallback;
}

struct ParentDirectory {
    std::array<char, kMaximumPathBytes + 1> text{};
    std::size_t size = 0;
};

bool parentDirectoryOf(const char* path, ParentDirectory& parent) noexcept {
    const std::string_view view(path, ::strnlen(path, kMaximumPathBytes + 1));
    if (view.size() > kMaximumPathBytes)
        return false;
    const std::size_t slash = view.find_last_of('/');
    std::string_view directory = ".";
    if (slash == 0)
        directory = "/";
    else if (slash != std::string_view::npos)
        directory = view.substr(0, slash);
    std::memcpy(parent.text.data(), directory.data(), directory.size());
    parent.text[directory.size()] = '\0';
    parent.size = directory.size();
    return true;
}

}

DescriptorResult openForAppend(const char* utf8Path) noexcept {
    errno = 0;
    const int descriptor =
        ::open(utf8Path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NONBLOCK, 0666);
    if (descriptor < 0)
        return {-1, currentErrorOr(EIO)};
    return {descriptor, 0};
}

DescriptorResult openForRead(const char* utf8Path) noexcept {
    errno = 0;
    const int descriptor = ::open(utf8Path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0)
        return {-1, currentErrorOr(EIO)};
    return {descriptor, 0};
}

DescriptorResult createExclusiveTemporary(char* utf8PathTemplate) noexcept {
    errno = 0;
    const int descriptor = ::mkostemp(utf8PathTemplate, O_CLOEXEC);
    if (descriptor < 0)
        return {-1, currentErrorOr(EIO)};
    return {descriptor, 0};
}

FileInspection inspectFile(int descriptor) noexcept {
    struct stat metadata{};
    errno = 0;
    if (::fstat(descriptor, &metadata) != 0)
        return {false, currentErrorOr(EIO), FileKind::Other, 0};
    const FileKind kind = S_ISREG(metadata.st_mode) ? FileKind::Regular : FileKind::Other;
    const std::uint64_t bytes =
        metadata.st_size > 0 ? static_cast<std::uint64_t>(metadata.st_size) : 0;
    return {true, 0, kind, bytes};
}

InheritanceInspection inspectInheritance(int descriptor) noexcept {
    int flags = -1;
    do {
        errno = 0;
        flags = ::fcntl(descriptor, F_GETFD);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0)
        return {false, currentErrorOr(EIO), true};
    return {true, 0, (flags & FD_CLOEXEC) == 0};
}

ReadResult readSome(int descriptor, std::span<char> destination) noexcept {
    while (true) {
        errno = 0;
        const ssize_t result = ::read(descriptor, destination.data(), destination.size());
        if (result >= 0)
            return {static_cast<std::size_t>(result), 0};
        if (errno == EINTR)
            continue;
        return {0, currentErrorOr(EIO)};
    }
}

int closeDescriptor(int descriptor) noexcept {
    errno = 0;
    if (::close(descriptor) == 0)
        return 0;
    return currentErrorOr(EIO);
}

StreamResult associateStream(int descriptor, const char* mode) noexcept {
    errno = 0;
    std::FILE* const stream = ::fdopen(descriptor, mode);
    if (!stream)
        return {nullptr, currentErrorOr(EIO)};
    return {stream, 0};
}

int synchronizeDescriptor(int descriptor) noexcept {
    errno = 0;
    if (::fsync(descriptor) == 0)
        return 0;
    return currentErrorOr(EIO);
}

int removeFile(const char* utf8Path) noexcept {
    errno = 0;
    if (::unlink(utf8Path) == 0)
        return 0;
    return currentErrorOr(EIO);
}

ReplacementResult replaceFile(const char* utf8TemporaryPath,
                              const char* utf8DestinationPath) noexcept {
    errno = 0;
    if (::rename(utf8TemporaryPath, utf8DestinationPath) != 0) {
        return {ReplacementStatus::NotCommitted,
                {ReplacementOperation::Rename, currentErrorOr(EIO)},
                {}};
    }
    ParentDirectory parent;
    if (!parentDirectoryOf(utf8DestinationPath, parent)) {
        return {ReplacementStatus::CommittedDurabilityUnconfirmed,
                {ReplacementOperation::OpenDirectory, ENAMETOOLONG},
                {}};
    }
    errno = 0;
    const int directory = ::open(parent.text.data(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (directory < 0) {
        return {ReplacementStatus::CommittedDurabilityUnconfirmed,
                {ReplacementOperation::OpenDirectory, currentErrorOr(EIO)},
                {}};
    }
    errno = 0;
    const int synchronizeResult = ::fsync(directory);
    const int synchronizeCode = synchronizeResult != 0 ? currentErrorOr(EIO) : 0;
    errno = 0;
    const int closeResult = ::close(directory);
    const int closeCode = closeResult != 0 ? currentErrorOr(EIO) : 0;
    if (synchronizeResult != 0) {
        ReplacementResult result{ReplacementStatus::CommittedDurabilityUnconfirmed,
                                 {ReplacementOperation::SynchronizeDirectory, synchronizeCode},
                                 {}};
        if (closeResult != 0)
            result.secondary = {ReplacementOperation::CloseDirectory, closeCode};
        return result;
    }
    if (closeResult != 0) {
        return {ReplacementStatus::CommittedCleanupFailed,
                {ReplacementOperation::CloseDirectory, closeCode},
                {}};
    }
    return {ReplacementStatus::Durable, {}, {}};
}

ConsoleInspection inspectConsole(std::FILE* stream) noexcept {
    errno = 0;
    const int descriptor = ::fileno(stream);
    if (descriptor < 0)
        return {false, currentErrorOr(EBADF), false};
    errno = 0;
    if (::isatty(descriptor) != 0)
        return {true, 0, true};
    if (errno == ENOTTY || errno == EINVAL)
        return {true, 0, false};
    return {false, currentErrorOr(EIO), false};
}

}
