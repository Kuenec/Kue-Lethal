#include "platform/Environment.h"

#include "platform/WindowsSupport.h"

#include <array>
#include <cstring>
#include <string_view>

namespace kue::platform {

namespace {

constexpr std::size_t kMaximumVariableNameBytes = 128;

EnvironmentValue fromWide(const wchar_t* units, std::size_t count,
                          EnvironmentStorage& storage) noexcept {
    const Utf16ToUtf8Result result =
        convertUtf16ToUtf8({reinterpret_cast<const std::uint16_t*>(units), count},
                           {storage.data(), kMaximumEnvironmentValueBytes});
    switch (result.status) {
    case Utf16ToUtf8Status::Success:
        storage[result.utf8Bytes] = '\0';
        return {EnvironmentStatus::Valid, {storage.data(), result.utf8Bytes}};
    case Utf16ToUtf8Status::EmptyInput:
        return {EnvironmentStatus::Empty, {}};
    case Utf16ToUtf8Status::OutputCapacityExceeded:
        return {EnvironmentStatus::TooLong, {}};
    case Utf16ToUtf8Status::NullInput:
    case Utf16ToUtf8Status::EmbeddedNull:
    case Utf16ToUtf8Status::InvalidSurrogate:
        return {EnvironmentStatus::InvalidEncoding, {}};
    }
    return {EnvironmentStatus::InvalidEncoding, {}};
}

bool regularFileExists(const wchar_t* path) noexcept {
    const DWORD attributes = ::GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}

EnvironmentValue readEnvironment(const char* name, EnvironmentStorage& storage) noexcept {
    WideText<kMaximumVariableNameBytes> wideName;
    if (toWide(name, wideName) != WideConversionStatus::Converted)
        return {EnvironmentStatus::Unset, {}};
    WideText<kMaximumEnvironmentValueBytes> wideValue;
    ::SetLastError(ERROR_SUCCESS);
    const DWORD count = ::GetEnvironmentVariableW(wideName.units.data(), wideValue.units.data(),
                                                  static_cast<DWORD>(wideValue.units.size()));
    if (count == 0) {
        if (::GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            return {EnvironmentStatus::Unset, {}};
        return {EnvironmentStatus::Empty, {}};
    }
    if (count > kMaximumEnvironmentValueBytes)
        return {EnvironmentStatus::TooLong, {}};
    return fromWide(wideValue.units.data(), count, storage);
}

int writeEnvironment(const char* name, const char* utf8Value) noexcept {
    WideText<kMaximumVariableNameBytes> wideName;
    if (toWide(name, wideName) != WideConversionStatus::Converted)
        return EINVAL;
    const wchar_t* value = nullptr;
    WideText<kMaximumEnvironmentValueBytes> wideValue;
    if (utf8Value) {
        const WideConversionStatus status = toWide(utf8Value, wideValue);
        if (status != WideConversionStatus::Converted && status != WideConversionStatus::Empty)
            return errnoFromWideConversion(status);
        value = wideValue.units.data();
    }
    if (::SetEnvironmentVariableW(wideName.units.data(), value))
        return 0;
    const DWORD error = ::GetLastError();
    if (!utf8Value && error == ERROR_ENVVAR_NOT_FOUND)
        return 0;
    return errnoFromWin32(error);
}

namespace {

constexpr std::array<std::string_view, 2> kWineHostBodyFonts = {
    "Z:\\usr\\share\\fonts\\TTF\\DejaVuSans.ttf",
    "Z:\\usr\\share\\fonts\\truetype\\dejavu\\DejaVuSans.ttf"};
constexpr std::array<std::string_view, 2> kWineHostTitleFonts = {
    "Z:\\usr\\share\\fonts\\TTF\\DejaVuSans-Bold.ttf",
    "Z:\\usr\\share\\fonts\\truetype\\dejavu\\DejaVuSans-Bold.ttf"};

SystemFontPath existingFont(std::string_view directory, std::string_view file,
                            EnvironmentStorage& storage) noexcept {
    if (directory.size() + file.size() > kMaximumEnvironmentValueBytes)
        return {false, {}};
    std::memcpy(storage.data(), directory.data(), directory.size());
    std::memcpy(storage.data() + directory.size(), file.data(), file.size());
    const std::size_t size = directory.size() + file.size();
    storage[size] = '\0';
    WideText<kMaximumEnvironmentValueBytes> widePath;
    if (toWide(std::string_view(storage.data(), size), widePath) !=
            WideConversionStatus::Converted ||
        !regularFileExists(widePath.units.data())) {
        return {false, {}};
    }
    return {true, {storage.data(), size}};
}

}

SystemFontPath systemFontPath(SystemFontRole role, EnvironmentStorage& storage) noexcept {
    for (const std::string_view hostFont :
         role == SystemFontRole::Body ? kWineHostBodyFonts : kWineHostTitleFonts) {
        const SystemFontPath font = existingFont(hostFont, "", storage);
        if (font.available)
            return font;
    }
    EnvironmentStorage windowsDirectory;
    const EnvironmentValue root = readEnvironment("WINDIR", windowsDirectory);
    if (root.status != EnvironmentStatus::Valid)
        return {false, {}};
    return existingFont(root.text,
                        role == SystemFontRole::Body ? "\\Fonts\\DejaVuSans.ttf"
                                                     : "\\Fonts\\DejaVuSans-Bold.ttf",
                        storage);
}

}
