#ifndef KUE_GAME_PLAYER_SNAPSHOT_H
#define KUE_GAME_PLAYER_SNAPSHOT_H

#include "core/Utf8.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace kue {

inline constexpr std::size_t kPlayerSnapshotCapacity = 64;
inline constexpr std::size_t kPlayerNameCapacity = 2048;

class PlayerSnapshotTransaction;

class PlayerName final {
  public:
    [[nodiscard]] const char* c_str() const noexcept { return mBytes.data(); }

    [[nodiscard]] std::string_view view() const noexcept { return {mBytes.data(), mLength}; }

    [[nodiscard]] std::size_t size() const noexcept { return mLength; }

  private:
    friend class PlayerSnapshotTransaction;

    void assign(std::string_view name) noexcept {
        std::memcpy(mBytes.data(), name.data(), name.size());
        mBytes[name.size()] = '\0';
        mLength = static_cast<std::uint16_t>(name.size());
    }

    std::array<char, kPlayerNameCapacity + 1> mBytes{};
    std::uint16_t mLength = 0;
};

struct PlayerSnapshotPlayer {
    PlayerName name;
    std::uint64_t steamId = 0;
    std::uint64_t clientId = 0;
    int health = 0;
    float insanity = 0.F;
    bool dead = false;
    bool local = false;
    bool controlled = false;
};

struct PlayerSnapshotInput {
    std::string_view name;
    std::uint64_t steamId = 0;
    std::uint64_t clientId = 0;
    int health = 0;
    float insanity = 0.F;
    bool dead = false;
    bool local = false;
    bool controlled = false;
};

enum class PlayerSnapshotReportResult : std::uint8_t {
    None,
    Recorded,
    PriorReportFailed,
    CapacityExceeded,
    EmptyName,
    EmbeddedNull,
    InvalidUtf8,
    NameTooLong
};

struct PlayerSnapshotReportOutcome {
    PlayerSnapshotReportResult result = PlayerSnapshotReportResult::None;
    std::uint16_t entryIndex = 0;
    std::uint64_t actual = 0;
    std::uint64_t limit = 0;
};

enum class PlayerSnapshotPublishResult : std::uint8_t { Published, PriorReportFailed };

struct PlayerSnapshotPublishOutcome {
    PlayerSnapshotPublishResult result = PlayerSnapshotPublishResult::PriorReportFailed;
    PlayerSnapshotReportOutcome failure;
};

class PlayerSnapshot final {
  public:
    using const_iterator = const PlayerSnapshotPlayer*;

    [[nodiscard]] std::size_t size() const noexcept { return mCount; }

    [[nodiscard]] bool empty() const noexcept { return mCount == 0; }

    [[nodiscard]] const PlayerSnapshotPlayer& operator[](std::size_t index) const noexcept {
        return mPlayers[index];
    }

    [[nodiscard]] const_iterator begin() const noexcept { return mPlayers.data(); }

    [[nodiscard]] const_iterator end() const noexcept { return mPlayers.data() + mCount; }

    [[nodiscard]] std::span<const PlayerSnapshotPlayer> view() const noexcept {
        return {mPlayers.data(), mCount};
    }

  private:
    friend class PlayerSnapshotTransaction;

    std::array<PlayerSnapshotPlayer, kPlayerSnapshotCapacity> mPlayers{};
    std::uint16_t mCount = 0;
};

class PlayerSnapshotTransaction final {
  public:
    PlayerSnapshotTransaction() = default;
    PlayerSnapshotTransaction(const PlayerSnapshotTransaction&) = delete;
    PlayerSnapshotTransaction& operator=(const PlayerSnapshotTransaction&) = delete;
    PlayerSnapshotTransaction(PlayerSnapshotTransaction&&) = delete;
    PlayerSnapshotTransaction& operator=(PlayerSnapshotTransaction&&) = delete;

    [[nodiscard]] PlayerSnapshotReportOutcome report(PlayerSnapshotInput input) noexcept {
        if (mFailure.result != PlayerSnapshotReportResult::None) {
            PlayerSnapshotReportOutcome prior = mFailure;
            prior.result = PlayerSnapshotReportResult::PriorReportFailed;
            return prior;
        }

        const std::size_t entryIndex = mCandidate.mCount;
        if (entryIndex == kPlayerSnapshotCapacity) {
            return fail({PlayerSnapshotReportResult::CapacityExceeded,
                         static_cast<std::uint16_t>(entryIndex),
                         static_cast<std::uint64_t>(entryIndex + 1),
                         static_cast<std::uint64_t>(kPlayerSnapshotCapacity)});
        }

        const std::size_t nameBytes = input.name.size();
        if (nameBytes == 0) {
            return fail({PlayerSnapshotReportResult::EmptyName,
                         static_cast<std::uint16_t>(entryIndex), 0, 1});
        }
        if (nameBytes > kPlayerNameCapacity) {
            return fail({PlayerSnapshotReportResult::NameTooLong,
                         static_cast<std::uint16_t>(entryIndex),
                         static_cast<std::uint64_t>(nameBytes),
                         static_cast<std::uint64_t>(kPlayerNameCapacity)});
        }
        if (std::memchr(input.name.data(), 0, nameBytes)) {
            return fail({PlayerSnapshotReportResult::EmbeddedNull,
                         static_cast<std::uint16_t>(entryIndex),
                         static_cast<std::uint64_t>(nameBytes), 0});
        }
        bool ascii = true;
        for (char character : input.name) {
            if (static_cast<unsigned char>(character) > 0x7fU) {
                ascii = false;
                break;
            }
        }
        if (!ascii && !isValidUtf8(input.name)) {
            return fail({PlayerSnapshotReportResult::InvalidUtf8,
                         static_cast<std::uint16_t>(entryIndex),
                         static_cast<std::uint64_t>(nameBytes), 0});
        }

        PlayerSnapshotPlayer& destination = mCandidate.mPlayers[entryIndex];
        destination.name.assign(input.name);
        destination.steamId = input.steamId;
        destination.clientId = input.clientId;
        destination.health = input.health;
        destination.insanity = input.insanity;
        destination.dead = input.dead;
        destination.local = input.local;
        destination.controlled = input.controlled;
        mCandidate.mCount = static_cast<std::uint16_t>(entryIndex + 1);
        return {PlayerSnapshotReportResult::Recorded, static_cast<std::uint16_t>(entryIndex), 0, 0};
    }

    [[nodiscard]] PlayerSnapshotPublishOutcome publish(PlayerSnapshot& destination) const noexcept {
        if (mFailure.result != PlayerSnapshotReportResult::None)
            return {PlayerSnapshotPublishResult::PriorReportFailed, mFailure};
        destination = mCandidate;
        return {PlayerSnapshotPublishResult::Published, {}};
    }

  private:
    [[nodiscard]] PlayerSnapshotReportOutcome fail(PlayerSnapshotReportOutcome failure) noexcept {
        mFailure = failure;
        return failure;
    }

    PlayerSnapshot mCandidate;
    PlayerSnapshotReportOutcome mFailure;
};

static_assert(sizeof(PlayerSnapshot) <= std::size_t{140} * std::size_t{1024});
static_assert(sizeof(PlayerSnapshotTransaction) <= std::size_t{140} * std::size_t{1024});
static_assert(std::is_same_v<decltype(PlayerSnapshotPlayer::steamId), std::uint64_t>);
static_assert(std::is_same_v<decltype(PlayerSnapshotPlayer::clientId), std::uint64_t>);

}

#endif
