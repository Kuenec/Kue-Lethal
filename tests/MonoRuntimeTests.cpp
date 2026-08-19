#include "mono/PeExport.h"
#include "mono/Runtime.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <pthread.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace kue::mono {

struct MonoDomain;
struct MonoVTable;

}

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

alignas(std::max_align_t) std::byte gDomainStorage{};
std::atomic<bool> gMeasureAllocations{false};
std::atomic<std::size_t> gAllocationCount{0};
std::atomic<std::size_t> gAllocatedBytes{0};
std::atomic<bool> gMonoFixtureEnabled{false};
std::atomic<std::size_t> gMonoAttachCalls{0};
std::atomic<std::size_t> gMonoDetachCalls{0};
std::atomic<std::size_t> gMonoStringCharsCalls{0};
std::atomic<bool> gMonoCharactersAvailable{true};
std::atomic<bool> gMonoAttachmentAvailable{true};
std::atomic<bool> gMonoRootDomainAvailable{true};
std::atomic<bool> gMonoDomainAvailable{true};
std::atomic<bool> gMonoVtableAvailable{true};
std::atomic<std::size_t> gMonoClassVtableCalls{0};
std::atomic<std::size_t> gMonoFieldGetValueCalls{0};
std::atomic<std::size_t> gMonoFieldStaticGetValueCalls{0};
std::atomic<bool> gMonoFieldCallMismatch{false};

enum class MissingMonoSymbol : std::uint8_t {
    None,
    ClassVtable,
    FieldGetValue,
    FieldStaticGetValue
};

std::atomic<MissingMonoSymbol> gMissingMonoSymbol{MissingMonoSymbol::None};

enum class MonoClassFixture : std::uint8_t { MissingImage, MissingClass, Resolved, MaximumName };

std::atomic<MonoClassFixture> gMonoClassFixture{MonoClassFixture::MissingImage};
alignas(std::max_align_t) std::byte gMonoImageStorage{};
alignas(std::max_align_t) std::byte gMonoClassStorage{};
alignas(std::max_align_t) std::byte gMonoVtableStorage{};
alignas(std::max_align_t) std::byte gMonoReferencedObjectStorage{};
alignas(std::max_align_t) std::byte gMonoObjectFieldStorage{};
alignas(std::max_align_t) std::byte gMonoFloatFieldStorage{};
alignas(std::max_align_t) std::byte gMonoIntFieldStorage{};
alignas(std::max_align_t) std::byte gMonoBoolFieldStorage{};
alignas(std::max_align_t) std::byte gMonoU64FieldStorage{};
alignas(std::max_align_t) std::byte gMonoStaticObjectFieldStorage{};
constexpr std::size_t kFixtureArrayLength = 64;

struct FakeMonoArray final {
    std::array<kue::mono::MonoObject*, kFixtureArrayLength> elements{};
};

FakeMonoArray gMonoArray;
std::atomic<std::size_t> gMonoArrayLength{kFixtureArrayLength};
std::atomic<std::size_t> gMonoArrayLengthCalls{0};
std::atomic<std::size_t> gMonoArrayAddressCalls{0};
std::atomic<bool> gMonoArrayAddressAvailable{true};

struct FakeMonoObjectFields {
    kue::mono::MonoObject* object;
    float floating;
    int integer;
    std::uint8_t boolean;
    std::uint64_t unsignedInteger;
};

FakeMonoObjectFields gMonoObjectFields{
    reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage), 41.75F, -9327, 1,
    UINT64_C(0xfedcba9876543210)};
kue::mono::MonoObject* gMonoStaticObject =
    reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage);

struct FakeMonoString {
    std::array<std::uint16_t, 513> characters{};
    int length = 0;
};

FakeMonoString gMonoString;

#define MSABI __attribute__((ms_abi))

void* MSABI fakeMonoGetRootDomain() {
    return gMonoRootDomainAvailable.load(std::memory_order_relaxed) ? &gDomainStorage : nullptr;
}

void* MSABI fakeMonoDomainGet() {
    return gMonoDomainAvailable.load(std::memory_order_relaxed) ? &gDomainStorage : nullptr;
}

bool isMaximumAssemblyName(const char* name) noexcept {
    if (!name)
        return false;
    for (std::size_t index = 0; index < kue::mono::kManagedImageNameCapacity; ++index) {
        if (name[index] != 'A')
            return false;
    }
    return std::strcmp(name + kue::mono::kManagedImageNameCapacity, ".dll") == 0;
}

void* MSABI fakeMonoImageLoaded(const char* name) {
    const MonoClassFixture fixture = gMonoClassFixture.load(std::memory_order_relaxed);
    if (fixture == MonoClassFixture::MaximumName)
        return isMaximumAssemblyName(name) ? &gMonoImageStorage : nullptr;
    if (fixture == MonoClassFixture::MissingImage)
        return nullptr;
    return name && std::strcmp(name, "Assembly-CSharp") == 0 ? &gMonoImageStorage : nullptr;
}

void* MSABI fakeMonoDomainAssemblyOpen(void*, const char*) {
    return nullptr;
}

void MSABI fakeMonoAddInternalCall(const char*, const void*) {}

void* MSABI fakeMonoAssemblyGetImage(void*) {
    return nullptr;
}

