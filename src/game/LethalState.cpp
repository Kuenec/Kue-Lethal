#include "game/LethalState.h"

#include "core/Log.h"
#include "mono/UnityMetadata.h"
#include "overlay/InternalHud.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace kue {

using mono::MonoArray;
using mono::MonoClass;
using mono::MonoClassField;
using mono::MonoMethod;
using mono::MonoObject;

namespace {

constexpr int kLocalPlayerRefreshMs = 1500;
constexpr auto kWorkerPreparationTimeout = std::chrono::seconds(1);
constexpr std::uint8_t kStatusLogPeriod = 40;

constexpr std::uint8_t nextStatusWait(std::uint8_t current) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(current) + 1U) % kStatusLogPeriod);
}

static_assert(nextStatusWait(0) == 1 && nextStatusWait(kStatusLogPeriod - 1U) == 0);

std::string internalHudPath() {
    if (const char* overridePath = std::getenv("KUE_INTERNAL_HUD")) {
        if (*overridePath)
            return overridePath;
    }
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&internalHudPath), &info) || !info.dli_fname)
        return {};
    std::string modulePath(info.dli_fname);
    const std::string::size_type slash = modulePath.find_last_of('/');
    if (slash == std::string::npos)
        return "managed/KueInternalHud.dll";
    return modulePath.substr(0, slash) + "/managed/KueInternalHud.dll";
}

void reportWorkerFailure(const char* detail) noexcept {
    try {
        logFormat(LogLevel::Error, "lethal: worker failed: %s", detail);
    } catch (...) {
        if (std::fprintf(stderr, "[kue] lethal worker failed: %s\n", detail) < 0 ||
            std::fflush(stderr) != 0) {
            std::terminate();
        }
    }
}

}

LethalState::LethalState() = default;
LethalState::~LethalState() {
    stop();
}

bool LethalState::start() {
    std::unique_lock<std::mutex> lifecycleLock(mLifecycleMutex);
    if (mWorkerState.load() != LethalWorkerState::Ready)
        return false;
    mMetadataFailureReporter.reset();
    mGameClassFailureReporter.reset();
    std::unique_lock<std::mutex> waitLock(mWaitMutex);
    mWorkerState = LethalWorkerState::Preparing;
    try {
        mThread = std::thread([this] { workerEntry(); });
    } catch (const std::system_error& exception) {
        mWorkerState = LethalWorkerState::Ready;
        KUE_ERR("lethal: worker creation failed: %s", exception.what());
        return false;
    } catch (...) {
        mWorkerState = LethalWorkerState::Ready;
        throw;
    }
    bool acknowledged = false;
    bool running = false;
    std::exception_ptr waitFailure;
    try {
        acknowledged = mWaitCondition.wait_for(waitLock, kWorkerPreparationTimeout, [this] {
            return mWorkerState.load() != LethalWorkerState::Preparing;
        });
        if (!acknowledged) {
            mWorkerState = LethalWorkerState::Stopped;
        } else {
            LethalWorkerState expected = LethalWorkerState::Prepared;
            running = mWorkerState.compare_exchange_strong(expected, LethalWorkerState::Running);
        }
    } catch (...) {
        if (!waitLock.owns_lock())
            std::terminate();
        mWorkerState = LethalWorkerState::Stopped;
        waitFailure = std::current_exception();
    }
    waitLock.unlock();
    mWaitCondition.notify_all();
    if (running)
        return true;
    joinWorkerOrTerminate();
    if (waitFailure)
        std::rethrow_exception(waitFailure);
    if (!acknowledged)
        KUE_ERR("lethal: worker preparation timed out after %lld ms",
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(kWorkerPreparationTimeout)
                        .count()));
    return false;
}

void LethalState::joinWorkerOrTerminate() noexcept {
    if (!mThread.joinable())
        return;
    try {
        mThread.join();
    } catch (const std::exception& exception) {
        reportWorkerFailure(exception.what());
        std::terminate();
    } catch (...) {
        reportWorkerFailure("thread join raised an unknown exception");
        std::terminate();
    }
}

