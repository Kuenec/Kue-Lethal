#ifndef KUE_CORE_UTF8_H
#define KUE_CORE_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kue {

inline bool isValidUtf8(std::string_view text) noexcept {
    struct ByteRange {
        std::size_t index;
        unsigned char minimum;
        unsigned char maximum;
    };
    const auto byteInRange = [&text](ByteRange range) {
        if (range.index >= text.size())
            return false;
        const auto byte = static_cast<unsigned char>(text[range.index]);
        return byte >= range.minimum && byte <= range.maximum;
    };

    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (!byteInRange({index + 1, 0x80U, 0xbfU}))
                return false;
            index += 2;
            continue;
        }
        if (first == 0xe0U) {
            if (!byteInRange({index + 1, 0xa0U, 0xbfU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU})) {
                return false;
            }
            index += 3;
            continue;
        }
        if ((first >= 0xe1U && first <= 0xecU) || (first >= 0xeeU && first <= 0xefU)) {
            if (!byteInRange({index + 1, 0x80U, 0xbfU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU})) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first == 0xedU) {
            if (!byteInRange({index + 1, 0x80U, 0x9fU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU})) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first == 0xf0U) {
            if (!byteInRange({index + 1, 0x90U, 0xbfU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU}) ||
                !byteInRange({index + 3, 0x80U, 0xbfU})) {
                return false;
            }
            index += 4;
            continue;
        }
        if (first >= 0xf1U && first <= 0xf3U) {
            if (!byteInRange({index + 1, 0x80U, 0xbfU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU}) ||
                !byteInRange({index + 3, 0x80U, 0xbfU})) {
                return false;
            }
            index += 4;
            continue;
        }
        if (first == 0xf4U) {
            if (!byteInRange({index + 1, 0x80U, 0x8fU}) ||
                !byteInRange({index + 2, 0x80U, 0xbfU}) ||
                !byteInRange({index + 3, 0x80U, 0xbfU})) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

struct Utf16Input {
    const std::uint16_t* codeUnits = nullptr;
    std::size_t codeUnitCount = 0;
};

struct Utf8Output {
    char* bytes = nullptr;
    std::size_t byteCapacity = 0;
};

enum class Utf16ToUtf8Status : std::uint8_t {
    Success,
    NullInput,
    EmptyInput,
    EmbeddedNull,
    InvalidSurrogate,
    OutputCapacityExceeded
};

struct Utf16ToUtf8Result {
    Utf16ToUtf8Status status = Utf16ToUtf8Status::NullInput;
    std::size_t validatedCodeUnits = 0;
    std::size_t utf8Bytes = 0;
};

[[nodiscard]] inline Utf16ToUtf8Result convertUtf16ToUtf8(Utf16Input input,
                                                          Utf8Output output) noexcept {
    constexpr std::uint32_t highSurrogateFirst = 0xd800U;
    constexpr std::uint32_t highSurrogateLast = 0xdbffU;
    constexpr std::uint32_t lowSurrogateFirst = 0xdc00U;
    constexpr std::uint32_t lowSurrogateLast = 0xdfffU;
    constexpr std::uint32_t supplementaryFirst = 0x10000U;

    if (!input.codeUnits)
        return {Utf16ToUtf8Status::NullInput, 0, 0};
    if (input.codeUnitCount == 0)
        return {Utf16ToUtf8Status::EmptyInput, 0, 0};

    std::size_t inputIndex = 0;
    std::size_t requiredBytes = 0;
    while (inputIndex < input.codeUnitCount) {
        std::uint32_t scalar = input.codeUnits[inputIndex];
        std::size_t scalarCodeUnits = 1;
        if (scalar == 0)
            return {Utf16ToUtf8Status::EmbeddedNull, inputIndex, requiredBytes};
        if (scalar >= highSurrogateFirst && scalar <= highSurrogateLast) {
            if (inputIndex + 1 == input.codeUnitCount)
                return {Utf16ToUtf8Status::InvalidSurrogate, inputIndex, requiredBytes};
            const std::uint32_t low = input.codeUnits[inputIndex + 1];
            if (low < lowSurrogateFirst || low > lowSurrogateLast)
                return {Utf16ToUtf8Status::InvalidSurrogate, inputIndex, requiredBytes};
            scalar = supplementaryFirst + ((scalar - highSurrogateFirst) << 10U) +
                     (low - lowSurrogateFirst);
            scalarCodeUnits = 2;
        } else if (scalar >= lowSurrogateFirst && scalar <= lowSurrogateLast) {
            return {Utf16ToUtf8Status::InvalidSurrogate, inputIndex, requiredBytes};
        }

        if (scalar <= 0x7fU)
            requiredBytes += 1;
        else if (scalar <= 0x7ffU)
            requiredBytes += 2;
        else if (scalar <= 0xffffU)
            requiredBytes += 3;
        else
            requiredBytes += 4;
        inputIndex += scalarCodeUnits;
    }

    if (!output.bytes || output.byteCapacity < requiredBytes) {
        return {Utf16ToUtf8Status::OutputCapacityExceeded, input.codeUnitCount, requiredBytes};
    }

    inputIndex = 0;
    std::size_t outputIndex = 0;
    while (inputIndex < input.codeUnitCount) {
        std::uint32_t scalar = input.codeUnits[inputIndex];
        ++inputIndex;
        if (scalar >= highSurrogateFirst && scalar <= highSurrogateLast) {
            const std::uint32_t low = input.codeUnits[inputIndex];
            ++inputIndex;
            scalar = supplementaryFirst + ((scalar - highSurrogateFirst) << 10U) +
                     (low - lowSurrogateFirst);
        }

        if (scalar <= 0x7fU) {
            output.bytes[outputIndex] = static_cast<char>(scalar);
            ++outputIndex;
        } else if (scalar <= 0x7ffU) {
            output.bytes[outputIndex] = static_cast<char>(0xc0U | (scalar >> 6U));
            output.bytes[outputIndex + 1] = static_cast<char>(0x80U | (scalar & 0x3fU));
            outputIndex += 2;
        } else if (scalar <= 0xffffU) {
            output.bytes[outputIndex] = static_cast<char>(0xe0U | (scalar >> 12U));
            output.bytes[outputIndex + 1] = static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU));
            output.bytes[outputIndex + 2] = static_cast<char>(0x80U | (scalar & 0x3fU));
            outputIndex += 3;
        } else {
            output.bytes[outputIndex] = static_cast<char>(0xf0U | (scalar >> 18U));
            output.bytes[outputIndex + 1] = static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU));
            output.bytes[outputIndex + 2] = static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU));
            output.bytes[outputIndex + 3] = static_cast<char>(0x80U | (scalar & 0x3fU));
            outputIndex += 4;
        }
    }

    return {Utf16ToUtf8Status::Success, input.codeUnitCount, outputIndex};
}

}

#endif