void* MSABI fakeMonoClassFromName(void* image, const char* namespaceName, const char* className) {
    const MonoClassFixture fixture = gMonoClassFixture.load(std::memory_order_relaxed);
    if (image != &gMonoImageStorage || !namespaceName || !className ||
        std::strcmp(namespaceName, "GameNetcodeStuff") != 0 ||
        (std::strcmp(className, "PlayerControllerB") != 0 &&
         std::strcmp(className, "GameNetworkManager") != 0)) {
        return nullptr;
    }
    return fixture == MonoClassFixture::Resolved || fixture == MonoClassFixture::MaximumName
               ? &gMonoClassStorage
               : nullptr;
}

void* MSABI fakeMonoClassGetFieldFromName(void*, const char*) {
    return nullptr;
}

void* MSABI fakeMonoClassGetMethodFromName(void*, const char*, int) {
    return nullptr;
}

[[nodiscard]] kue::mono::MonoVTable* MSABI fakeMonoClassVtable(kue::mono::MonoDomain* domain,
                                                               kue::mono::MonoClass* klass) {
    gMonoClassVtableCalls.fetch_add(1, std::memory_order_relaxed);
    if (domain != reinterpret_cast<kue::mono::MonoDomain*>(&gDomainStorage) ||
        klass != reinterpret_cast<kue::mono::MonoClass*>(&gMonoClassStorage)) {
        gMonoFieldCallMismatch.store(true, std::memory_order_relaxed);
    }
    return gMonoVtableAvailable.load(std::memory_order_relaxed)
               ? reinterpret_cast<kue::mono::MonoVTable*>(&gMonoVtableStorage)
               : nullptr;
}

void MSABI fakeMonoFieldGetValue(kue::mono::MonoObject* object, kue::mono::MonoClassField* field,
                                 void* value) {
    gMonoFieldGetValueCalls.fetch_add(1, std::memory_order_relaxed);
    if (object != reinterpret_cast<kue::mono::MonoObject*>(&gMonoObjectFields) || !value) {
        gMonoFieldCallMismatch.store(true, std::memory_order_relaxed);
        return;
    }
    if (field == reinterpret_cast<kue::mono::MonoClassField*>(&gMonoObjectFieldStorage)) {
        std::memcpy(value, static_cast<const void*>(&gMonoObjectFields.object),
                    sizeof(kue::mono::MonoObject*));
    } else if (field == reinterpret_cast<kue::mono::MonoClassField*>(&gMonoFloatFieldStorage)) {
        std::memcpy(value, &gMonoObjectFields.floating, sizeof(gMonoObjectFields.floating));
    } else if (field == reinterpret_cast<kue::mono::MonoClassField*>(&gMonoIntFieldStorage)) {
        std::memcpy(value, &gMonoObjectFields.integer, sizeof(gMonoObjectFields.integer));
    } else if (field == reinterpret_cast<kue::mono::MonoClassField*>(&gMonoBoolFieldStorage)) {
        std::memcpy(value, &gMonoObjectFields.boolean, sizeof(gMonoObjectFields.boolean));
    } else if (field == reinterpret_cast<kue::mono::MonoClassField*>(&gMonoU64FieldStorage)) {
        std::memcpy(value, &gMonoObjectFields.unsignedInteger,
                    sizeof(gMonoObjectFields.unsignedInteger));
    } else {
        gMonoFieldCallMismatch.store(true, std::memory_order_relaxed);
    }
}

void MSABI fakeMonoFieldStaticGetValue(kue::mono::MonoVTable* vtable,
                                       kue::mono::MonoClassField* field, void* value) {
    gMonoFieldStaticGetValueCalls.fetch_add(1, std::memory_order_relaxed);
    if (vtable != reinterpret_cast<kue::mono::MonoVTable*>(&gMonoVtableStorage) ||
        field != reinterpret_cast<kue::mono::MonoClassField*>(&gMonoStaticObjectFieldStorage) ||
        !value) {
        gMonoFieldCallMismatch.store(true, std::memory_order_relaxed);
        return;
    }
    std::memcpy(value, static_cast<const void*>(&gMonoStaticObject),
                sizeof(kue::mono::MonoObject*));
}

void* MSABI fakeMonoRuntimeInvoke(void*, void*, void**, void**) {
    return nullptr;
}

std::uintptr_t MSABI fakeMonoArrayLength(void* array) {
    gMonoArrayLengthCalls.fetch_add(1, std::memory_order_relaxed);
    return array == &gMonoArray ? gMonoArrayLength.load(std::memory_order_relaxed) : 0;
}

char* MSABI fakeMonoArrayAddress(void* array, int elementBytes, std::uintptr_t index) {
    gMonoArrayAddressCalls.fetch_add(1, std::memory_order_relaxed);
    if (array != &gMonoArray || !gMonoArrayAddressAvailable.load(std::memory_order_relaxed) ||
        elementBytes != static_cast<int>(sizeof(kue::mono::MonoObject*)) ||
        index >= kFixtureArrayLength) {
        return nullptr;
    }
    return reinterpret_cast<char*>(&gMonoArray.elements[static_cast<std::size_t>(index)]);
}

void* MSABI fakeMonoThreadAttach(void*) {
    gMonoAttachCalls.fetch_add(1, std::memory_order_relaxed);
    return gMonoAttachmentAvailable.load(std::memory_order_relaxed) ? &gDomainStorage : nullptr;
}

void MSABI fakeMonoThreadDetach(void*) {
    gMonoDetachCalls.fetch_add(1, std::memory_order_relaxed);
}

void* MSABI fakeMonoThreadCurrent() {
    return nullptr;
}