void LethalState::stop() {
    std::lock_guard<std::mutex> lifecycleLock(mLifecycleMutex);
    {
        std::lock_guard<std::mutex> waitLock(mWaitMutex);
        const LethalWorkerState state = mWorkerState.load();
        if (state == LethalWorkerState::Ready || state == LethalWorkerState::Preparing ||
            state == LethalWorkerState::Prepared || state == LethalWorkerState::Running) {
            mWorkerState = LethalWorkerState::Stopped;
        }
    }
    mWaitCondition.notify_all();
    joinWorkerOrTerminate();
}

void LethalState::waitForNextPoll(unsigned delayMilliseconds) {
    std::unique_lock<std::mutex> waitLock(mWaitMutex);
    mWaitCondition.wait_for(waitLock, std::chrono::milliseconds(delayMilliseconds),
                            [this] { return mWorkerState.load() != LethalWorkerState::Running; });
}

void LethalState::apply(const Config& cfg) {
    mPollFps = cfg.pollRate;
}

PlayerFrameSnapshot LethalState::readPlayerFrame() const {
    std::lock_guard<std::mutex> lock(mSnapshotMutex);
    return mPlayerFrame;
}

const char* LethalState::status() const {
    return mStatus.load();
}

LethalWorkerState LethalState::workerState() const noexcept {
    return mWorkerState.load();
}

void MetadataFailureReporter::reset() noexcept {
    mFailureCount = 0;
}

MetadataFailureReporter::MetadataFailureRecordResult
MetadataFailureReporter::record(const MetadataFailure& failure) noexcept {
    for (std::uint8_t index = 0; index < mFailureCount; ++index) {
        const MetadataFailure& recorded = mFailures[static_cast<std::size_t>(index)];
        if (recorded.kind == failure.kind && recorded.type == failure.type &&
            recorded.name == failure.name && recorded.status == failure.status) {
            return MetadataFailureRecordResult::Duplicate;
        }
    }
    if (static_cast<std::size_t>(mFailureCount) == mFailures.size())
        return MetadataFailureRecordResult::CapacityExceeded;
    mFailures[static_cast<std::size_t>(mFailureCount)] = failure;
    mFailureCount = static_cast<std::uint8_t>(mFailureCount + 1U);
    return MetadataFailureRecordResult::Recorded;
}

void MetadataFailureReporter::report(MetadataMemberKind kind, const MonoClass* type,
                                     const char* name, unity::MetadataLookupStatus status) {
    if (status == unity::MetadataLookupStatus::Resolved)
        return;
    const char* kindName = nullptr;
    switch (kind) {
    case MetadataMemberKind::Field:
        kindName = "field";
        break;
    case MetadataMemberKind::Method:
        kindName = "method";
        break;
    }
    const MetadataFailure failure{.type = type,
                                  .name = name ? std::string_view{name} : std::string_view{},
                                  .status = status,
                                  .kind = kind};
    switch (record(failure)) {
    case MetadataFailureRecordResult::Duplicate:
        return;
    case MetadataFailureRecordResult::CapacityExceeded:
        KUE_ERR("lethal: %s metadata lookup failed type=%p name=%s status=%s "
                "diagnostic-ledger-capacity=%zu",
                kindName, static_cast<const void*>(type), name ? name : "<null>",
                unity::metadataLookupStatusName(status), mFailures.size());
        return;
    case MetadataFailureRecordResult::Recorded:
        KUE_ERR("lethal: %s metadata lookup failed type=%p name=%s status=%s", kindName,
                static_cast<const void*>(type), name ? name : "<null>",
                unity::metadataLookupStatusName(status));
        return;
    }
}

void GameClassFailureReporter::reset() noexcept {
    mFailureCount = 0;
}

