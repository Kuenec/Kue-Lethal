#ifndef KUE_CORE_CONFIG_H
#define KUE_CORE_CONFIG_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace kue {

enum class MenuKey : std::uint8_t {
    Space = 1,
    Enter = 2,
    Tab = 3,
    A = 15,
    B = 16,
    C = 17,
    D = 18,
    E = 19,
    F = 20,
    G = 21,
    H = 22,
    I = 23,
    J = 24,
    K = 25,
    L = 26,
    M = 27,
    N = 28,
    O = 29,
    P = 30,
    Q = 31,
    R = 32,
    S = 33,
    T = 34,
    U = 35,
    V = 36,
    W = 37,
    X = 38,
    Y = 39,
    Z = 40,
    Digit1 = 41,
    Digit2 = 42,
    Digit3 = 43,
    Digit4 = 44,
    Digit5 = 45,
    Digit6 = 46,
    Digit7 = 47,
    Digit8 = 48,
    Digit9 = 49,
    Digit0 = 50,
    Insert = 70,
    F1 = 94,
    F2 = 95,
    F3 = 96,
    F4 = 97,
    F5 = 98,
    F6 = 99,
    F7 = 100,
    F8 = 101,
    F9 = 102,
    F10 = 103,
    F11 = 104,
    F12 = 105,
};

struct EspConfig {
    bool items = true;
    bool monsters = true;
    bool players = true;
    bool exits = true;
    bool fireExits = true;
    bool ships = true;
    bool outlines = true;
    bool names = true;
    bool values = true;
    bool distance = true;
    bool lines = false;
    bool useScrapTiers = true;
    bool deathNotifications = true;
    float maxDistance = 500.f;
    std::array<float, 4> colorItems = {1.00f, 0.72f, 0.25f, 1.0f};
    std::array<float, 4> colorMonsters = {0.88f, 0.08f, 0.17f, 1.0f};
    std::array<float, 4> colorPlayers = {0.35f, 0.85f, 1.00f, 1.0f};
    std::array<float, 4> colorExits = {0.20f, 0.85f, 0.45f, 1.0f};
    std::array<float, 4> colorFireExits = {0.95f, 0.55f, 0.15f, 1.0f};
    std::array<float, 4> colorShips = {0.35f, 0.65f, 1.00f, 1.0f};
};

struct Config {
    MenuKey menuKey = MenuKey::Insert;
    float pollRate = 30.f;
    int tickIntervalMs = 100;
    bool infiniteStamina = true;
    bool noWeight = true;
    bool infiniteBattery = false;
    bool extendedInventory = false;
    std::string fontPath;
    std::string logPath;
    EspConfig esp;
    std::string filePath = "kuelethal.json";
};

enum class ConfigSaveResult : std::uint8_t {
    Durable,
    NotCommitted,
    CommittedDurabilityUnconfirmed,
    CommittedCleanupFailed,
};

class ConfigSaveSchedule final {
  public:
    using Clock = std::chrono::steady_clock;

    void requestAutosave(Clock::time_point now);
    void requestExplicit();
    void record(ConfigSaveResult result, Clock::time_point now);
    [[nodiscard]] bool due(Clock::time_point now) const;

  private:
    enum class State : std::uint8_t { Clean, AutosavePending, ExplicitPending, RetryPending };

    State mState = State::Clean;
    Clock::time_point mDeadline{};
};

struct ConfigFileOperation final {
    const std::string& path;
    std::string& error;
};

bool configLoad(Config& cfg, ConfigFileOperation operation);
ConfigSaveResult configSave(const Config& cfg, ConfigFileOperation operation);

}

#endif