std::uint16_t* MSABI fakeMonoStringChars(void* stringObject) {
    gMonoStringCharsCalls.fetch_add(1, std::memory_order_relaxed);
    if (!gMonoCharactersAvailable.load(std::memory_order_relaxed))
        return nullptr;
    auto* string = static_cast<FakeMonoString*>(stringObject);
    return string ? string->characters.data() : nullptr;
}

int MSABI fakeMonoStringLength(void* stringObject) {
    const auto* string = static_cast<const FakeMonoString*>(stringObject);
    return string ? string->length : -1;
}

#undef MSABI

template <typename Function> void* functionAddress(Function function) noexcept {
    static_assert(sizeof(Function) == sizeof(void*));
    return reinterpret_cast<void*>(function);
}

void recordAllocation(std::size_t size) noexcept {
    if (!gMeasureAllocations.load(std::memory_order_relaxed))
        return;
    gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    gAllocatedBytes.fetch_add(size, std::memory_order_relaxed);
}

void* allocate(std::size_t size) {
    const std::size_t actualSize = size == 0 ? 1 : size;
    recordAllocation(actualSize);
    if (void* memory = std::malloc(actualSize))
        return memory;
    throw std::bad_alloc();
}

struct AlignedAllocation {
    std::size_t size;
    std::size_t alignment;
};

void* allocateAligned(AlignedAllocation request) {
    const std::size_t actualSize = request.size == 0 ? 1 : request.size;
    recordAllocation(actualSize);
    void* memory = nullptr;
    if (posix_memalign(&memory, request.alignment, actualSize) == 0)
        return memory;
    throw std::bad_alloc();
}

}

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned({.size = size, .alignment = static_cast<std::size_t>(alignment)});
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned({.size = size, .alignment = static_cast<std::size_t>(alignment)});
}

void operator delete(void* memory, std::align_val_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    std::free(memory);
}

namespace kue::mono::pe {

void* moduleBase(const char* moduleSubstring) {
    if (!gMonoFixtureEnabled.load(std::memory_order_acquire) || !moduleSubstring ||
        std::strcmp(moduleSubstring, "mono-2.0-bdwgc") != 0) {
        return nullptr;
    }
    return &gDomainStorage;
}

void* getExport(void* base, const char* name) {
    if (base != &gDomainStorage || !name)
        return nullptr;
    if (std::strcmp(name, "mono_get_root_domain") == 0)
        return functionAddress(&fakeMonoGetRootDomain);
    if (std::strcmp(name, "mono_domain_get") == 0)
        return functionAddress(&fakeMonoDomainGet);
    if (std::strcmp(name, "mono_image_loaded") == 0)
        return functionAddress(&fakeMonoImageLoaded);
    if (std::strcmp(name, "mono_domain_assembly_open") == 0)
        return functionAddress(&fakeMonoDomainAssemblyOpen);
    if (std::strcmp(name, "mono_add_internal_call") == 0)
        return functionAddress(&fakeMonoAddInternalCall);
    if (std::strcmp(name, "mono_assembly_get_image") == 0)
        return functionAddress(&fakeMonoAssemblyGetImage);
    if (std::strcmp(name, "mono_class_from_name") == 0)
        return functionAddress(&fakeMonoClassFromName);
    if (std::strcmp(name, "mono_class_get_field_from_name") == 0)
        return functionAddress(&fakeMonoClassGetFieldFromName);
    if (std::strcmp(name, "mono_class_get_method_from_name") == 0)
        return functionAddress(&fakeMonoClassGetMethodFromName);
    if (std::strcmp(name, "mono_class_vtable") == 0)
        return gMissingMonoSymbol.load(std::memory_order_relaxed) == MissingMonoSymbol::ClassVtable
                   ? nullptr
                   : functionAddress(&fakeMonoClassVtable);
    if (std::strcmp(name, "mono_field_get_value") == 0)
        return gMissingMonoSymbol.load(std::memory_order_relaxed) ==
                       MissingMonoSymbol::FieldGetValue
                   ? nullptr
                   : functionAddress(&fakeMonoFieldGetValue);
    if (std::strcmp(name, "mono_field_static_get_value") == 0)
        return gMissingMonoSymbol.load(std::memory_order_relaxed) ==
                       MissingMonoSymbol::FieldStaticGetValue
                   ? nullptr
                   : functionAddress(&fakeMonoFieldStaticGetValue);
    if (std::strcmp(name, "mono_runtime_invoke") == 0)
        return functionAddress(&fakeMonoRuntimeInvoke);
    if (std::strcmp(name, "mono_array_length") == 0)
        return functionAddress(&fakeMonoArrayLength);
    if (std::strcmp(name, "mono_array_addr_with_size") == 0)
        return functionAddress(&fakeMonoArrayAddress);
    if (std::strcmp(name, "mono_thread_attach") == 0)
        return functionAddress(&fakeMonoThreadAttach);
    if (std::strcmp(name, "mono_thread_detach") == 0)
        return functionAddress(&fakeMonoThreadDetach);
    if (std::strcmp(name, "mono_thread_current") == 0)
        return functionAddress(&fakeMonoThreadCurrent);
    if (std::strcmp(name, "mono_string_chars") == 0)
        return functionAddress(&fakeMonoStringChars);
    if (std::strcmp(name, "mono_string_length") == 0)
        return functionAddress(&fakeMonoStringLength);
    return nullptr;
}

}