void GameClassFailureReporter::report(GameClassKind kind, mono::ClassLookupStatus status) {
    if (status == mono::ClassLookupStatus::Resolved)
        return;
    for (std::uint8_t index = 0; index < mFailureCount; ++index) {
        const Failure& failure = mFailures[static_cast<std::size_t>(index)];
        if (failure.kind == kind && failure.status == status)
            return;
    }

    const char* namespaceName = "";
    const char* className = nullptr;
    switch (kind) {
    case GameClassKind::Player:
        namespaceName = "GameNetcodeStuff";
        className = "PlayerControllerB";
        break;
    case GameClassKind::Network:
        className = "GameNetworkManager";
        break;
    case GameClassKind::Round:
        className = "StartOfRound";
        break;
    case GameClassKind::Menu:
        className = "MenuManager";
        break;
    }

    if (static_cast<std::size_t>(mFailureCount) == mFailures.size()) {
        KUE_ERR("lethal: game class lookup failed image=Assembly-CSharp namespace=%s class=%s "
                "status=%s diagnostic-ledger-capacity=%zu",
                namespaceName, className, unity::gameClassLookupStatusName(status),
                mFailures.size());
        return;
    }
    mFailures[static_cast<std::size_t>(mFailureCount)] = {.kind = kind, .status = status};
    mFailureCount = static_cast<std::uint8_t>(mFailureCount + 1U);
    KUE_ERR("lethal: game class lookup failed image=Assembly-CSharp namespace=%s class=%s "
            "status=%s",
            namespaceName, className, unity::gameClassLookupStatusName(status));
}

MonoClassField* LethalState::requiredField(const MonoClass* type, const char* name,
                                           unity::MetadataLookupResult<MonoClassField> result) {
    if (result.status == unity::MetadataLookupStatus::Resolved)
        return result.member;
    mMetadataFailureReporter.report(MetadataMemberKind::Field, type, name, result.status);
    return nullptr;
}

MonoMethod* LethalState::requiredMethod(const MonoClass* type, const char* name,
                                        unity::MetadataLookupResult<MonoMethod> result) {
    if (result.status == unity::MetadataLookupStatus::Resolved)
        return result.member;
    mMetadataFailureReporter.report(MetadataMemberKind::Method, type, name, result.status);
    return nullptr;
}

MonoObject* LethalState::readRefField(MonoObject* object, const MonoClass* type, const char* name) {
    MonoClassField* field = requiredField(type, name, unity::cachedField(type, name));
    if (!field)
        return nullptr;
    MonoObject* value = nullptr;
    if (!mono::readInstanceObject(object, field, value))
        return nullptr;
    return value;
}

LethalState::StaticObjectRead LethalState::readStaticNamed(MonoClass* type, const char* name,
                                                           MonoObject*& object) {
    const unity::MetadataLookupResult<MonoClassField> lookup = unity::cachedField(type, name);
    if (lookup.status == unity::MetadataLookupStatus::MissingMember)
        return StaticObjectRead::Absent;
    MonoClassField* field = requiredField(type, name, lookup);
    if (!field)
        return StaticObjectRead::Failed;
    if (!mono::readStaticObject(type, field, object))
        return StaticObjectRead::Failed;
    return object ? StaticObjectRead::Found : StaticObjectRead::Absent;
}

MonoObject* LethalState::resolveInstance(MonoClass* type) {
    if (!type)
        return nullptr;
    static constexpr std::array<const char*, 3> names = {"<Instance>k__BackingField", "_instance",
                                                         "Instance"};
    for (const char* name : names) {
        MonoObject* object = nullptr;
        const StaticObjectRead result = readStaticNamed(type, name, object);
        if (result == StaticObjectRead::Found)
            return object;
        if (result == StaticObjectRead::Failed)
            return nullptr;
    }
    return nullptr;
}

MonoClass* LethalState::resolveGameClass(GameClassKind kind, GameClassState& state,
                                         unity::GameClassLocation location) {
    if (state.type)
        return state.type;
    if (state.attempted && !unity::gameClassLookupMayRetry(state.status))
        return nullptr;
    const mono::ClassLookupResult result = unity::gameClass(location);
    state = {.type = result.type, .status = result.status, .attempted = true};
    if (result.status != mono::ClassLookupStatus::Resolved)
        mGameClassFailureReporter.report(kind, result.status);
    return result.type;
}

