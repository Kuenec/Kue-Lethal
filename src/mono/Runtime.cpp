#include "mono/Runtime.h"

#include "core/Log.h"
#include "mono/PeExport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <capstone/capstone.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

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

struct MonoProfiler;
struct MonoProfilerCallContext;
struct MonoProfilerDesc;

using MonoProfilerHandle = MonoProfilerDesc*;

enum class MonoProfilerCallInstrumentationFlags : int {
    None = 0,
    Enter = 1U << 1,
};

using ProfilerCreateFn = MonoProfilerHandle(MSABI*)(MonoProfiler* profiler);
using ProfilerEnterCallback = void(MSABI*)(MonoProfiler* profiler, MonoMethod* method,
                                           MonoProfilerCallContext* context);
using ProfilerFilterCallback = MonoProfilerCallInstrumentationFlags(MSABI*)(MonoProfiler* profiler,
                                                                            MonoMethod* method);
using ProfilerSetEnterFn = void(MSABI*)(MonoProfilerHandle handle, ProfilerEnterCallback callback);
using ProfilerSetFilterFn = void(MSABI*)(MonoProfilerHandle handle,
                                         ProfilerFilterCallback callback);
using MonoThreadDetachFn = void(MSABI*)(void* thread);

struct CallbackBinding {
    MonoMethod* method = nullptr;
    MainThreadCallback callback = nullptr;
    void* context = nullptr;
    void* compiledTrampoline = nullptr;
};

struct CallbackBindingRequest {
    MonoMethod* method;
    MainThreadCallback callback;
    void* context;
    void* compiledTrampoline;
};

struct MemoryProtectionRequest {
    void* address;
    std::size_t length;
    int protection;
};

enum class EmptyTextPolicy : std::uint8_t { Allowed, Rejected };

struct ClassLocationTextRequirement {
    const char* text;
    std::size_t capacity;
    EmptyTextPolicy emptyText;
};

constexpr std::size_t kCallbackBindingCapacity = 3;
constexpr std::int64_t kManagedCallbackIntervalNs = 500000000;

std::array<CallbackBinding, kCallbackBindingCapacity> gCallbackBindings{};
std::size_t gCallbackBindingCount = 0;
std::mutex gCallbackInstallMutex;
std::atomic<const CallbackBinding*> gManagedCallbackBinding{nullptr};
std::atomic<const CallbackBinding*> gCompiledMethodBinding{nullptr};
std::atomic<std::uint64_t> gManagedCallbackClearRevision{0};
std::atomic<bool> gManagedCallbackBusy{false};
std::atomic<std::int64_t> gManagedCallbackLastNs{0};
thread_local bool gDispatchingManagedCallback = false;
thread_local void* gCurrentMonoThread = nullptr;
thread_local MonoThreadDetachFn gCurrentMonoThreadDetach = nullptr;

const CallbackBinding* createCallbackBinding(CallbackBindingRequest request) {
    if (gCallbackBindingCount >= gCallbackBindings.size()) {
        KUE_ERR("mono: callback binding capacity exhausted");
        return nullptr;
    }
    CallbackBinding& binding = gCallbackBindings[gCallbackBindingCount];
    binding = {.method = request.method,
               .callback = request.callback,
               .context = request.context,
               .compiledTrampoline = request.compiledTrampoline};
    ++gCallbackBindingCount;
    return &binding;
}

void deactivateManagedCallback() {
    gManagedCallbackClearRevision.fetch_add(1, std::memory_order_acq_rel);
    gManagedCallbackBinding.exchange(nullptr, std::memory_order_acq_rel);
    if (gDispatchingManagedCallback)
        return;
    while (gManagedCallbackBusy.load(std::memory_order_acquire))
        std::this_thread::yield();
}

bool publishManagedCallback(const CallbackBinding* binding, std::uint64_t clearRevision) {
    const CallbackBinding* expected = nullptr;
    if (!gManagedCallbackBinding.compare_exchange_strong(
            expected, binding, std::memory_order_release, std::memory_order_acquire)) {
        return expected == binding;
    }
    if (gManagedCallbackClearRevision.load(std::memory_order_acquire) == clearRevision)
        return true;
    expected = binding;
    gManagedCallbackBinding.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
    return false;
}

bool setMemoryProtection(MemoryProtectionRequest request) {
    return syscall(SYS_mprotect, request.address, request.length, request.protection) == 0;
}