namespace {

enum class ManagedStringProbeResult : std::uint8_t {
    Passed = 0,
    ResolutionFailed = 1,
    ConversionFailed = 2,
    ContentMismatch = 3,
    Allocated = 4,
    AttachmentMismatch = 5,
    EmptyStringMismatch = 6,
    MissingCharactersMismatch = 7,
    EmbeddedNullMismatch = 8,
    InvalidSurrogateMismatch = 9,
    OversizedMismatch = 10,
    ClassLookupMismatch = 11,
    FieldValueMismatch = 12,
    FieldFailureMismatch = 13,
    ForkFailed = 253,
    WaitFailed = 254,
    AbnormalExit = 255
};

ManagedStringProbeResult probeManagedStringAllocation() {
    gMonoFixtureEnabled.store(true, std::memory_order_release);
    if (!kue::mono::resolve())
        return ManagedStringProbeResult::ResolutionFailed;
    gMonoString.length = 512;
    gMonoString.characters.fill(static_cast<std::uint16_t>('A'));
    std::array<char, kue::mono::kManagedStringMaxUtf8Bytes> output{};
    std::array<char, 32> unchangedOutput{};
    unchangedOutput.fill('Z');
    const std::array<char, 32> unchangedReference = unchangedOutput;
    const kue::mono::ManagedStringUtf8Result nullString =
        kue::mono::readManagedStringUtf8(nullptr, {unchangedOutput.data(), unchangedOutput.size()});
    if (nullString.status != kue::mono::ManagedStringUtf8Status::NullString ||
        unchangedOutput != unchangedReference) {
        return ManagedStringProbeResult::ConversionFailed;
    }

    gAllocationCount.store(0, std::memory_order_relaxed);
    gAllocatedBytes.store(0, std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_release);
    const kue::mono::ManagedStringUtf8Result converted = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (converted.status != kue::mono::ManagedStringUtf8Status::Success)
        return ManagedStringProbeResult::ConversionFailed;
    if (converted.text.size() != static_cast<std::size_t>(gMonoString.length) ||
        converted.text.find_first_not_of('A') != std::string_view::npos) {
        return ManagedStringProbeResult::ContentMismatch;
    }
    if (gAllocationCount.load(std::memory_order_relaxed) != 0)
        return ManagedStringProbeResult::Allocated;

    gMonoString.length = 0;
    gMonoStringCharsCalls.store(0, std::memory_order_relaxed);
    const kue::mono::ManagedStringUtf8Result empty = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (empty.status != kue::mono::ManagedStringUtf8Status::Success || !empty.text.empty() ||
        gMonoStringCharsCalls.load(std::memory_order_relaxed) != 0) {
        return ManagedStringProbeResult::EmptyStringMismatch;
    }

    gMonoString.length = 1;
    gMonoCharactersAvailable.store(false, std::memory_order_relaxed);
    const kue::mono::ManagedStringUtf8Result missingCharacters = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    gMonoCharactersAvailable.store(true, std::memory_order_relaxed);
    if (missingCharacters.status != kue::mono::ManagedStringUtf8Status::CharactersUnavailable) {
        return ManagedStringProbeResult::MissingCharactersMismatch;
    }

    gMonoString.characters[0] = 0;
    const kue::mono::ManagedStringUtf8Result embeddedNull = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (embeddedNull.status != kue::mono::ManagedStringUtf8Status::EmbeddedNull)
        return ManagedStringProbeResult::EmbeddedNullMismatch;

    gMonoString.characters[0] = 0xd800U;
    const kue::mono::ManagedStringUtf8Result invalidSurrogate = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (invalidSurrogate.status != kue::mono::ManagedStringUtf8Status::InvalidSurrogate)
        return ManagedStringProbeResult::InvalidSurrogateMismatch;

    constexpr std::array<std::uint16_t, 2> supplementary{0xd83dU, 0xde80U};
    gMonoString.length = 2;
    std::copy(supplementary.begin(), supplementary.end(), gMonoString.characters.begin());
    unchangedOutput.fill('Z');
    const kue::mono::ManagedStringUtf8Result insufficient = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {unchangedOutput.data(), 3});
    if (insufficient.status != kue::mono::ManagedStringUtf8Status::OutputCapacityExceeded ||
        insufficient.validatedCodeUnits != 2 || insufficient.utf8Bytes != 4 ||
        unchangedOutput != unchangedReference) {
        return ManagedStringProbeResult::ConversionFailed;
    }

    gMonoString.length = 513;
    gMonoStringCharsCalls.store(0, std::memory_order_relaxed);
    const kue::mono::ManagedStringUtf8Result oversized = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (oversized.status != kue::mono::ManagedStringUtf8Status::InvalidCodeUnitCount ||
        gMonoStringCharsCalls.load(std::memory_order_relaxed) != 0) {
        return ManagedStringProbeResult::OversizedMismatch;
    }

