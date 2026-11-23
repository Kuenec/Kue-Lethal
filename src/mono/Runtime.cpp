#include "mono/Runtime.h"

#include "core/Log.h"
#include "mono/PeExport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

namespace kue::mono {

struct MonoVTable;
struct MonoDomain;
struct MonoImage;

namespace {

std::atomic<bool> gResolved{false};
std::mutex gMutex;

enum class RuntimeResolveFailure : std::uint8_t {
    None,
    MonoModuleUnavailable,
    MonoMissingSymbols,
    MonoRootDomainUnavailable,
    MonoThreadAttachFailed,
    RuntimeUnavailable
};

struct RuntimeResolveError {
    RuntimeResolveFailure failure = RuntimeResolveFailure::None;
    std::uint32_t missingSymbols = 0;
};

struct RuntimeResolveLogState {
    std::uint32_t failures = 0;
    std::uint32_t monoMissingSymbols = 0;
};

RuntimeResolveLogState gResolveLogState;

bool runtimeResolved() noexcept {
    return gResolved.load(std::memory_order_acquire);
}

#define MSABI __attribute__((ms_abi))

using MonoThreadDetachFn = void(MSABI*)(void* thread);

enum class EmptyTextPolicy : std::uint8_t { Allowed, Rejected };

struct ClassLocationTextRequirement {
    const char* text;
    std::size_t capacity;
    EmptyTextPolicy emptyText;
};

thread_local void* gCurrentMonoThread = nullptr;
thread_local MonoThreadDetachFn gCurrentMonoThreadDetach = nullptr;

struct MonoRaw {
    void*(MSABI* get_root_domain)() = nullptr;
    void*(MSABI* domain_get)() = nullptr;
    void*(MSABI* image_loaded)(const char* name) = nullptr;
    void*(MSABI* domain_assembly_open)(void* domain, const char* name) = nullptr;
    void(MSABI* add_internal_call)(const char* name, const void* method) = nullptr;
    void*(MSABI* assembly_get_image)(void* assembly) = nullptr;
    void*(MSABI* class_from_name)(void* image, const char* ns, const char* name) = nullptr;
    void*(MSABI* class_get_field_from_name)(void* klass, const char* name) = nullptr;
    void*(MSABI* class_get_method_from_name)(void* klass, const char* name,
                                             int paramCount) = nullptr;
    MonoVTable*(MSABI* class_vtable)(MonoDomain* domain, MonoClass* klass) = nullptr;
    void(MSABI* field_get_value)(MonoObject* object, MonoClassField* field, void* value) = nullptr;
    void(MSABI* field_static_get_value)(MonoVTable* vtable, MonoClassField* field,
                                        void* value) = nullptr;
    void*(MSABI* runtime_invoke)(void* method, void* obj, void** params, void** exc) = nullptr;
    std::uintptr_t(MSABI* array_length)(void* array) = nullptr;
    char*(MSABI* array_addr_with_size)(void* array, int elementBytes,
                                       std::uintptr_t index) = nullptr;
    void*(MSABI* thread_attach)(void* domain) = nullptr;
    MonoThreadDetachFn thread_detach = nullptr;
    void*(MSABI* thread_internal_current)() = nullptr;
    std::uint16_t*(MSABI* string_chars)(void* stringObject) = nullptr;
    int(MSABI* string_length)(void* stringObject) = nullptr;
};

enum class RequiredMonoSymbol : std::uint8_t {
    GetRootDomain,
    DomainGet,
    ImageLoaded,
    DomainAssemblyOpen,
    AddInternalCall,
    AssemblyGetImage,
    ClassFromName,
    ClassGetFieldFromName,
    ClassGetMethodFromName,
    ClassVtable,
    FieldGetValue,
    FieldStaticGetValue,
    RuntimeInvoke,
    ArrayLength,
    ArrayAddress,
    ThreadAttach,
    ThreadDetach,
    ThreadInternalCurrent,
    StringChars,
    StringLength,
    Count
};

constexpr std::size_t kRequiredMonoSymbolCount =
    static_cast<std::size_t>(RequiredMonoSymbol::Count);
constexpr auto kRequiredMonoSymbolNames =
    std::to_array<const char*>({"mono_get_root_domain",
                                "mono_domain_get",
                                "mono_image_loaded",
                                "mono_domain_assembly_open",
                                "mono_add_internal_call",
                                "mono_assembly_get_image",
                                "mono_class_from_name",
                                "mono_class_get_field_from_name",
                                "mono_class_get_method_from_name",
                                "mono_class_vtable",
                                "mono_field_get_value",
                                "mono_field_static_get_value",
                                "mono_runtime_invoke",
                                "mono_array_length",
                                "mono_array_addr_with_size",
                                "mono_thread_attach",
                                "mono_thread_detach",
                                "mono_thread_internal_current",
                                "mono_string_chars",
                                "mono_string_length"});
static_assert(kRequiredMonoSymbolNames.size() == kRequiredMonoSymbolCount);

constexpr std::uint32_t requiredMonoSymbolBit(RequiredMonoSymbol symbol) noexcept {
    return std::uint32_t{1} << static_cast<std::uint8_t>(symbol);
}

MonoRaw gMono{};
void* gMonoDomain = nullptr;

bool attachCurrentMonoThread(const MonoRaw& mono, void* domain) {
    if (gCurrentMonoThread)
        return true;
    if (void* current = mono.thread_internal_current()) {
        gCurrentMonoThread = current;
        gCurrentMonoThreadDetach = nullptr;
        return true;
    }
    void* attached = mono.thread_attach(domain);
    if (!attached)
        return false;
    gCurrentMonoThread = attached;
    gCurrentMonoThreadDetach = mono.thread_detach;
    return true;
}

template <typename T>
bool bindMono(void* base, T& destination, const char* name, RequiredMonoSymbol symbol,
              std::uint32_t& missingSymbols) {
    void* address = pe::getExport(base, name);
    if (!address) {
        missingSymbols |= requiredMonoSymbolBit(symbol);
        return false;
    }
    destination = reinterpret_cast<T>(address);
    return true;
}

bool resolveMono(RuntimeResolveError& error) {
    void* base = pe::moduleBase(pe::kMonoModuleName);
    if (!base) {
        error.failure = RuntimeResolveFailure::MonoModuleUnavailable;
        return false;
    }

    std::uint32_t missingSymbols = 0;
    MonoRaw m{};

#define BIND_M(member, symbol)                                                                     \
    bindMono(base, m.member, "mono_" #member, RequiredMonoSymbol::symbol, missingSymbols)

    BIND_M(get_root_domain, GetRootDomain);
    BIND_M(domain_get, DomainGet);
    BIND_M(image_loaded, ImageLoaded);
    BIND_M(domain_assembly_open, DomainAssemblyOpen);
    BIND_M(add_internal_call, AddInternalCall);
    BIND_M(assembly_get_image, AssemblyGetImage);
    BIND_M(class_from_name, ClassFromName);
    BIND_M(class_get_field_from_name, ClassGetFieldFromName);
    BIND_M(class_get_method_from_name, ClassGetMethodFromName);
    BIND_M(class_vtable, ClassVtable);
    BIND_M(field_get_value, FieldGetValue);
    BIND_M(field_static_get_value, FieldStaticGetValue);
    BIND_M(runtime_invoke, RuntimeInvoke);
    BIND_M(array_length, ArrayLength);
    BIND_M(array_addr_with_size, ArrayAddress);
    BIND_M(thread_attach, ThreadAttach);
    BIND_M(thread_detach, ThreadDetach);
    BIND_M(thread_internal_current, ThreadInternalCurrent);
    BIND_M(string_chars, StringChars);
    BIND_M(string_length, StringLength);

#undef BIND_M

    if (missingSymbols != 0) {
        error = {RuntimeResolveFailure::MonoMissingSymbols, missingSymbols};
        return false;
    }

    void* rootDomain = m.get_root_domain();
    if (!rootDomain) {
        error.failure = RuntimeResolveFailure::MonoRootDomainUnavailable;
        return false;
    }
    if (!attachCurrentMonoThread(m, rootDomain)) {
        error.failure = RuntimeResolveFailure::MonoThreadAttachFailed;
        return false;
    }
    gMono = m;
    gMonoDomain = rootDomain;
    gResolved.store(true, std::memory_order_release);
    KUE_INFO("mono api resolved (pe exports @ %p, domain %p)", base, gMonoDomain);
    return true;
}

constexpr std::uint32_t runtimeResolveFailureBit(RuntimeResolveFailure failure) noexcept {
    return std::uint32_t{1} << static_cast<std::uint8_t>(failure);
}

void logMissingMonoSymbols(std::uint32_t missingSymbols) {
    const std::uint32_t newSymbols = missingSymbols & ~gResolveLogState.monoMissingSymbols;
    if (newSymbols == 0)
        return;
    gResolveLogState.monoMissingSymbols |= newSymbols;
    for (std::size_t index = 0; index < kRequiredMonoSymbolNames.size(); ++index) {
        const auto symbol = static_cast<RequiredMonoSymbol>(index);
        if ((newSymbols & requiredMonoSymbolBit(symbol)) != 0) {
            KUE_WARN("runtime resolve failed: mono api missing required export %s",
                     kRequiredMonoSymbolNames[index]);
        }
    }
}

void logResolveFailure(const RuntimeResolveError& error) {
    switch (error.failure) {
    case RuntimeResolveFailure::None:
        return;
    case RuntimeResolveFailure::MonoMissingSymbols:
        logMissingMonoSymbols(error.missingSymbols);
        return;
    case RuntimeResolveFailure::MonoModuleUnavailable:
    case RuntimeResolveFailure::MonoRootDomainUnavailable:
    case RuntimeResolveFailure::MonoThreadAttachFailed:
    case RuntimeResolveFailure::RuntimeUnavailable:
        break;
    }

    const std::uint32_t failureBit = runtimeResolveFailureBit(error.failure);
    if ((gResolveLogState.failures & failureBit) != 0)
        return;
    gResolveLogState.failures |= failureBit;

    switch (error.failure) {
    case RuntimeResolveFailure::MonoModuleUnavailable:
        KUE_WARN("runtime resolve failed: mono-2.0-bdwgc.dll not mapped");
        return;
    case RuntimeResolveFailure::MonoRootDomainUnavailable:
        KUE_WARN("runtime resolve failed: mono root domain not initialized yet");
        return;
    case RuntimeResolveFailure::MonoThreadAttachFailed:
        KUE_WARN("runtime resolve failed: could not attach native thread to mono root domain");
        return;
    case RuntimeResolveFailure::RuntimeUnavailable:
        KUE_WARN("runtime resolve failed: mono runtime unavailable");
        return;
    case RuntimeResolveFailure::None:
    case RuntimeResolveFailure::MonoMissingSymbols:
        return;
    }
}

}

bool resolve(void) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (runtimeResolved())
        return true;

