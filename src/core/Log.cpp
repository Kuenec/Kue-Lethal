#include "core/Log.h"

#include "core/Utf8.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <mutex>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace kue {

namespace {

constexpr std::size_t kMaximumLogMessageBytes = 65536;
constexpr std::string_view kInvalidLogPathMessage =
    "log path must be diagnostic-safe UTF-8 without controls or line breaks and at most 4096 bytes";
constexpr std::string_view kInvalidLogUtf8Message = "log message rejected: invalid UTF-8";
constexpr std::string_view kInvalidLogCharacterMessage =
    "log message rejected: contains a control or line-breaking character";

struct LogState {
    std::mutex mutex;
    FILE* file = nullptr;
    bool console = true;
    std::array<char, kMaximumLogPathBytes + 1> filePath{};
    std::size_t filePathSize = 0;
    std::array<char, kMaximumLogMessageBytes + 1> formatBuffer{};

    ~LogState();
};

struct Timestamp {
    std::array<char, 64> text{};
    std::size_t size = 0;
};

struct StreamFailure {
    std::string_view operation;
    int code = 0;
};

struct LogRecord {
    LogLevel level;
    std::string_view message;
};

enum class LogDecoration : std::uint8_t { Plain, Ansi };

enum class DiagnosticTextStatus : std::uint8_t { Valid, InvalidUtf8, InvalidCharacter };

LogState gLog;

std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    std::terminate();
}

Timestamp timestamp() {
    Timestamp result;
    const auto now = std::chrono::system_clock::now();
    const auto wholeSeconds = std::chrono::floor<std::chrono::seconds>(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - wholeSeconds).count();
    const std::time_t value = std::chrono::system_clock::to_time_t(wholeSeconds);
    std::tm utc{};
    errno = 0;
    if (!::gmtime_r(&value, &utc)) {
        const int code = errno != 0 ? errno : EOVERFLOW;
        const int length = std::snprintf(result.text.data(), result.text.size(),
                                         "timestamp-unavailable(errno=%d)", code);
        if (length > 0)
            result.size = static_cast<std::size_t>(length);
        return result;
    }
    std::array<char, 20> calendar{};
    if (std::strftime(calendar.data(), calendar.size(), "%Y-%m-%dT%H:%M:%S", &utc) != 19) {
        constexpr std::string_view unavailable = "timestamp-unavailable";
        std::memcpy(result.text.data(), unavailable.data(), unavailable.size());
        result.size = unavailable.size();
        return result;
    }
    const int length = std::snprintf(result.text.data(), result.text.size(), "%s.%03lldZ",
                                     calendar.data(), static_cast<long long>(milliseconds));
    if (length == 24) {
        result.size = static_cast<std::size_t>(length);
        return result;
    }
    constexpr std::string_view unavailable = "timestamp-unavailable";
    std::memcpy(result.text.data(), unavailable.data(), unavailable.size());
    result.size = unavailable.size();
    return result;
}

bool writeBytes(FILE* stream, std::string_view bytes, StreamFailure& failure) {
    if (bytes.empty())
        return true;
    errno = 0;
    if (std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size())
        return true;
    failure.operation = "write";
    failure.code = errno != 0 ? errno : EIO;
    return false;
}

DiagnosticTextStatus validateDiagnosticText(std::string_view text) noexcept {
    if (!isValidUtf8(text))
        return DiagnosticTextStatus::InvalidUtf8;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x1fU || first == 0x7fU)
            return DiagnosticTextStatus::InvalidCharacter;
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (first == 0xc2U) {
                const auto second = static_cast<unsigned char>(text[index + 1]);
                if (second >= 0x80U && second <= 0x9fU)
                    return DiagnosticTextStatus::InvalidCharacter;
            }
            index += 2;
            continue;
        }
        if (first == 0xe2U && static_cast<unsigned char>(text[index + 1]) == 0x80U) {
            const auto third = static_cast<unsigned char>(text[index + 2]);
            if (third == 0xa8U || third == 0xa9U)
                return DiagnosticTextStatus::InvalidCharacter;
        }
        index += first <= 0xefU ? 3 : 4;
    }
    return DiagnosticTextStatus::Valid;
}

