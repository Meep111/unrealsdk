#include "unrealsdk/pch.h"

#include "unrealsdk/env.h"
#include "unrealsdk/game/abstract_hook.h"
#include "unrealsdk/hook_manager.h"
#include "unrealsdk/logging.h"
#include "unrealsdk/unreal/find_class.h"
#include "unrealsdk/unrealsdk.h"
#include "unrealsdk/version.h"

// Define this if you're having memory leaks which go through the unreal allocator - which debuggers
// normally won't be able to track.
// With it defined, all allocations will be stored in a set `unrealsdk::unreal_allocations`.
// Since this set uses the default allocator, debuggers should be able to hook onto it to tell you
// where the allocations are coming from.
#undef UNREALSDK_UNREAL_ALLOC_TRACKING

using namespace unrealsdk::unreal;

namespace unrealsdk {

namespace {

#ifndef UNREALSDK_IMPORTING
std::mutex mutex{};
std::unique_ptr<game::AbstractHook> hook_instance;
#endif

}  // namespace

#ifdef UNREALSDK_UNREAL_ALLOC_TRACKING
std::unordered_set<void*> unreal_allocations{};
#endif

#ifndef UNREALSDK_IMPORTING
bool init(std::unique_ptr<game::AbstractHook>&& game) {
    const std::lock_guard<std::mutex> lock(mutex);

    if (hook_instance != nullptr) {
        return false;
    }

    env::load_file();
    logging::init(utils::get_this_dll_dir() / env::get(env::LOG_FILE, env::defaults::LOG_FILE));

    auto version = unrealsdk::get_version_string();
    LOG(INFO, "{}", version);
    LOG(INFO, "{}", std::string(version.size(), '='));

    if (MH_Initialize() != MH_OK) {
        throw std::runtime_error("Minhook initialization failed!");
    }

    // Initialize the hook before moving it, to weed out any order of initialization problems.
    game->hook();

    hook_instance = std::move(game);
    return true;
}

#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(bool, is_initialized);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(bool, is_initialized) {
    const std::lock_guard<std::mutex> lock(mutex);
    return hook_instance != nullptr;
}
#endif
bool is_initialized(void) {
    return UNREALSDK_MANGLE(is_initialized)();
}

#pragma region Wrappers

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] const GObjects*, gobjects);
#endif
#ifdef UNREALSDK_IMPORTING
const GObjects& gobjects(void) {
    return *UNREALSDK_MANGLE(gobjects)();
}
#else
const GObjects& gobjects(void) {
    return hook_instance->gobjects();
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI([[nodiscard]] const GObjects*, gobjects) {
    return &hook_instance->gobjects();
}
#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] const GNames*, gnames);
#endif
#ifdef UNREALSDK_IMPORTING
const GNames& gnames(void) {
    return *UNREALSDK_MANGLE(gnames)();
}
#else
const GNames& gnames(void) {
    return hook_instance->gnames();
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI([[nodiscard]] const GNames*, gnames) {
    return &hook_instance->gnames();
}
#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(void, fname_init, FName* name, const wchar_t* str, int32_t number);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(void, fname_init, FName* name, const wchar_t* str, int32_t number) {
    hook_instance->fname_init(name, str, number);
}
#endif
void fname_init(FName* name, const wchar_t* str, int32_t number) {
    UNREALSDK_MANGLE(fname_init)(name, str, number);
}
void fname_init(FName* name, const std::wstring& str, int32_t number) {
    UNREALSDK_MANGLE(fname_init)(name, str.c_str(), number);
}

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(void, fframe_step, FFrame* frame, UObject* obj, void* param);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(void, fframe_step, FFrame* frame, UObject* obj, void* param) {
    hook_instance->fframe_step(frame, obj, param);
}
#endif
void fframe_step(FFrame* frame, UObject* obj, void* param) {
    UNREALSDK_MANGLE(fframe_step(frame, obj, param));
}

#if UNREALSDK_SHARED
UNREALSDK_CAPI(void*, u_malloc, size_t len);
UNREALSDK_CAPI(void*, u_realloc, void* original, size_t len);
UNREALSDK_CAPI(void, u_free, void* data);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(void*, u_malloc, size_t len) {
    auto ptr = hook_instance->u_malloc(len);

#ifdef UNREALSDK_UNREAL_ALLOC_TRACKING
    unreal_allocations.insert(ptr);
#endif

    return ptr;
}
UNREALSDK_CAPI(void*, u_realloc, void* original, size_t len) {
    auto ptr = hook_instance->u_realloc(original, len);

#ifdef UNREALSDK_UNREAL_ALLOC_TRACKING
    unreal_allocations.erase(original);
    unreal_allocations.insert(ptr);
#endif

    return ptr;
}
UNREALSDK_CAPI(void, u_free, void* data) {
#ifdef UNREALSDK_UNREAL_ALLOC_TRACKING
    unreal_allocations.erase(data);
#endif

    hook_instance->u_free(data);
}
#endif
void* u_malloc(size_t len) {
    return UNREALSDK_MANGLE(u_malloc)(len);
}
void* u_realloc(void* original, size_t len) {
    return UNREALSDK_MANGLE(u_realloc)(original, len);
}
void u_free(void* data) {
    UNREALSDK_MANGLE(u_free)(data);
}
#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(void, process_event, UObject* object, UFunction* function, void* params);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(void, process_event, UObject* object, UFunction* function, void* params) {
    hook_instance->process_event(object, function, params);
}
#endif
void process_event(UObject* object, UFunction* function, void* params) {
    UNREALSDK_MANGLE(process_event)(object, function, params);
}

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] UObject*,
               construct_object,
               UClass* cls,
               UObject* outer,
               const FName* name = nullptr,
               decltype(UObject::ObjectFlags) flags = 0,
               UObject* template_obj = nullptr);