    constexpr kue::mono::ManagedClassLocation gameClassLocation{
        "Assembly-CSharp", "GameNetcodeStuff", "PlayerControllerB"};
    gMonoClassFixture.store(MonoClassFixture::MissingImage, std::memory_order_relaxed);
    const kue::mono::ClassLookupResult imageMissing = kue::mono::findClass(gameClassLocation);
    gMonoClassFixture.store(MonoClassFixture::MissingClass, std::memory_order_relaxed);
    const kue::mono::ClassLookupResult classMissing = kue::mono::findClass(gameClassLocation);
    gMonoClassFixture.store(MonoClassFixture::Resolved, std::memory_order_relaxed);
    const kue::mono::ClassLookupResult classResolved = kue::mono::findClass(gameClassLocation);
    std::array<char, kue::mono::kManagedImageNameCapacity + 1> maximumImageName{};
    maximumImageName.fill('A');
    maximumImageName.back() = '\0';
    gMonoClassFixture.store(MonoClassFixture::MaximumName, std::memory_order_relaxed);
    const kue::mono::ClassLookupResult maximumName =
        kue::mono::findClass({maximumImageName.data(), "GameNetcodeStuff", "PlayerControllerB"});
    std::array<char, kue::mono::kManagedImageNameCapacity + 2> unboundedImageName{};
    unboundedImageName.fill('A');
    unboundedImageName.back() = '\0';
    const kue::mono::ClassLookupResult unboundedName =
        kue::mono::findClass({unboundedImageName.data(), "GameNetcodeStuff", "PlayerControllerB"});
    constexpr std::array<char, 2> invalidUtf8{static_cast<char>(0xc0), '\0'};
    const kue::mono::ClassLookupResult invalidEncoding =
        kue::mono::findClass({invalidUtf8.data(), "GameNetcodeStuff", "PlayerControllerB"});
    constexpr std::array<char, kue::mono::kManagedNamespaceNameCapacity + 2> unboundedNamespace = {
        'G', 'a', 'm', 'e', 'N', 'e', 't', 'c', 'o', 'd', 'e', 'S', 't', 'u', 'f', 'f', 'X', '\0'};
    const kue::mono::ClassLookupResult invalidNamespace =
        kue::mono::findClass({"Assembly-CSharp", unboundedNamespace.data(), "PlayerControllerB"});
    constexpr std::array<char, kue::mono::kManagedClassNameCapacity + 2> unboundedClass = {
        'G', 'a', 'm', 'e', 'N', 'e', 't', 'w', 'o', 'r',
        'k', 'M', 'a', 'n', 'a', 'g', 'e', 'r', 'X', '\0'};
    const kue::mono::ClassLookupResult invalidClass =
        kue::mono::findClass({"Assembly-CSharp", "GameNetcodeStuff", unboundedClass.data()});
    gMonoClassFixture.store(MonoClassFixture::Resolved, std::memory_order_relaxed);
    const kue::mono::ClassLookupResult maximumClass =
        kue::mono::findClass({"Assembly-CSharp", "GameNetcodeStuff", "GameNetworkManager"});
    if (imageMissing.status != kue::mono::ClassLookupStatus::ImageUnavailable ||
        classMissing.status != kue::mono::ClassLookupStatus::ClassUnavailable ||
        classResolved.status != kue::mono::ClassLookupStatus::Resolved ||
        classResolved.type != reinterpret_cast<kue::mono::MonoClass*>(&gMonoClassStorage) ||
        maximumName.status != kue::mono::ClassLookupStatus::Resolved ||
        unboundedName.status != kue::mono::ClassLookupStatus::InvalidLocation ||
        invalidEncoding.status != kue::mono::ClassLookupStatus::InvalidLocation ||
        invalidNamespace.status != kue::mono::ClassLookupStatus::InvalidLocation ||
        invalidClass.status != kue::mono::ClassLookupStatus::InvalidLocation ||
        maximumClass.status != kue::mono::ClassLookupStatus::Resolved ||
        gAllocationCount.load(std::memory_order_relaxed) != 0) {
        return ManagedStringProbeResult::ClassLookupMismatch;
    }

    auto* object = reinterpret_cast<kue::mono::MonoObject*>(&gMonoObjectFields);
    auto* objectField = reinterpret_cast<kue::mono::MonoClassField*>(&gMonoObjectFieldStorage);
    auto* floatField = reinterpret_cast<kue::mono::MonoClassField*>(&gMonoFloatFieldStorage);
    auto* intField = reinterpret_cast<kue::mono::MonoClassField*>(&gMonoIntFieldStorage);
    auto* boolField = reinterpret_cast<kue::mono::MonoClassField*>(&gMonoBoolFieldStorage);
    auto* u64Field = reinterpret_cast<kue::mono::MonoClassField*>(&gMonoU64FieldStorage);
    auto* staticObjectField =
        reinterpret_cast<kue::mono::MonoClassField*>(&gMonoStaticObjectFieldStorage);
    auto* type = reinterpret_cast<kue::mono::MonoClass*>(&gMonoClassStorage);
    kue::mono::MonoObject* objectValue = nullptr;
    float floatValue = 0.0F;
    int intValue = 0;
    bool boolValue = false;
    std::uint64_t u64Value = 0;
    kue::mono::MonoObject* staticObjectValue = nullptr;
    if (!kue::mono::readInstanceObject(object, objectField, objectValue) ||
        objectValue != gMonoObjectFields.object ||
        !kue::mono::readInstanceFloat(object, floatField, floatValue) ||
        std::bit_cast<std::uint32_t>(floatValue) !=
            std::bit_cast<std::uint32_t>(gMonoObjectFields.floating) ||
        !kue::mono::readInstanceInt(object, intField, intValue) ||
        intValue != gMonoObjectFields.integer ||
        !kue::mono::readInstanceBool(object, boolField, boolValue) ||
        boolValue != (gMonoObjectFields.boolean != 0) ||
        !kue::mono::readInstanceU64(object, u64Field, u64Value) ||
        u64Value != gMonoObjectFields.unsignedInteger ||
        !kue::mono::readStaticObject(type, staticObjectField, staticObjectValue) ||
        staticObjectValue != gMonoStaticObject ||
        gMonoFieldGetValueCalls.load(std::memory_order_relaxed) != 5 ||
        gMonoClassVtableCalls.load(std::memory_order_relaxed) != 1 ||
        gMonoFieldStaticGetValueCalls.load(std::memory_order_relaxed) != 1 ||
        gMonoFieldCallMismatch.load(std::memory_order_relaxed)) {
        return ManagedStringProbeResult::FieldValueMismatch;
    }

