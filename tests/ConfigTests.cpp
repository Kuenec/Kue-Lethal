#include "core/Config.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

namespace config_test {

enum class DirectorySyncBehavior : std::uint8_t { PassThrough, FailWithInputOutputError };

thread_local DirectorySyncBehavior directorySyncBehavior = DirectorySyncBehavior::PassThrough;
thread_local int directorySyncCalls = 0;
thread_local int regularFileSyncCalls = 0;
thread_local bool regularFileDescriptorCloseOnExec = true;
thread_local bool directoryDescriptorCloseOnExec = true;
thread_local bool measureAllocation = false;
thread_local std::size_t largestAllocation = 0;

struct ConfigInputDescriptorProbe {
    dev_t device = 0;
    ino_t inode = 0;
    int inspections = 0;
    bool closeOnExec = true;
    bool nonBlocking = true;
    bool armed = false;
};

thread_local ConfigInputDescriptorProbe configInputDescriptorProbe;

void resetDirectorySyncProbe(DirectorySyncBehavior behavior) {
    directorySyncBehavior = behavior;
    directorySyncCalls = 0;
    regularFileSyncCalls = 0;
    regularFileDescriptorCloseOnExec = true;
    directoryDescriptorCloseOnExec = true;
}

void recordAllocation(std::size_t bytes) noexcept {
    if (measureAllocation && bytes > largestAllocation)
        largestAllocation = bytes;
}

}

void* operator new(std::size_t bytes) {
    config_test::recordAllocation(bytes);
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

extern "C" ssize_t read(int descriptor, void* buffer, std::size_t bytes) {
    config_test::ConfigInputDescriptorProbe& probe = config_test::configInputDescriptorProbe;
    if (probe.armed) {
        struct stat metadata{};
        if (::fstat(descriptor, &metadata) == 0 && metadata.st_dev == probe.device &&
            metadata.st_ino == probe.inode) {
            ++probe.inspections;
            const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
            const int statusFlags = ::fcntl(descriptor, F_GETFL);
            probe.closeOnExec =
                probe.closeOnExec && descriptorFlags >= 0 && (descriptorFlags & FD_CLOEXEC) != 0;
            probe.nonBlocking =
                probe.nonBlocking && statusFlags >= 0 && (statusFlags & O_NONBLOCK) != 0;
        }
    }
    return static_cast<ssize_t>(::syscall(SYS_read, descriptor, buffer, bytes));
}

extern "C" int fsync(int descriptor) {
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) == 0) {
        if (S_ISREG(metadata.st_mode)) {
            ++config_test::regularFileSyncCalls;
            const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
            config_test::regularFileDescriptorCloseOnExec =
                config_test::regularFileDescriptorCloseOnExec && descriptorFlags >= 0 &&
                (descriptorFlags & FD_CLOEXEC) != 0;
        } else if (S_ISDIR(metadata.st_mode)) {
            ++config_test::directorySyncCalls;
            const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
            config_test::directoryDescriptorCloseOnExec =
                config_test::directoryDescriptorCloseOnExec && descriptorFlags >= 0 &&
                (descriptorFlags & FD_CLOEXEC) != 0;
            if (config_test::directorySyncBehavior ==
                config_test::DirectorySyncBehavior::FailWithInputOutputError) {
                errno = EIO;
                return -1;
            }
        }
    }
    return static_cast<int>(::syscall(SYS_fsync, descriptor));
}

namespace {

static_assert(!std::is_invocable_v<decltype(&kue::configLoad), kue::Config&, const std::string&>);
static_assert(
    !std::is_invocable_v<decltype(&kue::configSave), const kue::Config&, const std::string&>);

constexpr std::size_t kMaximumConfigBytes = 65536;
constexpr std::size_t kMaximumPathBytes = 4096;
constexpr float kFloatTolerance = 0.0001f;
constexpr std::array<std::string_view, 18> kTestFiles = {
    "round-trip.json",        "malformed.json",        "wrong-type.json",
    "numeric.json",           "invalid-color.json",    "oversized.json",
    "transactional.json",     "atomic.json",           "preserved.json",
    "obsolete-menu-key.json", "fallback.json",         "kuelethal.json",
    "write-failure.json",     "schema.json",           "directory-sync.json",
    "path-text.json",         "destination-null.json", "nonregular.json",
};

enum class FailureReporting : std::uint8_t { Immediate, Deferred };

class TestRun {
  public:
    explicit TestRun(FailureReporting reporting = FailureReporting::Immediate)
        : mReporting(reporting) {}

    bool expect(bool condition, std::string_view message) {
        ++mAssertions;
        if (condition)
            return true;
        ++mFailures;
        if (mReporting == FailureReporting::Immediate)
            std::cerr << "FAILED: " << message << '\n';
        return false;
    }

    int failureCount() const { return mFailures; }

    int result() const {
        if (mFailures == 0) {
            std::cout << "config tests passed: " << mAssertions << " assertions\n";
            return 0;
        }
        std::cerr << "config tests failed: " << mFailures << " of " << mAssertions
                  << " assertions\n";
        return 1;
    }

  private:
    FailureReporting mReporting;
    int mAssertions = 0;
    int mFailures = 0;
};

class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(TestRun& run) : mRun(run) {}

    bool create(std::string& error) {
        std::error_code code;
        const std::filesystem::path root = std::filesystem::temp_directory_path(code);
        if (code) {
            error = "cannot discover temporary directory: " + code.message();
            return false;
        }
        std::string pattern = (root / "kue-config-tests-XXXXXX").string();
        if (pattern.size() > kMaximumPathBytes) {
            error = "temporary directory path exceeds the path-length limit";
            return false;
        }
        std::array<char, kMaximumPathBytes + 1> writable{};
        std::memcpy(writable.data(), pattern.data(), pattern.size());
        char* const created = ::mkdtemp(writable.data());
        if (!created) {
            error = "cannot create temporary directory: " + std::string(std::strerror(errno));
            return false;
        }
        mPath = created;
        return true;
    }