LogDecoration decorationFor(FILE* stream, StreamFailure& failure) noexcept {
    errno = 0;
    const int descriptor = ::fileno(stream);
    if (descriptor < 0) {
        failure.operation = "inspect descriptor";
        failure.code = errno != 0 ? errno : EBADF;
        return LogDecoration::Plain;
    }
    errno = 0;
    if (::isatty(descriptor) != 0)
        return LogDecoration::Ansi;
    if (errno == ENOTTY || errno == EINVAL)
        return LogDecoration::Plain;
    if (errno == 0) {
        failure.operation = "inspect terminal capability";
        failure.code = EIO;
    } else {
        failure.operation = "inspect terminal capability";
        failure.code = errno;
    }
    return LogDecoration::Plain;
}

bool writeRecord(FILE* stream, LogRecord record, LogDecoration decoration, StreamFailure& failure) {
    const Timestamp time = timestamp();
    std::string_view colorPrefix;
    if (decoration == LogDecoration::Ansi && record.level == LogLevel::Error)
        colorPrefix = "\x1b[1;31m";
    else if (decoration == LogDecoration::Ansi && record.level == LogLevel::Warning)
        colorPrefix = "\x1b[1;33m";
    const bool hasColor = !colorPrefix.empty();
    constexpr std::string_view colorSuffix = "\x1b[0m";
    if (!writeBytes(stream, colorPrefix, failure) || !writeBytes(stream, "[", failure) ||
        !writeBytes(stream, std::string_view(time.text.data(), time.size), failure) ||
        !writeBytes(stream, "] [", failure) ||
        !writeBytes(stream, logLevelName(record.level), failure) ||
        !writeBytes(stream, "] ", failure) || !writeBytes(stream, record.message, failure) ||
        (hasColor && !writeBytes(stream, colorSuffix, failure)) ||
        !writeBytes(stream, "\n", failure)) {
        return false;
    }
    errno = 0;
    if (std::fflush(stream) == 0)
        return true;
    failure.operation = "flush";
    failure.code = errno != 0 ? errno : EIO;
    return false;
}

bool reportSinkFailure(std::string_view sink, const StreamFailure& failure) noexcept {
    const int written =
        std::fprintf(stderr, "[kue] log %.*s %.*s failed: %s\n", static_cast<int>(sink.size()),
                     sink.data(), static_cast<int>(failure.operation.size()),
                     failure.operation.data(), std::strerror(failure.code));
    return written >= 0 && std::fflush(stderr) == 0;
}

bool reportFileSinkFailure(const StreamFailure& failure) noexcept {
    const int written = std::fprintf(stderr, "[kue] log file sink '%.*s' %.*s failed: %s\n",
                                     static_cast<int>(gLog.filePathSize), gLog.filePath.data(),
                                     static_cast<int>(failure.operation.size()),
                                     failure.operation.data(), std::strerror(failure.code));
    return written >= 0 && std::fflush(stderr) == 0;
}

bool reportNonregularFileSink() noexcept {
    const int written = std::fprintf(stderr, "[kue] log file sink '%.*s' is not a regular file\n",
                                     static_cast<int>(gLog.filePathSize), gLog.filePath.data());
    return written >= 0 && std::fflush(stderr) == 0;
}

void requireVisibleFailure(bool reported) noexcept {
    if (!reported)
        std::terminate();
}

bool isValidLogPath(std::string_view path) noexcept {
    return path.size() <= kMaximumLogPathBytes &&
           validateDiagnosticText(path) == DiagnosticTextStatus::Valid;
}

