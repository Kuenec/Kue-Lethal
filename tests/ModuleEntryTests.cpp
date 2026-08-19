#include "core/Log.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr std::size_t kCapturedTextCapacity = 2048;
constexpr std::size_t kMaximumExceptionDetailBytes = 1024;
constexpr std::size_t kMaximumEnvironmentValueBytes = 128;
constexpr std::string_view kStandardFallback =
    "[kue] loader: start failed with standard exception: fixture standard failure; logging "
    "failed\n";
constexpr std::string_view kUnknownFallback =
    "[kue] loader: start failed with unknown exception; logging failed\n";

thread_local bool gMeasureAllocations = false;
thread_local std::size_t gAllocationCount = 0;
bool gConstructorComplete = false;
int gBootCallCount = 0;
bool gBootSawCompleteConstructor = false;
bool gLogThrows = false;
int gLogCallCount = 0;
bool gLogFormatSucceeded = false;
kue::LogLevel gLogLevel = kue::LogLevel::Info;
std::array<char, kCapturedTextCapacity> gLogMessage{};
std::size_t gLogMessageSize = 0;

enum class WriteBehavior : std::uint8_t { Complete, InterruptOnce, PartialOnce, Fail };

thread_local WriteBehavior gWriteBehavior = WriteBehavior::Complete;
thread_local std::size_t gWriteCallCount = 0;

void recordAllocation() noexcept {
    if (gMeasureAllocations)
        ++gAllocationCount;
}

void* allocate(std::size_t bytes) {
    recordAllocation();
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc{};
}

void* allocateAligned(std::size_t bytes, std::size_t alignment) {
    recordAllocation();
    void* memory = nullptr;
    if (::posix_memalign(&memory, alignment, bytes == 0 ? 1 : bytes) == 0)
        return memory;
    throw std::bad_alloc{};
}

[[gnu::noinline]] void deallocate(void* memory) noexcept {
    std::free(memory);
}

class TestRun final {
  public:
    void expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
    }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

class EnvironmentVariable final {
  public:
    EnvironmentVariable(TestRun& run, const char* name) : mRun(run), mName(name) {
        const char* const previous = ::getenv(name);
        if (!previous)
            return;
        const std::size_t size = ::strnlen(previous, mPrevious.size());
        if (size >= mPrevious.size()) {
            mRestorable = false;
            mRun.expect(false, "the inherited fixture behavior has a restorable bound");
            return;
        }
        mWasSet = true;
        std::memcpy(mPrevious.data(), previous, size + 1);
    }

    ~EnvironmentVariable() {
        if (!mChanged)
            return;
        const int result = mWasSet ? ::setenv(mName, mPrevious.data(), 1) : ::unsetenv(mName);
        if (result != 0)
            mRun.expect(false, "the fixture behavior is restored");
    }

    EnvironmentVariable(const EnvironmentVariable&) = delete;
    EnvironmentVariable& operator=(const EnvironmentVariable&) = delete;

    bool assign(const char* value) {
        if (!mRestorable)
            return false;
        if (::setenv(mName, value, 1) != 0)
            return false;
        mChanged = true;
        return true;
    }

    bool clear() {
        if (!mRestorable)
            return false;
        if (::unsetenv(mName) != 0)
            return false;
        mChanged = true;
        return true;
    }

  private:
    TestRun& mRun;
    const char* mName;
    std::array<char, kMaximumEnvironmentValueBytes + 1> mPrevious{};
    bool mWasSet = false;
    bool mChanged = false;
    bool mRestorable = true;
};

struct CapturedStart final {
    int result = -1;
    std::array<char, kCapturedTextCapacity> standardError{};
    std::size_t standardErrorSize = 0;
    std::size_t writeCalls = 0;
};

using StartFunction = int (*)() noexcept;

void resetLogCapture(bool throws) noexcept {
    gLogThrows = throws;
    gLogCallCount = 0;
    gLogFormatSucceeded = false;
    gLogLevel = kue::LogLevel::Info;
    gLogMessageSize = 0;
}

std::string_view loggedMessage() noexcept {
    return {gLogMessage.data(), gLogMessageSize};
}

bool assignBehavior(TestRun& run, EnvironmentVariable& behavior, const char* value) {
    const bool assigned = behavior.assign(value);
    run.expect(assigned, "the requested boot behavior is installed before start");
    return assigned;
}

