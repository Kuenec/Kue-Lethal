#include "core/Config.h"

#include "core/Utf8.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace kue {

using json = nlohmann::json;

namespace {

constexpr std::size_t kMaximumConfigBytes = 65536;
constexpr std::size_t kMaximumPathBytes = 4096;
constexpr std::string_view kTemporaryPathSuffix = ".tmp.XXXXXX";
constexpr std::size_t kMaximumTemporaryPathBytes = kMaximumPathBytes + kTemporaryPathSuffix.size();
constexpr std::string_view kDefaultConfigSuffix = "/.config/kuelethal/config.json";
constexpr std::size_t kMaximumJsonContainers = 4;
constexpr std::size_t kMaximumJsonObjectKeys = 20;
constexpr std::size_t kMaximumJsonKeyBytes = 64;
constexpr std::size_t kMaximumJsonTextBytes = kMaximumPathBytes;
constexpr std::array<std::string_view, 10> kConfigurationKeys = {
    "menuKey",         "pollRate",          "tickIntervalMs", "infiniteStamina", "noWeight",
    "infiniteBattery", "extendedInventory", "fontPath",       "logPath",         "esp",
};
constexpr std::array<std::string_view, 20> kEspKeys = {
    "items",
    "monsters",
    "players",
    "exits",
    "fireExits",
    "ships",
    "outlines",
    "names",
    "values",
    "distance",
    "lines",
    "useScrapTiers",
    "deathNotifications",
    "maxDistance",
    "colorItems",
    "colorMonsters",
    "colorPlayers",
    "colorExits",
    "colorFireExits",
    "colorShips",
};

enum class JsonBoundaryFailure : std::uint8_t {
    None,
    InvalidSyntax,
    ExcessiveDepth,
    ExcessiveObjectKeys,
    ExcessiveKeyBytes,
    ExcessiveTextBytes,
    DuplicateKey,
};

struct JsonBoundaryResult {
    JsonBoundaryFailure failure = JsonBoundaryFailure::None;
    std::size_t byte = 0;
};

class JsonBoundaryValidator final {
  public:
    explicit JsonBoundaryValidator(std::string_view input) noexcept : mInput(input) {}

    [[nodiscard]] JsonBoundaryResult validate() noexcept {
        skipWhitespace();
        if (!parseValue(0))
            return mResult;
        skipWhitespace();
        if (mPosition != mInput.size())
            static_cast<void>(reject(JsonBoundaryFailure::InvalidSyntax));
        return mResult;
    }

  private:
    struct Key {
        std::array<char, kMaximumJsonKeyBytes> bytes{};
        std::size_t size = 0;
    };

    struct ObjectKeys {
        std::array<Key, kMaximumJsonObjectKeys> values{};
        std::size_t size = 0;
    };

    enum class StringRole : std::uint8_t { Key, Value };

    [[nodiscard]] bool reject(JsonBoundaryFailure failure) noexcept {
        if (mResult.failure == JsonBoundaryFailure::None) {
            mResult.failure = failure;
            mResult.byte = mPosition + 1;
        }
        return false;
    }

