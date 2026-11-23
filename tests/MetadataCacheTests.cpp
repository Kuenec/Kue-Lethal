#include "mono/UnityMetadata.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string_view>

namespace {

class TestRun final {
  public:
    void expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
    }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

std::atomic<bool> gMeasureAllocations{false};
std::atomic<std::size_t> gAllocations{0};
std::atomic<int> gFieldResolutions{0};
std::atomic<bool> gRuntimeMemberLookupAvailable{true};
int gClassResolutions = 0;
kue::mono::ManagedClassLocation gLastClassLocation{nullptr, nullptr, nullptr};
kue::mono::ClassLookupResult gNextClassLookup{};
alignas(std::max_align_t) std::byte gClassStorage[2];
alignas(std::max_align_t) std::byte gFieldStorage[1];

void recordAllocation() {
    if (gMeasureAllocations.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

[[gnu::noinline]] void deallocate(void* memory) noexcept {
    std::free(memory);
}

kue::mono::MonoClassField* resolveField(const kue::mono::MonoClass*, const char* name) {
    gFieldResolutions.fetch_add(1, std::memory_order_relaxed);
    if (std::strcmp(name, "missingField") == 0)
        return nullptr;
    return reinterpret_cast<kue::mono::MonoClassField*>(gFieldStorage);
}

template <typename Member>
void expectStatus(TestRun& run, const kue::unity::MetadataLookupResult<Member>& result,
                  kue::unity::MetadataLookupStatus status, std::string_view contract) {
    run.expect(result.status == status, contract);
}

}

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void operator delete(void* memory) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory) noexcept {
    deallocate(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    deallocate(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    deallocate(memory);
}

namespace kue::mono {

bool ready() noexcept {
    return true;
}

FieldLookupResult findField(const MonoClass* type, const char* name) noexcept {
    if (!gRuntimeMemberLookupAvailable.load(std::memory_order_relaxed))
        return {nullptr, MetadataMemberLookupStatus::RuntimeUnavailable};
    MonoClassField* const field = resolveField(type, name);
    return {field, field ? MetadataMemberLookupStatus::Resolved
                         : MetadataMemberLookupStatus::MissingMember};
}

ClassLookupResult findClass(ManagedClassLocation location) noexcept {
    ++gClassResolutions;
    gLastClassLocation = location;
    return gNextClassLookup;
}

}

namespace {

void testGameClassLocation(TestRun& run, kue::mono::MonoClass* type) {
    using kue::mono::ClassLookupStatus;
    using kue::unity::GameClassLocation;

    gClassResolutions = 0;
    gNextClassLookup = {type, ClassLookupStatus::Resolved};
    const auto player = kue::unity::gameClass(
        GameClassLocation{.namespaceName = "GameNetcodeStuff", .className = "PlayerControllerB"});
    run.expect(player.type == type && player.status == ClassLookupStatus::Resolved,
               "a resolved game class preserves its typed runtime result");
    run.expect(gClassResolutions == 1, "a game class location performs one runtime lookup");
    run.expect(gLastClassLocation.imageName != nullptr &&
                   std::strcmp(gLastClassLocation.imageName, "Assembly-CSharp") == 0,
               "a game class lookup uses the exact game image");
    run.expect(gLastClassLocation.namespaceName != nullptr &&
                   std::strcmp(gLastClassLocation.namespaceName, "GameNetcodeStuff") == 0,
               "a game class lookup preserves its explicit namespace");
    run.expect(gLastClassLocation.className != nullptr &&
                   std::strcmp(gLastClassLocation.className, "PlayerControllerB") == 0,
               "a game class lookup preserves its explicit class name");

    gNextClassLookup = {nullptr, ClassLookupStatus::ClassUnavailable};
    const auto missing = kue::unity::gameClass(
        GameClassLocation{.namespaceName = "", .className = "PlayerControllerB"});
    run.expect(missing.type == nullptr && missing.status == ClassLookupStatus::ClassUnavailable,
               "a missing game class remains distinct from another lookup failure");
    run.expect(gLastClassLocation.namespaceName != nullptr &&
                   gLastClassLocation.namespaceName[0] == '\0',
               "an explicitly empty namespace is not inferred from the class name");

    constexpr std::array failureStatuses = {
        ClassLookupStatus::InvalidLocation, ClassLookupStatus::RuntimeUnavailable,
        ClassLookupStatus::DomainUnavailable, ClassLookupStatus::ImageUnavailable};
    for (const ClassLookupStatus status : failureStatuses) {
        gNextClassLookup = {nullptr, status};
        const auto failure = kue::unity::gameClass(
            GameClassLocation{.namespaceName = "", .className = "StartOfRound"});
        run.expect(failure.type == nullptr && failure.status == status,
                   "each runtime game-class failure remains typed");
    }
    run.expect(std::string_view(kue::unity::gameClassLookupStatusName(
                   ClassLookupStatus::RuntimeUnavailable)) == "runtime unavailable",
               "runtime unavailability has an exact actionable class-lookup status");
    run.expect(std::string_view(kue::unity::gameClassLookupStatusName(
                   ClassLookupStatus::ImageUnavailable)) == "image unavailable",
               "image unavailability remains distinct from a runtime failure");
    run.expect(std::string_view(kue::unity::gameClassLookupStatusName(
                   ClassLookupStatus::ClassUnavailable)) == "class unavailable",
               "class unavailability remains distinct from an image failure");
    run.expect(kue::unity::gameClassLookupMayRetry(ClassLookupStatus::RuntimeUnavailable) &&
                   kue::unity::gameClassLookupMayRetry(ClassLookupStatus::DomainUnavailable) &&
                   kue::unity::gameClassLookupMayRetry(ClassLookupStatus::ImageUnavailable),
               "only availability failures that can change during startup are retryable");
    run.expect(!kue::unity::gameClassLookupMayRetry(ClassLookupStatus::Resolved) &&
                   !kue::unity::gameClassLookupMayRetry(ClassLookupStatus::InvalidLocation) &&
                   !kue::unity::gameClassLookupMayRetry(ClassLookupStatus::ClassUnavailable),
               "resolved, invalid, and missing-class results are never retried");
}

void testValidationAndFieldCache(TestRun& run, kue::mono::MonoClass* firstType,
                                 kue::mono::MonoClass* secondType) {
    using kue::unity::MetadataLookupStatus;
    expectStatus(run, kue::unity::cachedField(nullptr, "health"), MetadataLookupStatus::NullType,
                 "a null declaring type is rejected");
    expectStatus(run, kue::unity::cachedField(nullptr, "health"), MetadataLookupStatus::NullType,
                 "a repeated null type remains rejected");
    expectStatus(run, kue::unity::cachedField(firstType, nullptr), MetadataLookupStatus::NullName,
                 "a null field name is rejected");
    expectStatus(run, kue::unity::cachedField(firstType, ""), MetadataLookupStatus::EmptyName,
                 "an empty field name is rejected");
    expectStatus(run, kue::unity::cachedField(firstType, ""), MetadataLookupStatus::EmptyName,
                 "a repeated empty name remains rejected");
    std::array<char, kue::unity::kMaximumMetadataNameBytes + 2> overlong{};
    overlong.fill('x');
    overlong.back() = '\0';
    expectStatus(run, kue::unity::cachedField(firstType, overlong.data()),
                 MetadataLookupStatus::NameTooLong, "an overlong field name is rejected");

    const auto missing = kue::unity::cachedField(firstType, "missingField");
    expectStatus(run, missing, MetadataLookupStatus::MissingMember,
                 "a missing field has an explicit result");
    run.expect(missing.member == nullptr, "a missing field has no member pointer");
    const int afterMissing = gFieldResolutions.load(std::memory_order_relaxed);
    expectStatus(run, kue::unity::cachedField(firstType, "missingField"),
                 MetadataLookupStatus::MissingMember, "a cached missing field remains explicit");
    run.expect(gFieldResolutions.load(std::memory_order_relaxed) == afterMissing,
               "a negative cache hit does not resolve again");

    const auto firstHealth = kue::unity::cachedField(firstType, "health");
    expectStatus(run, firstHealth, MetadataLookupStatus::Resolved,
                 "a field resolves for its declaring type");
    run.expect(firstHealth.member != nullptr, "a resolved field has a member pointer");
    const int afterFirstHealth = gFieldResolutions.load(std::memory_order_relaxed);
    const auto firstHealthHit = kue::unity::cachedField(firstType, "health");
    run.expect(firstHealthHit.status == MetadataLookupStatus::Resolved,
               "a positive field cache hit remains resolved");
    run.expect(gFieldResolutions.load(std::memory_order_relaxed) == afterFirstHealth,
               "a positive field cache hit does not resolve again");
    const auto secondHealth = kue::unity::cachedField(secondType, "health");
    run.expect(secondHealth.status == MetadataLookupStatus::Resolved,
               "the same field name on a different type has its own key");
    run.expect(gFieldResolutions.load(std::memory_order_relaxed) == afterFirstHealth + 1,
               "a different declaring type performs a distinct resolution");

    gRuntimeMemberLookupAvailable.store(false, std::memory_order_relaxed);
    expectStatus(run, kue::unity::cachedField(firstType, "runtimeField"),
                 MetadataLookupStatus::RuntimeUnavailable,
                 "an unavailable runtime field resolver is explicit");
    expectStatus(run, kue::unity::cachedField(firstType, "runtimeField"),
                 MetadataLookupStatus::RuntimeUnavailable,
                 "repeated runtime resolver failure remains explicit");
    gRuntimeMemberLookupAvailable.store(true, std::memory_order_relaxed);

    constexpr std::size_t existingFieldKeys = 3;
    for (std::size_t index = existingFieldKeys; index < kue::unity::kFieldMetadataCacheCapacity;
         ++index) {
        char name[16]{};
        const int length = std::snprintf(name, sizeof(name), "field%zu", index);
        run.expect(length > 0 && static_cast<std::size_t>(length) < sizeof(name),
                   "capacity test field name fits its fixed buffer");
        run.expect(kue::unity::cachedField(firstType, name).status ==
                       MetadataLookupStatus::Resolved,
                   "every declared field-cache entry is accepted");
    }
    const int beforeOverflow = gFieldResolutions.load(std::memory_order_relaxed);
    expectStatus(run, kue::unity::cachedField(firstType, "overflowField"),
                 MetadataLookupStatus::CapacityExceeded,
                 "the first field beyond current workload capacity is rejected");
    expectStatus(run, kue::unity::cachedField(firstType, "overflowField"),
                 MetadataLookupStatus::CapacityExceeded,
                 "a repeated over-capacity field remains rejected");
    run.expect(gFieldResolutions.load(std::memory_order_relaxed) == beforeOverflow,
               "a capacity rejection does not call the runtime resolver");
}

}

int main() {
    TestRun run;
    auto* firstType = reinterpret_cast<kue::mono::MonoClass*>(&gClassStorage[0]);
    auto* secondType = reinterpret_cast<kue::mono::MonoClass*>(&gClassStorage[1]);
    gAllocations.store(0, std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_relaxed);
    testGameClassLocation(run, firstType);
    testValidationAndFieldCache(run, firstType, secondType);
    for (std::size_t index = 0; index < 1000; ++index) {
        static_cast<void>(kue::unity::cachedField(firstType, "health"));
        static_cast<void>(kue::unity::cachedField(firstType, "missingField"));
    }
    gMeasureAllocations.store(false, std::memory_order_relaxed);
    run.expect(gAllocations.load(std::memory_order_relaxed) == 0,
               "cache initialization and steady lookups allocate no heap memory");
    return run.result();
}