bool clearBehavior(TestRun& run, EnvironmentVariable& behavior) {
    const bool cleared = behavior.clear();
    run.expect(cleared, "the inherited boot behavior is cleared before start");
    return cleared;
}

CapturedStart captureStart(TestRun& run, StartFunction start, WriteBehavior behavior) {
    CapturedStart captured;
    std::array<int, 2> pipeDescriptors{};
    if (::pipe2(pipeDescriptors.data(), O_CLOEXEC) != 0) {
        run.expect(false, "the fallback capture pipe opens");
        return captured;
    }
    const int savedError = ::dup(STDERR_FILENO);
    if (savedError < 0) {
        run.expect(false, "standard error is duplicated for fallback capture");
        if (::close(pipeDescriptors[0]) != 0 || ::close(pipeDescriptors[1]) != 0)
            run.expect(false, "fallback capture descriptors close after setup failure");
        return captured;
    }
    if (::dup2(pipeDescriptors[1], STDERR_FILENO) < 0) {
        run.expect(false, "standard error is redirected for fallback capture");
        if (::close(savedError) != 0 || ::close(pipeDescriptors[0]) != 0 ||
            ::close(pipeDescriptors[1]) != 0) {
            run.expect(false, "fallback capture descriptors close after redirect failure");
        }
        return captured;
    }
    bool resourcesSucceeded = true;
    if (::close(pipeDescriptors[1]) != 0)
        resourcesSucceeded = false;
    gWriteBehavior = behavior;
    gWriteCallCount = 0;
    captured.result = start();
    captured.writeCalls = gWriteCallCount;
    gWriteBehavior = WriteBehavior::Complete;
    if (::dup2(savedError, STDERR_FILENO) < 0)
        resourcesSucceeded = false;
    if (::close(savedError) != 0)
        resourcesSucceeded = false;
    while (captured.standardErrorSize < captured.standardError.size()) {
        const ssize_t count =
            ::read(pipeDescriptors[0], captured.standardError.data() + captured.standardErrorSize,
                   captured.standardError.size() - captured.standardErrorSize);
        if (count > 0) {
            captured.standardErrorSize += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            resourcesSucceeded = false;
        break;
    }
    if (::close(pipeDescriptors[0]) != 0)
        resourcesSucceeded = false;
    run.expect(resourcesSucceeded, "fallback capture resources complete successfully");
    return captured;
}

std::string_view capturedError(const CapturedStart& captured) noexcept {
    return {captured.standardError.data(), captured.standardErrorSize};
}

void expectLoggedException(TestRun& run, int result, std::string_view expected) {
    run.expect(result == 4, "a standard exception maps to its stable C result");
    run.expect(gLogCallCount == 1, "a contained standard exception uses the canonical logger once");
    run.expect(gLogLevel == kue::LogLevel::Error,
               "a contained standard exception has error severity");
    run.expect(gLogFormatSucceeded, "a contained standard exception formats successfully");
    run.expect(loggedMessage() == expected,
               "a contained standard exception preserves its exact bounded diagnostic");
}

void runBootResultContracts(TestRun& run, EnvironmentVariable& behavior, StartFunction start) {
    if (!clearBehavior(run, behavior))
        return;
    resetLogCapture(false);
    gAllocationCount = 0;
    gMeasureAllocations = true;
    const int running = start();
    gMeasureAllocations = false;
    run.expect(running == 0, "a successful boot retains its typed C result");
    run.expect(gAllocationCount == 0, "the successful C entry boundary allocates no heap memory");
    run.expect(gLogCallCount == 0, "the C entry does not duplicate canonical boot logging");
    run.expect(gBootSawCompleteConstructor,
               "boot begins only after shared-object constructors complete");

    constexpr std::array behaviors = {"configuration-failure", "logging-failure",
                                      "state-start-failure"};
    constexpr std::array results = {1, 2, 3};
    for (std::size_t index = 0; index < behaviors.size(); ++index) {
        if (!assignBehavior(run, behavior, behaviors[index]))
            return;
        resetLogCapture(false);
        run.expect(start() == results[index], "each boot failure retains its typed C result");
        run.expect(gLogCallCount == 0, "a returned boot failure is not logged twice by the entry");
    }
}

void runExceptionContracts(TestRun& run, EnvironmentVariable& behavior, StartFunction start) {
    if (!assignBehavior(run, behavior, "standard-exception"))
        return;
    resetLogCapture(false);
    const CapturedStart standard = captureStart(run, start, WriteBehavior::Complete);
    expectLoggedException(run, standard.result,
                          "start failed with standard exception: fixture standard failure");
    run.expect(capturedError(standard).empty(),
               "standard error fallback stays silent when canonical logging succeeds");
    run.expect(standard.writeCalls == 0,
               "canonical exception logging performs no direct entry-boundary write");

    if (!assignBehavior(run, behavior, "maximum-exception"))
        return;
    resetLogCapture(false);
    const int maximum = start();
    std::string maximumExpected = "start failed with standard exception: ";
    maximumExpected.append(kMaximumExceptionDetailBytes, 'x');
    expectLoggedException(run, maximum, maximumExpected);

    if (!assignBehavior(run, behavior, "oversized-exception"))
        return;
    resetLogCapture(false);
    expectLoggedException(run, start(),
                          "start failed with standard exception: "
                          "<detail exceeds 1024-byte limit>");

    if (!assignBehavior(run, behavior, "invalid-exception"))
        return;
    resetLogCapture(false);
    expectLoggedException(run, start(),
                          "start failed with standard exception: "
                          "<detail is not valid UTF-8>");

    if (!assignBehavior(run, behavior, "unknown-exception"))
        return;
    resetLogCapture(false);
    const CapturedStart unknown = captureStart(run, start, WriteBehavior::Complete);
    run.expect(unknown.result == 5, "an unknown exception maps to its stable C result");
    run.expect(gLogCallCount == 1, "a contained unknown exception uses the canonical logger once");
    run.expect(gLogLevel == kue::LogLevel::Error,
               "a contained unknown exception has error severity");
    run.expect(gLogFormatSucceeded && loggedMessage() == "start failed with unknown exception",
               "an unknown exception has its exact canonical diagnostic");
    run.expect(capturedError(unknown).empty(),
               "unknown-exception fallback stays silent when canonical logging succeeds");
}

void runFallbackContract(TestRun& run, EnvironmentVariable& behavior, StartFunction start,
                         const char* bootBehavior, WriteBehavior writeBehavior, int expectedResult,
                         std::string_view expectedRecord, std::size_t expectedWrites) {
    if (!assignBehavior(run, behavior, bootBehavior))
        return;
    resetLogCapture(true);
    const CapturedStart captured = captureStart(run, start, writeBehavior);
    run.expect(captured.result == expectedResult,
               "a logging failure does not replace the contained exception result");
    run.expect(gLogCallCount == 1, "the failed canonical log operation is attempted exactly once");
    run.expect(capturedError(captured) == expectedRecord,
               "a thrown log operation emits the exact bounded fallback record");
    run.expect(captured.writeCalls == expectedWrites,
               "fallback delivery has the exact checked write behavior");
}

void runFallbackContracts(TestRun& run, EnvironmentVariable& behavior, StartFunction start) {
    runFallbackContract(run, behavior, start, "standard-exception", WriteBehavior::Complete, 4,
                        kStandardFallback, 1);
    runFallbackContract(run, behavior, start, "unknown-exception", WriteBehavior::Complete, 5,
                        kUnknownFallback, 1);
    runFallbackContract(run, behavior, start, "standard-exception", WriteBehavior::InterruptOnce, 4,
                        kStandardFallback, 2);
    runFallbackContract(run, behavior, start, "standard-exception", WriteBehavior::PartialOnce, 4,
                        kStandardFallback, 2);
    runFallbackContract(run, behavior, start, "oversized-exception", WriteBehavior::Complete, 4,
                        "[kue] loader: start failed with standard exception: "
                        "<detail exceeds 1024-byte limit>; logging failed\n",
                        1);

    if (!assignBehavior(run, behavior, "standard-exception"))
        return;
    resetLogCapture(true);
    const pid_t child = ::fork();
    if (child < 0) {
        run.expect(false, "the failed-fallback check forks");
        return;
    }
    if (child == 0) {
        std::set_terminate([] { ::_exit(88); });
        gWriteBehavior = WriteBehavior::Fail;
        static_cast<void>(start());
        ::_exit(89);
    }
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    run.expect(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 88,
               "an undeliverable fallback terminates instead of swallowing the failure");
}

void runModuleContracts(TestRun& run, const char* modulePath, EnvironmentVariable& behavior) {
    ::dlerror();
    void* module = ::dlopen(modulePath, RTLD_NOW | RTLD_GLOBAL);
    const char* const loadError = ::dlerror();
    run.expect(module != nullptr, "the module-entry fixture loads");
    if (!module) {
        if (loadError)
            std::cerr << "FAIL: " << loadError << '\n';
        return;
    }
    run.expect(gConstructorComplete, "shared-object constructors finish before dlopen returns");
    run.expect(gBootCallCount == 0, "loading the module does not start production work");

    ::dlerror();
    void* const startSymbol = ::dlsym(module, "kue_start");
    const char* const symbolError = ::dlerror();
    run.expect(symbolError == nullptr, "the canonical start symbol resolves without an error");
    run.expect(startSymbol != nullptr, "the canonical start symbol has a callable address");
    const StartFunction start =
        startSymbol ? reinterpret_cast<StartFunction>(startSymbol) : StartFunction{};
    if (start) {
        runBootResultContracts(run, behavior, start);
        runExceptionContracts(run, behavior, start);
        runFallbackContracts(run, behavior, start);
    }
    run.expect(gBootCallCount == 14, "every requested start reaches the fixture boot boundary");
    run.expect(::dlclose(module) == 0, "the fixture module closes cleanly");
}

}

