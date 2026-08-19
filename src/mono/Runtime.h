#ifndef KUE_MONO_RUNTIME_H
#define KUE_MONO_RUNTIME_H

#include "core/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kue::mono {

struct MonoArray;
struct MonoClass;
struct MonoClassField;
struct MonoException;
struct MonoMethod;
struct MonoObject;

struct ManagedClassLocation final {
    const char* imageName;
    const char* namespaceName;
    const char* className;
};

struct ManagedMethodLocation final {
    const char* assemblyPath;
    const char* namespaceName;
    const char* className;
    const char* methodName;
    int parameterCount;
};

inline constexpr std::size_t kManagedImageNameCapacity = 22;
inline constexpr std::size_t kManagedNamespaceNameCapacity = 16;
inline constexpr std::size_t kManagedClassNameCapacity = 18;

enum class ClassLookupStatus : std::uint8_t {
    Resolved,
    InvalidLocation,
    RuntimeUnavailable,
    DomainUnavailable,
    ImageUnavailable,
    ClassUnavailable
};

struct ClassLookupResult final {
    MonoClass* type = nullptr;
    ClassLookupStatus status = ClassLookupStatus::RuntimeUnavailable;
};

enum class MetadataMemberLookupStatus : std::uint8_t {
    Resolved,
    InvalidInput,
    MissingMember,
    RuntimeUnavailable
};

struct FieldLookupResult final {
    MonoClassField* field = nullptr;
    MetadataMemberLookupStatus status = MetadataMemberLookupStatus::RuntimeUnavailable;
};

struct MethodLookupResult final {
    MonoMethod* method = nullptr;
    MetadataMemberLookupStatus status = MetadataMemberLookupStatus::RuntimeUnavailable;
};

enum class RuntimeArrayAccessStatus : std::uint8_t {
    Success,
    NullArray,
    RuntimeUnavailable,
    IndexOutOfRange,
    AddressUnavailable
};

struct RuntimeArrayObjectResult final {
    RuntimeArrayAccessStatus status = RuntimeArrayAccessStatus::RuntimeUnavailable;
    MonoObject* object = nullptr;
};

class ReferenceArrayView final {
  public:
    explicit ReferenceArrayView(const MonoArray* array) noexcept;

    [[nodiscard]] RuntimeArrayAccessStatus status() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] RuntimeArrayObjectResult object(std::size_t index) const noexcept;

  private:
    const MonoArray* mArray = nullptr;
    std::size_t mLength = 0;
    RuntimeArrayAccessStatus mStatus = RuntimeArrayAccessStatus::RuntimeUnavailable;
};

inline constexpr int kManagedStringMaxCodeUnits = 512;
inline constexpr std::size_t kManagedStringMaxUtf8Bytes =
    static_cast<std::size_t>(kManagedStringMaxCodeUnits) * 4;

enum class ManagedStringUtf8Status : std::uint8_t {
    Success,
    NullString,
    RuntimeUnavailable,
    ThreadAttachmentFailed,
    InvalidCodeUnitCount,
    CharactersUnavailable,
    EmbeddedNull,
    InvalidSurrogate,
    OutputCapacityExceeded
};

struct ManagedStringUtf8Result final {
    ManagedStringUtf8Status status = ManagedStringUtf8Status::RuntimeUnavailable;
    std::string_view text;
    int codeUnitCount = 0;
    std::size_t validatedCodeUnits = 0;
    std::size_t utf8Bytes = 0;
};

enum class StaticInvocationStatus : std::uint8_t {
    Succeeded,
    InvalidMethod,
    RuntimeUnavailable,
    ExceptionRaised
};

struct StaticInvocationResult final {
    StaticInvocationStatus status = StaticInvocationStatus::RuntimeUnavailable;
    MonoObject* result = nullptr;
    MonoException* exception = nullptr;
};

[[nodiscard]] bool resolve();
[[nodiscard]] bool ready() noexcept;
void detachCurrentThread();

using MainThreadCallback = void (*)(void* context);
[[nodiscard]] bool installManagedMethodCallback(MonoMethod* method, MainThreadCallback callback,
                                                void* context);
[[nodiscard]] bool installCompiledMethodCallback(MonoMethod* method, MainThreadCallback callback,
                                                 void* context);
void clearManagedMethodCallback();

[[nodiscard]] MonoMethod* loadManagedMethod(ManagedMethodLocation location);
[[nodiscard]] StaticInvocationResult invokeStatic(MonoMethod* method) noexcept;
[[nodiscard]] bool addInternalCall(const char* managedMethod, const void* nativeFunction);

[[nodiscard]] ClassLookupResult findClass(ManagedClassLocation location) noexcept;
[[nodiscard]] FieldLookupResult findField(const MonoClass* type, const char* name) noexcept;
[[nodiscard]] MethodLookupResult findMethod(const MonoClass* type, const char* name,
                                            int parameterCount) noexcept;
[[nodiscard]] bool readStaticObject(MonoClass* type, const MonoClassField* field,
                                    MonoObject*& output);
[[nodiscard]] bool readInstanceObject(MonoObject* object, const MonoClassField* field,
                                      MonoObject*& output);
[[nodiscard]] bool readInstanceFloat(MonoObject* object, const MonoClassField* field,
                                     float& output);
[[nodiscard]] bool readInstanceInt(MonoObject* object, const MonoClassField* field, int& output);
[[nodiscard]] bool readInstanceBool(MonoObject* object, const MonoClassField* field, bool& output);
[[nodiscard]] bool readInstanceU64(MonoObject* object, const MonoClassField* field,
                                   std::uint64_t& output);
[[nodiscard]] ManagedStringUtf8Result readManagedStringUtf8(MonoObject* stringObject,
                                                            Utf8Output output) noexcept;
[[nodiscard]] const char* managedStringUtf8StatusName(ManagedStringUtf8Status status) noexcept;
[[nodiscard]] const char* runtimeArrayAccessStatusName(RuntimeArrayAccessStatus status) noexcept;

}

#endif