    void skipWhitespace() noexcept {
        while (mPosition < mInput.size()) {
            const char byte = mInput[mPosition];
            if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r')
                return;
            ++mPosition;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        if (mPosition >= mInput.size() || mInput[mPosition] != expected)
            return reject(JsonBoundaryFailure::InvalidSyntax);
        ++mPosition;
        return true;
    }

    [[nodiscard]] bool parseValue(std::size_t containerDepth) noexcept {
        if (mPosition >= mInput.size())
            return reject(JsonBoundaryFailure::InvalidSyntax);
        switch (mInput[mPosition]) {
        case '{':
            return parseObject(containerDepth + 1);
        case '[':
            return parseArray(containerDepth + 1);
        case '"':
            return parseString(StringRole::Value, nullptr);
        case 't':
            return parseLiteral("true");
        case 'f':
            return parseLiteral("false");
        case 'n':
            return parseLiteral("null");
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parseNumber();
        default:
            return reject(JsonBoundaryFailure::InvalidSyntax);
        }
    }

    [[nodiscard]] bool parseObject(std::size_t depth) noexcept {
        if (depth > kMaximumJsonContainers)
            return reject(JsonBoundaryFailure::ExcessiveDepth);
        ++mPosition;
        ObjectKeys keys;
        skipWhitespace();
        if (mPosition < mInput.size() && mInput[mPosition] == '}') {
            ++mPosition;
            return true;
        }
        while (true) {
            Key key;
            if (!parseString(StringRole::Key, &key))
                return false;
            for (std::size_t index = 0; index < keys.size; ++index) {
                const Key& existing = keys.values[index];
                if (existing.size == key.size &&
                    std::memcmp(existing.bytes.data(), key.bytes.data(), key.size) == 0) {
                    return reject(JsonBoundaryFailure::DuplicateKey);
                }
            }
            if (keys.size == keys.values.size())
                return reject(JsonBoundaryFailure::ExcessiveObjectKeys);
            keys.values[keys.size] = key;
            ++keys.size;
            skipWhitespace();
            if (!consume(':'))
                return false;
            skipWhitespace();
            if (!parseValue(depth))
                return false;
            skipWhitespace();
            if (mPosition >= mInput.size())
                return reject(JsonBoundaryFailure::InvalidSyntax);
            if (mInput[mPosition] == '}') {
                ++mPosition;
                return true;
            }
            if (mInput[mPosition] != ',')
                return reject(JsonBoundaryFailure::InvalidSyntax);
            ++mPosition;
            skipWhitespace();
        }
    }

    [[nodiscard]] bool parseArray(std::size_t depth) noexcept {
        if (depth > kMaximumJsonContainers)
            return reject(JsonBoundaryFailure::ExcessiveDepth);
        ++mPosition;
        skipWhitespace();
        if (mPosition < mInput.size() && mInput[mPosition] == ']') {
            ++mPosition;
            return true;
        }
        while (true) {
            if (!parseValue(depth))
                return false;
            skipWhitespace();
            if (mPosition >= mInput.size())
                return reject(JsonBoundaryFailure::InvalidSyntax);
            if (mInput[mPosition] == ']') {
                ++mPosition;
                return true;
            }
            if (mInput[mPosition] != ',')
                return reject(JsonBoundaryFailure::InvalidSyntax);
            ++mPosition;
            skipWhitespace();
        }
    }

    [[nodiscard]] bool appendDecoded(StringRole role, Key* key, const char* bytes,
                                     std::size_t count, std::size_t& decodedBytes) noexcept {
        const std::size_t limit =
            role == StringRole::Key ? kMaximumJsonKeyBytes : kMaximumJsonTextBytes;
        if (count > limit - decodedBytes) {
            return reject(role == StringRole::Key ? JsonBoundaryFailure::ExcessiveKeyBytes
                                                  : JsonBoundaryFailure::ExcessiveTextBytes);
        }
        if (key)
            std::memcpy(key->bytes.data() + decodedBytes, bytes, count);
        decodedBytes += count;
        return true;
    }

    [[nodiscard]] bool parseHexCodeUnit(std::uint16_t& value) noexcept {
        if (mInput.size() - mPosition < 4)
            return reject(JsonBoundaryFailure::InvalidSyntax);
        std::uint16_t decoded = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const unsigned char byte = static_cast<unsigned char>(mInput[mPosition + index]);
            std::uint16_t digit = 0;
            if (byte >= static_cast<unsigned char>('0') &&
                byte <= static_cast<unsigned char>('9')) {
                digit = static_cast<std::uint16_t>(byte - static_cast<unsigned char>('0'));
            } else if (byte >= static_cast<unsigned char>('a') &&
                       byte <= static_cast<unsigned char>('f')) {
                digit = static_cast<std::uint16_t>(byte - static_cast<unsigned char>('a') + 10U);
            } else if (byte >= static_cast<unsigned char>('A') &&
                       byte <= static_cast<unsigned char>('F')) {
                digit = static_cast<std::uint16_t>(byte - static_cast<unsigned char>('A') + 10U);
            } else {
                return reject(JsonBoundaryFailure::InvalidSyntax);
            }
            decoded = static_cast<std::uint16_t>((decoded << 4U) | digit);
        }
        mPosition += 4;
        value = decoded;
        return true;
    }