    ~TemporaryDirectory() {
        if (mPath.empty())
            return;
        std::string error;
        const bool cleaned = cleanup(error);
        if (cleaned)
            mRun.expect(true, "temporary directory is clean");
        else
            mRun.expect(false, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::string file(std::string_view name) const { return mPath + '/' + std::string(name); }

    const std::string& path() const { return mPath; }

    bool cleanup(std::string& error) {
        for (const std::string_view name : kTestFiles) {
            const std::string target = file(name);
            if (::unlink(target.c_str()) != 0 && errno != ENOENT) {
                error = "cannot remove test file " + target + ": " + std::strerror(errno);
                return false;
            }
        }
        if (::rmdir(mPath.c_str()) != 0) {
            error = "cannot remove test directory " + mPath + ": " + std::strerror(errno);
            return false;
        }
        mPath.clear();
        return true;
    }

  private:
    TestRun& mRun;
    std::string mPath;
};

class EnvironmentVariable {
  public:
    EnvironmentVariable(TestRun& run, const char* name) : mRun(run), mName(name) {
        const char* const value = ::getenv(name);
        if (value) {
            mWasSet = true;
            mPreviousValue = value;
        }
    }

    ~EnvironmentVariable() {
        if (!mChanged)
            return;
        std::string error;
        if (!restore(error))
            mRun.expect(false, error);
    }

    EnvironmentVariable(const EnvironmentVariable&) = delete;
    EnvironmentVariable& operator=(const EnvironmentVariable&) = delete;

    bool assign(const char* value, std::string& error) {
        if (::setenv(mName.c_str(), value, 1) != 0) {
            error = "cannot set " + mName + ": " + std::strerror(errno);
            return false;
        }
        mChanged = true;
        return true;
    }

    bool clear(std::string& error) {
        if (::unsetenv(mName.c_str()) != 0) {
            error = "cannot unset " + mName + ": " + std::strerror(errno);
            return false;
        }
        mChanged = true;
        return true;
    }

    bool restore(std::string& error) {
        if (!mChanged)
            return true;
        const int result = mWasSet ? ::setenv(mName.c_str(), mPreviousValue.c_str(), 1)
                                   : ::unsetenv(mName.c_str());
        if (result != 0) {
            error = "cannot restore " + mName + ": " + std::strerror(errno);
            return false;
        }
        mChanged = false;
        return true;
    }

  private:
    TestRun& mRun;
    std::string mName;
    std::string mPreviousValue;
    bool mWasSet = false;
    bool mChanged = false;
};

class ScalarAllocationMeasurement {
  public:
    ScalarAllocationMeasurement() {
        config_test::largestAllocation = 0;
        config_test::measureAllocation = true;
    }

    ~ScalarAllocationMeasurement() { config_test::measureAllocation = false; }

    ScalarAllocationMeasurement(const ScalarAllocationMeasurement&) = delete;
    ScalarAllocationMeasurement& operator=(const ScalarAllocationMeasurement&) = delete;

    std::size_t largest() const noexcept { return config_test::largestAllocation; }
};

class CurrentDirectory {
  public:
    explicit CurrentDirectory(TestRun& run) : mRun(run) {}

    ~CurrentDirectory() {
        if (!mChanged)
            return;
        std::string error;
        if (!restore(error))
            mRun.expect(false, error);
    }

    CurrentDirectory(const CurrentDirectory&) = delete;
    CurrentDirectory& operator=(const CurrentDirectory&) = delete;

    bool enter(const char* path, std::string& error) {
        std::error_code code;
        mPrevious = std::filesystem::current_path(code);
        if (code) {
            error = "cannot inspect the current directory: " + code.message();
            return false;
        }
        std::filesystem::current_path(path, code);
        if (code) {
            error = std::string("cannot enter test directory ") + path + ": " + code.message();
            return false;
        }
        mChanged = true;
        return true;
    }

    bool restore(std::string& error) {
        if (!mChanged)
            return true;
        std::error_code code;
        std::filesystem::current_path(mPrevious, code);
        if (code) {
            error = "cannot restore the current directory: " + code.message();
            return false;
        }
        mChanged = false;
        return true;
    }

  private:
    TestRun& mRun;
    std::filesystem::path mPrevious;
    bool mChanged = false;
};

class FileSizeLimit {
  public:
    explicit FileSizeLimit(TestRun& run) : mRun(run) {}

    ~FileSizeLimit() {
        if (!mLimitChanged && !mSignalChanged)
            return;
        std::string error;
        if (!restore(error))
            mRun.expect(false, error);
    }

    FileSizeLimit(const FileSizeLimit&) = delete;
    FileSizeLimit& operator=(const FileSizeLimit&) = delete;

    bool constrain(rlim_t bytes, std::string& error) {
        if (::getrlimit(RLIMIT_FSIZE, &mPreviousLimit) != 0) {
            error = "cannot inspect the file-size limit: " + std::string(std::strerror(errno));
            return false;
        }
        if (::sigaction(SIGXFSZ, nullptr, &mPreviousSignal) != 0) {
            error = "cannot inspect SIGXFSZ: " + std::string(std::strerror(errno));
            return false;
        }
        struct sigaction ignored{};
        ignored.sa_handler = SIG_IGN;
        if (::sigemptyset(&ignored.sa_mask) != 0 || ::sigaction(SIGXFSZ, &ignored, nullptr) != 0) {
            error = "cannot ignore SIGXFSZ: " + std::string(std::strerror(errno));
            return false;
        }
        mSignalChanged = true;
        struct rlimit constrained = mPreviousLimit;
        if (constrained.rlim_max != RLIM_INFINITY && bytes > constrained.rlim_max) {
            error = "requested file-size limit exceeds the process hard limit";
            std::string restoreError;
            if (!restore(restoreError))
                error += "; " + restoreError;
            return false;
        }
        constrained.rlim_cur = bytes;
        if (::setrlimit(RLIMIT_FSIZE, &constrained) != 0) {
            error = "cannot constrain the file-size limit: " + std::string(std::strerror(errno));
            std::string restoreError;
            if (!restore(restoreError))
                error += "; " + restoreError;
            return false;
        }
        mLimitChanged = true;
        return true;
    }

    bool restore(std::string& error) {
        bool succeeded = true;
        if (mLimitChanged && ::setrlimit(RLIMIT_FSIZE, &mPreviousLimit) != 0) {
            error = "cannot restore the file-size limit: " + std::string(std::strerror(errno));
            succeeded = false;
        } else {
            mLimitChanged = false;
        }
        if (mSignalChanged && ::sigaction(SIGXFSZ, &mPreviousSignal, nullptr) != 0) {
            if (!error.empty())
                error += "; ";
            error += "cannot restore SIGXFSZ: " + std::string(std::strerror(errno));
            succeeded = false;
        } else {
            mSignalChanged = false;
        }
        return succeeded;
    }

  private:
    TestRun& mRun;
    struct rlimit mPreviousLimit{};
    struct sigaction mPreviousSignal{};
    bool mLimitChanged = false;
    bool mSignalChanged = false;
};

bool writeText(const std::string& path, std::string_view content, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open " + path + " for writing";
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

struct ReadTextResult {
    std::string content;
    std::string error;
};

bool readText(const std::string& path, ReadTextResult& result) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        result.error = "cannot open " + path + " for reading";
        return false;
    }
    const std::ifstream::pos_type end = input.tellg();
    if (end < std::ifstream::pos_type{}) {
        result.error = "cannot determine size of " + path;
        return false;
    }
    const auto bytes = static_cast<std::uint64_t>(end);
    if (bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        result.error = "test file is too large to read: " + path;
        return false;
    }
    result.content.assign(static_cast<std::size_t>(bytes), '\0');
    input.seekg(0);
    if (!result.content.empty())
        input.read(result.content.data(), static_cast<std::streamsize>(result.content.size()));
    if (!input && !result.content.empty()) {
        result.error = "cannot read " + path;
        return false;
    }
    return true;
}

struct BlockedCleanupFixture {
    std::string directory;
    std::string blocker;
};

bool returnWithBlockedCleanup(TestRun& run, BlockedCleanupFixture& fixture, std::string& error) {
    TemporaryDirectory directory(run);
    if (!directory.create(error))
        return false;
    fixture.directory = directory.path();
    fixture.blocker = directory.file("unexpected-entry");
    return writeText(fixture.blocker, "blocks directory removal\n", error);
}

void testEarlyReturnCleanupFailure(TestRun& run) {
    TestRun cleanupProbe(FailureReporting::Deferred);
    BlockedCleanupFixture fixture;
    std::string setupError;
    if (!run.expect(returnWithBlockedCleanup(cleanupProbe, fixture, setupError),
                    setupError.empty() ? "blocked cleanup fixture is created" : setupError)) {
        return;
    }
    run.expect(cleanupProbe.failureCount() == 1,
               "an early-return cleanup failure affects the test result");
    struct stat directoryMetadata{};
    run.expect(::stat(fixture.directory.c_str(), &directoryMetadata) == 0 &&
                   S_ISDIR(directoryMetadata.st_mode),
               "the cleanup blocker preserves the failed temporary directory");
    run.expect(::unlink(fixture.blocker.c_str()) == 0,
               "the early-return cleanup blocker is removed after the probe");
    run.expect(::rmdir(fixture.directory.c_str()) == 0,
               "the early-return temporary directory is removed after the probe");
}

bool near(float left, float right) {
    return std::abs(left - right) <= kFloatTolerance;
}

bool colorsEqual(const std::array<float, 4>& left, const std::array<float, 4>& right) {
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!near(left[index], right[index]))
            return false;
    }
    return true;
}