LogState::~LogState() {
    if (!file)
        return;
    errno = 0;
    if (std::fclose(file) != 0) {
        const StreamFailure failure{"close", errno != 0 ? errno : EIO};
        requireVisibleFailure(reportFileSinkFailure(failure));
    }
}

bool closeFileSink() {
    if (!gLog.file)
        return true;
    FILE* const file = gLog.file;
    gLog.file = nullptr;
    errno = 0;
    if (std::fclose(file) == 0) {
        gLog.filePathSize = 0;
        return true;
    }
    StreamFailure failure{"close", errno != 0 ? errno : EIO};
    requireVisibleFailure(reportFileSinkFailure(failure));
    gLog.filePathSize = 0;
    return false;
}

bool writeValidatedRecord(LogRecord record) {
    bool succeeded = true;
    bool consoleReceivedRecord = false;
    StreamFailure failure;
    if (gLog.console) {
        FILE* const stream = record.level == LogLevel::Info ? stdout : stderr;
        const LogDecoration decoration = decorationFor(stream, failure);
        if (!failure.operation.empty()) {
            succeeded = false;
            requireVisibleFailure(reportSinkFailure("console", failure));
            consoleReceivedRecord = true;
        } else {
            consoleReceivedRecord = writeRecord(stream, record, decoration, failure);
            if (!consoleReceivedRecord) {
                succeeded = false;
                requireVisibleFailure(stream != stderr && reportSinkFailure("console", failure));
            }
        }
    }
    if (gLog.file) {
        if (!writeRecord(gLog.file, record, LogDecoration::Plain, failure)) {
            succeeded = false;
            requireVisibleFailure(reportFileSinkFailure(failure));
            closeFileSink();
            gLog.console = true;
            if (!consoleReceivedRecord) {
                failure = {};
                const LogDecoration decoration = decorationFor(stderr, failure);
                if (!failure.operation.empty()) {
                    requireVisibleFailure(reportSinkFailure("console", failure));
                } else {
                    requireVisibleFailure(writeRecord(stderr, record, decoration, failure));
                }
            }
        }
    }
    return succeeded;
}

bool writeInitializationRecord() {
    constexpr LogRecord record{LogLevel::Info, "kue-lethal session started"};
    StreamFailure failure;
    if (gLog.file && !writeRecord(gLog.file, record, LogDecoration::Plain, failure)) {
        requireVisibleFailure(reportFileSinkFailure(failure));
        closeFileSink();
        gLog.console = true;
        return false;
    }
    if (!gLog.console)
        return true;
    FILE* const stream = stdout;
    const LogDecoration decoration = decorationFor(stream, failure);
    if (!failure.operation.empty()) {
        requireVisibleFailure(reportSinkFailure("console", failure));
        if (gLog.file) {
            gLog.console = false;
            return true;
        }
        return false;
    }
    if (writeRecord(stream, record, decoration, failure))
        return true;
    requireVisibleFailure(reportSinkFailure("console", failure));
    if (gLog.file) {
        gLog.console = false;
        return true;
    }
    return false;
}

void rejectRecord(std::string_view reason) {
    writeValidatedRecord({LogLevel::Error, reason});
}

}