MonoObject* LethalState::resolveRoundInstance() {
    MonoClass* const round = resolveGameClass(GameClassKind::Round, mClasses.round,
                                              {.namespaceName = "", .className = "StartOfRound"});
    return resolveInstance(round);
}

MonoObject* LethalState::resolveLocalPlayer() {
    const bool networkWasResolved = mClasses.network.type != nullptr;
    MonoClass* const network =
        resolveGameClass(GameClassKind::Network, mClasses.network,
                         {.namespaceName = "", .className = "GameNetworkManager"});
    if (network && !networkWasResolved) {
        KUE_INFO("lethal: GameNetworkManager=%p", static_cast<void*>(network));
    }
    if (network) {
        MonoObject* inst = resolveInstance(network);
        if (inst) {
            MonoObject* local = readRefField(inst, network, "localPlayerController");
            if (local)
                return local;
        }
    }
    if (MonoObject* round = resolveRoundInstance()) {
        MonoObject* local = readRefField(round, mClasses.round.type, "localPlayerController");
        if (local)
            return local;
    }
    return nullptr;
}

bool LethalState::gatherPlayers(PlayerSnapshotTransaction& transaction) {
    MonoObject* round = resolveRoundInstance();
    if (!round || !mClasses.player.type)
        return false;

    MonoClassField* scriptsField =
        requiredField(mClasses.round.type, "allPlayerScripts",
                      unity::cachedField(mClasses.round.type, "allPlayerScripts"));
    if (!scriptsField)
        return false;
    MonoObject* arrObj = nullptr;
    if (!mono::readInstanceObject(round, scriptsField, arrObj) || !arrObj)
        return false;

    const mono::ReferenceArrayView players(reinterpret_cast<const MonoArray*>(arrObj));
    if (players.status() != mono::RuntimeArrayAccessStatus::Success) {
        KUE_ERR("lethal: player array length failed status=%s",
                mono::runtimeArrayAccessStatusName(players.status()));
        return false;
    }
    const std::size_t n = players.size();
    if (n > kPlayerSnapshotCapacity) {
        KUE_ERR("lethal: player array length=%zu exceeds capacity=%zu", n, kPlayerSnapshotCapacity);
        return false;
    }

    MonoClassField* fName =
        requiredField(mClasses.player.type, "playerUsername",
                      unity::cachedField(mClasses.player.type, "playerUsername"));
    MonoClassField* fSteam =
        requiredField(mClasses.player.type, "playerSteamId",
                      unity::cachedField(mClasses.player.type, "playerSteamId"));
    MonoClassField* fId = requiredField(mClasses.player.type, "playerClientId",
                                        unity::cachedField(mClasses.player.type, "playerClientId"));
    MonoClassField* fHp = requiredField(mClasses.player.type, "health",
                                        unity::cachedField(mClasses.player.type, "health"));
    MonoClassField* fIns = requiredField(mClasses.player.type, "insanityLevel",
                                         unity::cachedField(mClasses.player.type, "insanityLevel"));
    MonoClassField* fDead = requiredField(mClasses.player.type, "isPlayerDead",
                                          unity::cachedField(mClasses.player.type, "isPlayerDead"));
    MonoClassField* fCtrl =
        requiredField(mClasses.player.type, "isPlayerControlled",
                      unity::cachedField(mClasses.player.type, "isPlayerControlled"));
    MonoClassField* fDisc =
        requiredField(mClasses.player.type, "disconnectedMidGame",
                      unity::cachedField(mClasses.player.type, "disconnectedMidGame"));
    if (!fName || !fSteam || !fId || !fHp || !fIns || !fDead || !fCtrl || !fDisc)
        return false;

    for (std::size_t i = 0; i < n; ++i) {
        const mono::RuntimeArrayObjectResult element = players.object(i);
        if (element.status != mono::RuntimeArrayAccessStatus::Success) {
            KUE_ERR("lethal: player array read failed index=%zu status=%s", i,
                    mono::runtimeArrayAccessStatusName(element.status));
            return false;
        }
        MonoObject* p = element.object;
        if (!p)
            continue;

        bool controlled = false;
        bool dead = false;
        bool disconnected = false;
        std::uint64_t steamId = 0;
        if (!mono::readInstanceBool(p, fCtrl, controlled) ||
            !mono::readInstanceBool(p, fDead, dead) ||
            !mono::readInstanceBool(p, fDisc, disconnected) ||
            !mono::readInstanceU64(p, fSteam, steamId)) {
            KUE_ERR("lethal: required player state read failed index=%zu", i);
            return false;
        }

        const bool local = (p == mLocalPlayer);

        if (!local && !controlled && (!dead || steamId == 0))
            continue;
        if (!local && disconnected)
            continue;

        std::array<char, mono::kManagedStringMaxUtf8Bytes> nameBytes{};
        MonoObject* nameObject = nullptr;
        if (!mono::readInstanceObject(p, fName, nameObject) || !nameObject) {
            KUE_ERR("lethal: required player name read failed index=%zu", i);
            return false;
        }
        const mono::ManagedStringUtf8Result nameResult =
            mono::readManagedStringUtf8(nameObject, {nameBytes.data(), nameBytes.size()});
        if (nameResult.status != mono::ManagedStringUtf8Status::Success) {
            KUE_ERR("lethal: required player name conversion failed index=%zu status=%s "
                    "code-units=%d validated-code-units=%zu utf8-bytes=%zu",
                    i, mono::managedStringUtf8StatusName(nameResult.status),
                    nameResult.codeUnitCount, nameResult.validatedCodeUnits, nameResult.utf8Bytes);
            return false;
        }
        const std::string_view name = nameResult.text;
        if (!local && steamId == 0 && !controlled)
            continue;
        if (!local && (name.empty() || name == "Player" || name == "Player #0"))
            continue;

        std::uint64_t clientId = 0;
        int health = 0;
        float insanity = 0.F;
        if (!mono::readInstanceU64(p, fId, clientId) || !mono::readInstanceInt(p, fHp, health) ||
            !mono::readInstanceFloat(p, fIns, insanity)) {
            KUE_ERR("lethal: required player details read failed index=%zu", i);
            return false;
        }
        const std::string_view displayName =
            name.empty() ? std::string_view{local ? "You" : "Player"} : name;
        const PlayerSnapshotReportOutcome report = transaction.report({.name = displayName,
                                                                       .steamId = steamId,
                                                                       .clientId = clientId,
                                                                       .health = health,
                                                                       .insanity = insanity,
                                                                       .dead = dead,
                                                                       .local = local,
                                                                       .controlled = controlled});
        if (report.result != PlayerSnapshotReportResult::Recorded) {
            KUE_ERR("lethal: player snapshot rejected index=%zu result=%u entry=%u actual=%llu "
                    "limit=%llu",
                    i, static_cast<unsigned>(report.result),
                    static_cast<unsigned>(report.entryIndex),
                    static_cast<unsigned long long>(report.actual),
                    static_cast<unsigned long long>(report.limit));
            return false;
        }
    }
    return true;
}