    [[nodiscard]] bool appendCodePoint(StringRole role, Key* key, std::uint32_t codePoint,
                                       std::size_t& decodedBytes) noexcept {
        std::array<char, 4> encoded{};
        std::size_t encodedBytes = 0;
        if (codePoint <= 0x7fU) {
            encoded[0] = static_cast<char>(codePoint);
            encodedBytes = 1;
        } else if (codePoint <= 0x7ffU) {
            encoded[0] = static_cast<char>(0xc0U | (codePoint >> 6U));
            encoded[1] = static_cast<char>(0x80U | (codePoint & 0x3fU));
            encodedBytes = 2;
        } else if (codePoint <= 0xffffU) {
            encoded[0] = static_cast<char>(0xe0U | (codePoint >> 12U));
            encoded[1] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU));
            encoded[2] = static_cast<char>(0x80U | (codePoint & 0x3fU));
            encodedBytes = 3;
        } else {
            encoded[0] = static_cast<char>(0xf0U | (codePoint >> 18U));
            encoded[1] = static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU));
            encoded[2] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU));
            encoded[3] = static_cast<char>(0x80U | (codePoint & 0x3fU));
            encodedBytes = 4;
        }
        return appendDecoded(role, key, encoded.data(), encodedBytes, decodedBytes);
    }

    [[nodiscard]] bool parseUnicodeEscape(StringRole role, Key* key,
                                          std::size_t& decodedBytes) noexcept {
        std::uint16_t first = 0;
        if (!parseHexCodeUnit(first))
            return false;
        std::uint32_t codePoint = first;
        if (first >= 0xd800U && first <= 0xdbffU) {
            if (mInput.size() - mPosition < 6 || mInput[mPosition] != '\\' ||
                mInput[mPosition + 1] != 'u') {
                return reject(JsonBoundaryFailure::InvalidSyntax);
            }
            mPosition += 2;
            std::uint16_t second = 0;
            if (!parseHexCodeUnit(second))
                return false;
            if (second < 0xdc00U || second > 0xdfffU)
                return reject(JsonBoundaryFailure::InvalidSyntax);
            codePoint = 0x10000U + ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                        (static_cast<std::uint32_t>(second) - 0xdc00U);
        } else if (first >= 0xdc00U && first <= 0xdfffU) {
            return reject(JsonBoundaryFailure::InvalidSyntax);
        }
        return appendCodePoint(role, key, codePoint, decodedBytes);
    }

    [[nodiscard]] bool parseString(StringRole role, Key* key) noexcept {
        if (!consume('"'))
            return false;
        std::size_t decodedBytes = 0;
        while (mPosition < mInput.size()) {
            const char byte = mInput[mPosition++];
            if (byte == '"') {
                if (key) {
                    key->size = decodedBytes;
                    if (!isValidUtf8(std::string_view(key->bytes.data(), key->size)))
                        return reject(JsonBoundaryFailure::InvalidSyntax);
                }
                return true;
            }
            if (static_cast<unsigned char>(byte) < 0x20U)
                return reject(JsonBoundaryFailure::InvalidSyntax);
            if (byte != '\\') {
                if (!appendDecoded(role, key, &byte, 1, decodedBytes))
                    return false;
                continue;
            }
            if (mPosition >= mInput.size())
                return reject(JsonBoundaryFailure::InvalidSyntax);
            const char escape = mInput[mPosition++];
            char decoded = 0;
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                decoded = escape;
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'u':
                if (!parseUnicodeEscape(role, key, decodedBytes))
                    return false;
                continue;
            default:
                return reject(JsonBoundaryFailure::InvalidSyntax);
            }
            if (!appendDecoded(role, key, &decoded, 1, decodedBytes))
                return false;
        }
        return reject(JsonBoundaryFailure::InvalidSyntax);
    }

    [[nodiscard]] bool parseLiteral(std::string_view literal) noexcept {
        if (literal.size() > mInput.size() - mPosition ||
            mInput.substr(mPosition, literal.size()) != literal) {
            return reject(JsonBoundaryFailure::InvalidSyntax);
        }
        mPosition += literal.size();
        return true;
    }

    [[nodiscard]] bool parseNumber() noexcept {
        if (mInput[mPosition] == '-') {
            ++mPosition;
            if (mPosition >= mInput.size())
                return reject(JsonBoundaryFailure::InvalidSyntax);
        }
        if (mInput[mPosition] == '0') {
            ++mPosition;
        } else {
            if (mInput[mPosition] < '1' || mInput[mPosition] > '9')
                return reject(JsonBoundaryFailure::InvalidSyntax);
            do {
                ++mPosition;
            } while (mPosition < mInput.size() && mInput[mPosition] >= '0' &&
                     mInput[mPosition] <= '9');
        }
        if (mPosition < mInput.size() && mInput[mPosition] == '.') {
            ++mPosition;
            if (mPosition >= mInput.size() || mInput[mPosition] < '0' || mInput[mPosition] > '9')
                return reject(JsonBoundaryFailure::InvalidSyntax);
            do {
                ++mPosition;
            } while (mPosition < mInput.size() && mInput[mPosition] >= '0' &&
                     mInput[mPosition] <= '9');
        }
        if (mPosition < mInput.size() && (mInput[mPosition] == 'e' || mInput[mPosition] == 'E')) {
            ++mPosition;
            if (mPosition < mInput.size() &&
                (mInput[mPosition] == '+' || mInput[mPosition] == '-')) {
                ++mPosition;
            }
            if (mPosition >= mInput.size() || mInput[mPosition] < '0' || mInput[mPosition] > '9')
                return reject(JsonBoundaryFailure::InvalidSyntax);
            do {
                ++mPosition;
            } while (mPosition < mInput.size() && mInput[mPosition] >= '0' &&
                     mInput[mPosition] <= '9');
        }
        return true;
    }

    std::string_view mInput;
    std::size_t mPosition = 0;
    JsonBoundaryResult mResult;
};

std::string jsonBoundaryError(JsonBoundaryResult result, const std::string& path) {
    switch (result.failure) {
    case JsonBoundaryFailure::None:
        return {};
    case JsonBoundaryFailure::InvalidSyntax:
        return "invalid JSON in " + path + " at byte " + std::to_string(result.byte);
    case JsonBoundaryFailure::ExcessiveDepth:
        return "JSON nesting exceeds the 4-container limit in " + path;
    case JsonBoundaryFailure::ExcessiveObjectKeys:
        return "JSON object exceeds the 20-key limit in " + path;
    case JsonBoundaryFailure::ExcessiveKeyBytes:
        return "JSON object key exceeds the 64-byte decoded-text limit in " + path;
    case JsonBoundaryFailure::ExcessiveTextBytes:
        return "JSON string exceeds the 4096-byte decoded-text limit in " + path;
    case JsonBoundaryFailure::DuplicateKey:
        return "duplicate JSON object key in " + path + " at byte " + std::to_string(result.byte);
    }
    return {};
}

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

struct PathTextValidation {
    std::string_view value;
    std::string_view name;
};

class ConfigInput final {
  public:
    explicit ConfigInput(int descriptor) noexcept : mDescriptor(descriptor) {}