#endif
#ifdef UNREALSDK_IMPORTING
UObject* construct_object(UClass* cls,
                          UObject* outer,
                          const FName& name,
                          decltype(UObject::ObjectFlags) flags,
                          UObject* template_obj) {
    return UNREALSDK_MANGLE(construct_object)(cls, outer, &name, flags, template_obj);
}
#else
UObject* construct_object(UClass* cls,
                          UObject* outer,
                          const FName& name,
                          decltype(UObject::ObjectFlags) flags,
                          UObject* template_obj) {
    return hook_instance->construct_object(cls, outer, name, flags, template_obj);
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI([[nodiscard]] UObject*,
               construct_object,
               UClass* cls,
               UObject* outer,
               const FName* name,
               decltype(UObject::ObjectFlags) flags,
               UObject* template_obj) {
    FName local_name{0, 0};
    if (name != nullptr) {
        local_name = *name;
    }
    return hook_instance->construct_object(cls, outer, local_name, flags, template_obj);
}
#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(void, uconsole_output_text, const wchar_t* str, size_t size);
#endif
#ifdef UNREALSDK_IMPORTING
void uconsole_output_text(const std::wstring& str) {
    UNREALSDK_MANGLE(uconsole_output_text)(str.c_str(), str.size());
}
#else
void uconsole_output_text(const std::wstring& str) {
    // Since this is called by logging, which we know will happen plenty before/during
    // initialization, just drop any messages which arrive before we have a hook to work with
    if (hook_instance) {
        hook_instance->uconsole_output_text(str);
    }
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI(void, uconsole_output_text, const wchar_t* str, size_t size) {
    uconsole_output_text({str, size});
}
#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] bool, is_console_ready);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI([[nodiscard]] bool, is_console_ready) {
    return hook_instance && hook_instance->is_console_ready();
}
#endif
[[nodiscard]] bool is_console_ready(void) {
    return UNREALSDK_MANGLE(is_console_ready);
}

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] wchar_t*, uobject_path_name, const UObject* obj);
#endif
#ifdef UNREALSDK_IMPORTING
std::wstring uobject_path_name(const UObject* obj) {
    auto ptr = UNREALSDK_MANGLE(uobject_path_name)(obj);
    std::wstring str{ptr};
    u_free(ptr);
    return str;
}
#else
std::wstring uobject_path_name(const UObject* obj) {
    return hook_instance->uobject_path_name(obj);
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI([[nodiscard]] wchar_t*, uobject_path_name, const UObject* obj) {
    auto name = hook_instance->uobject_path_name(obj);
    auto size = name.size();

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    auto mem = reinterpret_cast<wchar_t*>(u_malloc((size + 1) * sizeof(wchar_t)));
    wcsncpy_s(mem, size + 1, name.data(), size);

    return mem;
}
#endif

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI([[nodiscard]] UObject*,
               find_object,
               UClass* cls,
               const wchar_t* name,
               size_t name_size);
#endif
#ifdef UNREALSDK_IMPORTING
UObject* find_object(UClass* cls, const std::wstring& name) {
    return UNREALSDK_MANGLE(find_object)(cls, name.c_str(), name.size());
}
UObject* find_object(const FName& cls, const std::wstring& name) {
    return UNREALSDK_MANGLE(find_object)(find_class(cls), name.c_str(), name.size());
}
UObject* find_object(const std::wstring& cls, const std::wstring& name) {
    return UNREALSDK_MANGLE(find_object)(find_class(cls), name.c_str(), name.size());
}
#else
UObject* find_object(UClass* cls, const std::wstring& name) {
    return hook_instance->find_object(cls, name);
}
UObject* find_object(const FName& cls, const std::wstring& name) {
    return hook_instance->find_object(find_class(cls), name);
}
UObject* find_object(const std::wstring& cls, const std::wstring& name) {
    return hook_instance->find_object(find_class(cls), name);
}
#endif
#ifdef UNREALSDK_EXPORTING
UNREALSDK_CAPI([[nodiscard]] UObject*,
               find_object,
               UClass* cls,
               const wchar_t* name,
               size_t name_size) {
    return hook_instance->find_object(cls, {name, name_size});
}
#endif

#ifdef UE4

#ifdef UNREALSDK_SHARED
UNREALSDK_CAPI(void,
               ftext_as_culture_invariant,
               unreal::FText* text,
               unreal::TemporaryFString&& str);
#endif
#ifndef UNREALSDK_IMPORTING
UNREALSDK_CAPI(void,
               ftext_as_culture_invariant,
               unreal::FText* text,
               unreal::TemporaryFString&& str) {
    hook_instance->ftext_as_culture_invariant(text, std::move(str));
}
#endif
void ftext_as_culture_invariant(unreal::FText* text, unreal::TemporaryFString&& str) {
    UNREALSDK_MANGLE(ftext_as_culture_invariant)(text, std::move(str));
}

#endif

}  // namespace unrealsdk