void LethalState::mainThreadInstallThunk(void* context) {
    if (context)
        static_cast<LethalState*>(context)->installHudOnMainThread();
}

void LethalState::installHudOnMainThread() {
    if (mHudInstallAttempted.exchange(true))
        return;

    const std::string hudPath = internalHudPath();
    MonoMethod* install = nullptr;
    if (internalhud::registerManagedBridge()) {
        install = mono::loadManagedMethod({.assemblyPath = hudPath.c_str(),
                                           .namespaceName = "Kue.Internal",
                                           .className = "HudBootstrap",
                                           .methodName = "Install",
                                           .parameterCount = 0});
    }
    if (!install) {
        KUE_ERR("lethal: internal Unity HUD unavailable at %s", hudPath.c_str());
        mono::clearManagedMethodCallback();
        return;
    }

    const mono::StaticInvocationResult invocation = mono::invokeStatic(install);
    if (invocation.status != mono::StaticInvocationStatus::Succeeded) {
        KUE_ERR("lethal: internal Unity HUD Install threw an exception");
    } else {
        KUE_INFO("lethal: internal Unity HUD installed");
    }
    mono::clearManagedMethodCallback();
}

void LethalState::run() {
    const auto pollIntervalMs = [this]() -> unsigned {
        const float fps = mPollFps.load();
        return fps > 1.f ? static_cast<unsigned>(1000.f / fps) : 33u;
    };

    while (mWorkerState.load() == LethalWorkerState::Running) {
        if (!mono::ready()) {
            if (mono::resolve()) {
                KUE_INFO("mono runtime resolved");
                mStatus.store("runtime resolved");
            } else {
                waitForNextPoll(250);
                continue;
            }
        }

        if (!mClasses.player.type) {
            if (!mClasses.player.attempted)
                KUE_INFO("lethal: resolving game classes...");
            MonoClass* const player = resolveGameClass(
                GameClassKind::Player, mClasses.player,
                {.namespaceName = "GameNetcodeStuff", .className = "PlayerControllerB"});
            static_cast<void>(
                resolveGameClass(GameClassKind::Network, mClasses.network,
                                 {.namespaceName = "", .className = "GameNetworkManager"}));
            static_cast<void>(resolveGameClass(GameClassKind::Round, mClasses.round,
                                               {.namespaceName = "", .className = "StartOfRound"}));
            static_cast<void>(resolveGameClass(GameClassKind::Menu, mClasses.menu,
                                               {.namespaceName = "", .className = "MenuManager"}));
            if (!player) {
                mStatus.store(unity::gameClassLookupMayRetry(mClasses.player.status)
                                  ? "waiting for Assembly-CSharp"
                                  : "PlayerControllerB unavailable");
                waitForNextPoll(500);
                continue;
            }
            KUE_INFO("lethal: runtime classes resolved");
            if (!mHudHookInstallTried) {
                mHudHookInstallTried = true;
                MonoMethod* update =
                    requiredMethod(player, "Update", unity::cachedMethod(player, "Update", 0));
                if (update && mono::installManagedMethodCallback(
                                  update, &LethalState::mainThreadInstallThunk, this)) {
                    KUE_INFO("lethal: main-thread HUD installer armed");
                    mHudHookArmedAt = std::chrono::steady_clock::now();
                } else {
                    KUE_WARN("lethal: profiler HUD installer unavailable");
                }
            }
            mStatus.store("waiting for local player");
        }

        if (!mHudInstallAttempted.load() && !mLateHudHookInstalled &&
            mHudHookArmedAt != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - mHudHookArmedAt >= std::chrono::seconds(2) &&
            mLateHudHookAttempts < 5) {
            ++mLateHudHookAttempts;
            KUE_INFO("lethal: late HUD installer attempt %d/5", mLateHudHookAttempts);
            MonoMethod* update =
                requiredMethod(mClasses.player.type, "Update",
                               unity::cachedMethod(mClasses.player.type, "Update", 0));
            MonoMethod* menuUpdate = nullptr;
            MonoClass* const menu =
                resolveGameClass(GameClassKind::Menu, mClasses.menu,
                                 {.namespaceName = "", .className = "MenuManager"});
            if (menu) {
                menuUpdate = requiredMethod(menu, "Update", unity::cachedMethod(menu, "Update", 0));
            }
            bool installed = update && mono::installCompiledMethodCallback(
                                           update, &LethalState::mainThreadInstallThunk, this);
            if (!installed && menuUpdate) {
                KUE_INFO("lethal: trying title-menu HUD installer");
                installed = mono::installCompiledMethodCallback(
                    menuUpdate, &LethalState::mainThreadInstallThunk, this);
            }
            if (installed) {
                mLateHudHookInstalled = true;
                KUE_INFO("lethal: late HUD installer active");
            } else {
                mHudHookArmedAt = std::chrono::steady_clock::now();
            }
        }

        const bool gotFrame = poll();
        if (!gotFrame) {
            mStatusWaits = nextStatusWait(mStatusWaits);
            if (mStatusWaits == 1)
                KUE_INFO("lethal: status=%s", mStatus.load());
        }
        waitForNextPoll(gotFrame ? pollIntervalMs() : 120U);
    }
}