    ~ConfigInput() {
        if (mDescriptor >= 0)
            static_cast<void>(::close(mDescriptor));
    }

    ConfigInput(const ConfigInput&) = delete;
    ConfigInput& operator=(const ConfigInput&) = delete;

    [[nodiscard]] int descriptor() const noexcept { return mDescriptor; }

    [[nodiscard]] bool close(int& errorCode) noexcept {
        errno = 0;
        const int result = ::close(mDescriptor);
        errorCode = result == 0 ? 0 : (errno != 0 ? errno : EIO);
        mDescriptor = -1;
        return result == 0;
    }

  private:
    int mDescriptor;
};

bool validatePathText(PathTextValidation input, std::string& error) {
    if (input.value.size() > kMaximumPathBytes)
        return fail(error, std::string(input.name) + " exceeds the path-length limit");
    if (input.value.find('\0') != std::string_view::npos)
        return fail(error, std::string(input.name) + " must not contain null bytes");
    if (!isValidUtf8(input.value))
        return fail(error, std::string(input.name) + " must be valid UTF-8");
    return true;
}

std::string_view boundedPathView(const char* path) noexcept {
    return {path, ::strnlen(path, kMaximumPathBytes + 1)};
}

std::string operatingSystemError(std::string_view operation, int code) {
    return std::string(operation) + ": " + std::strerror(code);
}

void appendOperatingSystemError(std::string& message, std::string_view operation, int code) {
    message += "; ";
    message += operatingSystemError(operation, code);
}

bool failAndClose(ConfigInput& input, std::string& error, std::string message) {
    int closeCode = 0;
    if (!input.close(closeCode))
        appendOperatingSystemError(message, "cannot close configuration", closeCode);
    return fail(error, std::move(message));
}

template <std::size_t Count>
bool rejectUnknownKeys(const json& object, const std::array<std::string_view, Count>& knownKeys,
                       std::string_view errorPrefix, std::string& error) {
    for (auto member = object.cbegin(); member != object.cend(); ++member) {
        bool known = false;
        for (const std::string_view key : knownKeys) {
            if (member.key() == key) {
                known = true;
                break;
            }
        }
        if (!known)
            return fail(error, std::string(errorPrefix) + member.key());
    }
    return true;
}

ConfigSaveResult synchronizeDirectoryEntry(ConfigFileOperation operation) {
    const std::string& path = operation.path;
    std::string& error = operation.error;
    std::filesystem::path directory = std::filesystem::path(path).parent_path();
    if (directory.empty())
        directory = ".";
    errno = 0;
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (descriptor < 0) {
        fail(error, operatingSystemError("cannot open configuration directory", errno));
        return ConfigSaveResult::CommittedDurabilityUnconfirmed;
    }
    errno = 0;
    const int synchronizeResult = ::fsync(descriptor);
    const int synchronizeCode = synchronizeResult != 0 ? (errno != 0 ? errno : EIO) : 0;
    errno = 0;
    const int closeResult = ::close(descriptor);
    const int closeCode = closeResult != 0 ? (errno != 0 ? errno : EIO) : 0;
    if (synchronizeResult != 0) {
        std::string message =
            operatingSystemError("cannot synchronize configuration directory", synchronizeCode);
        if (closeResult != 0)
            appendOperatingSystemError(message, "cannot close configuration directory", closeCode);
        fail(error, std::move(message));
        return ConfigSaveResult::CommittedDurabilityUnconfirmed;
    }
    if (closeResult != 0) {
        fail(error, operatingSystemError("cannot close configuration directory", closeCode));
        return ConfigSaveResult::CommittedCleanupFailed;
    }
    return ConfigSaveResult::Durable;
}

enum class ConfigPathStatus : std::uint8_t { Failed, Selected };

struct ConfigPathSelection {
    ConfigPathStatus status = ConfigPathStatus::Failed;
    std::string path;
    std::string error;
};