void* operator new(std::size_t bytes) {
    return allocate(bytes);
}

void* operator new[](std::size_t bytes) {
    return allocate(bytes);
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocateAligned(bytes, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return allocateAligned(bytes, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory) noexcept {
    deallocate(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    deallocate(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    deallocate(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    deallocate(memory);
}

extern "C" [[gnu::visibility("default"), gnu::used]] ssize_t
write(int descriptor, const void* bytes, std::size_t count) {
    ++gWriteCallCount;
    if (gWriteBehavior == WriteBehavior::InterruptOnce && gWriteCallCount == 1) {
        errno = EINTR;
        return -1;
    }
    if (gWriteBehavior == WriteBehavior::Fail) {
        errno = EIO;
        return -1;
    }
    if (gWriteBehavior == WriteBehavior::PartialOnce && gWriteCallCount == 1 && count > 1)
        count /= 2;
    return static_cast<ssize_t>(::syscall(SYS_write, descriptor, bytes, count));
}

namespace kue {

[[gnu::visibility("default")]] void logFormat(LogLevel level, const char* format, ...) {
    ++gLogCallCount;
    gLogLevel = level;
    if (gLogThrows)
        throw std::runtime_error("fixture log failure");
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(gLogMessage.data(), gLogMessage.size(), format, arguments);
    va_end(arguments);
    gLogFormatSucceeded = length >= 0 && static_cast<std::size_t>(length) < gLogMessage.size();
    gLogMessageSize = gLogFormatSucceeded ? static_cast<std::size_t>(length) : 0;
}

}

extern "C" [[gnu::visibility("default")]] void kueFixtureCompleteConstructor() {
    gConstructorComplete = true;
}

extern "C" [[gnu::visibility("default")]] void kueFixtureRecordBoot() {
    ++gBootCallCount;
    gBootSawCompleteConstructor = gConstructorComplete;
}

extern "C" [[gnu::visibility("default"), noreturn]] void kueFixtureThrowMaximumException() {
    throw std::runtime_error(std::string(kMaximumExceptionDetailBytes, 'x'));
}

extern "C" [[gnu::visibility("default"), noreturn]] void kueFixtureThrowOversizedException() {
    throw std::runtime_error(std::string(kMaximumExceptionDetailBytes + 1, 'x'));
}

extern "C" [[gnu::visibility("default"), noreturn]] void kueFixtureThrowInvalidException() {
    throw std::runtime_error(std::string{static_cast<char>(0xc0), 'x'});
}

int main(int argc, char** argv) {
    TestRun run;
    run.expect(argc == 2, "the fixture shared object path is provided");
    if (argc == 2) {
        EnvironmentVariable behavior(run, "KUE_FIXTURE_BOOT");
        runModuleContracts(run, argv[1], behavior);
    }
    return run.result();
}
