#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace log_test {

thread_local bool measureScalarAllocation = false;
thread_local std::size_t scalarAllocations = 0;

void recordScalarAllocation() noexcept {
    if (measureScalarAllocation)
        ++scalarAllocations;
}

}

[[gnu::noinline]] void* operator new(std::size_t bytes) {
    log_test::recordScalarAllocation();
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc{};
}

[[gnu::noinline]] void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

constexpr std::uint64_t kMaximumFixtureBytes = 131072;
static_assert(!std::is_convertible_v<std::string_view, kue::LogLevel>);
static_assert(std::is_same_v<decltype(&kue::logInit), bool (*)(std::string_view)>);
static_assert(std::is_same_v<decltype(kue::logFormat(kue::LogLevel::Info, "%s", "value")), void>);

class TestRun {
  public:
    bool expect(bool condition, std::string_view message) {
        ++mAssertions;
        if (condition)
            return true;
        ++mFailures;
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }

    int result() const {
        if (mFailures == 0) {
            std::cout << "log tests passed: " << mAssertions << " assertions\n";
            return 0;
        }
        std::cerr << "log tests failed: " << mFailures << " of " << mAssertions << " assertions\n";
        return 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

class ScalarAllocationMeasurement {
  public:
    ScalarAllocationMeasurement() {
        log_test::scalarAllocations = 0;
        log_test::measureScalarAllocation = true;
    }

    ~ScalarAllocationMeasurement() { log_test::measureScalarAllocation = false; }

    ScalarAllocationMeasurement(const ScalarAllocationMeasurement&) = delete;
    ScalarAllocationMeasurement& operator=(const ScalarAllocationMeasurement&) = delete;

    std::size_t count() const noexcept { return log_test::scalarAllocations; }
};

void appendError(std::string& error, std::string_view detail) {
    if (!error.empty())
        error += "; ";
    error.append(detail);
}

void appendErrno(std::string& error, std::string_view operation, int code) {
    std::string detail(operation);
    detail += ": ";
    detail += std::strerror(code);
    appendError(error, detail);
}

class TemporaryFile {
  public:
    explicit TemporaryFile(TestRun& run) : mRun(run) {}

    bool create(std::string& error) {
        error.clear();
        std::error_code code;
        const std::filesystem::path root = std::filesystem::temp_directory_path(code);
        if (code) {
            error = "cannot discover temporary directory: " + code.message();
            return false;
        }
        std::string pattern = (root / "kue-log-tests-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const int descriptor = ::mkstemp(writable.data());
        if (descriptor < 0) {
            error = "cannot create temporary log: " + std::string(std::strerror(errno));
            return false;
        }
        mPath = writable.data();
        if (::close(descriptor) != 0) {
            error = "cannot close temporary log: " + std::string(std::strerror(errno));
            return false;
        }
        return true;
    }

    ~TemporaryFile() {
        if (mPath.empty())
            return;
        std::string error;
        const bool removed = remove(error);
        mRun.expect(removed, error);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    const std::string& path() const { return mPath; }

    bool remove(std::string& error) {
        error.clear();
        if (mPath.empty())
            return true;
        errno = 0;
        if (::unlink(mPath.c_str()) == 0 || errno == ENOENT) {
            mPath.clear();
            return true;
        }
        const int code = errno;
        error = "cannot remove temporary log '" + mPath + "': " + std::strerror(code);
        return false;
    }

  private:
    TestRun& mRun;
    std::string mPath;
};

struct TextReadResult {
    bool succeeded = false;
    std::string text;
    std::string error;
};

TextReadResult readText(const std::string& path) {
    TextReadResult result;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        result.error = "cannot open log for reading: " + path;
        return result;
    }
    result.succeeded = true;
    const std::ifstream::pos_type end = input.tellg();
    if (end < std::ifstream::pos_type{}) {
        result.error = "cannot determine log size: " + path;
        result.succeeded = false;
    } else {
        const auto bytes = static_cast<std::uint64_t>(end);
        if (bytes > kMaximumFixtureBytes) {
            result.error = "log fixture exceeds the 131072-byte test limit";
            result.succeeded = false;
        } else {
            result.text.assign(static_cast<std::size_t>(bytes), '\0');
            input.seekg(0);
            if (!input) {
                result.error = "cannot seek to the beginning of log: " + path;
                result.succeeded = false;
            } else if (!result.text.empty()) {
                input.read(result.text.data(), static_cast<std::streamsize>(result.text.size()));
                if (!input) {
                    result.error = "cannot read complete log: " + path;
                    result.succeeded = false;
                }
            }
        }
    }
    input.clear();
    input.close();
    if (input.fail()) {
        appendError(result.error, "cannot close log after reading: " + path);
        result.succeeded = false;
    }
    return result;
}

TextReadResult readStream(FILE* stream) {
    TextReadResult result;
    result.succeeded = true;
    errno = 0;
    if (std::fseek(stream, 0, SEEK_END) != 0) {
        appendErrno(result.error, "cannot seek to captured stderr end", errno != 0 ? errno : EIO);
        result.succeeded = false;
    }
    errno = 0;
    const long end = result.succeeded ? std::ftell(stream) : -1;
    if (result.succeeded && end < 0) {
        appendErrno(result.error, "cannot determine captured stderr size",
                    errno != 0 ? errno : EIO);
        result.succeeded = false;
    }
    if (result.succeeded && static_cast<std::uint64_t>(end) > kMaximumFixtureBytes) {
        appendError(result.error, "captured stderr exceeds the 131072-byte test limit");
        result.succeeded = false;
    }
    if (result.succeeded) {
        result.text.assign(static_cast<std::size_t>(end), '\0');
        errno = 0;
        if (std::fseek(stream, 0, SEEK_SET) != 0) {
            appendErrno(result.error, "cannot seek to captured stderr beginning",
                        errno != 0 ? errno : EIO);
            result.succeeded = false;
        } else if (!result.text.empty() && std::fread(result.text.data(), 1, result.text.size(),
                                                      stream) != result.text.size()) {
            appendErrno(result.error, "cannot read complete captured stderr",
                        errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
    }
    return result;
}

bool isDigit(char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

bool hasUtcTimestamp(std::string_view line) {
    if (line.size() < 26 || line[0] != '[' || line[5] != '-' || line[8] != '-' || line[11] != 'T' ||
        line[14] != ':' || line[17] != ':' || line[20] != '.' || line[24] != 'Z' ||
        line[25] != ']') {
        return false;
    }
    constexpr std::array<std::size_t, 17> digitPositions = {
        1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18, 19, 21, 22, 23,
    };
    for (const std::size_t position : digitPositions) {
        if (!isDigit(line[position]))
            return false;
    }
    return true;
}

bool closeDescriptor(int& descriptor, std::string_view purpose, std::string& error) {
    if (descriptor < 0)
        return true;
    const int value = descriptor;
    descriptor = -1;
    errno = 0;
    if (::close(value) == 0)
        return true;
    appendErrno(error, "cannot close " + std::string(purpose), errno != 0 ? errno : EIO);
    return false;
}

bool closeStream(FILE*& stream, std::string_view purpose, std::string& error) {
    if (!stream)
        return true;
    FILE* const value = stream;
    stream = nullptr;
    errno = 0;
    if (std::fclose(value) == 0)
        return true;
    appendErrno(error, "cannot close " + std::string(purpose), errno != 0 ? errno : EIO);
    return false;
}

int duplicateDescriptor(int descriptor) {
    int result = -1;
    do {
        errno = 0;
        result = ::dup(descriptor);
    } while (result < 0 && errno == EINTR);
    return result;
}

int replaceDescriptor(int source, int destination) {
    int result = -1;
    do {
        errno = 0;
        result = ::dup2(source, destination);
    } while (result < 0 && errno == EINTR);
    return result;
}

struct SinkCaptureResult {
    bool succeeded = false;
    std::string stderrText;
    std::string error;
};

struct DescriptorSecurityResult {
    bool succeeded = false;
    std::size_t matches = 0;
    bool closeOnExec = false;
    bool nonBlocking = false;
    std::string error;
};

DescriptorSecurityResult inspectLogDescriptor(const char* path) {
    DescriptorSecurityResult result;
    struct stat target{};
    if (::stat(path, &target) != 0) {
        result.error = "cannot inspect active log path: " + std::string(std::strerror(errno));
        return result;
    }
    DIR* const directory = ::opendir("/proc/self/fd");
    if (!directory) {
        result.error = "cannot inspect process descriptors: " + std::string(std::strerror(errno));
        return result;
    }
    result.succeeded = true;
    errno = 0;
    while (const dirent* const entry = ::readdir(directory)) {
        std::string_view name(entry->d_name);
        int descriptor = -1;
        const auto parsed = std::from_chars(name.data(), name.data() + name.size(), descriptor);
        if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size() || descriptor < 3)
            continue;
        struct stat candidate{};
        if (::fstat(descriptor, &candidate) == 0 && candidate.st_dev == target.st_dev &&
            candidate.st_ino == target.st_ino) {
            ++result.matches;
            errno = 0;
            const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
            if (descriptorFlags < 0) {
                result.error = "cannot inspect active log descriptor flags: " +
                               std::string(std::strerror(errno != 0 ? errno : EIO));
                result.succeeded = false;
            } else {
                result.closeOnExec = result.matches == 1 && (descriptorFlags & FD_CLOEXEC) != 0;
                errno = 0;
                const int statusFlags = ::fcntl(descriptor, F_GETFL);
                if (statusFlags < 0) {
                    result.error = "cannot inspect active log descriptor status: " +
                                   std::string(std::strerror(errno != 0 ? errno : EIO));
                    result.succeeded = false;
                } else {
                    result.nonBlocking = result.matches == 1 && (statusFlags & O_NONBLOCK) != 0;
                }
            }
        }
        errno = 0;
    }
    const int iterationError = errno;
    if (result.succeeded && result.matches == 0 && iterationError != 0) {
        result.error =
            "cannot enumerate process descriptors: " + std::string(std::strerror(iterationError));
        result.succeeded = false;
    }
    errno = 0;
    if (::closedir(directory) != 0) {
        appendErrno(result.error, "cannot close process descriptor directory",
                    errno != 0 ? errno : EIO);
        result.succeeded = false;
    }
    return result;
}

SinkCaptureResult captureRejectedLogPaths(std::string_view missingParent) {
    SinkCaptureResult result;
    FILE* capture = std::tmpfile();
    if (!capture) {
        appendErrno(result.error, "cannot create path-diagnostic capture",
                    errno != 0 ? errno : EIO);
        return result;
    }
    errno = 0;
    const int captureDescriptor = ::fileno(capture);
    if (captureDescriptor < 0) {
        appendErrno(result.error, "cannot obtain path-diagnostic capture descriptor",
                    errno != 0 ? errno : EBADF);
        closeStream(capture, "path-diagnostic capture", result.error);
        return result;
    }
    errno = 0;
    if (std::fflush(stdout) != 0 || std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush console before path-diagnostic capture",
                    errno != 0 ? errno : EIO);
        closeStream(capture, "path-diagnostic capture", result.error);
        return result;
    }
    int originalOutput = duplicateDescriptor(STDOUT_FILENO);
    if (originalOutput < 0) {
        appendErrno(result.error, "cannot duplicate stdout", errno != 0 ? errno : EIO);
        closeStream(capture, "path-diagnostic capture", result.error);
        return result;
    }
    int originalError = duplicateDescriptor(STDERR_FILENO);
    if (originalError < 0) {
        appendErrno(result.error, "cannot duplicate stderr", errno != 0 ? errno : EIO);
        closeDescriptor(originalOutput, "saved stdout", result.error);
        closeStream(capture, "path-diagnostic capture", result.error);
        return result;
    }
    if (replaceDescriptor(captureDescriptor, STDOUT_FILENO) < 0 ||
        replaceDescriptor(captureDescriptor, STDERR_FILENO) < 0) {
        appendErrno(result.error, "cannot redirect console", errno != 0 ? errno : EIO);
        replaceDescriptor(originalOutput, STDOUT_FILENO);
        replaceDescriptor(originalError, STDERR_FILENO);
        closeDescriptor(originalOutput, "saved stdout", result.error);
        closeDescriptor(originalError, "saved stderr", result.error);
        closeStream(capture, "path-diagnostic capture", result.error);
        return result;
    }

    const std::string base(missingParent);
    const std::array<std::string, 7> paths = {
        base + "tab\tforged",
        base + "line-feed\nforged",
        base + "carriage-return\rforged",
        base + "escape" + std::string(1, '\x1b') + "forged",
        base + "next-line" + std::string("\xc2\x85", 2) + "forged",
        base + "line-separator" + std::string("\xe2\x80\xa8", 3) + "forged",
        base + "paragraph-separator" + std::string("\xe2\x80\xa9", 3) + "forged",
    };
    bool allRejected = true;
    for (const std::string& path : paths)
        allRejected = !kue::logInit(path) && allRejected;

    errno = 0;
    if (std::fflush(stdout) != 0 || std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush captured path diagnostics",
                    errno != 0 ? errno : EIO);
        result.succeeded = false;
    } else {
        result.succeeded = allRejected;
        if (!allRejected)
            appendError(result.error, "a control-bearing log path was accepted");
    }
    const int outputRestoreResult = replaceDescriptor(originalOutput, STDOUT_FILENO);
    const int outputRestoreError = errno;
    const int errorRestoreResult = replaceDescriptor(originalError, STDERR_FILENO);
    const int errorRestoreError = errno;
    if (!closeDescriptor(originalOutput, "saved stdout", result.error) ||
        !closeDescriptor(originalError, "saved stderr", result.error)) {
        result.succeeded = false;
    }
    if (outputRestoreResult < 0) {
        appendErrno(result.error, "cannot restore stdout",
                    outputRestoreError != 0 ? outputRestoreError : EIO);
        result.succeeded = false;
    }
    if (errorRestoreResult < 0) {
        appendErrno(result.error, "cannot restore stderr",
                    errorRestoreError != 0 ? errorRestoreError : EIO);
        result.succeeded = false;
    }
    TextReadResult captured = readStream(capture);
    if (!captured.succeeded) {
        appendError(result.error, captured.error);
        result.succeeded = false;
    }
    result.stderrText = std::move(captured.text);
    if (!closeStream(capture, "path-diagnostic capture", result.error))
        result.succeeded = false;
    return result;
}

void testNonregularLogSink(TestRun& run, const std::string& regularPath) {
    const std::string fifoPath = regularPath + ".fifo";
    if (::mkfifo(fifoPath.c_str(), 0600) != 0) {
        run.expect(false, "cannot create log FIFO fixture");
        return;
    }
    const int guard = ::open(fifoPath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (guard < 0) {
        run.expect(false, "cannot open log FIFO guard");
        run.expect(::unlink(fifoPath.c_str()) == 0, "failed FIFO fixture is removed");
        return;
    }
    run.expect(!kue::logInit(fifoPath), "a FIFO log sink is rejected without blocking");
    run.expect(kue::logInit(""), "the logger returns to console after FIFO rejection");
    run.expect(::close(guard) == 0, "the log FIFO guard is closed");
    run.expect(::unlink(fifoPath.c_str()) == 0, "the log FIFO fixture is removed");
    run.expect(!kue::logInit("/dev/null"), "a character-device log sink is rejected");
    run.expect(kue::logInit(""), "the logger returns to console after device rejection");
}

SinkCaptureResult captureFailedLogSink() {
    SinkCaptureResult result;
    FILE* capture = std::tmpfile();
    if (!capture) {
        appendErrno(result.error, "cannot create stderr capture", errno != 0 ? errno : EIO);
        return result;
    }
    errno = 0;
    const int captureDescriptor = ::fileno(capture);
    if (captureDescriptor < 0) {
        appendErrno(result.error, "cannot obtain stderr capture descriptor",
                    errno != 0 ? errno : EBADF);
        closeStream(capture, "stderr capture", result.error);
        return result;
    }
    errno = 0;
    if (std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush stderr before capture", errno != 0 ? errno : EIO);
        closeStream(capture, "stderr capture", result.error);
        return result;
    }
    int original = duplicateDescriptor(STDERR_FILENO);
    if (original < 0) {
        result.error = "cannot duplicate stderr: " + std::string(std::strerror(errno));
        closeStream(capture, "stderr capture", result.error);
        return result;
    }
    if (replaceDescriptor(captureDescriptor, STDERR_FILENO) < 0) {
        result.error = "cannot redirect stderr: " + std::string(std::strerror(errno));
        closeDescriptor(original, "saved stderr", result.error);
        closeStream(capture, "stderr capture", result.error);
        return result;
    }
    result.succeeded = true;
    const bool initialized = kue::logInit("/dev/full");
    errno = 0;
    if (std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush captured stderr", errno != 0 ? errno : EIO);
        result.succeeded = false;
    }
    const int restoreResult = replaceDescriptor(original, STDERR_FILENO);
    const int restoreError = errno;
    if (!closeDescriptor(original, "saved stderr", result.error))
        result.succeeded = false;
    if (restoreResult < 0) {
        appendErrno(result.error, "cannot restore stderr", restoreError != 0 ? restoreError : EIO);
        result.succeeded = false;
    }
    if (initialized) {
        appendError(result.error,
                    "logInit accepted a sink that could not write its session record");
        result.succeeded = false;
    }
    TextReadResult captured = readStream(capture);
    if (!captured.succeeded) {
        appendError(result.error, captured.error);
        result.succeeded = false;
    }
    result.stderrText = std::move(captured.text);
    if (!closeStream(capture, "stderr capture", result.error))
        result.succeeded = false;
    return result;
}

SinkCaptureResult captureRejectedLogRecords() {
    SinkCaptureResult result;
    FILE* capture = std::tmpfile();
    if (!capture) {
        appendErrno(result.error, "cannot create rejected-record capture",
                    errno != 0 ? errno : EIO);
        return result;
    }
    errno = 0;
    const int captureDescriptor = ::fileno(capture);
    if (captureDescriptor < 0) {
        appendErrno(result.error, "cannot obtain rejected-record capture descriptor",
                    errno != 0 ? errno : EBADF);
        closeStream(capture, "rejected-record capture", result.error);
        return result;
    }
    errno = 0;
    if (std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush stderr before rejected-record capture",
                    errno != 0 ? errno : EIO);
        closeStream(capture, "rejected-record capture", result.error);
        return result;
    }
    int original = duplicateDescriptor(STDERR_FILENO);
    if (original < 0) {
        appendErrno(result.error, "cannot duplicate stderr", errno != 0 ? errno : EIO);
        closeStream(capture, "rejected-record capture", result.error);
        return result;
    }
    if (replaceDescriptor(captureDescriptor, STDERR_FILENO) < 0) {
        appendErrno(result.error, "cannot redirect stderr", errno != 0 ? errno : EIO);
        closeDescriptor(original, "saved stderr", result.error);
        closeStream(capture, "rejected-record capture", result.error);
        return result;
    }
    result.succeeded = true;
    if (!kue::logInit("")) {
        appendError(result.error, "cannot initialize console logger for rejected-record capture");
        result.succeeded = false;
    }
    KUE_WARN("redirected warning");
    const std::array<char, 3> invalidUtf8 = {static_cast<char>(0xc0), static_cast<char>(0xaf),
                                             '\0'};
    kue::logFormat(kue::LogLevel::Error, "%s", invalidUtf8.data());
    KUE_ERR("line one\n[2000-01-01T00:00:00.000Z] [error] forged");
    KUE_ERR("carriage\rreturn");
    KUE_ERR("tab\tseparated");
    KUE_ERR("escape\x1b[31mforged-color");
    kue::logFormat(kue::LogLevel::Error, "embedded-null%cforged", 0);
    const std::array<char, 3> unicodeNextLine = {static_cast<char>(0xc2), static_cast<char>(0x85),
                                                 '\0'};
    kue::logFormat(kue::LogLevel::Error, "unicode-next-line=%s", unicodeNextLine.data());
    const std::array<char, 4> unicodeLineSeparator = {
        static_cast<char>(0xe2), static_cast<char>(0x80), static_cast<char>(0xa8), '\0'};
    kue::logFormat(kue::LogLevel::Error, "unicode-line-separator=%s", unicodeLineSeparator.data());
    KUE_WARN("valid UTF-8=\xe2\x98\x83");
    errno = 0;
    if (std::fflush(stderr) != 0) {
        appendErrno(result.error, "cannot flush rejected-record capture", errno != 0 ? errno : EIO);
        result.succeeded = false;
    }
    errno = 0;
    const int restoreResult = replaceDescriptor(original, STDERR_FILENO);
    const int restoreError = errno;
    if (!closeDescriptor(original, "saved stderr", result.error))
        result.succeeded = false;
    if (restoreResult < 0) {
        appendErrno(result.error, "cannot restore stderr", restoreError != 0 ? restoreError : EIO);
        result.succeeded = false;
    }
    TextReadResult captured = readStream(capture);
    if (!captured.succeeded) {
        appendError(result.error, captured.error);
        result.succeeded = false;
    }
    result.stderrText = std::move(captured.text);
    if (!closeStream(capture, "rejected-record capture", result.error))
        result.succeeded = false;
    return result;
}

void executeLogTests(TestRun& run, TemporaryFile& log) {
    std::string error;
    if (!log.create(error)) {
        run.expect(false, error);
        return;
    }
    run.expect(true, "temporary log is created");

    if (!run.expect(kue::logInit(log.path()), "writable file logger initializes"))
        return;

    const DescriptorSecurityResult descriptorProbe = inspectLogDescriptor(log.path().c_str());
    if (!run.expect(descriptorProbe.succeeded, descriptorProbe.error))
        return;
    run.expect(descriptorProbe.matches == 1,
               "the active log descriptor has one discoverable owner");
    run.expect(descriptorProbe.closeOnExec,
               "the active log descriptor cannot cross an executable-image replacement");
    run.expect(descriptorProbe.nonBlocking,
               "the exact active log descriptor remains nonblocking after stream association");

    std::size_t validRecordAllocations = 0;
    {
        ScalarAllocationMeasurement measurement;
        KUE_INFO("allocation-free valid record");
        validRecordAllocations = measurement.count();
    }
    run.expect(validRecordAllocations == 0,
               "a valid steady-state log record performs no scalar allocation");

    {
        const std::string payload(1024, 'x');
        KUE_INFO("payload=%s", payload.c_str());
        KUE_WARN("typed warning");
        KUE_ERR("typed error");
        if (!run.expect(kue::logInit(""), "logger closes after formatted write"))
            return;

        const TextReadResult logRead = readText(log.path());
        if (!run.expect(logRead.succeeded, logRead.error))
            return;
        const std::size_t firstLineEnd = logRead.text.find('\n');
        run.expect(firstLineEnd != std::string::npos &&
                       hasUtcTimestamp(std::string_view(logRead.text).substr(0, firstLineEnd)),
                   "log records use an unambiguous UTC date and timestamp");
        const std::string expectedPayload = "payload=" + payload;
        run.expect(logRead.text.find(expectedPayload) != std::string::npos,
                   "formatted log messages are not silently truncated");
        run.expect(logRead.text.find("[warn] typed warning") != std::string::npos,
                   "the warning level maps to its exact label");
        run.expect(logRead.text.find("[error] typed error") != std::string::npos,
                   "the error level maps to its exact label");
    }

    const SinkCaptureResult rejectedRecords = captureRejectedLogRecords();
    if (!run.expect(rejectedRecords.succeeded, rejectedRecords.error))
        return;
    run.expect(rejectedRecords.stderrText.find('\x1b') == std::string::npos,
               "a redirected console sink receives no terminal escape sequence");
    run.expect(rejectedRecords.stderrText.find("forged") == std::string::npos,
               "a rejected record cannot forge content or formatting");
    run.expect(rejectedRecords.stderrText.find('\r') == std::string::npos &&
                   rejectedRecords.stderrText.find('\t') == std::string::npos &&
                   rejectedRecords.stderrText.find('\0') == std::string::npos,
               "rejected control bytes never reach the console sink");
    const std::array<char, 2> invalidUtf8 = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
    run.expect(rejectedRecords.stderrText.find(
                   std::string_view(invalidUtf8.data(), invalidUtf8.size())) == std::string::npos,
               "ill-formed UTF-8 never reaches the console sink");
    run.expect(rejectedRecords.stderrText.find("log message rejected: invalid UTF-8") !=
                   std::string::npos,
               "ill-formed UTF-8 produces an accurate visible rejection");
    run.expect(rejectedRecords.stderrText.find(
                   "log message rejected: contains a control or line-breaking character") !=
                   std::string::npos,
               "control input produces an accurate visible rejection");
    run.expect(rejectedRecords.stderrText.find("valid UTF-8=\xe2\x98\x83") != std::string::npos,
               "a valid Unicode record is preserved exactly");
    const std::size_t physicalLines = static_cast<std::size_t>(
        std::count(rejectedRecords.stderrText.cbegin(), rejectedRecords.stderrText.cend(), '\n'));
    run.expect(physicalLines == 10, "each attempted record produces exactly one physical line");
    bool everyLineHasTimestamp = true;
    std::size_t lineStart = 0;
    while (lineStart < rejectedRecords.stderrText.size()) {
        const std::size_t lineEnd = rejectedRecords.stderrText.find('\n', lineStart);
        if (lineEnd == std::string::npos ||
            !hasUtcTimestamp(std::string_view(rejectedRecords.stderrText)
                                 .substr(lineStart, lineEnd - lineStart))) {
            everyLineHasTimestamp = false;
            break;
        }
        lineStart = lineEnd + 1;
    }
    run.expect(everyLineHasTimestamp, "every captured physical line is one complete log record");

    const SinkCaptureResult rejectedPaths = captureRejectedLogPaths(log.path() + ".missing/");
    if (!run.expect(rejectedPaths.succeeded, rejectedPaths.error))
        return;
    run.expect(rejectedPaths.stderrText.find("forged") == std::string::npos,
               "control-bearing log paths cannot forge direct diagnostic content");
    run.expect(rejectedPaths.stderrText.find("session started") == std::string::npos,
               "rejected log paths emit no false session-start record");
    run.expect(
        rejectedPaths.stderrText.find('\x1b') == std::string::npos &&
            rejectedPaths.stderrText.find('\r') == std::string::npos &&
            rejectedPaths.stderrText.find('\t') == std::string::npos &&
            rejectedPaths.stderrText.find(std::string("\xc2\x85", 2)) == std::string::npos &&
            rejectedPaths.stderrText.find(std::string("\xe2\x80\xa8", 3)) == std::string::npos &&
            rejectedPaths.stderrText.find(std::string("\xe2\x80\xa9", 3)) == std::string::npos,
        "direct path diagnostics contain no control or Unicode line-breaking text");
    const std::size_t pathDiagnosticLines = static_cast<std::size_t>(
        std::count(rejectedPaths.stderrText.cbegin(), rejectedPaths.stderrText.cend(), '\n'));
    run.expect(pathDiagnosticLines == 7,
               "each rejected control-bearing path produces one safe physical line");

    testNonregularLogSink(run, log.path());

    if (!run.expect(kue::logInit(log.path()), "logger reopens for boundary validation"))
        return;
    {
        const std::string oversized(65537, 'z');
        kue::logFormat(kue::LogLevel::Info, "%s", oversized.c_str());
        if (!run.expect(kue::logInit(""), "logger closes after boundary validation"))
            return;
        const TextReadResult logRead = readText(log.path());
        if (!run.expect(logRead.succeeded, logRead.error))
            return;
        run.expect(logRead.text.find("log message exceeds 65536 bytes (65537 bytes)") !=
                       std::string::npos,
                   "oversize diagnostic has byte bound");
    }

    if (!run.expect(kue::logInit(log.path()), "logger reopens for concurrent writes"))
        return;
    constexpr int threadCount = 4;
    constexpr int recordsPerThread = 32;
    try {
        std::array<std::jthread, threadCount> workers;
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
            workers[static_cast<std::size_t>(threadIndex)] = std::jthread([threadIndex] {
                for (int recordIndex = 0; recordIndex < recordsPerThread; ++recordIndex)
                    KUE_INFO("thread=%d record=%d", threadIndex, recordIndex);
            });
        }
    } catch (const std::system_error& failure) {
        run.expect(false, "cannot start concurrent log writer: " + std::string(failure.what()));
        return;
    }
    if (!run.expect(kue::logInit(""), "logger closes after concurrent writes"))
        return;
    const TextReadResult logRead = readText(log.path());
    if (!run.expect(logRead.succeeded, logRead.error))
        return;
    int matchedRecords = 0;
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        for (int recordIndex = 0; recordIndex < recordsPerThread; ++recordIndex) {
            const std::string record = "[info] thread=" + std::to_string(threadIndex) +
                                       " record=" + std::to_string(recordIndex) + '\n';
            const std::size_t first = logRead.text.find(record);
            if (first != std::string::npos &&
                logRead.text.find(record, first + 1) == std::string::npos) {
                ++matchedRecords;
            }
        }
    }
    run.expect(matchedRecords == threadCount * recordsPerThread,
               "concurrent log records remain complete and unique");

    const SinkCaptureResult sinkFailure = captureFailedLogSink();
    if (!run.expect(sinkFailure.succeeded, sinkFailure.error))
        return;
    run.expect(sinkFailure.stderrText.find("log file sink '/dev/full' is not a regular file") !=
                   std::string::npos,
               "nonregular sink failure is visible");
    run.expect(sinkFailure.stderrText.find("close failed") == std::string::npos,
               "no hidden close failure");
    run.expect(sinkFailure.stderrText.find("session started") == std::string::npos,
               "failed sink initialization emits no false session-start record");
}

void runLogTests(TestRun& run) {
    TemporaryFile log(run);
    executeLogTests(run, log);
    run.expect(kue::logInit(""), "logger returns to console during cleanup");
    if (!log.path().empty()) {
        std::string error;
        const bool removed = log.remove(error);
        run.expect(removed, error);
    }
}

}

int main() {
    TestRun run;
    runLogTests(run);
    return run.result();
}