    gMonoObjectFields.boolean = 0;
    boolValue = true;
    if (!kue::mono::readInstanceBool(object, boolField, boolValue) || boolValue ||
        gMonoFieldGetValueCalls.load(std::memory_order_relaxed) != 6) {
        return ManagedStringProbeResult::FieldValueMismatch;
    }

    gMonoObjectFields.object = nullptr;
    objectValue = reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage);
    if (!kue::mono::readInstanceObject(object, objectField, objectValue) || objectValue ||
        gMonoFieldGetValueCalls.load(std::memory_order_relaxed) != 7) {
        return ManagedStringProbeResult::FieldValueMismatch;
    }

    gMonoRootDomainAvailable.store(false, std::memory_order_relaxed);
    gMonoDomainAvailable.store(false, std::memory_order_relaxed);
    staticObjectValue = reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage);
    const std::size_t classVtableCalls = gMonoClassVtableCalls.load(std::memory_order_relaxed);
    if (kue::mono::readStaticObject(type, staticObjectField, staticObjectValue) ||
        staticObjectValue ||
        gMonoClassVtableCalls.load(std::memory_order_relaxed) != classVtableCalls) {
        return ManagedStringProbeResult::FieldFailureMismatch;
    }
    gMonoRootDomainAvailable.store(true, std::memory_order_relaxed);
    gMonoDomainAvailable.store(true, std::memory_order_relaxed);

    gMonoVtableAvailable.store(false, std::memory_order_relaxed);
    staticObjectValue = reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage);
    const std::size_t staticGetCalls =
        gMonoFieldStaticGetValueCalls.load(std::memory_order_relaxed);
    if (kue::mono::readStaticObject(type, staticObjectField, staticObjectValue) ||
        staticObjectValue ||
        gMonoClassVtableCalls.load(std::memory_order_relaxed) != classVtableCalls + 1 ||
        gMonoFieldStaticGetValueCalls.load(std::memory_order_relaxed) != staticGetCalls) {
        return ManagedStringProbeResult::FieldFailureMismatch;
    }
    gMonoVtableAvailable.store(true, std::memory_order_relaxed);

    kue::mono::detachCurrentThread();
    gMonoAttachmentAvailable.store(false, std::memory_order_relaxed);
    const std::size_t fieldGetCalls = gMonoFieldGetValueCalls.load(std::memory_order_relaxed);
    const std::size_t failedAttachmentVtableCalls =
        gMonoClassVtableCalls.load(std::memory_order_relaxed);
    floatValue = -77.25F;
    if (kue::mono::readInstanceFloat(object, floatField, floatValue) ||
        std::bit_cast<std::uint32_t>(floatValue) != std::bit_cast<std::uint32_t>(-77.25F) ||
        gMonoFieldGetValueCalls.load(std::memory_order_relaxed) != fieldGetCalls ||
        gAllocationCount.load(std::memory_order_relaxed) != 0) {
        return ManagedStringProbeResult::FieldFailureMismatch;
    }
    staticObjectValue = reinterpret_cast<kue::mono::MonoObject*>(&gMonoReferencedObjectStorage);
    if (kue::mono::readStaticObject(type, staticObjectField, staticObjectValue) ||
        staticObjectValue ||
        gMonoClassVtableCalls.load(std::memory_order_relaxed) != failedAttachmentVtableCalls ||
        gAllocationCount.load(std::memory_order_relaxed) != 0) {
        return ManagedStringProbeResult::FieldFailureMismatch;
    }
    gMeasureAllocations.store(false, std::memory_order_release);
    const kue::mono::ManagedStringUtf8Result attachmentFailure = kue::mono::readManagedStringUtf8(
        reinterpret_cast<kue::mono::MonoObject*>(&gMonoString), {output.data(), output.size()});
    if (attachmentFailure.status != kue::mono::ManagedStringUtf8Status::ThreadAttachmentFailed ||
        gMonoAttachCalls.load(std::memory_order_relaxed) != 4 ||
        gMonoDetachCalls.load(std::memory_order_relaxed) != 1) {
        return ManagedStringProbeResult::AttachmentMismatch;
    }
    return ManagedStringProbeResult::Passed;
}

enum class MissingMonoSymbolProbeResult : std::uint8_t {
    Passed = 0,
    UnexpectedResolution = 1,
    UnexpectedReadiness = 2,
    ForkFailed = 253,
    WaitFailed = 254,
    AbnormalExit = 255
};

MissingMonoSymbolProbeResult runMissingMonoSymbolProbe(MissingMonoSymbol missingSymbol) {
    gMissingMonoSymbol.store(missingSymbol, std::memory_order_release);
    const pid_t child = ::fork();
    if (child < 0) {
        gMissingMonoSymbol.store(MissingMonoSymbol::None, std::memory_order_release);
        return MissingMonoSymbolProbeResult::ForkFailed;
    }
    if (child == 0) {
        gMonoFixtureEnabled.store(true, std::memory_order_release);
        if (kue::mono::resolve())
            std::_Exit(static_cast<int>(MissingMonoSymbolProbeResult::UnexpectedResolution));
        if (kue::mono::ready())
            std::_Exit(static_cast<int>(MissingMonoSymbolProbeResult::UnexpectedReadiness));
        std::_Exit(static_cast<int>(MissingMonoSymbolProbeResult::Passed));
    }
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    gMissingMonoSymbol.store(MissingMonoSymbol::None, std::memory_order_release);
    if (waited != child)
        return MissingMonoSymbolProbeResult::WaitFailed;
    if (!WIFEXITED(status))
        return MissingMonoSymbolProbeResult::AbnormalExit;
    return static_cast<MissingMonoSymbolProbeResult>(WEXITSTATUS(status));
}

