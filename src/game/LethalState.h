#ifndef KUE_GAME_LETHAL_STATE_H
#define KUE_GAME_LETHAL_STATE_H

#include "core/Config.h"
#include "game/Game.h"
#include "mono/UnityMetadata.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <thread>

namespace kue {

enum class LethalWorkerState : std::uint8_t {
    Ready,
    Preparing,
    Prepared,
    Running,
    Stopped,
    Failed
};

enum class MetadataMemberKind : std::uint8_t { Field, Method };

class MetadataFailureReporter final {
  public:
    static constexpr std::size_t kCapacity =
        unity::kFieldMetadataCacheCapacity + unity::kMethodMetadataCacheCapacity;

    void reset() noexcept;
    void report(MetadataMemberKind kind, const mono::MonoClass* type, const char* name,
                unity::MetadataLookupStatus status);

  private:
    enum class MetadataFailureRecordResult : std::uint8_t { Recorded, Duplicate, CapacityExceeded };

    struct MetadataFailure final {
        const mono::MonoClass* type = nullptr;
        std::string_view name;
        unity::MetadataLookupStatus status = unity::MetadataLookupStatus::Resolved;
        MetadataMemberKind kind = MetadataMemberKind::Field;
    };

    static constexpr std::size_t kSingletonInstanceFieldRequirementCount = 6;
    static constexpr std::size_t kLocalPlayerReferenceFieldRequirementCount = 2;
    static constexpr std::size_t kPlayerSnapshotFieldRequirementCount = 9;
    static constexpr std::size_t kLocalPlayerStateFieldRequirementCount = 2;
    static constexpr std::size_t kHudInstallMethodRequirementCount = 2;
    static constexpr std::size_t kFieldMetadataFailureCapacity =
        kSingletonInstanceFieldRequirementCount + kLocalPlayerReferenceFieldRequirementCount +
        kPlayerSnapshotFieldRequirementCount + kLocalPlayerStateFieldRequirementCount;
    static_assert(kFieldMetadataFailureCapacity == unity::kFieldMetadataCacheCapacity);
    static_assert(kHudInstallMethodRequirementCount == unity::kMethodMetadataCacheCapacity);
    static_assert(kCapacity == kFieldMetadataFailureCapacity + kHudInstallMethodRequirementCount);
    static_assert(kCapacity == 21);

    [[nodiscard]] MetadataFailureRecordResult record(const MetadataFailure& failure) noexcept;
    std::array<MetadataFailure, kCapacity> mFailures{};
    std::uint8_t mFailureCount = 0;
};

enum class GameClassKind : std::uint8_t { Player, Network, Round, Menu };

class GameClassFailureReporter final {
  public:
    static constexpr std::size_t kClassCount = 4;
    static constexpr std::size_t kFailureStatusCount = 5;
    static constexpr std::size_t kCapacity = kClassCount * kFailureStatusCount;

    void reset() noexcept;
    void report(GameClassKind kind, mono::ClassLookupStatus status);

  private:
    struct Failure final {
        GameClassKind kind = GameClassKind::Player;
        mono::ClassLookupStatus status = mono::ClassLookupStatus::RuntimeUnavailable;
    };

    std::array<Failure, kCapacity> mFailures{};
    std::uint8_t mFailureCount = 0;
};

class LethalState final {
  public:
    LethalState();
    ~LethalState();

    [[nodiscard]] bool start();
    void stop();
    void apply(const Config& cfg);
    [[nodiscard]] PlayerFrameSnapshot readPlayerFrame() const;
    const char* status() const;
    [[nodiscard]] LethalWorkerState workerState() const noexcept;

  private:
    enum class StaticObjectRead : std::uint8_t { Found, Absent, Failed };

    struct GameClassState final {
        mono::MonoClass* type = nullptr;
        mono::ClassLookupStatus status = mono::ClassLookupStatus::RuntimeUnavailable;
        bool attempted = false;
    };

    struct Classes final {
        GameClassState player;
        GameClassState network;
        GameClassState round;
        GameClassState menu;
    };

    void run();
    void workerEntry() noexcept;
    void joinWorkerOrTerminate() noexcept;
    void waitForNextPoll(unsigned delayMilliseconds);
    bool poll();
    mono::MonoClassField* requiredField(const mono::MonoClass* type, const char* name,
                                        unity::MetadataLookupResult<mono::MonoClassField> result);
    mono::MonoMethod* requiredMethod(const mono::MonoClass* type, const char* name,
                                     unity::MetadataLookupResult<mono::MonoMethod> result);
    mono::MonoObject* readRefField(mono::MonoObject* object, const mono::MonoClass* type,
                                   const char* name);
    StaticObjectRead readStaticNamed(mono::MonoClass* type, const char* name,
                                     mono::MonoObject*& object);
    mono::MonoObject* resolveInstance(mono::MonoClass* type);
    mono::MonoClass* resolveGameClass(GameClassKind kind, GameClassState& state,
                                      unity::GameClassLocation location);
    mono::MonoObject* resolveLocalPlayer();
    mono::MonoObject* resolveRoundInstance();
    bool gatherPlayers(PlayerSnapshotTransaction& transaction);
    void installHudOnMainThread();
    static void mainThreadInstallThunk(void* context);
    std::thread mThread;
    std::mutex mLifecycleMutex;
    std::mutex mWaitMutex;
    std::condition_variable mWaitCondition;
    mutable std::mutex mSnapshotMutex;
    PlayerFrameSnapshot mPlayerFrame;
    std::atomic<LethalWorkerState> mWorkerState{LethalWorkerState::Ready};
    std::atomic<float> mPollFps{30.f};
    std::atomic<const char*> mStatus{"scanning runtime"};
    MetadataFailureReporter mMetadataFailureReporter;
    GameClassFailureReporter mGameClassFailureReporter;
    Classes mClasses{};
    mono::MonoObject* mLocalPlayer = nullptr;
    bool mHudHookInstallTried = false;
    std::atomic<bool> mHudInstallAttempted{false};
    bool mLateHudHookInstalled = false;
    int mLateHudHookAttempts = 0;
    std::uint8_t mStatusWaits = 0;
    std::chrono::steady_clock::time_point mHudHookArmedAt;
    std::chrono::steady_clock::time_point mLastPlayerRecheck;
};

}

#endif
