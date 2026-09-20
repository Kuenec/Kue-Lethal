#include "core/Utf8.h"
#include "entry/RemoteStart.h"
#include "platform/Environment.h"
#include "platform/FileSystem.h"
#include "platform/WindowsSupport.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <tlhelp32.h>

namespace {

using kue::platform::EnvironmentStatus;
using kue::platform::EnvironmentStorage;
using kue::platform::WideText;

constexpr std::size_t kWidePathCapacity = kue::platform::kMaximumPathBytes;
constexpr std::string_view kBuildIdentityPrefix = "KUE_BUILD_ID=";
constexpr std::size_t kMaximumModuleBytes = std::size_t{64} * 1024 * 1024;
constexpr std::size_t kMaximumFreshLogBytes = std::size_t{1024} * 1024;
constexpr auto kRemoteCallTimeout = std::chrono::seconds(30);
constexpr auto kArmTimeout = std::chrono::seconds(10);
constexpr auto kHudTimeout = std::chrono::seconds(15);
constexpr auto kLogPollInterval = std::chrono::milliseconds(250);
constexpr DWORD kTargetAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE;

struct Settings {
    std::string module;
    std::string config;
    std::string log;
    std::string processName;
};

struct RemoteModule {
    std::uintptr_t base = 0;
    bool found = false;
};

class ScopedHandle final {
  public:
    explicit ScopedHandle(HANDLE handle) noexcept : mHandle(handle) {}
    ~ScopedHandle() {
        if (mHandle && mHandle != INVALID_HANDLE_VALUE)
            ::CloseHandle(mHandle);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return mHandle; }
    [[nodiscard]] bool valid() const noexcept { return mHandle && mHandle != INVALID_HANDLE_VALUE; }