bool configsEqual(const kue::Config& left, const kue::Config& right) {
    return left.menuKey == right.menuKey && near(left.pollRate, right.pollRate) &&
           left.tickIntervalMs == right.tickIntervalMs &&
           left.infiniteStamina == right.infiniteStamina && left.noWeight == right.noWeight &&
           left.infiniteBattery == right.infiniteBattery &&
           left.extendedInventory == right.extendedInventory && left.fontPath == right.fontPath &&
           left.logPath == right.logPath && left.esp.items == right.esp.items &&
           left.esp.monsters == right.esp.monsters && left.esp.players == right.esp.players &&
           left.esp.exits == right.esp.exits && left.esp.fireExits == right.esp.fireExits &&
           left.esp.ships == right.esp.ships && left.esp.outlines == right.esp.outlines &&
           left.esp.names == right.esp.names && left.esp.values == right.esp.values &&
           left.esp.distance == right.esp.distance && left.esp.lines == right.esp.lines &&
           left.esp.useScrapTiers == right.esp.useScrapTiers &&
           left.esp.deathNotifications == right.esp.deathNotifications &&
           near(left.esp.maxDistance, right.esp.maxDistance) &&
           colorsEqual(left.esp.colorItems, right.esp.colorItems) &&
           colorsEqual(left.esp.colorMonsters, right.esp.colorMonsters) &&
           colorsEqual(left.esp.colorPlayers, right.esp.colorPlayers) &&
           colorsEqual(left.esp.colorExits, right.esp.colorExits) &&
           colorsEqual(left.esp.colorFireExits, right.esp.colorFireExits) &&
           colorsEqual(left.esp.colorShips, right.esp.colorShips) &&
           left.filePath == right.filePath;
}