ConfigPathSelection selectConfigPath(const std::string& userPath) {
    std::string validationError;
    if (!userPath.empty()) {
        if (!validatePathText(
                PathTextValidation{.value = userPath, .name = "explicit configuration path"},
                validationError)) {
            return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                       .path = {},
                                       .error = std::move(validationError)};
        }
        return ConfigPathSelection{
            .status = ConfigPathStatus::Selected, .path = userPath, .error = {}};
    }
    const char* const environmentPath = ::getenv("KUE_CONFIG");
    if (environmentPath) {
        if (environmentPath[0] == '\0')
            return ConfigPathSelection{
                .status = ConfigPathStatus::Failed, .path = {}, .error = "KUE_CONFIG is empty"};
        const std::string_view selected = boundedPathView(environmentPath);
        if (!validatePathText(PathTextValidation{.value = selected, .name = "KUE_CONFIG"},
                              validationError)) {
            return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                       .path = {},
                                       .error = std::move(validationError)};
        }
        return ConfigPathSelection{
            .status = ConfigPathStatus::Selected, .path = std::string(selected), .error = {}};
    }

    std::string homePath;
    const char* const home = ::getenv("HOME");
    if (home && home[0] != '\0') {
        const std::string_view homeValue = boundedPathView(home);
        if (!validatePathText(PathTextValidation{.value = homeValue, .name = "HOME"},
                              validationError)) {
            return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                       .path = {},
                                       .error = std::move(validationError)};
        }
        if (homeValue.size() > kMaximumPathBytes - kDefaultConfigSuffix.size())
            return ConfigPathSelection{
                .status = ConfigPathStatus::Failed,
                .path = {},
                .error = "default configuration path exceeds the path-length limit"};
        homePath.reserve(homeValue.size() + kDefaultConfigSuffix.size());
        homePath.append(homeValue);
        homePath.append(kDefaultConfigSuffix);
        std::error_code code;
        if (std::filesystem::exists(homePath, code)) {
            return ConfigPathSelection{
                .status = ConfigPathStatus::Selected, .path = std::move(homePath), .error = {}};
        }
        if (code) {
            return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                       .path = {},
                                       .error = "cannot inspect configuration '" + homePath +
                                                "': " + code.message()};
        }
    }

    constexpr std::string_view localPath = "kuelethal.json";
    std::error_code code;
    if (std::filesystem::exists(localPath, code)) {
        return ConfigPathSelection{
            .status = ConfigPathStatus::Selected, .path = std::string(localPath), .error = {}};
    }
    if (code) {
        return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                   .path = {},
                                   .error = "cannot inspect configuration 'kuelethal.json': " +
                                            code.message()};
    }
    if (homePath.empty())
        return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                                   .path = {},
                                   .error = "configuration not found; searched 'kuelethal.json'"};
    return ConfigPathSelection{.status = ConfigPathStatus::Failed,
                               .path = {},
                               .error = "configuration not found; searched '" + homePath +
                                        "' and 'kuelethal.json'"};
}

json arrayToJson(const std::array<float, 4>& c) {
    return json::array({c[0], c[1], c[2], c[3]});
}

bool readBoolean(const json& object, std::string_view key, bool& output, std::string& error) {
    const auto value = object.find(key);
    if (value == object.end())
        return true;
    if (!value->is_boolean())
        return fail(error, std::string(key) + " must be a boolean");
    output = value->get<bool>();
    return true;
}

bool readInteger(const json& object, std::string_view key, int& output, std::string& error) {
    const auto value = object.find(key);
    if (value == object.end())
        return true;
    if (value->is_number_unsigned()) {
        const std::uint64_t parsed = value->get<std::uint64_t>();
        if (parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return fail(error, std::string(key) + " is outside the integer range");
        output = static_cast<int>(parsed);
        return true;
    }
    if (!value->is_number_integer())
        return fail(error, std::string(key) + " must be an integer");
    const std::int64_t parsed = value->get<std::int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
        return fail(error, std::string(key) + " is outside the integer range");
    output = static_cast<int>(parsed);
    return true;
}

bool readFloat(const json& object, std::string_view key, float& output, std::string& error) {
    const auto value = object.find(key);
    if (value == object.end())
        return true;
    if (!value->is_number())
        return fail(error, std::string(key) + " must be a number");
    const double parsed = value->get<double>();
    if (!std::isfinite(parsed) ||
        std::abs(parsed) > static_cast<double>(std::numeric_limits<float>::max())) {
        return fail(error, std::string(key) + " must be a finite floating-point value");
    }
    output = static_cast<float>(parsed);
    return true;
}

struct ConfigurationPathField {
    std::string_view name;
    std::string& value;
};

bool readPath(const json& object, ConfigurationPathField field, std::string& error) {
    const auto value = object.find(field.name);
    if (value == object.end())
        return true;
    if (!value->is_string())
        return fail(error, std::string(field.name) + " must be a string");
    const std::string& parsed = value->get_ref<const std::string&>();
    if (!validatePathText(PathTextValidation{.value = parsed, .name = field.name}, error))
        return false;
    field.value = parsed;
    return true;
}

bool readColor(const json& object, std::string_view key, std::array<float, 4>& output,
               std::string& error) {
    const auto value = object.find(key);
    if (value == object.end())
        return true;
    if (!value->is_array() || value->size() != output.size())
        return fail(error, std::string(key) + " must contain exactly four numbers");
    std::array<float, 4> parsed{};
    for (std::size_t index = 0; index < parsed.size(); ++index) {
        const json& component = (*value)[index];
        if (!component.is_number())
            return fail(error, std::string(key) + " contains a non-numeric component");
        const double numeric = component.get<double>();
        if (!std::isfinite(numeric) || numeric < 0.0 || numeric > 1.0)
            return fail(error, std::string(key) + " components must be finite and within [0, 1]");
        parsed[index] = static_cast<float>(numeric);
    }
    output = parsed;
    return true;
}

bool validateColor(const std::array<float, 4>& color, std::string_view key, std::string& error) {
    for (const float component : color) {
        if (!std::isfinite(component) || component < 0.f || component > 1.f) {
            return fail(error, std::string(key) + " components must be finite and within [0, 1]");
        }
    }
    return true;
}

bool supportedMenuKey(int key) {
    return (key >= static_cast<int>(MenuKey::Space) && key <= static_cast<int>(MenuKey::Tab)) ||
           (key >= static_cast<int>(MenuKey::A) && key <= static_cast<int>(MenuKey::Digit0)) ||
           key == static_cast<int>(MenuKey::Insert) ||
           (key >= static_cast<int>(MenuKey::F1) && key <= static_cast<int>(MenuKey::F12));
}

bool readMenuKey(const json& object, MenuKey& output, std::string& error) {
    int parsed = static_cast<int>(output);
    if (!readInteger(object, "menuKey", parsed, error))
        return false;
    if (!supportedMenuKey(parsed))
        return fail(error, "menuKey must be a supported Unity Input System key");
    output = static_cast<MenuKey>(parsed);
    return true;
}

bool validate(const Config& cfg, std::string& error) {
    if (!supportedMenuKey(static_cast<int>(cfg.menuKey)))
        return fail(error, "menuKey must be a supported Unity Input System key");
    if (!std::isfinite(cfg.pollRate) || cfg.pollRate < 10.f || cfg.pollRate > 90.f)
        return fail(error, "pollRate must be finite and within [10, 90]");
    if (cfg.tickIntervalMs < 33 || cfg.tickIntervalMs > 500)
        return fail(error, "tickIntervalMs must be within [33, 500]");
    if (!std::isfinite(cfg.esp.maxDistance) || cfg.esp.maxDistance < 10.f ||
        cfg.esp.maxDistance > 1000.f) {
        return fail(error, "esp.maxDistance must be finite and within [10, 1000]");
    }
    if (!validateColor(cfg.esp.colorItems, "colorItems", error) ||
        !validateColor(cfg.esp.colorMonsters, "colorMonsters", error) ||
        !validateColor(cfg.esp.colorPlayers, "colorPlayers", error) ||
        !validateColor(cfg.esp.colorExits, "colorExits", error) ||
        !validateColor(cfg.esp.colorFireExits, "colorFireExits", error) ||
        !validateColor(cfg.esp.colorShips, "colorShips", error)) {
        return false;
    }
    return validatePathText(PathTextValidation{.value = cfg.fontPath, .name = "fontPath"}, error) &&
           validatePathText(PathTextValidation{.value = cfg.logPath, .name = "logPath"}, error) &&
           validatePathText(PathTextValidation{.value = cfg.filePath, .name = "filePath"}, error);
}

}