void LethalState::workerEntry() noexcept {
    {
        std::unique_lock<std::mutex> waitLock(mWaitMutex);
        if (mWorkerState.load() == LethalWorkerState::Stopped)
            return;
        if (mWorkerState.load() != LethalWorkerState::Preparing) {
            mWorkerState = LethalWorkerState::Failed;
            waitLock.unlock();
            mWaitCondition.notify_all();
            reportWorkerFailure("worker entered with an invalid preparation state");
            return;
        }
        mWorkerState = LethalWorkerState::Prepared;
        mWaitCondition.notify_all();
        mWaitCondition.wait(waitLock,
                            [this] { return mWorkerState.load() != LethalWorkerState::Prepared; });
        if (mWorkerState.load() != LethalWorkerState::Running)
            return;
    }
    try {
        run();
    } catch (const std::exception& exception) {
        mStatus.store("worker failed");
        mWorkerState = LethalWorkerState::Failed;
        reportWorkerFailure(exception.what());
    } catch (...) {
        mStatus.store("worker failed");
        mWorkerState = LethalWorkerState::Failed;
        reportWorkerFailure("unknown exception");
    }
    try {
        mono::detachCurrentThread();
    } catch (const std::exception& exception) {
        mStatus.store("worker failed");
        mWorkerState = LethalWorkerState::Failed;
        reportWorkerFailure(exception.what());
    } catch (...) {
        mStatus.store("worker failed");
        mWorkerState = LethalWorkerState::Failed;
        reportWorkerFailure("thread detach raised an unknown exception");
    }
}

