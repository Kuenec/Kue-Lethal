#include "main.h"

#include "core/Config.h"
#include "core/Log.h"
#include "game/LethalState.h"
#include "overlay/InternalHud.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <latch>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <type_traits>

namespace main_boot_test {

thread_local bool measureScalarAllocation = false;
thread_local std::size_t largestScalarAllocation = 0;

void recordScalarAllocation(std::size_t bytes) noexcept {
    if (measureScalarAllocation && bytes > largestScalarAllocation)
        largestScalarAllocation = bytes;
}

}

struct alignas(std::max_align_t) ScalarAllocationHeader {
    std::size_t mappingBytes;
};

void* operator new(std::size_t bytes) {
    main_boot_test::recordScalarAllocation(bytes);
    const std::size_t payloadBytes = bytes == 0 ? 1 : bytes;
    if (payloadBytes > std::numeric_limits<std::size_t>::max() - sizeof(ScalarAllocationHeader))
        throw std::bad_alloc{};
    const std::size_t mappingBytes = sizeof(ScalarAllocationHeader) + payloadBytes;
    void* const mapping =
        ::mmap(nullptr, mappingBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
        throw std::bad_alloc{};
    auto* const header = static_cast<ScalarAllocationHeader*>(mapping);
    header->mappingBytes = mappingBytes;
    return header + 1;
}

void operator delete(void* memory) noexcept {
    if (!memory)
        return;
    auto* const header = static_cast<ScalarAllocationHeader*>(memory) - 1;
    const std::size_t mappingBytes = header->mappingBytes;
    if (::munmap(header, mappingBytes) != 0)
        std::terminate();
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

enum class ConfigBehavior : std::uint8_t { FailAfterMutation, Succeed };
enum class StartBehavior : std::uint8_t { Fail, Succeed };

std::atomic<ConfigBehavior> gConfigBehavior{ConfigBehavior::FailAfterMutation};
std::atomic<StartBehavior> gStartBehavior{StartBehavior::Fail};
std::atomic<kue::LethalWorkerState> gWorkerState{kue::LethalWorkerState::Ready};
std::atomic<bool> gLogInitializationSucceeds{false};
std::atomic<bool> gSawFreshConfig{false};
std::atomic<bool> gActivated{false};
std::atomic<bool> gLoggedAfterActivation{false};
std::atomic<int> gConfigLoads{0};
std::atomic<int> gStartCalls{0};
std::atomic<int> gLogInitializationCalls{0};
std::latch* gStartEntered = nullptr;
std::latch* gStartRelease = nullptr;

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

}

namespace kue {

bool configLoad(Config& config, ConfigFileOperation operation) {
    std::string& error = operation.error;
    ++gConfigLoads;
    if (gConfigBehavior.load() == ConfigBehavior::FailAfterMutation) {
        config.pollRate = 88.f;
        error = "fixture configuration failure";
        return false;
    }
    constexpr float canonicalPollRate = 30.f;
    static_assert(sizeof(canonicalPollRate) == sizeof(std::uint32_t));
    std::uint32_t actualPollRateBits = 0;
    std::uint32_t canonicalPollRateBits = 0;
    std::memcpy(&actualPollRateBits, &config.pollRate, sizeof(actualPollRateBits));
    std::memcpy(&canonicalPollRateBits, &canonicalPollRate, sizeof(canonicalPollRateBits));
    gSawFreshConfig = actualPollRateBits == canonicalPollRateBits;
    config.filePath = "fixture.json";
    error.clear();
    return true;
}

bool logInit(std::string_view) {
    ++gLogInitializationCalls;
    return gLogInitializationSucceeds.load();
}

void logFormat(LogLevel, const char*, ...) {
    if (gActivated.load())
        gLoggedAfterActivation = true;
}

LethalState::LethalState() = default;
LethalState::~LethalState() = default;

void LethalState::apply(const Config&) {}

void LethalState::stop() {
    gWorkerState = LethalWorkerState::Stopped;
}

bool LethalState::start() {
    ++gStartCalls;
    if (gStartBehavior.load() == StartBehavior::Fail)
        return false;
    gWorkerState = LethalWorkerState::Preparing;
    if (gStartEntered)
        gStartEntered->count_down();
    if (gStartRelease)
        gStartRelease->wait();
    gWorkerState = LethalWorkerState::Running;
    gActivated = true;
    return true;
}

LethalWorkerState LethalState::workerState() const noexcept {
    return gWorkerState.load();
}

namespace internalhud {

void initialize(LethalState&, Config&) {}

}

}

int main() {
    TestRun run;

    static_assert(std::is_same_v<decltype(&kue::logInit), bool (*)(std::string_view)>);
    static_assert(
        std::is_same_v<decltype(kue::logFormat(kue::LogLevel::Info, "%s", "value")), void>);
    const std::string oversizedLogPath(kue::kMaximumLogPathBytes + 1, 'x');
    if (::setenv("KUE_LOG", oversizedLogPath.c_str(), 1) != 0) {
        run.expect(false, "oversized KUE_LOG fixture is installed");
        return run.result();
    }
    gConfigBehavior = ConfigBehavior::Succeed;
    gLogInitializationSucceeds = true;
    gLogInitializationCalls = 0;
    main_boot_test::largestScalarAllocation = 0;
    main_boot_test::measureScalarAllocation = true;
    const kue::BootResult oversizedLogResult = kue::boot();
    main_boot_test::measureScalarAllocation = false;
    run.expect(oversizedLogResult == kue::BootResult::LoggingFailed,
               "oversized KUE_LOG fails before worker startup");
    run.expect(gLogInitializationCalls.load() == 0,
               "oversized KUE_LOG never reaches the logging sink boundary");
    run.expect(main_boot_test::largestScalarAllocation <= kue::kMaximumLogPathBytes,
               "oversized KUE_LOG is rejected before an owning allocation");

    if (::setenv("KUE_LOG", "", 1) != 0) {
        run.expect(false, "empty KUE_LOG fixture is installed");
        return run.result();
    }
    gLogInitializationCalls = 0;
    run.expect(kue::boot() == kue::BootResult::LoggingFailed,
               "defined-empty KUE_LOG is invalid rather than a console request");
    run.expect(gLogInitializationCalls.load() == 0,
               "defined-empty KUE_LOG never reaches logger initialization");

    const std::array<char, 3> invalidUtf8LogPath = {static_cast<char>(0xc0), 'x', '\0'};
    if (::setenv("KUE_LOG", invalidUtf8LogPath.data(), 1) != 0) {
        run.expect(false, "invalid UTF-8 KUE_LOG fixture is installed");
        return run.result();
    }
    gLogInitializationCalls = 0;
    run.expect(kue::boot() == kue::BootResult::LoggingFailed,
               "ill-formed UTF-8 KUE_LOG fails before worker startup");
    run.expect(gLogInitializationCalls.load() == 0,
               "ill-formed UTF-8 KUE_LOG never reaches logger initialization");
    if (::unsetenv("KUE_LOG") != 0) {
        run.expect(false, "KUE_LOG fixture is removed");
        return run.result();
    }
    gConfigBehavior = ConfigBehavior::FailAfterMutation;
    gLogInitializationSucceeds = false;
    gConfigLoads = 0;
    gStartCalls = 0;
    gLogInitializationCalls = 0;
    gSawFreshConfig = false;

    run.expect(kue::boot() == kue::BootResult::ConfigurationFailed,
               "configuration failure has a typed result");
    gConfigBehavior = ConfigBehavior::Succeed;
    run.expect(kue::boot() == kue::BootResult::LoggingFailed, "logging failure has a typed result");
    run.expect(gSawFreshConfig.load(), "retry starts from canonical configuration defaults");

    gLogInitializationSucceeds = true;
    run.expect(kue::boot() == kue::BootResult::StateStartFailed,
               "worker launch failure has a typed result");
    run.expect(gStartCalls.load() == 1, "failed worker start is attempted exactly once");

    gStartBehavior = StartBehavior::Succeed;
    std::latch startEntered(1);
    std::latch startRelease(1);
    gStartEntered = &startEntered;
    gStartRelease = &startRelease;
    std::atomic<bool> bootReturned{false};
    kue::BootResult successfulBoot = kue::BootResult::StateStartFailed;
    std::thread bootThread([&successfulBoot, &bootReturned] {
        successfulBoot = kue::boot();
        bootReturned = true;
    });
    startEntered.wait();
    run.expect(!bootReturned.load(), "boot cannot return before worker start completes");
    run.expect(gWorkerState.load() == kue::LethalWorkerState::Preparing,
               "boot remains inside the sole worker-start boundary");
    startRelease.count_down();
    bootThread.join();
    gStartEntered = nullptr;
    gStartRelease = nullptr;
    run.expect(successfulBoot == kue::BootResult::Running,
               "successful worker start commits running");
    run.expect(!gLoggedAfterActivation.load(),
               "no potentially throwing log follows the worker-start boundary");
    run.expect(gStartCalls.load() == 2, "successful retry starts exactly one worker");

    constexpr std::size_t callers = 8;
    std::array<kue::BootResult, callers> results{};
    std::array<std::thread, callers> threads;
    for (std::size_t index = 0; index < callers; ++index)
        threads[index] = std::thread([&results, index] { results[index] = kue::boot(); });
    for (std::thread& thread : threads)
        thread.join();
    bool allRunning = true;
    for (const kue::BootResult result : results)
        allRunning = allRunning && result == kue::BootResult::Running;
    run.expect(allRunning, "concurrent repeated boot calls share the running result");
    run.expect(gStartCalls.load() == 2, "repeated boot creates no additional worker");

    gWorkerState = kue::LethalWorkerState::Failed;
    run.expect(kue::boot() == kue::BootResult::StateStartFailed,
               "dead worker is never reported as running");
    run.expect(gConfigLoads.load() == 4, "committed boot performs no configuration retry");

    return run.result();
}
