#ifndef KUE_MONO_UNITY_METADATA_H
#define KUE_MONO_UNITY_METADATA_H

#include "mono/Runtime.h"

#include <cstddef>
#include <cstdint>

namespace kue::unity {

constexpr std::size_t kMaximumMetadataNameBytes = 25;
constexpr std::size_t kMethodMetadataCacheCapacity = 2;
constexpr std::size_t kFieldMetadataCacheCapacity = 19;

enum class MetadataLookupStatus : std::uint8_t {
    Resolved,
    MissingMember,
    NullType,
    NullName,
    EmptyName,
    NameTooLong,
    InvalidParameterCount,
    CapacityExceeded,
    RuntimeUnavailable,
    Count
};

template <typename Member> struct MetadataLookupResult final {
    Member* member = nullptr;
    MetadataLookupStatus status = MetadataLookupStatus::RuntimeUnavailable;
};

struct GameClassLocation final {
    const char* namespaceName;
    const char* className;
};

[[nodiscard]] mono::ClassLookupResult gameClass(GameClassLocation location) noexcept;
[[nodiscard]] bool gameClassLookupMayRetry(mono::ClassLookupStatus status) noexcept;
[[nodiscard]] const char* gameClassLookupStatusName(mono::ClassLookupStatus status) noexcept;

MetadataLookupResult<mono::MonoMethod> cachedMethod(const mono::MonoClass* type, const char* name,
                                                    int parameterCount);
MetadataLookupResult<mono::MonoClassField> cachedField(const mono::MonoClass* type,
                                                       const char* name);
const char* metadataLookupStatusName(MetadataLookupStatus status) noexcept;

}

#endif