ManagedStringProbeResult runManagedStringProbe() {
    const pid_t child = ::fork();
    if (child < 0)
        return ManagedStringProbeResult::ForkFailed;
    if (child == 0)
        std::_Exit(static_cast<int>(probeManagedStringAllocation()));
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
        return ManagedStringProbeResult::WaitFailed;
    if (!WIFEXITED(status))
        return ManagedStringProbeResult::AbnormalExit;
    return static_cast<ManagedStringProbeResult>(WEXITSTATUS(status));
}

enum class PublicationProbeResult : std::uint8_t {
    Passed = 0,
    ResolutionFailed = 1,
    PartialPublication = 2,
    ThreadCreateFailed = 3,
    ThreadJoinFailed = 4,
    ForkFailed = 253,
    WaitFailed = 254,
    AbnormalExit = 255
};

struct PublicationReaderContext {
    std::atomic<std::size_t>& waiting;
    std::atomic<std::size_t>& invalidSnapshots;
    std::atomic<bool>& start;
    std::size_t reads;
};

void* readPublishedRuntime(void* input) noexcept {
    auto& context = *static_cast<PublicationReaderContext*>(input);
    context.waiting.fetch_add(1, std::memory_order_release);
    context.waiting.notify_one();
    while (!context.start.load(std::memory_order_acquire))
        context.start.wait(false, std::memory_order_acquire);
    for (std::size_t index = 0; index < context.reads; ++index) {
        if (!kue::mono::ready())
            continue;
        const kue::mono::ClassLookupResult result =
            kue::mono::findClass({"Assembly-CSharp", "GameNetcodeStuff", "PlayerControllerB"});
        if (result.status != kue::mono::ClassLookupStatus::Resolved || !result.type)
            context.invalidSnapshots.fetch_add(1, std::memory_order_relaxed);
    }
    return nullptr;
}

PublicationProbeResult probeConcurrentPublication() {
    constexpr std::size_t readerCount = 8;
    constexpr std::size_t readsPerThread = 100000;
    std::array<pthread_t, readerCount> readers{};
    std::atomic<std::size_t> waiting{0};
    std::atomic<std::size_t> invalidSnapshots{0};
    std::atomic<bool> start{false};
    PublicationReaderContext context{waiting, invalidSnapshots, start, readsPerThread};
    gMonoFixtureEnabled.store(true, std::memory_order_release);
    gMonoClassFixture.store(MonoClassFixture::Resolved, std::memory_order_release);
    std::size_t created = 0;
    for (; created < readers.size(); ++created) {
        if (::pthread_create(&readers[created], nullptr, readPublishedRuntime, &context) == 0)
            continue;
        start.store(true, std::memory_order_release);
        start.notify_all();
        for (std::size_t index = 0; index < created; ++index) {
            if (::pthread_join(readers[index], nullptr) != 0)
                return PublicationProbeResult::ThreadJoinFailed;
        }
        return PublicationProbeResult::ThreadCreateFailed;
    }
    std::size_t waitingReaders = waiting.load(std::memory_order_acquire);
    while (waitingReaders != readers.size()) {
        waiting.wait(waitingReaders, std::memory_order_acquire);
        waitingReaders = waiting.load(std::memory_order_acquire);
    }
    start.store(true, std::memory_order_release);
    start.notify_all();
    const bool resolved = kue::mono::resolve();
    for (pthread_t reader : readers) {
        if (::pthread_join(reader, nullptr) != 0)
            return PublicationProbeResult::ThreadJoinFailed;
    }
    if (!resolved)
        return PublicationProbeResult::ResolutionFailed;
    return invalidSnapshots.load(std::memory_order_relaxed) == 0
               ? PublicationProbeResult::Passed
               : PublicationProbeResult::PartialPublication;
}

PublicationProbeResult runConcurrentPublicationProbe() {
    const pid_t child = ::fork();
    if (child < 0)
        return PublicationProbeResult::ForkFailed;
    if (child == 0)
        std::_Exit(static_cast<int>(probeConcurrentPublication()));
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
        return PublicationProbeResult::WaitFailed;
    if (!WIFEXITED(status))
        return PublicationProbeResult::AbnormalExit;
    return static_cast<PublicationProbeResult>(WEXITSTATUS(status));
}

}