void dispatchManagedCallback(const CallbackBinding* expectedBinding) {
    const CallbackBinding* binding = gManagedCallbackBinding.load(std::memory_order_acquire);
    if (!binding || (expectedBinding && binding != expectedBinding))
        return;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    const auto last = gManagedCallbackLastNs.load(std::memory_order_relaxed);
    if (ns - last < kManagedCallbackIntervalNs)
        return;
    if (gManagedCallbackBusy.exchange(true, std::memory_order_acq_rel))
        return;

    struct DispatchCompletion {
        ~DispatchCompletion() {
            gDispatchingManagedCallback = false;
            gManagedCallbackBusy.store(false, std::memory_order_release);
        }
    } completion;

    binding = gManagedCallbackBinding.load(std::memory_order_acquire);
    if (!binding || (expectedBinding && binding != expectedBinding))
        return;
    gManagedCallbackLastNs.store(ns, std::memory_order_relaxed);
    gDispatchingManagedCallback = true;
    binding->callback(binding->context);
}

MonoProfilerCallInstrumentationFlags MSABI profilerFilter(MonoProfiler*, MonoMethod* method) {
    const CallbackBinding* binding = gManagedCallbackBinding.load(std::memory_order_acquire);
    return binding && method == binding->method ? MonoProfilerCallInstrumentationFlags::Enter
                                                : MonoProfilerCallInstrumentationFlags::None;
}

void MSABI profilerEnter(MonoProfiler*, MonoMethod* method, MonoProfilerCallContext*) {
    const CallbackBinding* binding = gManagedCallbackBinding.load(std::memory_order_acquire);
    if (!binding || method != binding->method)
        return;
    dispatchManagedCallback(binding);
}

using CompiledUpdateFn = void(MSABI*)(void* instance);