  private:
    HANDLE mHandle;
};

class RemoteAllocation final {
  public:
    RemoteAllocation(HANDLE process, std::size_t bytes) noexcept
        : mProcess(process), mAddress(::VirtualAllocEx(process, nullptr, bytes,
                                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {}
    ~RemoteAllocation() {
        if (mAddress)
            ::VirtualFreeEx(mProcess, mAddress, 0, MEM_RELEASE);
    }
    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;
    [[nodiscard]] void* address() const noexcept { return mAddress; }

  private:
    HANDLE mProcess;
    void* mAddress;
};

void report(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[kue] ", stdout);
    std::vfprintf(stdout, format, arguments);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    va_end(arguments);
}

std::string narrow(const wchar_t* units, std::size_t count) {
    std::string result(count * 4, '\0');
    const kue::Utf16ToUtf8Result conversion = kue::convertUtf16ToUtf8(
        {reinterpret_cast<const std::uint16_t*>(units), count}, {result.data(), result.size()});
    if (conversion.status != kue::Utf16ToUtf8Status::Success &&
        conversion.status != kue::Utf16ToUtf8Status::EmptyInput) {
        return {};
    }
    result.resize(conversion.utf8Bytes);
    return result;
}

bool widen(std::string_view utf8, WideText<kWidePathCapacity>& wide) {
    return kue::platform::toWide(utf8, wide) == kue::platform::WideConversionStatus::Converted;
}

bool environmentPath(const char* name, std::string& output, bool& overridden) {
    EnvironmentStorage storage;
    const kue::platform::EnvironmentValue value = kue::platform::readEnvironment(name, storage);
    overridden = false;
    switch (value.status) {
    case EnvironmentStatus::Unset:
        return true;
    case EnvironmentStatus::Valid:
        output = std::string(value.text);
        overridden = true;
        return true;
    case EnvironmentStatus::Empty:
        report("error: %s is set but empty", name);
        return false;
    case EnvironmentStatus::TooLong:
        report("error: %s exceeds its text contract", name);
        return false;
    case EnvironmentStatus::InvalidEncoding:
        report("error: %s must be valid UTF-8", name);
        return false;
    }
    return false;
}

std::string executableDirectory() {
    WideText<kWidePathCapacity> wide;
    const DWORD length =
        ::GetModuleFileNameW(nullptr, wide.units.data(), static_cast<DWORD>(wide.units.size()));
    if (length == 0 || length >= wide.units.size())
        return {};
    const std::string path = narrow(wide.units.data(), length);
    const std::size_t separator = path.find_last_of("\\/");
    return separator == std::string::npos ? std::string(".") : path.substr(0, separator);
}

std::string fullPath(const std::string& path) {
    WideText<kWidePathCapacity> wide;
    if (!widen(path, wide))
        return {};
    WideText<kWidePathCapacity> resolved;
    const DWORD length =
        ::GetFullPathNameW(wide.units.data(), static_cast<DWORD>(resolved.units.size()),
                           resolved.units.data(), nullptr);
    if (length == 0 || length >= resolved.units.size())
        return {};
    return narrow(resolved.units.data(), length);
}

bool regularFileExists(const std::string& path) {
    WideText<kWidePathCapacity> wide;
    if (!widen(path, wide))
        return false;
    const DWORD attributes = ::GetFileAttributesW(wide.units.data());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ensureDirectory(const std::string& path) {
    WideText<kWidePathCapacity> wide;
    if (!widen(path, wide))
        return false;
    const DWORD attributes = ::GetFileAttributesW(wide.units.data());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const std::size_t separator = path.find_last_of("\\/");
    if (separator != std::string::npos && separator > 0 &&
        !ensureDirectory(path.substr(0, separator)))
        return false;
    return ::CreateDirectoryW(wide.units.data(), nullptr) != 0 ||
           ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool resolveSettings(Settings& settings) {
    bool overridden = false;
    const std::string directory = executableDirectory();
    if (directory.empty()) {
        report("error: cannot determine the injector directory");
        return false;
    }
    settings.module = directory + "\\kuelethal.dll";
    if (!environmentPath("KUE_MODULE", settings.module, overridden))
        return false;
    settings.config = directory + "\\..\\config\\kuelethal.json";
    if (!environmentPath("KUE_CONFIG", settings.config, overridden))
        return false;
    EnvironmentStorage localAppData;
    const kue::platform::EnvironmentValue localAppDataValue =
        kue::platform::readEnvironment("LOCALAPPDATA", localAppData);
    settings.log.clear();
    if (localAppDataValue.status == EnvironmentStatus::Valid)
        settings.log = std::string(localAppDataValue.text) + "\\kuelethal\\kuelethal.log";
    if (!environmentPath("KUE_LOG", settings.log, overridden))
        return false;
    if (settings.log.empty()) {
        report("error: LOCALAPPDATA is unavailable; set KUE_LOG explicitly");
        return false;
    }
    settings.processName = "Lethal Company.exe";
    if (!environmentPath("GAME_PATTERN", settings.processName, overridden))
        return false;

    settings.module = fullPath(settings.module);
    settings.config = fullPath(settings.config);
    settings.log = fullPath(settings.log);
    if (settings.module.empty() || settings.config.empty() || settings.log.empty()) {
        report("error: cannot resolve module, configuration, or log paths");
        return false;
    }
    if (!regularFileExists(settings.module)) {
        report("error: module not found: %s", settings.module.c_str());
        return false;
    }
    if (!regularFileExists(settings.config)) {
        report("error: configuration file not found: %s", settings.config.c_str());
        return false;
    }
    const std::size_t logSeparator = settings.log.find_last_of("\\/");
    if (logSeparator == std::string::npos || logSeparator + 1 == settings.log.size()) {
        report("error: log path does not name a file: %s", settings.log.c_str());
        return false;
    }
    if (!ensureDirectory(settings.log.substr(0, logSeparator))) {
        report("error: cannot create log directory for %s", settings.log.c_str());
        return false;
    }
    if (settings.module.size() > kue::entry::kRemoteStartPathCapacity ||
        settings.config.size() > kue::entry::kRemoteStartPathCapacity ||
        settings.log.size() > kue::entry::kRemoteStartPathCapacity) {
        report("error: injection paths exceed the 4096-byte limit");
        return false;
    }
    return true;
}

bool readBuildIdentity(const std::string& module, std::string& identity) {
    const kue::platform::DescriptorResult opened = kue::platform::openForRead(module.c_str());
    if (opened.descriptor < 0) {
        report("error: cannot open module: %s", module.c_str());
        return false;
    }
    const kue::platform::FileInspection inspection = kue::platform::inspectFile(opened.descriptor);
    if (!inspection.succeeded || inspection.kind != kue::platform::FileKind::Regular ||
        inspection.bytes == 0 || inspection.bytes > kMaximumModuleBytes) {
        static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
        report("error: module is not a regular file within %zu bytes", kMaximumModuleBytes);
        return false;
    }
    std::string content(static_cast<std::size_t>(inspection.bytes), '\0');
    std::size_t loaded = 0;
    while (loaded < content.size()) {
        const kue::platform::ReadResult read = kue::platform::readSome(
            opened.descriptor, std::span<char>(content.data() + loaded, content.size() - loaded));
        if (read.errorCode != 0 || read.bytes == 0)
            break;
        loaded += read.bytes;
    }
    static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
    if (loaded != content.size()) {
        report("error: cannot read module: %s", module.c_str());
        return false;
    }
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = content.find(kBuildIdentityPrefix, position)) != std::string::npos) {
        const std::size_t end = content.find('\0', position);
        const std::string_view record(content.data() + position,
                                      (end == std::string::npos ? content.size() : end) - position);
        if (count == 0)
            identity = std::string(record);
        ++count;
        position += kBuildIdentityPrefix.size();
    }
    if (count != 1) {
        report("error: module lacks one unique build identity: %s", module.c_str());
        return false;
    }
    return true;
}

bool sameName(const wchar_t* left, const wchar_t* right) {
    return ::CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

bool findTargetProcess(const std::string& processName, DWORD& processId) {
    WideText<kWidePathCapacity> wideName;
    if (!widen(processName, wideName)) {
        report("error: GAME_PATTERN must be valid UTF-8 within 4096 bytes");
        return false;
    }
    ScopedHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        report("error: cannot enumerate processes (error %lu)", ::GetLastError());
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> matches;
    if (::Process32FirstW(snapshot.get(), &entry)) {
        do {
            if (sameName(entry.szExeFile, wideName.units.data()) &&
                entry.th32ProcessID != ::GetCurrentProcessId()) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (::Process32NextW(snapshot.get(), &entry));
    }
    if (matches.empty()) {
        report("error: no running game matched '%s'", processName.c_str());
        report("launch Lethal Company first; process-start is not implemented");
        return false;
    }
    if (matches.size() > 1) {
        report("error: multiple matching game processes found");
        return false;
    }
    processId = matches.front();
    return true;
}

bool findRemoteModule(DWORD processId, const std::string& module, RemoteModule& result) {
    result = {};
    WideText<kWidePathCapacity> wideModule;
    if (!widen(module, wideModule))
        return false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        ScopedHandle snapshot(
            ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId));
        if (!snapshot.valid()) {
            if (::GetLastError() == ERROR_BAD_LENGTH) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            report("error: cannot enumerate target modules (error %lu)", ::GetLastError());
            return false;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Module32FirstW(snapshot.get(), &entry)) {
            do {
                if (sameName(entry.szExePath, wideModule.units.data())) {
                    result.base = std::bit_cast<std::uintptr_t>(entry.modBaseAddr);
                    result.found = true;
                    return true;
                }
            } while (::Module32NextW(snapshot.get(), &entry));
        }
        return true;
    }
    report("error: cannot enumerate target modules");
    return false;
}

bool targetIsNative64(HANDLE process) {
    USHORT processMachine = 0;
    USHORT nativeMachine = 0;
    if (!::IsWow64Process2(process, &processMachine, &nativeMachine)) {
        report("error: cannot inspect the target architecture (error %lu)", ::GetLastError());
        return false;
    }
    if (processMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64) {
        report("error: the target is not a native x86-64 process");
        return false;
    }
    return true;
}

bool runRemote(HANDLE process, const void* function, void* parameter, DWORD& exitCode,
               const char* description) {
    ScopedHandle thread(::CreateRemoteThread(
        process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(const_cast<void*>(function)),
        parameter, 0, nullptr));
    if (!thread.valid()) {
        report("error: cannot start the remote %s thread (error %lu)", description,
               ::GetLastError());
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(
        thread.get(),
        static_cast<DWORD>(
            std::chrono::duration_cast<std::chrono::milliseconds>(kRemoteCallTimeout).count()));
    if (waited != WAIT_OBJECT_0) {
        report("error: the remote %s thread did not finish within 30 seconds", description);
        return false;
    }
    if (!::GetExitCodeThread(thread.get(), &exitCode)) {
        report("error: cannot read the remote %s result (error %lu)", description,
               ::GetLastError());
        return false;
    }
    return true;
}

bool writeRemote(HANDLE process, void* destination, const void* source, std::size_t bytes) {
    SIZE_T written = 0;
    return ::WriteProcessMemory(process, destination, source, bytes, &written) && written == bytes;
}

std::uint64_t fileSize(const std::string& path) {
    const kue::platform::DescriptorResult opened = kue::platform::openForRead(path.c_str());
    if (opened.descriptor < 0)
        return 0;
    const kue::platform::FileInspection inspection = kue::platform::inspectFile(opened.descriptor);
    static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
    return inspection.succeeded ? inspection.bytes : 0;
}

bool freshLog(const std::string& path, std::uint64_t startBytes, std::string& fresh) {
    fresh.clear();
    const kue::platform::DescriptorResult opened = kue::platform::openForRead(path.c_str());
    if (opened.descriptor < 0)
        return true;
    const kue::platform::FileInspection inspection = kue::platform::inspectFile(opened.descriptor);
    if (!inspection.succeeded || inspection.bytes < startBytes) {
        static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
        report("error: configured log truncated during injection: %s", path.c_str());
        return false;
    }
    const std::uint64_t freshBytes = inspection.bytes - startBytes;
    if (freshBytes > kMaximumFreshLogBytes) {
        static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
        report("error: fresh log data exceeds %zu bytes: %s", kMaximumFreshLogBytes, path.c_str());
        return false;
    }
    std::string content(static_cast<std::size_t>(inspection.bytes), '\0');
    std::size_t loaded = 0;
    while (loaded < content.size()) {
        const kue::platform::ReadResult read = kue::platform::readSome(
            opened.descriptor, std::span<char>(content.data() + loaded, content.size() - loaded));
        if (read.errorCode != 0 || read.bytes == 0)
            break;
        loaded += read.bytes;
    }
    static_cast<void>(kue::platform::closeDescriptor(opened.descriptor));
    if (loaded < startBytes)
        return true;
    fresh = content.substr(static_cast<std::size_t>(startBytes),
                           loaded - static_cast<std::size_t>(startBytes));
    return true;
}

void printMatchingLines(const std::string& text, std::span<const std::string_view> needles,
                        std::size_t maximumLines) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line(text.data() + start,
                                    (end == std::string::npos ? text.size() : end) - start);
        for (const std::string_view needle : needles) {
            if (line.find(needle) != std::string_view::npos) {
                lines.push_back(line);
                break;
            }
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    const std::size_t first = lines.size() > maximumLines ? lines.size() - maximumLines : 0;
    for (std::size_t index = first; index < lines.size(); ++index)
        std::printf("%.*s\n", static_cast<int>(lines[index].size()), lines[index].data());
    std::fflush(stdout);
}

int waitForRuntime(const Settings& settings, const std::string& identity, std::uint64_t logStart) {
    const auto armDeadline = std::chrono::steady_clock::now() + kArmTimeout;
    std::string fresh;
    bool armed = false;
    while (std::chrono::steady_clock::now() < armDeadline) {
        if (!freshLog(settings.log, logStart, fresh))
            return 1;
        if (fresh.find(identity) != std::string::npos &&
            fresh.find("main-thread HUD installer armed") != std::string::npos) {
            armed = true;
            break;
        }
        std::this_thread::sleep_for(kLogPollInterval);
    }
    if (!armed) {
        report("module started but runtime readiness was not confirmed");
        report("no matching loader trace appeared in %s", settings.log.c_str());
        report("exit the game before retrying; no second copy will run");
        return 1;
    }
    report("injection complete - identity confirmed (log: %s)", settings.log.c_str());
    report("HUD should appear after the late Update hook runs");
    constexpr std::array<std::string_view, 3> installedNeedles = {
        "late HUD installer attempt", "late HUD installer active", "internal Unity HUD installed"};
    constexpr std::array<std::string_view, 5> exhaustedNeedles = {
        "late HUD installer attempt", "mono: target method", "mono: compiled Update",
        "mono: could not allocate", "mono: could not make"};
    constexpr std::array<std::string_view, 3> armNeedles = {
        "late HUD installer attempt", "mono: target method", "main-thread HUD installer armed"};
    const auto hudDeadline = std::chrono::steady_clock::now() + kHudTimeout;
    while (std::chrono::steady_clock::now() < hudDeadline) {
        if (!freshLog(settings.log, logStart, fresh))
            return 1;
        if (fresh.find("internal Unity HUD installed") != std::string::npos) {
            report("HUD delivery hook confirmed");
            printMatchingLines(fresh, installedNeedles, 10);
            return 0;
        }
        if (fresh.find("late HUD installer attempt 5/5") != std::string::npos) {
            report("identity matched, but the live hook exhausted retries:");
            printMatchingLines(fresh, exhaustedNeedles, 14);
            return 1;
        }
        std::this_thread::sleep_for(kLogPollInterval);
    }
    if (!freshLog(settings.log, logStart, fresh))
        return 1;
    printMatchingLines(fresh, armNeedles, 10);
    report("error: HUD delivery was not confirmed within 15 seconds");
    report("module remains loaded; fully exit the game before retrying");
    return 1;
}

const char* startResultName(int result) {
    switch (result) {
    case 0:
        return "running";
    case 1:
        return "configuration failed";
    case 2:
        return "logging failed";
    case 3:
        return "state start failed";
    case 4:
        return "standard exception";
    case 5:
        return "unknown exception";
    case static_cast<int>(kue::entry::RemoteStartFailure::InvalidRequest):
        return "invalid remote request";
    case static_cast<int>(kue::entry::RemoteStartFailure::EnvironmentCaptureFailed):
        return "target environment capture failed";
    case static_cast<int>(kue::entry::RemoteStartFailure::EnvironmentMutationFailed):
        return "target setenv failed";
    default:
        return "unrecognized result";
    }
}

int inject(const Settings& settings) {
    std::string identity;
    if (!readBuildIdentity(settings.module, identity))
        return 1;
    report("build inputs: %s", identity.c_str());
    DWORD processId = 0;
    if (!findTargetProcess(settings.processName, processId))
        return 1;
    ScopedHandle process(::OpenProcess(kTargetAccess, FALSE, processId));
    if (!process.valid()) {
        report("error: cannot open pid %lu (error %lu); run the injector as administrator "
               "if the game runs elevated",
               processId, ::GetLastError());
        return 1;
    }
    if (!targetIsNative64(process.get()))
        return 1;
    RemoteModule remote;
    if (!findRemoteModule(processId, settings.module, remote))
        return 1;
    if (remote.found) {
        report("error: Kue Lethal is already loaded in pid %lu", processId);
        report("fully exit and restart the game before loading this rebuild");
        return 1;
    }
    report("injecting %s into pid %lu (64-bit)", settings.module.c_str(), processId);

    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    const void* loadLibrary =
        kernel ? reinterpret_cast<const void*>(::GetProcAddress(kernel, "LoadLibraryW")) : nullptr;
    const void* freeLibrary =
        kernel ? reinterpret_cast<const void*>(::GetProcAddress(kernel, "FreeLibrary")) : nullptr;
    if (!loadLibrary || !freeLibrary) {
        report("error: cannot resolve the kernel32 loader exports");
        return 1;
    }
    HMODULE local = ::LoadLibraryW([&settings] {
        static WideText<kWidePathCapacity> wide;
        widen(settings.module, wide);
        return wide.units.data();
    }());
    if (!local) {
        report("error: cannot load the module locally (error %lu)", ::GetLastError());
        return 1;
    }
    const void* localStart =
        reinterpret_cast<const void*>(::GetProcAddress(local, "kue_start_remote"));
    if (!localStart) {
        report("error: module lacks the kue_start_remote export");
        ::FreeLibrary(local);
        return 1;
    }
    const std::uintptr_t startOffset =
        std::bit_cast<std::uintptr_t>(localStart) - std::bit_cast<std::uintptr_t>(local);
    ::FreeLibrary(local);

    WideText<kWidePathCapacity> wideModule;
    widen(settings.module, wideModule);
    const std::size_t pathBytes = (wideModule.size + 1) * sizeof(wchar_t);
    RemoteAllocation remotePath(process.get(), pathBytes);
    if (!remotePath.address() ||
        !writeRemote(process.get(), remotePath.address(), wideModule.units.data(), pathBytes)) {
        report("error: cannot write the module path into the target");
        return 1;
    }
    const std::uint64_t logStart = fileSize(settings.log);
    DWORD loadResult = 0;
    if (!runRemote(process.get(), loadLibrary, remotePath.address(), loadResult, "LoadLibraryW"))
        return 1;
    if (!findRemoteModule(processId, settings.module, remote) || !remote.found) {
        report("error: dlopen failed: the target did not load %s (LoadLibraryW result %lu)",
               settings.module.c_str(), loadResult);
        return 1;
    }

    kue::entry::RemoteStartRequest request{};
    request.configPathBytes = static_cast<std::uint32_t>(settings.config.size());
    request.logPathBytes = static_cast<std::uint32_t>(settings.log.size());
    std::memcpy(request.configPath, settings.config.data(), settings.config.size());
    std::memcpy(request.logPath, settings.log.data(), settings.log.size());
    RemoteAllocation remoteRequest(process.get(), sizeof(request));
    const void* remoteStart = std::bit_cast<const void*>(remote.base + startOffset);
    DWORD startResult = 0;
    bool started = false;
    if (remoteRequest.address() &&
        writeRemote(process.get(), remoteRequest.address(), &request, sizeof(request))) {
        if (!runRemote(process.get(), remoteStart, remoteRequest.address(), startResult,
                       "kue_start_remote")) {
            report("module state is unknown; exit the game before retrying");
            return 1;
        }
        started = (static_cast<int>(startResult) & kue::entry::kRemoteStartResultMask) == 0;
    } else {
        report("error: cannot write the start request into the target");
    }
    if (started) {
        report("kue_start ok: base=0x%llx", static_cast<unsigned long long>(remote.base));
        return waitForRuntime(settings, identity, logStart);
    }
    const int code = static_cast<int>(startResult) & kue::entry::kRemoteStartResultMask;
    report("kue_start failed: result=%d (%s)", code, startResultName(code));
    if ((static_cast<int>(startResult) & kue::entry::kRemoteStartRollbackFailedFlag) != 0)
        report("target environment rollback failed");
    else
        report("failed startup target environment restored");
    DWORD freeResult = 0;
    if (runRemote(process.get(), freeLibrary, std::bit_cast<void*>(remote.base), freeResult,
                  "FreeLibrary") &&
        freeResult != 0) {
        report("failed startup module handle released");
    } else {
        report("error: could not release the failed startup module");
    }
    return 1;
}

}

int main() {
    ::SetConsoleOutputCP(CP_UTF8);
    Settings settings;
    if (!resolveSettings(settings))
        return 1;
    return inject(settings);
}
