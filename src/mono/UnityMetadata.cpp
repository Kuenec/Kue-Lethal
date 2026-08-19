#include "mono/UnityMetadata.h"

#include <array>
#include <cstring>
#include <mutex>

namespace kue::unity {
namespace {

using mono::MonoClass;
using mono::MonoClassField;
using mono::MonoMethod;

template <typename Member> struct MemberCacheEntry {
    const MonoClass* type;
    std::array<char, kMaximumMetadataNameBytes + 1> name;
    std::size_t nameLength;
    int parameterCount;
    Member* member;
};

template <typename Member, std::size_t Capacity> struct MemberCache {
    std::mutex mutex;
    std::array<MemberCacheEntry<Member>, Capacity> entries{};
    std::size_t size = 0;
};

struct MemberName {
    const char* data;
    std::size_t length;
};

MemberCache<MonoMethod, kMethodMetadataCacheCapacity> methodCache;
MemberCache<MonoClassField, kFieldMetadataCacheCapacity> fieldCache;

std::size_t boundedNameLength(const char* name) noexcept {
    std::size_t length = 0;
    while (length <= kMaximumMetadataNameBytes && name[length] != '\0')
        ++length;
    return length;
}

template <typename Member> MetadataLookupResult<Member> failure(MetadataLookupStatus status) {
    return {nullptr, status};
}

template <typename Member>
MetadataLookupResult<Member> validate(const MonoClass* type, const char* name, int parameterCount,
                                      MemberName& memberName) {
    if (!type)
        return failure<Member>(MetadataLookupStatus::NullType);
    if (!name)
        return failure<Member>(MetadataLookupStatus::NullName);
    memberName = {name, boundedNameLength(name)};
    if (memberName.length == 0)
        return failure<Member>(MetadataLookupStatus::EmptyName);
    if (memberName.length > kMaximumMetadataNameBytes)
        return failure<Member>(MetadataLookupStatus::NameTooLong);
    if (parameterCount < 0)
        return failure<Member>(MetadataLookupStatus::InvalidParameterCount);
    return {nullptr, MetadataLookupStatus::Resolved};
}

template <typename Member, std::size_t Capacity>
bool findCached(MemberCache<Member, Capacity>& cache, const MonoClass* type, MemberName memberName,
                int parameterCount, MetadataLookupResult<Member>& result) {
    for (std::size_t index = 0; index < cache.size; ++index) {
        const MemberCacheEntry<Member>& entry = cache.entries[index];
        if (entry.type == type && entry.parameterCount == parameterCount &&
            entry.nameLength == memberName.length &&
            std::memcmp(entry.name.data(), memberName.data, memberName.length) == 0) {
            result = {entry.member, entry.member ? MetadataLookupStatus::Resolved
                                                 : MetadataLookupStatus::MissingMember};
            return true;
        }
    }
    return false;
}

template <typename Member, std::size_t Capacity>
MetadataLookupResult<Member> insert(MemberCache<Member, Capacity>& cache, const MonoClass* type,
                                    MemberName memberName, int parameterCount, Member* member) {
    if (cache.size >= cache.entries.size())
        return failure<Member>(MetadataLookupStatus::CapacityExceeded);
    MemberCacheEntry<Member>& entry = cache.entries[cache.size];
    entry.type = type;
    std::memcpy(entry.name.data(), memberName.data, memberName.length);
    entry.name[memberName.length] = '\0';
    entry.nameLength = memberName.length;
    entry.parameterCount = parameterCount;
    entry.member = member;
    ++cache.size;
    return {member, member ? MetadataLookupStatus::Resolved : MetadataLookupStatus::MissingMember};
}

}

mono::ClassLookupResult gameClass(GameClassLocation location) noexcept {
    return mono::findClass({.imageName = "Assembly-CSharp",
                            .namespaceName = location.namespaceName,
                            .className = location.className});
}

bool gameClassLookupMayRetry(mono::ClassLookupStatus status) noexcept {
    switch (status) {
    case mono::ClassLookupStatus::RuntimeUnavailable:
    case mono::ClassLookupStatus::DomainUnavailable:
    case mono::ClassLookupStatus::ImageUnavailable:
        return true;
    case mono::ClassLookupStatus::Resolved:
    case mono::ClassLookupStatus::InvalidLocation:
    case mono::ClassLookupStatus::ClassUnavailable:
        return false;
    }
    return false;
}

const char* gameClassLookupStatusName(mono::ClassLookupStatus status) noexcept {
    switch (status) {
    case mono::ClassLookupStatus::Resolved:
        return "resolved";
    case mono::ClassLookupStatus::InvalidLocation:
        return "invalid class location";
    case mono::ClassLookupStatus::RuntimeUnavailable:
        return "runtime unavailable";
    case mono::ClassLookupStatus::DomainUnavailable:
        return "domain unavailable";
    case mono::ClassLookupStatus::ImageUnavailable:
        return "image unavailable";
    case mono::ClassLookupStatus::ClassUnavailable:
        return "class unavailable";
    }
    return "invalid class lookup status";
}

MetadataLookupResult<MonoMethod> cachedMethod(const MonoClass* type, const char* name,
                                              int parameterCount) {
    std::lock_guard<std::mutex> lock(methodCache.mutex);
    MemberName memberName{};
    const MetadataLookupResult<MonoMethod> validation =
        validate<MonoMethod>(type, name, parameterCount, memberName);
    if (validation.status != MetadataLookupStatus::Resolved)
        return validation;
    MetadataLookupResult<MonoMethod> cached;
    if (findCached(methodCache, type, memberName, parameterCount, cached))
        return cached;
    if (methodCache.size >= methodCache.entries.size())
        return failure<MonoMethod>(MetadataLookupStatus::CapacityExceeded);
    const mono::MethodLookupResult lookup = mono::findMethod(type, name, parameterCount);
    if (lookup.status == mono::MetadataMemberLookupStatus::RuntimeUnavailable)
        return failure<MonoMethod>(MetadataLookupStatus::RuntimeUnavailable);
    return insert(methodCache, type, memberName, parameterCount, lookup.method);
}

MetadataLookupResult<MonoClassField> cachedField(const MonoClass* type, const char* name) {
    std::lock_guard<std::mutex> lock(fieldCache.mutex);
    MemberName memberName{};
    const MetadataLookupResult<MonoClassField> validation =
        validate<MonoClassField>(type, name, 0, memberName);
    if (validation.status != MetadataLookupStatus::Resolved)
        return validation;
    MetadataLookupResult<MonoClassField> cached;
    if (findCached(fieldCache, type, memberName, 0, cached))
        return cached;
    if (fieldCache.size >= fieldCache.entries.size())
        return failure<MonoClassField>(MetadataLookupStatus::CapacityExceeded);
    const mono::FieldLookupResult lookup = mono::findField(type, name);
    if (lookup.status == mono::MetadataMemberLookupStatus::RuntimeUnavailable)
        return failure<MonoClassField>(MetadataLookupStatus::RuntimeUnavailable);
    return insert(fieldCache, type, memberName, 0, lookup.field);
}

const char* metadataLookupStatusName(MetadataLookupStatus status) noexcept {
    switch (status) {
    case MetadataLookupStatus::Resolved:
        return "resolved";
    case MetadataLookupStatus::MissingMember:
        return "missing member";
    case MetadataLookupStatus::NullType:
        return "null declaring type";
    case MetadataLookupStatus::NullName:
        return "null member name";
    case MetadataLookupStatus::EmptyName:
        return "empty member name";
    case MetadataLookupStatus::NameTooLong:
        return "member name exceeds 25 bytes";
    case MetadataLookupStatus::InvalidParameterCount:
        return "negative method parameter count";
    case MetadataLookupStatus::CapacityExceeded:
        return "metadata cache capacity exceeded";
    case MetadataLookupStatus::RuntimeUnavailable:
        return "runtime metadata resolver unavailable";
    case MetadataLookupStatus::Count:
        return "invalid metadata status";
    }
    return "invalid metadata status";
}

}
