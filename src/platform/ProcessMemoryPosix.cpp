#include "platform/ProcessMemory.h"

#include <bit>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace kue::platform {

namespace {

std::string_view nextField(std::string_view line, std::size_t& cursor) noexcept {
    cursor = line.find_first_not_of(' ', cursor);
    if (cursor == std::string_view::npos) {
        cursor = line.size();
        return {};
    }
    const std::size_t end = line.find(' ', cursor);
    const std::string_view field = line.substr(cursor, end - cursor);
    cursor = end == std::string_view::npos ? line.size() : end;
    return field;
}

bool parseHex(std::string_view text, std::uintptr_t& value) noexcept {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseRegion(std::string_view line, MemoryRegion& region) noexcept {
    std::size_t cursor = 0;
    const std::string_view range = nextField(line, cursor);
    const std::string_view permissions = nextField(line, cursor);
    const std::string_view offset = nextField(line, cursor);
    static_cast<void>(nextField(line, cursor));
    static_cast<void>(nextField(line, cursor));
    const std::size_t dash = range.find('-');
    if (dash == std::string_view::npos || permissions.size() < 3)
        return false;
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    std::uintptr_t fileOffset = 0;
    if (!parseHex(range.substr(0, dash), begin) || !parseHex(range.substr(dash + 1), end) ||
        !parseHex(offset, fileOffset) || end < begin) {
        return false;
    }
    PageProtection protection = 0;
    if (permissions[0] == 'r')
        protection |= PROT_READ;
    if (permissions[1] == 'w')
        protection |= PROT_WRITE;
    if (permissions[2] == 'x')
        protection |= PROT_EXEC;
    const std::size_t pathStart = line.find_first_not_of(' ', cursor);
    const std::string_view path =
        pathStart == std::string_view::npos ? std::string_view{} : line.substr(pathStart);
    MemoryRegionKind kind = MemoryRegionKind::Image;
    if (path.empty() || path.front() == '[' || path.find("memfd") != std::string_view::npos)
        kind = MemoryRegionKind::Private;
    region = {.begin = begin,
              .end = end,
              .protection = protection,
              .kind = kind,
              .readable = (protection & PROT_READ) != 0,
              .executable = (protection & PROT_EXEC) != 0,
              .fileOffset = fileOffset,
              .path = path};
    return true;
}

}

MemoryRegionScan::MemoryRegionScan() noexcept {
    mDescriptor = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    mExhausted = mDescriptor < 0;
}

MemoryRegionScan::~MemoryRegionScan() {
    if (mDescriptor >= 0)
        static_cast<void>(::close(mDescriptor));
}

bool MemoryRegionScan::readLine(std::size_t& lineBytes) noexcept {
    lineBytes = 0;
    bool overflow = false;
    while (true) {
        if (mBufferPosition == mBufferBytes) {
            if (mExhausted)
                return lineBytes != 0 && !overflow;
            ssize_t result = 0;
            do {
                errno = 0;
                result = ::read(mDescriptor, mBuffer.data(), mBuffer.size());
            } while (result < 0 && errno == EINTR);
            if (result <= 0) {
                mExhausted = true;
                return lineBytes != 0 && !overflow;
            }
            mBufferBytes = static_cast<std::size_t>(result);
            mBufferPosition = 0;
        }
        const char byte = mBuffer[mBufferPosition];
        ++mBufferPosition;
        if (byte == '\n') {
            if (overflow) {
                overflow = false;
                lineBytes = 0;
                continue;
            }
            return true;
        }
        if (lineBytes == mLine.size()) {
            overflow = true;
            continue;
        }
        mLine[lineBytes] = byte;
        ++lineBytes;
    }
}

bool MemoryRegionScan::next(MemoryRegion& region) noexcept {
    std::size_t lineBytes = 0;
    while (readLine(lineBytes)) {
        if (parseRegion({mLine.data(), lineBytes}, region))
            return true;
    }
    return false;
}

std::size_t pageSize() noexcept {
    const long size = ::sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : 4096;
}

void* reserveNear(std::uintptr_t address, std::size_t bytes) noexcept {
#if defined(MAP_FIXED_NOREPLACE)
    void* const mapping = ::mmap(std::bit_cast<void*>(address), bytes, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return mapping == MAP_FAILED ? nullptr : mapping;
#else
    static_cast<void>(address);
    static_cast<void>(bytes);
    return nullptr;
#endif
}

void releaseReservation(void* address, std::size_t bytes) noexcept {
    static_cast<void>(::munmap(address, bytes));
}

bool setProtection(void* address, std::size_t bytes, PageProtection protection) noexcept {
    return ::syscall(SYS_mprotect, address, bytes, protection) == 0;
}

void flushInstructionCache(void* address, std::size_t bytes) noexcept {
    char* const begin = static_cast<char*>(address);
    __builtin___clear_cache(begin, begin + bytes);
}

}
