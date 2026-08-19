#include "game/LethalState.h"

#include "core/Log.h"
#include "mono/UnityMetadata.h"
#include "overlay/InternalHud.h"

#include <array>
#include <atomic>
#include <barrier>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <latch>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

namespace {

enum class RuntimeBehavior : std::uint8_t { Block, Throw, Unavailable };

static_assert(std::is_same_v<decltype(kue::PlayerFrameSnapshot::players), kue::PlayerSnapshot>);
static_assert(std::is_same_v<decltype(&kue::LethalState::readPlayerFrame),
                             kue::PlayerFrameSnapshot (kue::LethalState::*)() const>);

std::atomic<RuntimeBehavior> gRuntimeBehavior{RuntimeBehavior::Block};
std::latch* gWorkerEntered = nullptr;
std::latch* gWorkerRelease = nullptr;
std::atomic<int> gRuntimeCalls{0};
std::atomic<int> gDetachCalls{0};
std::atomic<bool> gDetachThrows{false};
std::atomic<bool> gLogThrows{false};
std::atomic<bool> gRuntimeObservedRunning{true};
std::atomic<kue::LethalState*> gObservedState{nullptr};
std::array<char, 256> gLastLog{};
std::array<std::array<char, 256>, 8> gErrorLogs{};
std::atomic<std::size_t> gErrorLogCount{0};

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

struct CapturedStderr {
    std::array<char, 4096> bytes{};
    std::size_t size = 0;
    std::string error;
    bool succeeded = false;

    std::string_view text() const noexcept { return {bytes.data(), size}; }
};

class StderrCapture final {
  public:
    StderrCapture() = default;

    ~StderrCapture() {
        if (mStream || mSavedDescriptor >= 0)
            std::terminate();
    }

    bool begin(std::string& error) {
        error.clear();
        errno = 0;
        mStream = std::tmpfile();
        if (!mStream) {
            appendFailure(error, "cannot create stderr capture", errno != 0 ? errno : EIO);
            return false;
        }
        errno = 0;
        if (std::fflush(stderr) != 0) {
            appendFailure(error, "cannot flush stderr before capture", errno != 0 ? errno : EIO);
            releaseUnredirected(error);
            return false;
        }
        errno = 0;
        const int captureDescriptor = ::fileno(mStream);
        if (captureDescriptor < 0) {
            appendFailure(error, "cannot obtain stderr capture descriptor",
                          errno != 0 ? errno : EBADF);
            releaseUnredirected(error);
            return false;
        }
        mSavedDescriptor = duplicateDescriptor(STDERR_FILENO);
        if (mSavedDescriptor < 0) {
            appendFailure(error, "cannot duplicate stderr", errno != 0 ? errno : EIO);
            releaseUnredirected(error);
            return false;
        }
        if (replaceDescriptor({.source = captureDescriptor, .destination = STDERR_FILENO}) < 0) {
            appendFailure(error, "cannot redirect stderr", errno != 0 ? errno : EIO);
            releaseUnredirected(error);
            return false;
        }
        return true;
    }