void MSABI compiledUpdateDetour(void* instance) {
    const CallbackBinding* binding = gCompiledMethodBinding.load(std::memory_order_acquire);
    auto original =
        binding ? reinterpret_cast<CompiledUpdateFn>(binding->compiledTrampoline) : nullptr;
    if (original)
        original(instance);
    if (binding)
        dispatchManagedCallback(nullptr);
}

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
    void*(MSABI* thread_current)() = nullptr;
    std::uint16_t*(MSABI* string_chars)(void* stringObject) = nullptr;
    int(MSABI* string_length)(void* stringObject) = nullptr;
    void*(MSABI* jit_info_table_find)(void* domain, void* address) = nullptr;
    void*(MSABI* jit_info_get_method)(void* jitInfo) = nullptr;
    void*(MSABI* jit_info_get_code_start)(void* jitInfo) = nullptr;
    int(MSABI* jit_info_get_code_size)(void* jitInfo) = nullptr;
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
    ThreadCurrent,
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
                                "mono_thread_current",
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
    if (void* current = mono.thread_current()) {
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
    void* base = pe::moduleBase("mono-2.0-bdwgc");
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
    BIND_M(thread_current, ThreadCurrent);
    BIND_M(string_chars, StringChars);
    BIND_M(string_length, StringLength);

#undef BIND_M

    m.jit_info_table_find = reinterpret_cast<decltype(m.jit_info_table_find)>(
        pe::getExport(base, "mono_jit_info_table_find"));
    m.jit_info_get_method = reinterpret_cast<decltype(m.jit_info_get_method)>(
        pe::getExport(base, "mono_jit_info_get_method"));
    m.jit_info_get_code_start = reinterpret_cast<decltype(m.jit_info_get_code_start)>(
        pe::getExport(base, "mono_jit_info_get_code_start"));
    m.jit_info_get_code_size = reinterpret_cast<decltype(m.jit_info_get_code_size)>(
        pe::getExport(base, "mono_jit_info_get_code_size"));
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

bool installManagedMethodCallback(MonoMethod* method, MainThreadCallback callback, void* context) {
    if (!runtimeResolved() || !method || !callback)
        return false;
    void* base = pe::moduleBase("mono-2.0-bdwgc");
    if (!base)
        return false;
    if (!attachCurrentMonoThread(gMono, gMonoDomain)) {
        KUE_ERR("mono: could not attach callback installer thread");
        return false;
    }

    std::lock_guard<std::mutex> installLock(gCallbackInstallMutex);
    const CallbackBinding* current = gManagedCallbackBinding.load(std::memory_order_acquire);
    if (current && current->method == method && current->callback == callback &&
        current->context == context) {
        return true;
    }
    if (current) {
        KUE_ERR("mono: a different managed-method callback is already active");
        return false;
    }
    const std::uint64_t clearRevision =
        gManagedCallbackClearRevision.load(std::memory_order_acquire);

    auto create = reinterpret_cast<ProfilerCreateFn>(pe::getExport(base, "mono_profiler_create"));
    auto setEnter = reinterpret_cast<ProfilerSetEnterFn>(
        pe::getExport(base, "mono_profiler_set_method_enter_callback"));
    auto setFilter = reinterpret_cast<ProfilerSetFilterFn>(
        pe::getExport(base, "mono_profiler_set_call_instrumentation_filter_callback"));
    if (!create || !setEnter || !setFilter)
        return false;

    MonoProfilerHandle handle = create(nullptr);
    if (!handle)
        return false;
    const CallbackBinding* binding = createCallbackBinding({.method = method,
                                                            .callback = callback,
                                                            .context = context,
                                                            .compiledTrampoline = nullptr});
    if (!binding)
        return false;
    setEnter(handle, &profilerEnter);
    setFilter(handle, &profilerFilter);
    if (!publishManagedCallback(binding, clearRevision)) {
        KUE_WARN("mono: managed-method callback was cleared during installation");
        return false;
    }
    KUE_INFO("mono: main-thread callback installed for method %p", static_cast<void*>(method));
    return true;
}

bool installCompiledMethodCallback(MonoMethod* method, MainThreadCallback callback, void* context) {
    if (!runtimeResolved() || !method || !callback)
        return false;
    if (!attachCurrentMonoThread(gMono, gMonoDomain)) {
        KUE_ERR("mono: could not attach compiled callback installer thread");
        return false;
    }

    std::lock_guard<std::mutex> installLock(gCallbackInstallMutex);
    if (gCompiledMethodBinding.load(std::memory_order_acquire))
        return true;
    const std::uint64_t clearRevision =
        gManagedCallbackClearRevision.load(std::memory_order_acquire);
    if (!gMono.jit_info_table_find || !gMono.jit_info_get_method ||
        !gMono.jit_info_get_code_start || !gMono.jit_info_get_code_size) {
        KUE_WARN("mono: late JIT lookup exports unavailable");
        return false;
    }

    void* domain =
        gMonoDomain ? gMonoDomain : (gMono.get_root_domain ? gMono.get_root_domain() : nullptr);
    if (!domain)
        return false;

    void* code = nullptr;
    int codeSize = 0;
    int originalCodeProtection = PROT_READ | PROT_EXEC;
    std::size_t executableMaps = 0;
    std::size_t probes = 0;
    std::size_t jitHits = 0;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (!code && std::getline(maps, line)) {
        std::istringstream row(line);
        std::string range, perms, offset, device, inode, path;
        if (!(row >> range >> perms >> offset >> device >> inode))
            continue;
        std::getline(row, path);
        const auto first = path.find_first_not_of(' ');
        path = first == std::string::npos ? "" : path.substr(first);
        if (perms.find('x') == std::string::npos)
            continue;
        if (!path.empty() && path.front() != '[' && path.find("memfd") == std::string::npos)
            continue;
        const auto dash = range.find('-');
        if (dash == std::string::npos)
            continue;
        std::uintptr_t begin = 0, end = 0;
        try {
            begin = std::stoull(range.substr(0, dash), nullptr, 16);
            end = std::stoull(range.substr(dash + 1), nullptr, 16);
        } catch (...) {
            continue;
        }
        ++executableMaps;

        for (std::uintptr_t address = begin; address < end; address += 64) {
            ++probes;
            void* ji = gMono.jit_info_table_find(domain, std::bit_cast<void*>(address));
            if (!ji)
                continue;
            ++jitHits;
            void* jiMethod = gMono.jit_info_get_method(ji);
            void* jiCode = gMono.jit_info_get_code_start(ji);
            const int jiSize = gMono.jit_info_get_code_size(ji);
            if (jiMethod == method && jiCode && jiSize > 16) {
                code = jiCode;
                codeSize = jiSize;
                originalCodeProtection = 0;
                if (!perms.empty() && perms[0] == 'r')
                    originalCodeProtection |= PROT_READ;
                if (perms.size() > 1 && perms[1] == 'w')
                    originalCodeProtection |= PROT_WRITE;
                if (perms.size() > 2 && perms[2] == 'x')
                    originalCodeProtection |= PROT_EXEC;
                break;
            }
            if (jiCode && jiSize > 0) {
                const auto next =
                    reinterpret_cast<std::uintptr_t>(jiCode) + static_cast<std::uintptr_t>(jiSize);
                if (next > address)
                    address = next - 1;
            }
        }
    }
    if (!code) {
        KUE_INFO("mono: target method not in JIT table (maps=%zu probes=%zu hits=%zu)",
                 executableMaps, probes, jitHits);
        return false;
    }

    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    const std::size_t pageSize = pageSizeLong > 0 ? static_cast<std::size_t>(pageSizeLong) : 4096;
    const auto target = reinterpret_cast<std::uintptr_t>(code);
    const auto pageMask = ~(static_cast<std::uintptr_t>(pageSize) - 1u);
    const auto targetPage = target & pageMask;
    void* executable = MAP_FAILED;
#ifdef MAP_FIXED_NOREPLACE

    constexpr std::uintptr_t kStep = 1u << 20;
    constexpr std::uintptr_t kLimit = 0x70000000u;
    for (std::uintptr_t distance = kStep; distance < kLimit && executable == MAP_FAILED;
         distance += kStep) {
        for (int direction : {-1, 1}) {
            std::uintptr_t hint = direction < 0 ? targetPage - distance : targetPage + distance;
            if (direction < 0 && distance > targetPage)
                continue;
            executable = mmap(std::bit_cast<void*>(hint), pageSize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (executable != MAP_FAILED)
                break;
        }
    }
#endif
    if (executable == MAP_FAILED) {
        KUE_WARN("mono: could not allocate a near trampoline for late injection");
        return false;
    }

    csh capstone = 0;
    cs_insn* instructions = nullptr;
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone) != CS_ERR_OK) {
        munmap(executable, pageSize);
        return false;
    }
    cs_option(capstone, CS_OPT_DETAIL, CS_OPT_ON);
    const std::size_t disassemblySize =
        std::min(static_cast<std::size_t>(codeSize), std::size_t{64});
    const std::size_t count = cs_disasm(capstone, reinterpret_cast<const std::uint8_t*>(code),
                                        disassemblySize, target, 0, &instructions);
    std::size_t copied = 0;
    for (std::size_t i = 0; i < count && copied < 8; ++i)
        copied += instructions[i].size;
    if (!instructions || copied < 8 || copied + 160 >= pageSize || (target & 7u) != 0) {
        if (instructions)
            cs_free(instructions, count);
        cs_close(&capstone);
        munmap(executable, pageSize);
        KUE_WARN("mono: compiled Update prologue cannot be patched atomically");
        return false;
    }

    auto* trampoline = static_cast<std::uint8_t*>(executable);
    std::size_t written = 0;
    bool relocatable = true;
    for (std::size_t i = 0; i < count && written < copied; ++i) {
        const cs_insn& ins = instructions[i];
        std::memcpy(trampoline + written, ins.bytes, ins.size);
        const cs_x86& x86 = ins.detail->x86;
        if (x86.encoding.disp_size) {
            bool ripRelative = false;
            for (std::uint8_t op = 0; op < x86.op_count; ++op)
                if (x86.operands[op].type == X86_OP_MEM && x86.operands[op].mem.base == X86_REG_RIP)
                    ripRelative = true;
            if (ripRelative && x86.encoding.disp_size == 4) {
                const std::int64_t absolute =
                    static_cast<std::int64_t>(ins.address + ins.size) + x86.disp;
                const std::int64_t replacement =
                    absolute - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
                                   trampoline + written + ins.size));
                if (replacement < std::numeric_limits<std::int32_t>::min() ||
                    replacement > std::numeric_limits<std::int32_t>::max())
                    relocatable = false;
                else {
                    const auto disp = static_cast<std::int32_t>(replacement);
                    std::memcpy(trampoline + written + x86.encoding.disp_offset, &disp, 4);
                }
            }
        }
        if (x86.encoding.imm_size && (cs_insn_group(capstone, &ins, CS_GRP_CALL) ||
                                      cs_insn_group(capstone, &ins, CS_GRP_JUMP))) {
            const cs_x86_op* immediate = nullptr;
            for (std::uint8_t op = 0; op < x86.op_count; ++op)
                if (x86.operands[op].type == X86_OP_IMM)
                    immediate = &x86.operands[op];
            if (immediate) {
                const std::int64_t replacement =
                    immediate->imm - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
                                         trampoline + written + ins.size));
                if (x86.encoding.imm_size == 4 &&
                    replacement >= std::numeric_limits<std::int32_t>::min() &&
                    replacement <= std::numeric_limits<std::int32_t>::max()) {
                    const auto rel = static_cast<std::int32_t>(replacement);
                    std::memcpy(trampoline + written + x86.encoding.imm_offset, &rel, 4);
                } else if (x86.encoding.imm_size == 1 && replacement >= -128 &&
                           replacement <= 127) {
                    const auto rel = static_cast<std::int8_t>(replacement);
                    std::memcpy(trampoline + written + x86.encoding.imm_offset, &rel, 1);
                } else {
                    relocatable = false;
                }
            }
        }
        written += ins.size;
    }
    cs_free(instructions, count);
    cs_close(&capstone);
    if (!relocatable) {
        munmap(executable, pageSize);
        KUE_WARN("mono: compiled Update prologue contains an unsupported relative instruction");
        return false;
    }

    auto writeAbsoluteJump = [](std::uint8_t* destination, const void* address) {
        destination[0] = 0xFF;
        destination[1] = 0x25;
        std::memset(destination + 2, 0, 4);
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        std::memcpy(destination + 6, &value, sizeof(value));
    };
    writeAbsoluteJump(trampoline + copied, std::bit_cast<void*>(target + copied));
    auto* relay = trampoline + copied + 32;
    writeAbsoluteJump(relay, reinterpret_cast<const void*>(&compiledUpdateDetour));
    const std::uintptr_t relayAddress = reinterpret_cast<std::uintptr_t>(relay);
    const std::uintptr_t jumpEnd = target + 5;
    std::int32_t relativeJump = 0;
    if (relayAddress >= jumpEnd) {
        const std::uintptr_t distance = relayAddress - jumpEnd;
        if (distance > static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max())) {
            munmap(executable, pageSize);
            return false;
        }
        relativeJump = static_cast<std::int32_t>(distance);
    } else {
        const std::uintptr_t distance = jumpEnd - relayAddress;
        constexpr std::uintptr_t kMinimumMagnitude =
            static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max()) + 1;
        if (distance > kMinimumMagnitude) {
            munmap(executable, pageSize);
            return false;
        }
        relativeJump = distance == kMinimumMagnitude ? std::numeric_limits<std::int32_t>::min()
                                                     : -static_cast<std::int32_t>(distance);
    }
    if (!setMemoryProtection(
            {.address = executable, .length = pageSize, .protection = PROT_READ | PROT_EXEC})) {
        munmap(executable, pageSize);
        KUE_WARN("mono: could not make the compiled trampoline executable");
        return false;
    }

    const auto protectStart = targetPage;
    const auto protectEnd = (target + 8 + pageSize - 1u) & pageMask;
    if (!setMemoryProtection({.address = std::bit_cast<void*>(protectStart),
                              .length = protectEnd - protectStart,
                              .protection = PROT_READ | PROT_WRITE | PROT_EXEC})) {
        munmap(executable, pageSize);
        KUE_WARN("mono: could not make compiled Update writable");
        return false;
    }

    const CallbackBinding* binding = createCallbackBinding({.method = method,
                                                            .callback = callback,
                                                            .context = context,
                                                            .compiledTrampoline = trampoline});
    if (!binding) {
        munmap(executable, pageSize);
        return false;
    }
    gCompiledMethodBinding.store(binding, std::memory_order_release);
    if (!gManagedCallbackBinding.load(std::memory_order_acquire))
        publishManagedCallback(binding, clearRevision);
    std::uint8_t patch[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    std::memcpy(patch + 1, &relativeJump, sizeof(relativeJump));
    std::uint64_t patchWord = 0;
    std::memcpy(&patchWord, patch, sizeof(patchWord));
    reinterpret_cast<std::atomic<std::uint64_t>*>(code)->store(patchWord,
                                                               std::memory_order_seq_cst);
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code) + 8);
    setMemoryProtection({.address = std::bit_cast<void*>(protectStart),
                         .length = protectEnd - protectStart,
                         .protection = originalCodeProtection});
    KUE_INFO("mono: late main-thread hook installed at %p (method=%p size=%d copied=%zu)", code,
             static_cast<void*>(method), codeSize, copied);
    return true;
}

void clearManagedMethodCallback() {
    deactivateManagedCallback();
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

MethodLookupResult findMethod(const MonoClass* type, const char* name,
                              int parameterCount) noexcept {
    if (!type || !name || parameterCount < 0)
        return {nullptr, MetadataMemberLookupStatus::InvalidInput};
    if (!runtimeResolved())
        return {nullptr, MetadataMemberLookupStatus::RuntimeUnavailable};
    MonoMethod* const method = reinterpret_cast<MonoMethod*>(
        gMono.class_get_method_from_name(const_cast<MonoClass*>(type), name, parameterCount));
    return method ? MethodLookupResult{method, MetadataMemberLookupStatus::Resolved}
                  : MethodLookupResult{nullptr, MetadataMemberLookupStatus::MissingMember};
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