bool logInit(std::string_view path) {
    std::lock_guard<std::mutex> lock(gLog.mutex);
    if (!closeFileSink()) {
        gLog.console = true;
        return false;
    }
    gLog.console = ::getenv("KUE_CONSOLE") != nullptr || path.empty();
    if (!isValidLogPath(path)) {
        gLog.console = true;
        rejectRecord(kInvalidLogPathMessage);
        return false;
    }
    if (!path.empty()) {
        std::memcpy(gLog.filePath.data(), path.data(), path.size());
        gLog.filePath[path.size()] = '\0';
        gLog.filePathSize = path.size();
        errno = 0;
        const int descriptor = ::open(gLog.filePath.data(),
                                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NONBLOCK, 0666);
        if (descriptor < 0) {
            const StreamFailure failure{"open", errno != 0 ? errno : EIO};
            gLog.console = true;
            requireVisibleFailure(reportFileSinkFailure(failure));
            gLog.filePathSize = 0;
            return false;
        }
        struct stat metadata{};
        errno = 0;
        if (::fstat(descriptor, &metadata) != 0) {
            const StreamFailure failure{"inspect descriptor", errno != 0 ? errno : EIO};
            requireVisibleFailure(reportFileSinkFailure(failure));
            errno = 0;
            if (::close(descriptor) != 0) {
                const StreamFailure closeFailure{"close descriptor", errno != 0 ? errno : EIO};
                requireVisibleFailure(reportFileSinkFailure(closeFailure));
            }
            gLog.console = true;
            gLog.filePathSize = 0;
            return false;
        }
        if (!S_ISREG(metadata.st_mode)) {
            requireVisibleFailure(reportNonregularFileSink());
            errno = 0;
            if (::close(descriptor) != 0) {
                const StreamFailure closeFailure{"close descriptor", errno != 0 ? errno : EIO};
                requireVisibleFailure(reportFileSinkFailure(closeFailure));
            }
            gLog.console = true;
            gLog.filePathSize = 0;
            return false;
        }
        errno = 0;
        gLog.file = ::fdopen(descriptor, "a");
        if (!gLog.file) {
            const int associateCode = errno != 0 ? errno : EIO;
            errno = 0;
            if (::close(descriptor) != 0) {
                const StreamFailure closeFailure{"close descriptor", errno != 0 ? errno : EIO};
                requireVisibleFailure(reportFileSinkFailure(closeFailure));
            }
            const StreamFailure failure{"associate descriptor", associateCode};
            gLog.console = true;
            requireVisibleFailure(reportFileSinkFailure(failure));
            gLog.filePathSize = 0;
            return false;
        }
    }
    return writeInitializationRecord();
}

void logFormat(LogLevel level, const char* format, ...) {
    std::lock_guard<std::mutex> lock(gLog.mutex);
    if (!format) {
        rejectRecord("log format is null");
        return;
    }
    errno = 0;
    va_list arguments;
    va_start(arguments, format);
    const int length =
        std::vsnprintf(gLog.formatBuffer.data(), gLog.formatBuffer.size(), format, arguments);
    va_end(arguments);
    if (length < 0) {
        const int code = errno != 0 ? errno : EILSEQ;
        const int failureLength = std::snprintf(gLog.formatBuffer.data(), gLog.formatBuffer.size(),
                                                "cannot format %.*s log: %s",
                                                static_cast<int>(logLevelName(level).size()),
                                                logLevelName(level).data(), std::strerror(code));
        if (failureLength <= 0) {
            rejectRecord("cannot format log");
        } else {
            rejectRecord(std::string_view(gLog.formatBuffer.data(),
                                          static_cast<std::size_t>(failureLength)));
        }
        return;
    }
    const std::size_t messageBytes = static_cast<std::size_t>(length);
    if (messageBytes > kMaximumLogMessageBytes) {
        const int failureLength =
            std::snprintf(gLog.formatBuffer.data(), gLog.formatBuffer.size(),
                          "log message exceeds 65536 bytes (%zu bytes)", messageBytes);
        if (failureLength <= 0) {
            rejectRecord("log message exceeds 65536 bytes");
        } else {
            rejectRecord(std::string_view(gLog.formatBuffer.data(),
                                          static_cast<std::size_t>(failureLength)));
        }
        return;
    }
    const std::string_view message(gLog.formatBuffer.data(), messageBytes);
    switch (validateDiagnosticText(message)) {
    case DiagnosticTextStatus::Valid:
        break;
    case DiagnosticTextStatus::InvalidUtf8:
        rejectRecord(kInvalidLogUtf8Message);
        return;
    case DiagnosticTextStatus::InvalidCharacter:
        rejectRecord(kInvalidLogCharacterMessage);
        return;
    }
    writeValidatedRecord({level, message});
}

}