kue::Config distinctConfig() {
    kue::Config config;
    config.menuKey = kue::MenuKey::F4;
    config.pollRate = 45.5f;
    config.tickIntervalMs = 67;
    config.infiniteStamina = false;
    config.noWeight = false;
    config.infiniteBattery = true;
    config.extendedInventory = true;
    config.fontPath = "fonts/test.ttf";
    config.logPath = "logs/test.log";
    config.esp.items = false;
    config.esp.monsters = false;
    config.esp.players = false;
    config.esp.exits = false;
    config.esp.fireExits = false;
    config.esp.ships = false;
    config.esp.outlines = false;
    config.esp.names = false;
    config.esp.values = false;
    config.esp.distance = false;
    config.esp.lines = true;
    config.esp.useScrapTiers = false;
    config.esp.deathNotifications = false;
    config.esp.maxDistance = 765.5f;
    config.esp.colorItems = {0.1f, 0.2f, 0.3f, 0.4f};
    config.esp.colorMonsters = {0.2f, 0.3f, 0.4f, 0.5f};
    config.esp.colorPlayers = {0.3f, 0.4f, 0.5f, 0.6f};
    config.esp.colorExits = {0.4f, 0.5f, 0.6f, 0.7f};
    config.esp.colorFireExits = {0.5f, 0.6f, 0.7f, 0.8f};
    config.esp.colorShips = {0.6f, 0.7f, 0.8f, 0.9f};
    config.filePath = "original-config-path.json";
    return config;
}

void testRoundTrip(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("round-trip.json");
    const kue::Config expected = distinctConfig();
    std::string error = "stale error";
    config_test::resetDirectorySyncProbe(config_test::DirectorySyncBehavior::PassThrough);
    if (!run.expect(kue::configSave(expected, {.path = path, .error = error}) ==
                        kue::ConfigSaveResult::Durable,
                    "valid configuration saves"))
        return;
    run.expect(config_test::regularFileSyncCalls == 1,
               "valid save synchronizes the temporary file exactly once");
    run.expect(config_test::regularFileDescriptorCloseOnExec,
               "the temporary file cannot cross an executable-image replacement");
    run.expect(config_test::directoryDescriptorCloseOnExec,
               "the parent directory cannot cross an executable-image replacement");
    run.expect(error.empty(), "successful save clears the error");
    kue::Config actual;
    error = "stale error";
    struct stat inputMetadata{};
    if (!run.expect(::stat(path.c_str(), &inputMetadata) == 0,
                    "saved configuration input can be inspected")) {
        return;
    }
    config_test::configInputDescriptorProbe = {
        .device = inputMetadata.st_dev,
        .inode = inputMetadata.st_ino,
        .inspections = 0,
        .closeOnExec = true,
        .nonBlocking = true,
        .armed = true,
    };
    const bool loaded = kue::configLoad(actual, {.path = path, .error = error});
    config_test::configInputDescriptorProbe.armed = false;
    if (!run.expect(loaded, "saved configuration loads"))
        return;
    run.expect(config_test::configInputDescriptorProbe.inspections > 0,
               "the exact configuration input descriptor is observed during reads");
    run.expect(config_test::configInputDescriptorProbe.closeOnExec,
               "the configuration input cannot cross an executable-image replacement");
    run.expect(config_test::configInputDescriptorProbe.nonBlocking,
               "the configuration input remains nonblocking while validated and read");
    run.expect(error.empty(), "successful load clears the error");
    kue::Config expectedLoaded = expected;
    expectedLoaded.filePath = path;
    run.expect(configsEqual(actual, expectedLoaded), "round-trip preserves every serialized field");
}