    CapturedStderr finish() {
        CapturedStderr result;
        result.succeeded = true;
        errno = 0;
        if (std::fflush(stderr) != 0) {
            appendFailure(result.error, "cannot flush captured stderr", errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
        if (replaceDescriptor({.source = mSavedDescriptor, .destination = STDERR_FILENO}) < 0)
            std::terminate();
        if (::close(mSavedDescriptor) != 0) {
            appendFailure(result.error, "cannot close saved stderr", errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
        mSavedDescriptor = -1;

        errno = 0;
        if (std::fseek(mStream, 0, SEEK_END) != 0) {
            appendFailure(result.error, "cannot seek to captured stderr end",
                          errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
        errno = 0;
        const long end = result.succeeded ? std::ftell(mStream) : -1;
        if (result.succeeded && end < 0) {
            appendFailure(result.error, "cannot determine captured stderr size",
                          errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
        if (result.succeeded && static_cast<unsigned long>(end) > result.bytes.size()) {
            result.error = "captured stderr exceeds the 4096-byte fixture bound";
            result.succeeded = false;
        }
        if (result.succeeded) {
            result.size = static_cast<std::size_t>(end);
            errno = 0;
            if (std::fseek(mStream, 0, SEEK_SET) != 0) {
                appendFailure(result.error, "cannot seek to captured stderr beginning",
                              errno != 0 ? errno : EIO);
                result.succeeded = false;
            } else if (result.size != 0 &&
                       std::fread(result.bytes.data(), 1, result.size, mStream) != result.size) {
                appendFailure(result.error, "cannot read complete captured stderr",
                              errno != 0 ? errno : EIO);
                result.succeeded = false;
            }
        }
        FILE* const stream = mStream;
        mStream = nullptr;
        errno = 0;
        if (std::fclose(stream) != 0) {
            appendFailure(result.error, "cannot close captured stderr", errno != 0 ? errno : EIO);
            result.succeeded = false;
        }
        return result;
    }

    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

  private:
    static void appendFailure(std::string& error, std::string_view operation, int code) {
        if (!error.empty())
            error += "; ";
        error.append(operation);
        error += ": ";
        error += std::strerror(code);
    }

    static int duplicateDescriptor(int descriptor) noexcept {
        int result = -1;
        do {
            errno = 0;
            result = ::dup(descriptor);
        } while (result < 0 && errno == EINTR);
        return result;
    }

    struct DescriptorReplacement {
        int source;
        int destination;
    };

    static int replaceDescriptor(DescriptorReplacement replacement) noexcept {
        int result = -1;
        do {
            errno = 0;
            result = ::dup2(replacement.source, replacement.destination);
        } while (result < 0 && errno == EINTR);
        return result;
    }

    void releaseUnredirected(std::string& error) {
        if (mSavedDescriptor >= 0) {
            errno = 0;
            if (::close(mSavedDescriptor) != 0) {
                appendFailure(error, "cannot close saved stderr after capture failure",
                              errno != 0 ? errno : EIO);
            }
            mSavedDescriptor = -1;
        }
        if (mStream) {
            FILE* const stream = mStream;
            mStream = nullptr;
            errno = 0;
            if (std::fclose(stream) != 0) {
                appendFailure(error, "cannot close stderr capture after failure",
                              errno != 0 ? errno : EIO);
            }
        }
    }

    FILE* mStream = nullptr;
    int mSavedDescriptor = -1;
};

struct OccurrenceSearch {
    std::string_view text;
    std::string_view needle;
};

std::size_t occurrenceCount(OccurrenceSearch search) noexcept {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = search.text.find(search.needle, position)) != std::string_view::npos) {
        ++count;
        position += search.needle.size();
    }
    return count;
}

}

namespace kue {

void logFormat(LogLevel level, const char* format, ...) {
    if (gLogThrows.load())
        throw std::runtime_error("fixture log failure");
    std::array<char, 256> formatted{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(formatted.data(), formatted.size(), format, arguments);
    va_end(arguments);
    gLastLog = formatted;
    if (level == LogLevel::Error) {
        const std::size_t index = gErrorLogCount.fetch_add(1, std::memory_order_relaxed);
        if (index < gErrorLogs.size())
            gErrorLogs[index] = formatted;
    }
    if (written < 0 || static_cast<std::size_t>(written) >= formatted.size())
        throw std::runtime_error("fixture log formatting failure");
}

namespace mono {

bool ready() noexcept {
    return false;
}

bool resolve() {
    ++gRuntimeCalls;
    LethalState* const state = gObservedState.load();
    if (!state || state->workerState() != LethalWorkerState::Running)
        gRuntimeObservedRunning = false;
    gWorkerEntered->count_down();
    if (gRuntimeBehavior.load() == RuntimeBehavior::Throw)
        throw std::runtime_error("fixture runtime failure");
    if (gRuntimeBehavior.load() == RuntimeBehavior::Block)
        gWorkerRelease->wait();
    return false;
}

void detachCurrentThread() {
    ++gDetachCalls;
    if (gDetachThrows.load())
        throw std::runtime_error("fixture detach failure");
}

bool installManagedMethodCallback(MonoMethod*, MainThreadCallback, void*) {
    return false;
}

bool installCompiledMethodCallback(MonoMethod*, MainThreadCallback, void*) {
    return false;
}

void clearManagedMethodCallback() {}

MonoMethod* loadManagedMethod(ManagedMethodLocation) {
    return nullptr;
}

StaticInvocationResult invokeStatic(MonoMethod*) noexcept {
    return {};
}

FieldLookupResult findField(const MonoClass*, const char*) noexcept {
    return {};
}

MethodLookupResult findMethod(const MonoClass*, const char*, int) noexcept {
    return {};
}

bool readStaticObject(MonoClass*, const MonoClassField*, MonoObject*&) {
    return false;
}

bool readInstanceObject(MonoObject*, const MonoClassField*, MonoObject*&) {
    return false;
}

bool readInstanceFloat(MonoObject*, const MonoClassField*, float&) {
    return false;
}

bool readInstanceInt(MonoObject*, const MonoClassField*, int&) {
    return false;
}

bool readInstanceBool(MonoObject*, const MonoClassField*, bool&) {
    return false;
}

bool readInstanceU64(MonoObject*, const MonoClassField*, std::uint64_t&) {
    return false;
}

ManagedStringUtf8Result readManagedStringUtf8(MonoObject*, Utf8Output) noexcept {
    return {};
}

const char* managedStringUtf8StatusName(ManagedStringUtf8Status) noexcept {
    return "fixture";
}

ReferenceArrayView::ReferenceArrayView(const MonoArray* array) noexcept : mArray(array) {
    if (!array) {
        mStatus = RuntimeArrayAccessStatus::NullArray;
        return;
    }
}

RuntimeArrayAccessStatus ReferenceArrayView::status() const noexcept {
    return mStatus;
}

std::size_t ReferenceArrayView::size() const noexcept {
    return mLength;
}

RuntimeArrayObjectResult ReferenceArrayView::object(std::size_t index) const noexcept {
    if (mStatus != RuntimeArrayAccessStatus::Success)
        return {mStatus, nullptr};
    if (!mArray)
        return {RuntimeArrayAccessStatus::NullArray, nullptr};
    if (index >= mLength)
        return {RuntimeArrayAccessStatus::IndexOutOfRange, nullptr};
    return {RuntimeArrayAccessStatus::AddressUnavailable, nullptr};
}

const char* runtimeArrayAccessStatusName(RuntimeArrayAccessStatus) noexcept {
    return "fixture";
}

}

namespace unity {

mono::ClassLookupResult gameClass(GameClassLocation) noexcept {
    return {};
}

bool gameClassLookupMayRetry(mono::ClassLookupStatus status) noexcept {
    return status == mono::ClassLookupStatus::RuntimeUnavailable ||
           status == mono::ClassLookupStatus::DomainUnavailable ||
           status == mono::ClassLookupStatus::ImageUnavailable;
}

const char* gameClassLookupStatusName(mono::ClassLookupStatus status) noexcept {
    switch (status) {
    case mono::ClassLookupStatus::Resolved:
        return "resolved";
    case mono::ClassLookupStatus::InvalidLocation:
        return "invalid class location";
    case mono::ClassLookupStatus::RuntimeUnavailable:
        return "runtime unavailable";
    case mono::ClassLookupStatus::DomainUnavailable:
        return "domain unavailable";
    case mono::ClassLookupStatus::ImageUnavailable:
        return "image unavailable";
    case mono::ClassLookupStatus::ClassUnavailable:
        return "class unavailable";
    }
    return "invalid class lookup status";
}

MetadataLookupResult<mono::MonoMethod> cachedMethod(const mono::MonoClass*, const char*, int) {
    return {};
}

MetadataLookupResult<mono::MonoClassField> cachedField(const mono::MonoClass*, const char*) {
    return {};
}

const char* metadataLookupStatusName(MetadataLookupStatus) noexcept {
    return "fixture";
}

}

namespace internalhud {

bool registerManagedBridge() {
    return false;
}

}

}

namespace {

void testGameClassFailureReporter(TestRun& run) {
    using kue::GameClassKind;
    using kue::mono::ClassLookupStatus;
    static_assert(kue::GameClassFailureReporter::kCapacity == 20);
    static_assert(sizeof(kue::GameClassFailureReporter) <= 64);
    static_assert(!std::is_polymorphic_v<kue::GameClassFailureReporter>);
    static_assert(std::is_trivially_destructible_v<kue::GameClassFailureReporter>);

    kue::GameClassFailureReporter firstSession;
    kue::GameClassFailureReporter freshSession;
    kue::GameClassFailureReporter fullSession;
    gErrorLogCount = 0;
    for (auto& record : gErrorLogs)
        record.fill('\0');
    firstSession.report(GameClassKind::Player, ClassLookupStatus::RuntimeUnavailable);
    firstSession.report(GameClassKind::Player, ClassLookupStatus::RuntimeUnavailable);
    firstSession.report(GameClassKind::Player, ClassLookupStatus::ImageUnavailable);
    firstSession.report(GameClassKind::Player, ClassLookupStatus::ClassUnavailable);
    firstSession.report(GameClassKind::Network, ClassLookupStatus::RuntimeUnavailable);
    firstSession.report(GameClassKind::Network, ClassLookupStatus::Resolved);
    const std::size_t distinctLogCount = gErrorLogCount.load(std::memory_order_relaxed);
    const std::array<char, 256> runtimeLog = gErrorLogs[0];
    const std::array<char, 256> imageLog = gErrorLogs[1];
    const std::array<char, 256> classLog = gErrorLogs[2];

    gErrorLogCount = 0;
    firstSession.reset();
    firstSession.report(GameClassKind::Player, ClassLookupStatus::RuntimeUnavailable);
    const std::size_t resetLogCount = gErrorLogCount.load(std::memory_order_relaxed);

    gErrorLogCount = 0;
    freshSession.report(GameClassKind::Player, ClassLookupStatus::RuntimeUnavailable);
    const std::size_t freshLogCount = gErrorLogCount.load(std::memory_order_relaxed);

    constexpr std::array kinds = {GameClassKind::Player, GameClassKind::Network,
                                  GameClassKind::Round, GameClassKind::Menu};
    constexpr std::array failureStatuses = {
        ClassLookupStatus::InvalidLocation, ClassLookupStatus::RuntimeUnavailable,
        ClassLookupStatus::DomainUnavailable, ClassLookupStatus::ImageUnavailable,
        ClassLookupStatus::ClassUnavailable};
    gErrorLogCount = 0;
    for (const GameClassKind kind : kinds) {
        for (const ClassLookupStatus status : failureStatuses)
            fullSession.report(kind, status);
    }
    const std::size_t fullLogCount = gErrorLogCount.load(std::memory_order_relaxed);
    fullSession.report(GameClassKind::Menu, ClassLookupStatus::ClassUnavailable);
    const std::size_t duplicateAfterFullCount = gErrorLogCount.load(std::memory_order_relaxed);

    run.expect(distinctLogCount == 4,
               "distinct class requirements and statuses are visible while exact repeats and "
               "success are silent");
    run.expect(
        std::string_view(runtimeLog.data())
                .find("image=Assembly-CSharp namespace=GameNetcodeStuff class=PlayerControllerB "
                      "status=runtime unavailable") != std::string_view::npos,
        "a runtime class failure preserves its exact location and cause");
    run.expect(std::string_view(imageLog.data()).find("status=image unavailable") !=
                   std::string_view::npos,
               "an image failure remains distinct in the diagnostic ledger");
    run.expect(std::string_view(classLog.data()).find("status=class unavailable") !=
                   std::string_view::npos,
               "a missing class remains distinct from an image failure");
    run.expect(resetLogCount == 1,
               "reset begins a new class-diagnostic session without retained suppression");
    run.expect(freshLogCount == 1, "a fresh class-diagnostic session inherits no suppression");
    run.expect(fullLogCount == kue::GameClassFailureReporter::kCapacity,
               "the bounded ledger holds every reachable class and failure-status pair");
    run.expect(duplicateAfterFullCount == fullLogCount,
               "a duplicate stays suppressed after every ledger slot is occupied");
}

void testMetadataFailureReporter(TestRun& run) {
    static_assert(kue::MetadataFailureReporter::kCapacity == 21);
    static_assert(sizeof(kue::MetadataFailureReporter) <= 1024);
    static_assert(!std::is_polymorphic_v<kue::MetadataFailureReporter>);
    static_assert(std::is_trivially_destructible_v<kue::MetadataFailureReporter>);
    std::array<std::uintptr_t, kue::MetadataFailureReporter::kCapacity + 1> typeStorage{};
    const auto typeAt = [&typeStorage](std::size_t index) {
        return reinterpret_cast<kue::mono::MonoClass*>(&typeStorage[index]);
    };
    kue::MetadataFailureReporter firstSession;
    kue::MetadataFailureReporter freshSession;
    kue::MetadataFailureReporter fullSession;
    gErrorLogCount = 0;
    for (auto& record : gErrorLogs)
        record.fill('\0');
    firstSession.report(kue::MetadataMemberKind::Method, typeAt(0), "Update",
                        kue::unity::MetadataLookupStatus::RuntimeUnavailable);
    firstSession.report(kue::MetadataMemberKind::Field, typeAt(0), "health",
                        kue::unity::MetadataLookupStatus::RuntimeUnavailable);
    firstSession.report(kue::MetadataMemberKind::Field, typeAt(0), "health",
                        kue::unity::MetadataLookupStatus::RuntimeUnavailable);
    firstSession.report(kue::MetadataMemberKind::Field, typeAt(0), "health",
                        kue::unity::MetadataLookupStatus::MissingMember);
    const std::size_t distinctLogCount = gErrorLogCount.load(std::memory_order_relaxed);
    const std::array<char, 256> methodLog = gErrorLogs[0];
    const std::array<char, 256> fieldLog = gErrorLogs[1];

    gErrorLogCount.store(0, std::memory_order_relaxed);
    firstSession.reset();
    firstSession.report(kue::MetadataMemberKind::Method, typeAt(0), "Update",
                        kue::unity::MetadataLookupStatus::RuntimeUnavailable);
    const std::size_t resetSessionLogCount = gErrorLogCount.load(std::memory_order_relaxed);

    gErrorLogCount.store(0, std::memory_order_relaxed);
    freshSession.report(kue::MetadataMemberKind::Method, typeAt(0), "Update",
                        kue::unity::MetadataLookupStatus::RuntimeUnavailable);
    const std::size_t freshSessionLogCount = gErrorLogCount.load(std::memory_order_relaxed);

    gErrorLogCount.store(0, std::memory_order_relaxed);
    for (std::size_t index = 0; index < kue::MetadataFailureReporter::kCapacity; ++index) {
        fullSession.report(kue::MetadataMemberKind::Field, typeAt(index), "health",
                           kue::unity::MetadataLookupStatus::MissingMember);
    }
    fullSession.report(kue::MetadataMemberKind::Field,
                       typeAt(kue::MetadataFailureReporter::kCapacity), "health",
                       kue::unity::MetadataLookupStatus::MissingMember);
    const std::size_t capacityLogCount = gErrorLogCount.load(std::memory_order_relaxed);
    const std::array<char, 256> capacityLog = gLastLog;
    fullSession.report(kue::MetadataMemberKind::Field, typeAt(0), "health",
                       kue::unity::MetadataLookupStatus::MissingMember);
    const std::size_t duplicateAfterCapacityLogCount =
        gErrorLogCount.load(std::memory_order_relaxed);

    run.expect(distinctLogCount == 3,
               "distinct same-status requirements and distinct statuses are visible while an "
               "exact repeat is deduplicated");
    run.expect(std::string_view(methodLog.data()).find("method metadata lookup failed") !=
                   std::string_view::npos,
               "the typed method failure is preserved");
    run.expect(std::string_view(fieldLog.data()).find("field metadata lookup failed") !=
                   std::string_view::npos,
               "the typed field failure is preserved");
    run.expect(resetSessionLogCount == 1,
               "reset begins a new diagnostic session without retained suppression");
    run.expect(freshSessionLogCount == 1,
               "a fresh session does not inherit another session's diagnostic ledger");
    run.expect(capacityLogCount == kue::MetadataFailureReporter::kCapacity + 1,
               "every reachable ledger entry and an exhaustion diagnostic remain visible");
    run.expect(std::string_view(capacityLog.data()).find("diagnostic-ledger-capacity=21") !=
                   std::string_view::npos,
               "ledger exhaustion reports its exact capacity");
    run.expect(duplicateAfterCapacityLogCount == capacityLogCount,
               "a known duplicate remains deduplicated after the ledger reaches capacity");
}

void testInitialPlayerFrameAndStopBeforeStart(TestRun& run,
                                              std::optional<kue::LethalState>& stateStorage) {
    kue::LethalState& state = stateStorage.emplace();
    std::latch entered(1);
    std::latch release(1);
    gWorkerEntered = &entered;
    gWorkerRelease = &release;
    gRuntimeBehavior = RuntimeBehavior::Block;
    gRuntimeCalls = 0;
    gDetachCalls = 0;
    gDetachThrows = false;
    gLogThrows = false;
    gRuntimeObservedRunning = true;
    gObservedState = &state;

    const kue::PlayerFrameSnapshot snapshot = state.readPlayerFrame();
    run.expect(snapshot.players.empty(), "an initial menu snapshot has no players");
    run.expect(std::fpclassify(snapshot.sprintMeter) == FP_ZERO &&
                   std::fpclassify(snapshot.carryWeight) == FP_ZERO,
               "an initial menu snapshot preserves canonical local-player values");
    state.stop();
    run.expect(state.workerState() == kue::LethalWorkerState::Stopped,
               "stop before start closes the one-shot lifecycle");
    run.expect(!state.start(), "a stopped lifecycle cannot create a worker");
    run.expect(gRuntimeCalls.load() == 0, "stop before start performs no runtime work");
    run.expect(gDetachCalls.load() == 0, "a worker that never existed is never detached");
    gObservedState = nullptr;
    stateStorage.reset();
}

void testWorkerFallbackSinkFailure(TestRun& run) {
    constexpr int terminateExitStatus = 86;
    errno = 0;
    const pid_t child = ::fork();
    if (child < 0) {
        run.expect(false, "cannot fork worker fallback sink fixture: " +
                              std::string(std::strerror(errno)));
        return;
    }
    if (child == 0) {
        if (!std::freopen("/dev/full", "w", stderr))
            ::_Exit(81);
        std::set_terminate([] { ::_Exit(terminateExitStatus); });
        kue::LethalState state;
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Throw;
        gDetachThrows = false;
        gLogThrows = true;
        gObservedState = &state;
        if (!state.start())
            ::_Exit(82);
        entered.wait();
        state.stop();
        ::_Exit(83);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        errno = 0;
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        run.expect(false, "worker fallback sink fixture is reaped exactly once");
        return;
    }
    run.expect(true, "worker fallback sink fixture is reaped exactly once");
    run.expect(WIFEXITED(status) && WEXITSTATUS(status) == terminateExitStatus,
               "worker failure terminates when both logging and its direct fallback sink fail");
}

}

int main() {
    TestRun run;
    std::optional<kue::LethalState> stateStorage;

    testGameClassFailureReporter(run);
    testMetadataFailureReporter(run);
    testInitialPlayerFrameAndStopBeforeStart(run, stateStorage);

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Unavailable;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = false;
        gLogThrows = false;
        gRuntimeObservedRunning = true;
        gObservedState = &state;

        run.expect(state.start(), "runtime-backoff fixture starts one worker");
        entered.wait();
        run.expect(gRuntimeObservedRunning.load(),
                   "runtime work begins only after the running commit");
        state.stop();
        run.expect(state.workerState() == kue::LethalWorkerState::Stopped,
                   "stop interrupts runtime backoff and joins the worker");
        run.expect(gDetachCalls.load() == 1, "backoff worker detaches exactly once");
        gObservedState = nullptr;
        stateStorage.reset();
    }

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Block;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = false;
        gLogThrows = false;
        gRuntimeObservedRunning = true;
        gObservedState = &state;

        constexpr std::size_t callers = 8;
        std::barrier startLine(static_cast<std::ptrdiff_t>(callers));
        std::array<bool, callers> results{};
        std::array<std::thread, callers> threads;
        for (std::size_t index = 0; index < callers; ++index) {
            threads[index] = std::thread([&state, &startLine, &results, index] {
                startLine.arrive_and_wait();
                results[index] = state.start();
            });
        }
        for (std::thread& thread : threads)
            thread.join();
        std::size_t started = 0;
        for (const bool result : results) {
            if (result)
                ++started;
        }
        run.expect(started == 1, "concurrent start creates exactly one worker");
        entered.wait();
        run.expect(state.workerState() == kue::LethalWorkerState::Running,
                   "successful start publishes running state");
        run.expect(gRuntimeObservedRunning.load(),
                   "concurrent start exposes no pre-commit runtime work");

        std::barrier stopLine(2);
        std::array<std::thread, 2> stoppers;
        for (std::thread& stopper : stoppers) {
            stopper = std::thread([&state, &stopLine] {
                stopLine.arrive_and_wait();
                state.stop();
            });
        }
        release.count_down();
        for (std::thread& stopper : stoppers)
            stopper.join();
        run.expect(state.workerState() == kue::LethalWorkerState::Stopped,
                   "concurrent stop joins one worker safely");
        run.expect(gDetachCalls.load() == 1, "normal worker exit detaches exactly once");
        run.expect(!state.start(), "a stopped session cannot reuse retained runtime state");
        gObservedState = nullptr;
        stateStorage.reset();
    }

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Unavailable;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = false;
        gLogThrows = false;
        gRuntimeObservedRunning = true;
        gObservedState = &state;

        std::barrier startLine(2);
        bool started = false;
        std::thread starter([&state, &startLine, &started] {
            startLine.arrive_and_wait();
            started = state.start();
        });
        std::thread stopper([&state, &startLine] {
            startLine.arrive_and_wait();
            state.stop();
        });
        starter.join();
        stopper.join();
        run.expect(state.workerState() == kue::LethalWorkerState::Stopped,
                   "start-stop race joins the worker in stopped state");
        run.expect(started || gRuntimeCalls.load() == 0,
                   "a start that loses to stop performs no runtime work");
        run.expect(gDetachCalls.load() == (gRuntimeCalls.load() == 0 ? 0 : 1),
                   "start-stop race detaches only after runtime entry");
        gObservedState = nullptr;
        stateStorage.reset();
    }

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Throw;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = false;
        gLogThrows = false;
        gRuntimeObservedRunning = true;
        gObservedState = &state;
        gLastLog.fill('\0');

        StderrCapture stderrCapture;
        std::string captureError;
        if (!stderrCapture.begin(captureError)) {
            run.expect(false, captureError);
            return run.result();
        }
        const bool started = state.start();
        entered.wait();
        state.stop();
        const CapturedStderr captured = stderrCapture.finish();
        run.expect(started, "fault fixture starts one worker");
        run.expect(captured.succeeded, captured.error);
        run.expect(occurrenceCount(
                       {.text = captured.text(), .needle = "[kue] lethal worker failed:"}) == 0,
                   "successful logger delivery produces no direct stderr fallback duplicate");
        run.expect(state.workerState() == kue::LethalWorkerState::Failed,
                   "worker exception publishes failed state");
        run.expect(gDetachCalls.load() == 1, "failed worker detaches exactly once");
        run.expect(std::string_view(gLastLog.data()).find("fixture runtime failure") !=
                       std::string_view::npos,
                   "worker failure preserves the original exception cause");
        run.expect(gRuntimeObservedRunning.load(), "fault begins only after the running commit");
        run.expect(!state.start(), "failed worker cannot be reported as restarted");
        gObservedState = nullptr;
        stateStorage.reset();
    }

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Throw;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = false;
        gLogThrows = true;
        gRuntimeObservedRunning = true;
        gObservedState = &state;

        StderrCapture stderrCapture;
        std::string captureError;
        if (!stderrCapture.begin(captureError)) {
            run.expect(false, captureError);
            return run.result();
        }
        const bool started = state.start();
        entered.wait();
        state.stop();
        const CapturedStderr captured = stderrCapture.finish();
        run.expect(started, "logging-failure fixture starts one worker");
        run.expect(captured.succeeded, captured.error);
        run.expect(occurrenceCount(
                       {.text = captured.text(), .needle = "[kue] lethal worker failed:"}) == 1,
                   "throwing logger produces exactly one direct stderr fallback");
        run.expect(captured.text().find("fixture runtime failure") != std::string_view::npos,
                   "direct stderr fallback preserves the original worker cause");
        run.expect(state.workerState() == kue::LethalWorkerState::Failed,
                   "worker survives failure reporting failure");
        run.expect(gDetachCalls.load() == 1,
                   "failure reporting failure still detaches exactly once");
        run.expect(gRuntimeObservedRunning.load(), "logging failure follows the running commit");
        gLogThrows = false;
        gObservedState = nullptr;
        stateStorage.reset();
    }

    {
        kue::LethalState& state = stateStorage.emplace();
        std::latch entered(1);
        std::latch release(1);
        gWorkerEntered = &entered;
        gWorkerRelease = &release;
        gRuntimeBehavior = RuntimeBehavior::Block;
        gRuntimeCalls = 0;
        gDetachCalls = 0;
        gDetachThrows = true;
        gLogThrows = false;
        gRuntimeObservedRunning = true;
        gObservedState = &state;
        gLastLog.fill('\0');

        run.expect(state.start(), "detach-failure fixture starts one worker");
        entered.wait();
        release.count_down();
        state.stop();
        run.expect(state.workerState() == kue::LethalWorkerState::Failed,
                   "detach exception publishes failed state");
        run.expect(gDetachCalls.load() == 1, "detach failure is attempted exactly once");
        run.expect(std::string_view(gLastLog.data()).find("fixture detach failure") !=
                       std::string_view::npos,
                   "detach failure preserves the original exception cause");
        run.expect(gRuntimeObservedRunning.load(), "detach failure follows the running commit");
        gObservedState = nullptr;
        stateStorage.reset();
    }

    testWorkerFallbackSinkFailure(run);

    return run.result();
}