    RuntimeResolveError error;

    if (resolveMono(error))
        return true;

    if (error.failure == RuntimeResolveFailure::None)
        error.failure = RuntimeResolveFailure::RuntimeUnavailable;
    logResolveFailure(error);
    return false;
}

bool ready(void) noexcept {
    return runtimeResolved();
}

void detachCurrentThread() {
    void* thread = gCurrentMonoThread;
    MonoThreadDetachFn detach = gCurrentMonoThreadDetach;
    gCurrentMonoThread = nullptr;
    gCurrentMonoThreadDetach = nullptr;
    if (thread && detach)
        detach(thread);
}

MonoMethod* loadManagedMethod(ManagedMethodLocation location) {
    const char* assemblyPath = location.assemblyPath;
    const char* namespaceName = location.namespaceName;
    const char* className = location.className;
    const char* methodName = location.methodName;
    const int parameterCount = location.parameterCount;
    if (!runtimeResolved() || !assemblyPath || !namespaceName || !className || !methodName ||
        parameterCount < 0) {
        return nullptr;
    }
    if (!attachCurrentMonoThread(gMono, gMonoDomain)) {
        KUE_ERR("mono: could not attach assembly loader thread");
        return nullptr;
    }
    void* domain = gMonoDomain;
    if (!domain && gMono.get_root_domain)
        domain = gMono.get_root_domain();
    if (!domain)
        return nullptr;

    std::string monoPath(assemblyPath);
    if (!monoPath.empty() && monoPath.front() == '/') {
        monoPath.insert(0, "Z:");
        std::replace(monoPath.begin(), monoPath.end(), '/', '\\');
    }
    void* assembly = gMono.domain_assembly_open(domain, monoPath.c_str());
    if (!assembly) {
        KUE_ERR("mono: could not load internal HUD assembly: %s", assemblyPath);
        return nullptr;
    }
    void* image = gMono.assembly_get_image(assembly);
    void* klass = image ? gMono.class_from_name(image, namespaceName, className) : nullptr;
    void* method =
        klass ? gMono.class_get_method_from_name(klass, methodName, parameterCount) : nullptr;
    if (!method) {
        KUE_ERR("mono: internal HUD entry method not found (%s.%s::%s)", namespaceName, className,
                methodName);
        return nullptr;
    }
    KUE_INFO("mono: internal HUD assembly loaded: %s", assemblyPath);
    return reinterpret_cast<MonoMethod*>(method);
}

StaticInvocationResult invokeStatic(MonoMethod* method) noexcept {
    if (!method)
        return {StaticInvocationStatus::InvalidMethod, nullptr, nullptr};
    if (!runtimeResolved() || !attachCurrentMonoThread(gMono, gMonoDomain))
        return {StaticInvocationStatus::RuntimeUnavailable, nullptr, nullptr};
    void* exception = nullptr;
    void* result = gMono.runtime_invoke(method, nullptr, nullptr, &exception);
    if (exception) {
        return {StaticInvocationStatus::ExceptionRaised, reinterpret_cast<MonoObject*>(result),
                reinterpret_cast<MonoException*>(exception)};
    }
    return {StaticInvocationStatus::Succeeded, reinterpret_cast<MonoObject*>(result), nullptr};
}

bool addInternalCall(const char* managedMethod, const void* nativeFunction) {
    if (!runtimeResolved() || !managedMethod || !nativeFunction) {
        return false;
    }
    if (!attachCurrentMonoThread(gMono, gMonoDomain)) {
        KUE_ERR("mono: could not attach internal-call registration thread");
        return false;
    }
    gMono.add_internal_call(managedMethod, nativeFunction);
    return true;
}

namespace {

bool validClassLocationText(ClassLocationTextRequirement requirement) noexcept {
    if (!requirement.text)
        return false;
    const std::size_t size = strnlen(requirement.text, requirement.capacity + 1);
    return size <= requirement.capacity &&
           (requirement.emptyText == EmptyTextPolicy::Allowed || size != 0) &&
           isValidUtf8({requirement.text, size});
}

MonoImage* findMonoImageByName(const char* name) noexcept {
    if (!name || !gMono.image_loaded)
        return nullptr;
    void* image = gMono.image_loaded(name);
    if (!image) {
        constexpr std::string_view suffix = ".dll";
        std::array<char, kManagedImageNameCapacity + suffix.size() + 1> withDll{};
        const std::size_t nameLength = strnlen(name, kManagedImageNameCapacity + 1);
        if (nameLength > kManagedImageNameCapacity)
            return nullptr;
        std::memcpy(withDll.data(), name, nameLength);
        std::memcpy(withDll.data() + nameLength, suffix.data(), suffix.size());
        image = gMono.image_loaded(withDll.data());
    }
    return reinterpret_cast<MonoImage*>(image);
}

MonoDomain* currentDomain() noexcept {
    if (!runtimeResolved())
        return nullptr;
    void* rootDomain = gMono.get_root_domain();
    if (!rootDomain || !attachCurrentMonoThread(gMono, rootDomain))
        return nullptr;
    void* domain = gMono.domain_get();
    return reinterpret_cast<MonoDomain*>(domain ? domain : rootDomain);
}

}

ClassLookupResult findClass(ManagedClassLocation location) noexcept {
    if (!validClassLocationText({.text = location.imageName,
                                 .capacity = kManagedImageNameCapacity,
                                 .emptyText = EmptyTextPolicy::Allowed}) ||
        !validClassLocationText({.text = location.namespaceName,
                                 .capacity = kManagedNamespaceNameCapacity,
                                 .emptyText = EmptyTextPolicy::Allowed}) ||
        !validClassLocationText({.text = location.className,
                                 .capacity = kManagedClassNameCapacity,
                                 .emptyText = EmptyTextPolicy::Rejected})) {
        return {nullptr, ClassLookupStatus::InvalidLocation};
    }
    if (!runtimeResolved())
        return {nullptr, ClassLookupStatus::RuntimeUnavailable};
    if (!currentDomain())
        return {nullptr, ClassLookupStatus::DomainUnavailable};
    const char* namespaceName = location.namespaceName;

    if (location.imageName[0] != '\0') {
        MonoImage* const image = findMonoImageByName(location.imageName);
        if (!image)
            return {nullptr, ClassLookupStatus::ImageUnavailable};
        MonoClass* const type = reinterpret_cast<MonoClass*>(
            gMono.class_from_name(image, namespaceName, location.className));
        return type ? ClassLookupResult{type, ClassLookupStatus::Resolved}
                    : ClassLookupResult{nullptr, ClassLookupStatus::ClassUnavailable};
    }

    constexpr std::array<const char*, 3> baseClassLibraries = {"mscorlib", "netstandard",
                                                               "System.Private.CoreLib"};
    bool imageFound = false;
    for (const char* imageName : baseClassLibraries) {
        MonoImage* const image = findMonoImageByName(imageName);
        if (!image)
            continue;
        imageFound = true;
        MonoClass* const type = reinterpret_cast<MonoClass*>(
            gMono.class_from_name(image, namespaceName, location.className));
        if (type)
            return {type, ClassLookupStatus::Resolved};
    }
    return {nullptr,
            imageFound ? ClassLookupStatus::ClassUnavailable : ClassLookupStatus::ImageUnavailable};
}

FieldLookupResult findField(const MonoClass* type, const char* name) noexcept {
    if (!type || !name)
        return {nullptr, MetadataMemberLookupStatus::InvalidInput};
    if (!runtimeResolved())
        return {nullptr, MetadataMemberLookupStatus::RuntimeUnavailable};
    MonoClassField* const field = reinterpret_cast<MonoClassField*>(
        gMono.class_get_field_from_name(const_cast<MonoClass*>(type), name));
    return field ? FieldLookupResult{field, MetadataMemberLookupStatus::Resolved}
                 : FieldLookupResult{nullptr, MetadataMemberLookupStatus::MissingMember};
}

bool readStaticObject(MonoClass* type, const MonoClassField* field, MonoObject*& out) {
    out = nullptr;
    if (!runtimeResolved() || !type || !field)
        return false;
    MonoDomain* const domain = currentDomain();
    if (!domain)
        return false;
    MonoVTable* const vtable = gMono.class_vtable(domain, type);
    if (!vtable)
        return false;
    MonoObject* value = nullptr;
    gMono.field_static_get_value(vtable, const_cast<MonoClassField*>(field),
                                 static_cast<void*>(&value));
    out = value;
    return true;
}

namespace {

template <typename Value>
bool readInstanceValue(MonoObject* object, const MonoClassField* field, Value& output) {
    if (!runtimeResolved() || !object || !field)
        return false;
    Value value{};
    if (!attachCurrentMonoThread(gMono, gMonoDomain))
        return false;
    gMono.field_get_value(object, const_cast<MonoClassField*>(field), static_cast<void*>(&value));
    output = value;
    return true;
}

}

bool readInstanceObject(MonoObject* object, const MonoClassField* field, MonoObject*& out) {
    out = nullptr;
    return readInstanceValue(object, field, out);
}

bool readInstanceFloat(MonoObject* object, const MonoClassField* field, float& out) {
    return readInstanceValue(object, field, out);
}

bool readInstanceInt(MonoObject* object, const MonoClassField* field, int& out) {
    return readInstanceValue(object, field, out);
}

bool readInstanceBool(MonoObject* object, const MonoClassField* field, bool& out) {
    std::uint8_t value = 0;
    if (!readInstanceValue(object, field, value))
        return false;
    out = value != 0;
    return true;
}

bool readInstanceU64(MonoObject* object, const MonoClassField* field, std::uint64_t& out) {
    return readInstanceValue(object, field, out);
}

ManagedStringUtf8Result readManagedStringUtf8(MonoObject* stringObject,
                                              Utf8Output output) noexcept {
    if (!stringObject)
        return {ManagedStringUtf8Status::NullString, {}, 0, 0, 0};
    if (!runtimeResolved())
        return {ManagedStringUtf8Status::RuntimeUnavailable, {}, 0, 0, 0};
    if (!attachCurrentMonoThread(gMono, gMonoDomain))
        return {ManagedStringUtf8Status::ThreadAttachmentFailed, {}, 0, 0, 0};

    const int codeUnits = gMono.string_length(stringObject);
    if (codeUnits < 0 || codeUnits > kManagedStringMaxCodeUnits) {
        return {ManagedStringUtf8Status::InvalidCodeUnitCount, {}, codeUnits, 0, 0};
    }
    if (codeUnits == 0)
        return {ManagedStringUtf8Status::Success, {}, 0, 0, 0};

    const std::uint16_t* characters = gMono.string_chars(stringObject);
    if (!characters)
        return {ManagedStringUtf8Status::CharactersUnavailable, {}, codeUnits, 0, 0};
    const Utf16ToUtf8Result conversion =
        convertUtf16ToUtf8({characters, static_cast<std::size_t>(codeUnits)}, output);
    switch (conversion.status) {
    case Utf16ToUtf8Status::Success:
        return {ManagedStringUtf8Status::Success,
                {output.bytes, conversion.utf8Bytes},
                codeUnits,
                conversion.validatedCodeUnits,
                conversion.utf8Bytes};
    case Utf16ToUtf8Status::EmbeddedNull:
        return {ManagedStringUtf8Status::EmbeddedNull,
                {},
                codeUnits,
                conversion.validatedCodeUnits,
                conversion.utf8Bytes};
    case Utf16ToUtf8Status::InvalidSurrogate:
        return {ManagedStringUtf8Status::InvalidSurrogate,
                {},
                codeUnits,
                conversion.validatedCodeUnits,
                conversion.utf8Bytes};
    case Utf16ToUtf8Status::OutputCapacityExceeded:
        return {ManagedStringUtf8Status::OutputCapacityExceeded,
                {},
                codeUnits,
                conversion.validatedCodeUnits,
                conversion.utf8Bytes};
    case Utf16ToUtf8Status::NullInput:
        return {ManagedStringUtf8Status::CharactersUnavailable, {}, codeUnits, 0, 0};
    case Utf16ToUtf8Status::EmptyInput:
        return {ManagedStringUtf8Status::Success, {}, codeUnits, 0, 0};
    }
    return {ManagedStringUtf8Status::InvalidSurrogate, {}, codeUnits, 0, 0};
}

const char* managedStringUtf8StatusName(ManagedStringUtf8Status status) noexcept {
    switch (status) {
    case ManagedStringUtf8Status::Success:
        return "success";
    case ManagedStringUtf8Status::NullString:
        return "null-string";
    case ManagedStringUtf8Status::RuntimeUnavailable:
        return "runtime-unavailable";
    case ManagedStringUtf8Status::ThreadAttachmentFailed:
        return "thread-attachment-failed";
    case ManagedStringUtf8Status::InvalidCodeUnitCount:
        return "invalid-code-unit-count";
    case ManagedStringUtf8Status::CharactersUnavailable:
        return "characters-unavailable";
    case ManagedStringUtf8Status::EmbeddedNull:
        return "embedded-null";
    case ManagedStringUtf8Status::InvalidSurrogate:
        return "invalid-surrogate";
    case ManagedStringUtf8Status::OutputCapacityExceeded:
        return "output-capacity-exceeded";
    }
    return "invalid";
}

ReferenceArrayView::ReferenceArrayView(const MonoArray* array) noexcept : mArray(array) {
    if (!array) {
        mStatus = RuntimeArrayAccessStatus::NullArray;
        return;
    }
    if (!runtimeResolved() || !attachCurrentMonoThread(gMono, gMonoDomain))
        return;
    mLength = static_cast<std::size_t>(gMono.array_length(const_cast<MonoArray*>(array)));
    mStatus = RuntimeArrayAccessStatus::Success;
}

RuntimeArrayAccessStatus ReferenceArrayView::status() const noexcept {
    return mStatus;
}

std::size_t ReferenceArrayView::size() const noexcept {
    return mLength;
}

RuntimeArrayObjectResult ReferenceArrayView::object(std::size_t index) const noexcept {
    if (mStatus != RuntimeArrayAccessStatus::Success)
        return {mStatus, nullptr};
    if (index >= mLength)
        return {RuntimeArrayAccessStatus::IndexOutOfRange, nullptr};
    if (!runtimeResolved() || !attachCurrentMonoThread(gMono, gMonoDomain))
        return {RuntimeArrayAccessStatus::RuntimeUnavailable, nullptr};
    static_assert(sizeof(MonoObject*) <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    void* const slot = gMono.array_addr_with_size(const_cast<MonoArray*>(mArray),
                                                  static_cast<int>(sizeof(MonoObject*)), index);
    if (!slot)
        return {RuntimeArrayAccessStatus::AddressUnavailable, nullptr};
    MonoObject* object = nullptr;
    std::memcpy(static_cast<void*>(&object), slot, sizeof(MonoObject*));
    return {RuntimeArrayAccessStatus::Success, object};
}

const char* runtimeArrayAccessStatusName(RuntimeArrayAccessStatus status) noexcept {
    switch (status) {
    case RuntimeArrayAccessStatus::Success:
        return "success";
    case RuntimeArrayAccessStatus::NullArray:
        return "null-array";
    case RuntimeArrayAccessStatus::RuntimeUnavailable:
        return "runtime-unavailable";
    case RuntimeArrayAccessStatus::IndexOutOfRange:
        return "index-out-of-range";
    case RuntimeArrayAccessStatus::AddressUnavailable:
        return "address-unavailable";
    }
    return "invalid";
}

}