void testMalformedJson(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("malformed.json");
    std::string setupError;
    if (!writeText(path, "{\"menuKey\":", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "malformed JSON fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "malformed JSON is rejected");
    run.expect(error.find("invalid JSON") != std::string::npos,
               "malformed JSON reports its parse failure");
    run.expect(configsEqual(config, before), "malformed JSON does not mutate configuration");
}

void testWrongType(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("wrong-type.json");
    std::string setupError;
    if (!writeText(path, "{\"pollRate\":\"fast\"}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "wrong-type fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "wrong field type is rejected");
    run.expect(error == "pollRate must be a number", "wrong field type reports the field contract");
    run.expect(configsEqual(config, before), "wrong field type does not mutate configuration");
}

void testObsoleteMenuKey(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("obsolete-menu-key.json");
    std::string setupError;
    if (!writeText(path, "{\"menuKey\":260}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "obsolete menu-key fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "obsolete input key code is rejected");
    run.expect(error == "menuKey must be a supported Unity Input System key",
               "obsolete menu key reports the production input contract");
    run.expect(configsEqual(config, before), "obsolete menu key does not mutate configuration");
}

void testExactSchema(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("schema.json");
    std::string setupError;
    if (!writeText(path, "{\"unexpected\":true}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "unknown root-key fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config beforeRootKey = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "unknown root key is rejected");
    run.expect(error == "unknown configuration key: unexpected",
               "unknown root key identifies the exact invalid key");
    run.expect(configsEqual(config, beforeRootKey),
               "unknown root key preserves caller configuration");

    if (!writeText(path, "{\"esp\":{\"unexpected\":true}}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "unknown ESP-key fixture is written");
    config = distinctConfig();
    const kue::Config beforeEspKey = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "unknown ESP key is rejected");
    run.expect(error == "unknown esp key: unexpected",
               "unknown ESP key identifies the exact invalid key");
    run.expect(configsEqual(config, beforeEspKey),
               "unknown ESP key preserves caller configuration");

    if (!writeText(path, "{\"esp\":{\"boxes\":false}}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "obsolete ESP alias fixture is written");
    config = distinctConfig();
    config.esp.outlines = true;
    const kue::Config beforeAlias = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "obsolete ESP alias is rejected");
    run.expect(error == "unknown esp key: boxes",
               "obsolete ESP alias reports the canonical schema violation");
    run.expect(configsEqual(config, beforeAlias),
               "obsolete ESP alias preserves caller configuration");
}

void testDuplicateJsonKeys(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("schema.json");
    struct DuplicateFixture {
        std::string_view content;
        std::string_view contract;
    };
    constexpr std::array<DuplicateFixture, 5> fixtures = {
        DuplicateFixture{R"({"pollRate":30,"pollRate":60})", "duplicate root key"},
        DuplicateFixture{R"({"esp":{"items":true,"items":false}})", "duplicate nested key"},
        DuplicateFixture{R"({"logPath":"first","log\u0050ath":"second"})",
                         "escaped-equivalent root key"},
        DuplicateFixture{R"({"☃":1,"\u2603":2})", "escaped-equivalent Unicode key"},
        DuplicateFixture{R"({"unexpected":{"nested":{"value":1,"value":2}}})",
                         "duplicate deeply nested key"},
    };
    for (const DuplicateFixture& fixture : fixtures) {
        std::string setupError;
        if (!writeText(path, fixture.content, setupError)) {
            run.expect(false, setupError);
            return;
        }
        kue::Config config = distinctConfig();
        const kue::Config before = config;
        std::string error;
        run.expect(!kue::configLoad(config, {.path = path, .error = error}),
                   std::string(fixture.contract) + " is rejected");
        run.expect(error.find("duplicate JSON object key in ") == 0,
                   std::string(fixture.contract) + " reports the duplicate-key contract");
        run.expect(configsEqual(config, before),
                   std::string(fixture.contract) + " preserves caller configuration");
    }

    const std::string excessiveDepth = R"({"unexpected":{"one":{"two":{"three":{"four":true}}}}})";
    std::string setupError;
    if (!writeText(path, excessiveDepth, setupError)) {
        run.expect(false, setupError);
        return;
    }
    kue::Config config = distinctConfig();
    const kue::Config beforeDepth = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "excessive JSON depth is rejected");
    run.expect(error == "JSON nesting exceeds the 4-container limit in " + path,
               "JSON depth failure reports its exact bound");
    run.expect(configsEqual(config, beforeDepth),
               "JSON depth failure preserves caller configuration");

    const std::string excessiveKey = "{\"" + std::string(65, 'k') + "\":true}";
    if (!writeText(path, excessiveKey, setupError)) {
        run.expect(false, setupError);
        return;
    }
    config = distinctConfig();
    const kue::Config beforeKey = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "excessive decoded JSON key is rejected");
    run.expect(error == "JSON object key exceeds the 64-byte decoded-text limit in " + path,
               "JSON key failure reports its exact bound");
    run.expect(configsEqual(config, beforeKey), "JSON key failure preserves caller configuration");

    const std::string excessiveText = "{\"logPath\":\"" + std::string(4097, 'p') + "\"}";
    if (!writeText(path, excessiveText, setupError)) {
        run.expect(false, setupError);
        return;
    }
    config = distinctConfig();
    const kue::Config beforeText = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "excessive decoded JSON text is rejected");
    run.expect(error == "JSON string exceeds the 4096-byte decoded-text limit in " + path,
               "JSON string failure reports its exact bound");
    run.expect(configsEqual(config, beforeText),
               "JSON string failure preserves caller configuration");

    std::string excessiveMembers = "{\"unexpected\":{";
    for (int index = 0; index < 21; ++index) {
        if (index != 0)
            excessiveMembers += ',';
        excessiveMembers += "\"k" + std::to_string(index) + "\":true";
    }
    excessiveMembers += "}}";
    if (!writeText(path, excessiveMembers, setupError)) {
        run.expect(false, setupError);
        return;
    }
    config = distinctConfig();
    const kue::Config beforeMembers = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "excessive object membership is rejected");
    run.expect(error == "JSON object exceeds the 20-key limit in " + path,
               "JSON membership failure reports its exact bound");
    run.expect(configsEqual(config, beforeMembers),
               "JSON membership failure preserves caller configuration");
}

void testNumericFailures(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("numeric.json");
    std::string setupError;
    if (!writeText(path, "{\"pollRate\":1e100}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "floating-point range fixture is written");
    kue::Config config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "number outside the floating-point range is rejected");
    run.expect(error == "pollRate must be a finite floating-point value",
               "floating-point conversion failure is actionable");
    const kue::Config beforeOverflow = config;
    if (!writeText(path, "{\"pollRate\":1e10000}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "double overflow fixture is written");
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "double overflow is rejected");
    run.expect(error.find("numeric overflow in ") == 0,
               "double overflow is converted to the configuration error contract");
    run.expect(configsEqual(config, beforeOverflow),
               "double overflow does not mutate configuration");
    if (!writeText(path, "{\"pollRate\":9}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "runtime range fixture is written");
    error.clear();
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "runtime range violation is rejected");
    run.expect(error == "pollRate must be finite and within [10, 90]",
               "runtime range failure reports its accepted interval");
}

void testInvalidColor(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("invalid-color.json");
    std::string setupError;
    if (!writeText(path, "{\"esp\":{\"colorItems\":[0.1,0.2,1.1,1.0]}}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "invalid-color fixture is written");
    kue::Config config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "invalid color component is rejected");
    run.expect(error == "colorItems components must be finite and within [0, 1]",
               "invalid color reports its accepted interval");

    const auto expectRejectedSave = [&](std::array<float, 4> kue::EspConfig::* member,
                                        std::string_view name) {
        kue::Config invalid = distinctConfig();
        (invalid.esp.*member)[2] = 1.1f;
        error.clear();
        run.expect(kue::configSave(invalid, {.path = path, .error = error}) ==
                       kue::ConfigSaveResult::NotCommitted,
                   std::string(name) + " rejects an invalid saved component");
        run.expect(error == std::string(name) + " components must be finite and within [0, 1]",
                   std::string(name) + " save reports its accepted interval");
    };
    expectRejectedSave(&kue::EspConfig::colorItems, "colorItems");
    expectRejectedSave(&kue::EspConfig::colorMonsters, "colorMonsters");
    expectRejectedSave(&kue::EspConfig::colorPlayers, "colorPlayers");
    expectRejectedSave(&kue::EspConfig::colorExits, "colorExits");
    expectRejectedSave(&kue::EspConfig::colorFireExits, "colorFireExits");
    expectRejectedSave(&kue::EspConfig::colorShips, "colorShips");
}

void testPathText(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("path-text.json");
    std::string setupError;
    if (!writeText(path, "{\"logPath\":\"logs/\\u0000hidden.log\"}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "embedded-null path fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "embedded-null loaded path is rejected");
    run.expect(error == "logPath must not contain null bytes",
               "embedded-null loaded path identifies its field and cause");
    run.expect(configsEqual(config, before),
               "embedded-null loaded path preserves caller configuration");

    const std::string invalidUtf8{static_cast<char>(0xc0), static_cast<char>(0xaf)};
    struct PathMember {
        std::string kue::Config::* member;
        std::string_view name;
    };
    constexpr std::array<PathMember, 3> members = {
        PathMember{&kue::Config::fontPath, "fontPath"},
        PathMember{&kue::Config::logPath, "logPath"},
        PathMember{&kue::Config::filePath, "filePath"},
    };
    for (const PathMember& entry : members) {
        kue::Config invalid = distinctConfig();
        invalid.*entry.member = std::string("prefix\0suffix", 13);
        error.clear();
        run.expect(kue::configSave(invalid, {.path = path, .error = error}) ==
                       kue::ConfigSaveResult::NotCommitted,
                   std::string(entry.name) + " rejects embedded null bytes");
        run.expect(error == std::string(entry.name) + " must not contain null bytes",
                   std::string(entry.name) + " identifies embedded null bytes");

        invalid = distinctConfig();
        invalid.*entry.member = "prefix" + invalidUtf8;
        error.clear();
        run.expect(kue::configSave(invalid, {.path = path, .error = error}) ==
                       kue::ConfigSaveResult::NotCommitted,
                   std::string(entry.name) + " rejects ill-formed UTF-8");
        run.expect(error == std::string(entry.name) + " must be valid UTF-8",
                   std::string(entry.name) + " identifies ill-formed UTF-8");
    }

    const std::string nullDestination = directory.file("destination-null.json") + '\0' + "suffix";
    error.clear();
    run.expect(kue::configSave(distinctConfig(), {.path = nullDestination, .error = error}) ==
                   kue::ConfigSaveResult::NotCommitted,
               "embedded-null destination is rejected");
    run.expect(error == "configuration destination must not contain null bytes",
               "embedded-null destination reports its cause");
    run.expect(!std::filesystem::exists(directory.file("destination-null.json")),
               "embedded-null destination is rejected before filesystem mutation");
}

void testSaveSchedule(TestRun& run) {
    using namespace std::chrono_literals;
    using SaveClock = kue::ConfigSaveSchedule::Clock;
    const SaveClock::time_point start{};
    kue::ConfigSaveSchedule schedule;
    run.expect(!schedule.due(start), "clean save schedule performs no work");

    schedule.requestAutosave(start);
    run.expect(!schedule.due(start + 349ms), "autosave waits for the debounce interval");
    run.expect(schedule.due(start + 350ms), "autosave is due at the debounce boundary");
    schedule.requestAutosave(start + 100ms);
    run.expect(!schedule.due(start + 449ms), "new mutation resets the autosave debounce");
    run.expect(schedule.due(start + 450ms), "debounced autosave preserves the newest mutation");

    schedule.record(kue::ConfigSaveResult::NotCommitted, start + 450ms);
    run.expect(!schedule.due(start + 1449ms), "automatic retry waits one full second");
    run.expect(schedule.due(start + 1450ms), "automatic retry is due after one second");
    schedule.requestAutosave(start + 500ms);
    run.expect(!schedule.due(start + 1449ms), "new mutation cannot accelerate a failed retry");
    run.expect(schedule.due(start + 1450ms), "failed retry saves the latest mutation");

    schedule.requestExplicit();
    run.expect(schedule.due(start + 500ms), "explicit save is due immediately");
    schedule.record(kue::ConfigSaveResult::CommittedDurabilityUnconfirmed, start + 500ms);
    run.expect(!schedule.due(start + 1499ms), "durability retry is rate limited");
    run.expect(schedule.due(start + 1500ms), "durability retry occurs after one second");
    schedule.record(kue::ConfigSaveResult::Durable, start + 1500ms);
    run.expect(!schedule.due(start + 10s), "durable completion returns to idle");
}

void testOversizedInput(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("oversized.json");
    const std::string content(kMaximumConfigBytes + 1, ' ');
    std::string setupError;
    if (!writeText(path, content, setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "oversized fixture is written");
    kue::Config config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "oversized configuration is rejected");
    run.expect(error.find("65536-byte limit") != std::string::npos,
               "oversized configuration reports the byte limit");
}

void testNonregularInput(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("nonregular.json");
    if (::mkfifo(path.c_str(), 0600) != 0) {
        run.expect(false, "cannot create configuration FIFO");
        return;
    }
    const int guard = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (guard < 0) {
        run.expect(false, "cannot open configuration FIFO guard");
        return;
    }

    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "a configuration FIFO is rejected");
    run.expect(error == "configuration is not a regular file: " + path,
               "the FIFO failure names the regular-file contract");
    run.expect(configsEqual(config, before), "a configuration FIFO preserves caller state");
    run.expect(::close(guard) == 0, "the FIFO guard is closed");

    error.clear();
    run.expect(!kue::configLoad(config, {.path = "/dev/null", .error = error}),
               "a configuration character device is rejected");
    run.expect(error == "configuration is not a regular file: /dev/null",
               "the character-device failure names the regular-file contract");
    run.expect(configsEqual(config, before),
               "a configuration character device preserves caller state");
}

void testConfigPathSelection(TestRun& run, const TemporaryDirectory& directory) {
    const std::string fallbackPath = directory.file("fallback.json");
    const std::string localPath = directory.file("kuelethal.json");
    std::string setupError;
    if (!writeText(fallbackPath, "{\"pollRate\":60}", setupError) ||
        !writeText(localPath, "{\"pollRate\":60}", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "configuration lookup fixtures are written");

    EnvironmentVariable configEnvironment(run, "KUE_CONFIG");
    EnvironmentVariable homeEnvironment(run, "HOME");
    CurrentDirectory currentDirectory(run);
    if (!configEnvironment.assign(fallbackPath.c_str(), setupError) ||
        !homeEnvironment.assign(directory.path().c_str(), setupError) ||
        !currentDirectory.enter(directory.path().c_str(), setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "configuration lookup environment is isolated");

    kue::Config config = distinctConfig();
    const kue::Config beforeExplicitMiss = config;
    const std::string missingExplicit = directory.file("missing-explicit.json");
    std::string error;
    run.expect(!kue::configLoad(config, {.path = missingExplicit, .error = error}),
               "a missing explicit configuration does not fall through");
    run.expect(error.find(missingExplicit) != std::string::npos,
               "a missing explicit configuration identifies the requested path");
    run.expect(configsEqual(config, beforeExplicitMiss),
               "a missing explicit configuration preserves caller state");

    const std::string missingEnvironment = directory.file("missing-environment.json");
    if (!configEnvironment.assign(missingEnvironment.c_str(), setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "missing KUE_CONFIG fixture is selected");
    const kue::Config beforeEnvironmentMiss = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = "", .error = error}),
               "a missing KUE_CONFIG path does not fall through");
    run.expect(error.find(missingEnvironment) != std::string::npos,
               "a missing KUE_CONFIG path identifies the requested path");
    run.expect(configsEqual(config, beforeEnvironmentMiss),
               "a missing KUE_CONFIG path preserves caller state");

    if (!configEnvironment.assign("", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "empty KUE_CONFIG fixture is selected");
    const kue::Config beforeEmptyEnvironment = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = "", .error = error}),
               "an empty KUE_CONFIG value is rejected");
    run.expect(error == "KUE_CONFIG is empty", "an empty KUE_CONFIG value is actionable");
    run.expect(configsEqual(config, beforeEmptyEnvironment),
               "an empty KUE_CONFIG value preserves caller state");

    const std::string oversizedEnvironmentPath(kMaximumPathBytes + 1, 'x');
    if (!configEnvironment.assign(oversizedEnvironmentPath.c_str(), setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "oversized KUE_CONFIG fixture is selected");
    const kue::Config beforeOversizedEnvironment = config;
    error.clear();
    bool oversizedEnvironmentLoaded = false;
    std::size_t largestAllocation = 0;
    {
        ScalarAllocationMeasurement measurement;
        oversizedEnvironmentLoaded = kue::configLoad(config, {.path = "", .error = error});
        largestAllocation = measurement.largest();
    }
    run.expect(!oversizedEnvironmentLoaded, "an oversized KUE_CONFIG value is rejected");
    run.expect(error == "KUE_CONFIG exceeds the path-length limit",
               "an oversized KUE_CONFIG value reports its exact limit");
    run.expect(configsEqual(config, beforeOversizedEnvironment),
               "an oversized KUE_CONFIG value preserves caller state");
    run.expect(largestAllocation <= kMaximumPathBytes,
               "an oversized KUE_CONFIG value is bounded before string allocation");

    if (!configEnvironment.clear(setupError)) {
        run.expect(false, setupError);
        return;
    }
    const std::string oversizedHome(kMaximumPathBytes + 1, 'h');
    if (!homeEnvironment.assign(oversizedHome.c_str(), setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "oversized HOME fixture is selected");
    const kue::Config beforeOversizedHome = config;
    error.clear();
    bool oversizedHomeLoaded = false;
    {
        ScalarAllocationMeasurement measurement;
        oversizedHomeLoaded = kue::configLoad(config, {.path = "", .error = error});
        largestAllocation = measurement.largest();
    }
    run.expect(!oversizedHomeLoaded, "an oversized HOME value is rejected");
    run.expect(error == "HOME exceeds the path-length limit",
               "an oversized HOME value reports its exact limit");
    run.expect(configsEqual(config, beforeOversizedHome),
               "an oversized HOME value preserves caller state");
    run.expect(largestAllocation <= kMaximumPathBytes,
               "an oversized HOME value is bounded before string allocation");

    if (!homeEnvironment.assign(directory.path().c_str(), setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "HOME fixture is restored after its oversized boundary");

    if (::unlink(localPath.c_str()) != 0) {
        run.expect(false, "cannot remove the local configuration fixture");
        return;
    }
    if (!configEnvironment.clear(setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "configuration overrides are cleared for default lookup");
    const kue::Config beforeTotalMiss = config;
    error.clear();
    run.expect(!kue::configLoad(config, {.path = "", .error = error}),
               "a total configuration miss is rejected");
    run.expect(error.find(directory.path() + "/.config/kuelethal/config.json") !=
                       std::string::npos &&
                   error.find("kuelethal.json") != std::string::npos,
               "a total configuration miss reports every searched location");
    run.expect(configsEqual(config, beforeTotalMiss),
               "a total configuration miss preserves caller state");

    if (!currentDirectory.restore(setupError) || !homeEnvironment.restore(setupError) ||
        !configEnvironment.restore(setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "configuration lookup environment is restored");
}

void testTransactionalLoad(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("transactional.json");
    const std::string_view content = "{\"menuKey\":32,\"pollRate\":60,\"extendedInventory\":false,"
                                     "\"esp\":{\"items\":true,\"colorShips\":[0.2,0.3,0.4,2]}}";
    std::string setupError;
    if (!writeText(path, content, setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "transactional-load fixture is written");
    kue::Config config = distinctConfig();
    const kue::Config before = config;
    std::string error;
    run.expect(!kue::configLoad(config, {.path = path, .error = error}),
               "late validation failure rejects the whole load");
    run.expect(configsEqual(config, before),
               "late validation failure preserves the complete caller state");
}

void testAtomicSave(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("atomic.json");
    std::string setupError;
    if (!writeText(path, "previous destination\n", setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "existing destination fixture is written");
    struct stat before{};
    if (!run.expect(::stat(path.c_str(), &before) == 0, "existing destination can be inspected"))
        return;
    const kue::Config expected = distinctConfig();
    std::string error;
    config_test::resetDirectorySyncProbe(config_test::DirectorySyncBehavior::PassThrough);
    if (!run.expect(kue::configSave(expected, {.path = path, .error = error}) ==
                        kue::ConfigSaveResult::Durable,
                    "atomic save replaces an existing destination")) {
        return;
    }
    run.expect(config_test::directorySyncCalls == 1,
               "atomic save synchronizes the containing directory exactly once");
    struct stat after{};
    if (!run.expect(::stat(path.c_str(), &after) == 0, "replacement destination can be inspected"))
        return;
    run.expect(before.st_dev == after.st_dev && before.st_ino != after.st_ino,
               "save commits a same-filesystem temporary inode by replacement");
    kue::Config actual;
    run.expect(kue::configLoad(actual, {.path = path, .error = error}),
               "atomic replacement contains valid JSON");
    kue::Config expectedLoaded = expected;
    expectedLoaded.filePath = path;
    run.expect(configsEqual(actual, expectedLoaded),
               "atomic replacement contains the requested configuration");
}

void testDirectorySyncFailure(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("directory-sync.json");
    const kue::Config expected = distinctConfig();
    std::string error;
    config_test::resetDirectorySyncProbe(
        config_test::DirectorySyncBehavior::FailWithInputOutputError);
    const kue::ConfigSaveResult saved = kue::configSave(expected, {.path = path, .error = error});
    const int syncCalls = config_test::directorySyncCalls;
    config_test::resetDirectorySyncProbe(config_test::DirectorySyncBehavior::PassThrough);
    run.expect(saved == kue::ConfigSaveResult::CommittedDurabilityUnconfirmed,
               "directory synchronization failure reports a committed but unconfirmed result");
    run.expect(syncCalls == 1, "directory synchronization is attempted exactly once");
    run.expect(error ==
                   "cannot synchronize configuration directory: " + std::string(std::strerror(EIO)),
               "directory synchronization failure preserves its operation and errno");

    kue::Config actual;
    std::string loadError;
    if (!run.expect(kue::configLoad(actual, {.path = path, .error = loadError}),
                    "committed destination remains readable after a durability failure")) {
        return;
    }
    kue::Config expectedLoaded = expected;
    expectedLoaded.filePath = path;
    run.expect(configsEqual(actual, expectedLoaded),
               "durability failure accurately reports a committed replacement");
}

void testRejectedSavePreservesDestination(TestRun& run, const TemporaryDirectory& directory) {
    const std::string path = directory.file("preserved.json");
    constexpr std::string_view original = "preserve this destination byte for byte\n";
    std::string setupError;
    if (!writeText(path, original, setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "preserved destination fixture is written");
    struct stat before{};
    if (!run.expect(::stat(path.c_str(), &before) == 0, "preserved destination can be inspected"))
        return;
    kue::Config invalid = distinctConfig();
    invalid.pollRate = std::numeric_limits<float>::quiet_NaN();
    std::string error;
    run.expect(kue::configSave(invalid, {.path = path, .error = error}) ==
                   kue::ConfigSaveResult::NotCommitted,
               "non-finite configuration is rejected before saving");
    run.expect(error == "pollRate must be finite and within [10, 90]",
               "non-finite save failure reports the field contract");
    ReadTextResult readResult;
    const bool readSucceeded = readText(path, readResult);
    run.expect(readSucceeded, readSucceeded ? "preserved destination is read" : readResult.error);
    run.expect(readResult.content == original, "rejected save preserves destination bytes");
    struct stat after{};
    if (!run.expect(::stat(path.c_str(), &after) == 0,
                    "preserved destination remains inspectable")) {
        return;
    }
    run.expect(before.st_dev == after.st_dev && before.st_ino == after.st_ino,
               "rejected save preserves the destination inode");
}

void testSaveFailureOperation(TestRun& run, const TemporaryDirectory& directory) {
    FileSizeLimit fileSizeLimit(run);
    std::string setupError;
    if (!fileSizeLimit.constrain(1, setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "file-size failure fixture is active");
    const std::string path = directory.file("write-failure.json");
    const kue::Config config = distinctConfig();
    std::string error;
    run.expect(kue::configSave(config, {.path = path, .error = error}) ==
                   kue::ConfigSaveResult::NotCommitted,
               "a temporary configuration write failure is reported");
    run.expect(error.find("cannot flush temporary configuration: ") == 0,
               "the first failed save operation is identified");
    run.expect(error.find(std::strerror(EFBIG)) != std::string::npos,
               "the first failed save operation preserves its errno");
    if (!fileSizeLimit.restore(setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "file-size failure fixture is restored");
}

void runConfigTests(TestRun& run) {
    TemporaryDirectory directory(run);
    std::string setupError;
    if (!directory.create(setupError)) {
        run.expect(false, setupError);
        return;
    }
    run.expect(true, "unique temporary directory is created");

    testRoundTrip(run, directory);
    testMalformedJson(run, directory);
    testWrongType(run, directory);
    testObsoleteMenuKey(run, directory);
    testExactSchema(run, directory);
    testDuplicateJsonKeys(run, directory);
    testNumericFailures(run, directory);
    testInvalidColor(run, directory);
    testPathText(run, directory);
    testSaveSchedule(run);
    testOversizedInput(run, directory);
    testNonregularInput(run, directory);
    testConfigPathSelection(run, directory);
    testTransactionalLoad(run, directory);
    testAtomicSave(run, directory);
    testDirectorySyncFailure(run, directory);
    testRejectedSavePreservesDestination(run, directory);
    testSaveFailureOperation(run, directory);
    testEarlyReturnCleanupFailure(run);
}

}

int main() {
    TestRun run;
    runConfigTests(run);
    return run.result();
}