bool LethalState::poll() {
    if (!mClasses.player.type)
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (!mLocalPlayer ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastPlayerRecheck).count() >
            kLocalPlayerRefreshMs) {
        mLastPlayerRecheck = now;
        MonoObject* resolved = resolveLocalPlayer();
        if (resolved != mLocalPlayer) {
            mLocalPlayer = resolved;
        }
    }

    PlayerSnapshotTransaction players;
    if (!mLocalPlayer) {
        mStatus.store("waiting for local player");
        PlayerSnapshotPublishOutcome publish;
        {
            std::lock_guard<std::mutex> lock(mSnapshotMutex);
            publish = players.publish(mPlayerFrame.players);
            if (publish.result == PlayerSnapshotPublishResult::Published) {
                mPlayerFrame.sprintMeter = 0.F;
                mPlayerFrame.carryWeight = 0.F;
            }
        }
        if (publish.result != PlayerSnapshotPublishResult::Published) {
            KUE_ERR("lethal: empty player snapshot publication failed result=%u",
                    static_cast<unsigned>(publish.result));
        }
        return false;
    }

    MonoClassField* sprint = requiredField(mClasses.player.type, "sprintMeter",
                                           unity::cachedField(mClasses.player.type, "sprintMeter"));
    MonoClassField* weight = requiredField(mClasses.player.type, "carryWeight",
                                           unity::cachedField(mClasses.player.type, "carryWeight"));
    if (!sprint || !weight)
        return false;
    float sprintMeter = 0.F;
    float carryWeight = 0.F;
    if (!mono::readInstanceFloat(mLocalPlayer, sprint, sprintMeter) ||
        !mono::readInstanceFloat(mLocalPlayer, weight, carryWeight)) {
        KUE_ERR("lethal: required local-player state read failed");
        return false;
    }

    if (!gatherPlayers(players))
        return false;
    PlayerSnapshotPublishOutcome publish;
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);
        publish = players.publish(mPlayerFrame.players);
        if (publish.result == PlayerSnapshotPublishResult::Published) {
            mPlayerFrame.sprintMeter = sprintMeter;
            mPlayerFrame.carryWeight = carryWeight;
        }
    }
    if (publish.result != PlayerSnapshotPublishResult::Published) {
        KUE_ERR("lethal: player snapshot publication failed result=%u report=%u entry=%u",
                static_cast<unsigned>(publish.result),
                static_cast<unsigned>(publish.failure.result),
                static_cast<unsigned>(publish.failure.entryIndex));
        return false;
    }
    mStatus.store("attached");
    return true;
}

}