void ConfigSaveSchedule::requestAutosave(Clock::time_point now) {
    if (mState == State::ExplicitPending || mState == State::RetryPending)
        return;
    mState = State::AutosavePending;
    mDeadline = now + std::chrono::milliseconds(350);
}

void ConfigSaveSchedule::requestExplicit() {
    mState = State::ExplicitPending;
    mDeadline = Clock::time_point::min();
}

void ConfigSaveSchedule::record(ConfigSaveResult result, Clock::time_point now) {
    switch (result) {
    case ConfigSaveResult::Durable:
        mState = State::Clean;
        mDeadline = {};
        return;
    case ConfigSaveResult::NotCommitted:
    case ConfigSaveResult::CommittedDurabilityUnconfirmed:
    case ConfigSaveResult::CommittedCleanupFailed:
        mState = State::RetryPending;
        mDeadline = now + std::chrono::seconds(1);
        return;
    }
}

bool ConfigSaveSchedule::due(Clock::time_point now) const {
    return mState != State::Clean && now >= mDeadline;
}

bool configLoad(Config& cfg, ConfigFileOperation operation) {
    const std::string& path = operation.path;
    std::string& error = operation.error;
    error.clear();
    ConfigPathSelection selection = selectConfigPath(path);
    if (selection.status != ConfigPathStatus::Selected) {
        error = std::move(selection.error);
        return false;
    }
    const std::string& found = selection.path;
    errno = 0;
    const int rawDescriptor = ::open(found.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (rawDescriptor < 0)
        return fail(error,
                    operatingSystemError("cannot open configuration '" + found + "'", errno));
    ConfigInput input(rawDescriptor);
    struct stat metadata{};
    errno = 0;
    if (::fstat(input.descriptor(), &metadata) != 0) {
        const int code = errno != 0 ? errno : EIO;
        return failAndClose(
            input, error,
            operatingSystemError("cannot inspect configuration '" + found + "'", code));
    }
    if (!S_ISREG(metadata.st_mode))
        return failAndClose(input, error, "configuration is not a regular file: " + found);
    std::array<char, kMaximumConfigBytes + 1> content{};
    std::size_t readBytes = 0;
    while (readBytes < content.size()) {
        errno = 0;
        const ssize_t result =
            ::read(input.descriptor(), content.data() + readBytes, content.size() - readBytes);
        if (result > 0) {
            readBytes += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0)
            break;
        if (result < 0 && errno == EINTR)
            continue;
        const int code = errno != 0 ? errno : EIO;
        return failAndClose(
            input, error, operatingSystemError("cannot read configuration '" + found + "'", code));
    }
    if (readBytes > kMaximumConfigBytes)
        return failAndClose(input, error, "configuration exceeds 65536-byte limit: " + found);
    int closeCode = 0;
    if (!input.close(closeCode))
        return fail(error, operatingSystemError("cannot close configuration", closeCode));
    const std::string_view encodedJson{content.data(), readBytes};
    const JsonBoundaryResult boundary = JsonBoundaryValidator(encodedJson).validate();
    if (boundary.failure != JsonBoundaryFailure::None)
        return fail(error, jsonBoundaryError(boundary, found));
    json j;
    try {
        j = json::parse(encodedJson);
    } catch (const json::parse_error& exception) {
        return fail(error, "invalid JSON in " + found + ": " + exception.what());
    } catch (const json::out_of_range& exception) {
        return fail(error, "numeric overflow in " + found + ": " + exception.what());
    }
    if (!j.is_object())
        return fail(error, "configuration root must be an object: " + found);
    if (!rejectUnknownKeys(j, kConfigurationKeys, "unknown configuration key: ", error))
        return false;

    Config candidate = cfg;
    candidate.filePath = found;
    if (!readMenuKey(j, candidate.menuKey, error) ||
        !readFloat(j, "pollRate", candidate.pollRate, error) ||
        !readInteger(j, "tickIntervalMs", candidate.tickIntervalMs, error) ||
        !readBoolean(j, "infiniteStamina", candidate.infiniteStamina, error) ||
        !readBoolean(j, "noWeight", candidate.noWeight, error) ||
        !readBoolean(j, "infiniteBattery", candidate.infiniteBattery, error) ||
        !readBoolean(j, "extendedInventory", candidate.extendedInventory, error) ||
        !readPath(j, ConfigurationPathField{.name = "fontPath", .value = candidate.fontPath},
                  error) ||
        !readPath(j, ConfigurationPathField{.name = "logPath", .value = candidate.logPath},
                  error)) {
        return false;
    }
    const auto espValue = j.find("esp");
    if (espValue != j.end()) {
        if (!espValue->is_object())
            return fail(error, "esp must be an object");
        const json& esp = *espValue;
        if (!rejectUnknownKeys(esp, kEspKeys, "unknown esp key: ", error))
            return false;
        if (!readBoolean(esp, "items", candidate.esp.items, error) ||
            !readBoolean(esp, "monsters", candidate.esp.monsters, error) ||
            !readBoolean(esp, "players", candidate.esp.players, error) ||
            !readBoolean(esp, "exits", candidate.esp.exits, error) ||
            !readBoolean(esp, "fireExits", candidate.esp.fireExits, error) ||
            !readBoolean(esp, "ships", candidate.esp.ships, error) ||
            !readBoolean(esp, "outlines", candidate.esp.outlines, error) ||
            !readBoolean(esp, "names", candidate.esp.names, error) ||
            !readBoolean(esp, "values", candidate.esp.values, error) ||
            !readBoolean(esp, "distance", candidate.esp.distance, error) ||
            !readBoolean(esp, "lines", candidate.esp.lines, error) ||
            !readBoolean(esp, "useScrapTiers", candidate.esp.useScrapTiers, error) ||
            !readBoolean(esp, "deathNotifications", candidate.esp.deathNotifications, error) ||
            !readFloat(esp, "maxDistance", candidate.esp.maxDistance, error) ||
            !readColor(esp, "colorItems", candidate.esp.colorItems, error) ||
            !readColor(esp, "colorMonsters", candidate.esp.colorMonsters, error) ||
            !readColor(esp, "colorPlayers", candidate.esp.colorPlayers, error) ||
            !readColor(esp, "colorExits", candidate.esp.colorExits, error) ||
            !readColor(esp, "colorFireExits", candidate.esp.colorFireExits, error) ||
            !readColor(esp, "colorShips", candidate.esp.colorShips, error)) {
            return false;
        }
    }
    if (!validate(candidate, error))
        return false;
    cfg = std::move(candidate);
    return true;
}

ConfigSaveResult configSave(const Config& cfg, ConfigFileOperation operation) {
    const std::string& path = operation.path;
    std::string& error = operation.error;
    error.clear();
    const auto reject = [&error](std::string message) {
        fail(error, std::move(message));
        return ConfigSaveResult::NotCommitted;
    };
    if (path.empty())
        return reject("configuration destination is empty");
    if (!validatePathText(PathTextValidation{.value = path, .name = "configuration destination"},
                          error))
        return ConfigSaveResult::NotCommitted;
    if (!validate(cfg, error))
        return ConfigSaveResult::NotCommitted;
    json j;
    j["menuKey"] = static_cast<int>(cfg.menuKey);
    j["pollRate"] = cfg.pollRate;
    j["tickIntervalMs"] = cfg.tickIntervalMs;
    j["infiniteStamina"] = cfg.infiniteStamina;
    j["noWeight"] = cfg.noWeight;
    j["infiniteBattery"] = cfg.infiniteBattery;
    j["extendedInventory"] = cfg.extendedInventory;
    j["fontPath"] = cfg.fontPath;
    j["logPath"] = cfg.logPath;
    j["esp"] = json::object();
    j["esp"]["items"] = cfg.esp.items;
    j["esp"]["monsters"] = cfg.esp.monsters;
    j["esp"]["players"] = cfg.esp.players;
    j["esp"]["exits"] = cfg.esp.exits;
    j["esp"]["fireExits"] = cfg.esp.fireExits;
    j["esp"]["ships"] = cfg.esp.ships;
    j["esp"]["outlines"] = cfg.esp.outlines;
    j["esp"]["names"] = cfg.esp.names;
    j["esp"]["values"] = cfg.esp.values;
    j["esp"]["distance"] = cfg.esp.distance;
    j["esp"]["lines"] = cfg.esp.lines;
    j["esp"]["useScrapTiers"] = cfg.esp.useScrapTiers;
    j["esp"]["deathNotifications"] = cfg.esp.deathNotifications;
    j["esp"]["maxDistance"] = cfg.esp.maxDistance;
    j["esp"]["colorItems"] = arrayToJson(cfg.esp.colorItems);
    j["esp"]["colorMonsters"] = arrayToJson(cfg.esp.colorMonsters);
    j["esp"]["colorPlayers"] = arrayToJson(cfg.esp.colorPlayers);
    j["esp"]["colorExits"] = arrayToJson(cfg.esp.colorExits);
    j["esp"]["colorFireExits"] = arrayToJson(cfg.esp.colorFireExits);
    j["esp"]["colorShips"] = arrayToJson(cfg.esp.colorShips);
    std::string content;
    try {
        content = j.dump(2) + '\n';
    } catch (const json::type_error& exception) {
        return reject("configuration contains invalid text: " + std::string(exception.what()));
    }
    if (content.size() > kMaximumConfigBytes)
        return reject("serialized configuration exceeds 65536-byte limit");

    std::array<char, kMaximumTemporaryPathBytes + 1> temporaryPath{};
    std::memcpy(temporaryPath.data(), path.data(), path.size());
    std::memcpy(temporaryPath.data() + path.size(), kTemporaryPathSuffix.data(),
                kTemporaryPathSuffix.size());
    const int descriptor = ::mkostemp(temporaryPath.data(), O_CLOEXEC);
    if (descriptor < 0)
        return reject(operatingSystemError("cannot create temporary configuration", errno));
    int descriptorFlags = -1;
    do {
        errno = 0;
        descriptorFlags = ::fcntl(descriptor, F_GETFD);
    } while (descriptorFlags < 0 && errno == EINTR);
    if (descriptorFlags < 0 || (descriptorFlags & FD_CLOEXEC) == 0) {
        const int code = descriptorFlags < 0 ? (errno != 0 ? errno : EIO) : EIO;
        std::string message =
            operatingSystemError("cannot secure temporary configuration descriptor", code);
        errno = 0;
        if (::close(descriptor) != 0) {
            appendOperatingSystemError(message, "cannot close temporary configuration",
                                       errno != 0 ? errno : EIO);
        }
        errno = 0;
        if (::unlink(temporaryPath.data()) != 0) {
            appendOperatingSystemError(message, "cannot remove temporary configuration",
                                       errno != 0 ? errno : EIO);
        }
        return reject(std::move(message));
    }
    FILE* output = ::fdopen(descriptor, "wb");
    if (!output) {
        const int code = errno;
        std::string message = operatingSystemError("cannot open temporary configuration", code);
        errno = 0;
        if (::close(descriptor) != 0) {
            appendOperatingSystemError(message, "cannot close temporary configuration", errno);
        }
        errno = 0;
        if (::unlink(temporaryPath.data()) != 0) {
            appendOperatingSystemError(message, "cannot remove temporary configuration", errno);
        }
        return reject(std::move(message));
    }

    std::string_view failedOperation;
    int failedCode = 0;
    errno = 0;
    if (std::fwrite(content.data(), 1, content.size(), output) != content.size()) {
        failedOperation = "cannot write temporary configuration";
        failedCode = errno != 0 ? errno : EIO;
    }
    if (failedOperation.empty()) {
        errno = 0;
        if (std::fflush(output) != 0) {
            failedOperation = "cannot flush temporary configuration";
            failedCode = errno != 0 ? errno : EIO;
        }
    }
    if (failedOperation.empty()) {
        errno = 0;
        if (::fsync(descriptor) != 0) {
            failedOperation = "cannot synchronize temporary configuration";
            failedCode = errno != 0 ? errno : EIO;
        }
    }
    errno = 0;
    const int closeResult = std::fclose(output);
    const int closeCode = closeResult != 0 ? (errno != 0 ? errno : EIO) : 0;
    if (failedOperation.empty() && closeResult != 0) {
        failedOperation = "cannot close temporary configuration";
        failedCode = closeCode;
    }
    if (!failedOperation.empty()) {
        std::string message = operatingSystemError(failedOperation, failedCode);
        if (closeResult != 0 && failedOperation != "cannot close temporary configuration") {
            appendOperatingSystemError(message, "cannot close temporary configuration", closeCode);
        }
        errno = 0;
        if (::unlink(temporaryPath.data()) != 0) {
            appendOperatingSystemError(message, "cannot remove temporary configuration", errno);
        }
        return reject(std::move(message));
    }
    if (::rename(temporaryPath.data(), path.c_str()) != 0) {
        const int code = errno;
        std::string message = operatingSystemError("cannot replace configuration", code);
        errno = 0;
        if (::unlink(temporaryPath.data()) != 0) {
            appendOperatingSystemError(message, "cannot remove temporary configuration", errno);
        }
        return reject(std::move(message));
    }
    return synchronizeDirectoryEntry({.path = path, .error = error});
}

}