int main() {
    TestRun run;
    const auto* array = reinterpret_cast<const kue::mono::MonoArray*>(&gMonoArray);
    kue::mono::ReferenceArrayView unavailableArray(array);
    run.expect(unavailableArray.status() == kue::mono::RuntimeArrayAccessStatus::RuntimeUnavailable,
               "an unpublished runtime prevents array access");
    run.expect(unavailableArray.size() == 0, "an unavailable array view exposes no length");
    kue::mono::RuntimeArrayObjectResult element = unavailableArray.object(0);
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::RuntimeUnavailable,
               "an unavailable array element reports its exact cause");
    run.expect(element.object == nullptr, "an unavailable array element exposes no object");
    constexpr kue::mono::ManagedClassLocation gameClassLocation{
        "Assembly-CSharp", "GameNetcodeStuff", "PlayerControllerB"};
    const kue::mono::ClassLookupResult unavailableClass = kue::mono::findClass(gameClassLocation);
    run.expect(unavailableClass.status == kue::mono::ClassLookupStatus::RuntimeUnavailable &&
                   unavailableClass.type == nullptr,
               "class lookup reports an unpublished runtime exactly");
    const ManagedStringProbeResult managedStringProbe = runManagedStringProbe();
    run.expect(managedStringProbe == ManagedStringProbeResult::Passed,
               "managed-string, class, and field access use bounded allocation-free runtime APIs");
    run.expect(runMissingMonoSymbolProbe(MissingMonoSymbol::ClassVtable) ==
                   MissingMonoSymbolProbeResult::Passed,
               "mono resolution requires the official class-vtable export");
    run.expect(runMissingMonoSymbolProbe(MissingMonoSymbol::FieldGetValue) ==
                   MissingMonoSymbolProbeResult::Passed,
               "mono resolution requires the official instance-field export");
    run.expect(runMissingMonoSymbolProbe(MissingMonoSymbol::FieldStaticGetValue) ==
                   MissingMonoSymbolProbeResult::Passed,
               "mono resolution requires the official static-field export");
    run.expect(runConcurrentPublicationProbe() == PublicationProbeResult::Passed,
               "concurrent readers never observe partially published Mono operations");

    gMonoFixtureEnabled.store(true, std::memory_order_release);
    run.expect(kue::mono::resolve(), "the complete Mono export set resolves");
    run.expect(kue::mono::ready(), "successful Mono publication becomes visible");
    run.expect(kue::mono::findField(nullptr, "health").status ==
                   kue::mono::MetadataMemberLookupStatus::InvalidInput,
               "a null declaring class never reaches Mono field lookup");
    run.expect(kue::mono::findMethod(reinterpret_cast<kue::mono::MonoClass*>(&gMonoClassStorage),
                                     "Update", -1)
                       .status == kue::mono::MetadataMemberLookupStatus::InvalidInput,
               "a negative parameter count never reaches Mono method lookup");

    kue::mono::ReferenceArrayView nullArray(nullptr);
    run.expect(nullArray.status() == kue::mono::RuntimeArrayAccessStatus::NullArray,
               "a null array is rejected before calling Mono");
    run.expect(gMonoArrayLengthCalls.load(std::memory_order_relaxed) == 0,
               "null-array validation performs no runtime call");

    std::byte firstObject{};
    gMonoArray.elements[0] = reinterpret_cast<kue::mono::MonoObject*>(&firstObject);
    gAllocationCount.store(0, std::memory_order_relaxed);
    gAllocatedBytes.store(0, std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_release);
    kue::mono::ReferenceArrayView view(array);
    run.expect(view.status() == kue::mono::RuntimeArrayAccessStatus::Success,
               "a valid Mono reference array is available");
    run.expect(view.size() == kFixtureArrayLength, "the Mono array length is preserved exactly");
    run.expect(gMonoArrayLengthCalls.load(std::memory_order_relaxed) == 1,
               "one array view captures its length exactly once");

    element = view.object(0);
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::Success,
               "a valid reference-array element is read");
    run.expect(element.object == gMonoArray.elements[0],
               "the first managed object reference is preserved");
    element = view.object(1);
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::Success,
               "a valid null reference remains a successful read");
    run.expect(element.object == nullptr, "a managed null reference remains null");

    bool allElementsRead = true;
    for (std::size_t index = 0; index < view.size(); ++index) {
        const kue::mono::RuntimeArrayObjectResult current = view.object(index);
        allElementsRead =
            current.status == kue::mono::RuntimeArrayAccessStatus::Success && allElementsRead;
    }
    run.expect(allElementsRead, "all 64 validated player slots remain readable");
    run.expect(gMonoArrayLengthCalls.load(std::memory_order_relaxed) == 1,
               "a complete traversal never repeats the captured length call");

    const std::size_t addressCalls = gMonoArrayAddressCalls.load(std::memory_order_relaxed);
    element = view.object(kFixtureArrayLength);
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::IndexOutOfRange,
               "an index equal to the array length is rejected");
    run.expect(gMonoArrayAddressCalls.load(std::memory_order_relaxed) == addressCalls,
               "an out-of-range index never reaches Mono address access");
    run.expect(gMonoArrayLengthCalls.load(std::memory_order_relaxed) == 1,
               "a rejected element never repeats the captured length call");

    gMonoArrayAddressAvailable.store(false, std::memory_order_relaxed);
    element = view.object(0);
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::AddressUnavailable,
               "a missing managed slot address is explicit");
    run.expect(element.object == nullptr, "a missing managed slot exposes no object");
    gMonoArrayAddressAvailable.store(true, std::memory_order_relaxed);

    gMonoArrayLength.store(std::numeric_limits<std::size_t>::max(), std::memory_order_relaxed);
    kue::mono::ReferenceArrayView maximumArray(array);
    element = maximumArray.object(std::numeric_limits<std::size_t>::max());
    run.expect(element.status == kue::mono::RuntimeArrayAccessStatus::IndexOutOfRange,
               "the maximum index is rejected without arithmetic overflow");
    gMeasureAllocations.store(false, std::memory_order_release);
    run.expect(gAllocationCount.load(std::memory_order_relaxed) == 0 &&
                   gAllocatedBytes.load(std::memory_order_relaxed) == 0,
               "array validation and traversal allocate no heap memory");

    return run.result();
}
