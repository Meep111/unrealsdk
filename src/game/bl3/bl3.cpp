#include "pch.h"

#include "game/bl3/bl3.h"
#include "memory.h"

#if defined(UE4) && defined(ARCH_X64)

using namespace unrealsdk::unreal;
using namespace unrealsdk::memory;

namespace unrealsdk::game {

void BL3Hook::hook(void) {
    hook_process_event();
    hook_call_function();

    find_gobjects();
    find_gnames();
    find_fname_init();
    find_fframe_step();
    find_gmalloc();
    find_construct_object();

    inject_console();
}

#pragma region FName::Init

using fname_init_func = FName (*)(const wchar_t* str, int32_t number, int32_t find_type);
static fname_init_func fname_init_ptr;

void BL3Hook::find_fname_init(void) {
    static const Pattern FNAME_INIT_PATTERN{
        "\x40\x53\x48\x83\xEC\x30\xC7\x44\x24\x00\x00\x00\x00\x00",
        "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00"};

    fname_init_ptr = sigscan<fname_init_func>(FNAME_INIT_PATTERN);
    LOG(MISC, "FName::Init: 0x%p", fname_init_ptr);
}

void BL3Hook::fname_init(FName* name, const std::wstring& str, int32_t number) const {
    this->fname_init(name, str.c_str(), number);
}
void BL3Hook::fname_init(FName* name, const wchar_t* str, int32_t number) const {
    *name = fname_init_ptr(str, number, 1);
}

#pragma endregion

#pragma region FFrame::Step

using fframe_step_func = void (*)(FFrame* stack, UObject* obj, void* param);
static fframe_step_func fframe_step_ptr;

void BL3Hook::find_fframe_step(void) {
    static const Pattern FFRAME_STEP_SIG{"\x48\x8B\x41\x20\x4C\x8B\xD2\x48\x8B\xD1",
                                         "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"};

    fframe_step_ptr = sigscan<fframe_step_func>(FFRAME_STEP_SIG);
    LOG(MISC, "FFrame::Step: 0x%p", fframe_step_ptr);
}
void BL3Hook::fframe_step(FFrame* frame, UObject* obj, void* param) const {
    fframe_step_ptr(frame, obj, param);
}

#pragma endregion

#pragma region ConstructObject

using construct_obj_func = UObject* (*)(UClass* cls,
                                        UObject* obj,
                                        FName name,
                                        uint32_t flags,
                                        uint32_t internal_flags,
                                        UObject* template_obj,
                                        uint32_t copy_transients_from_class_defaults,
                                        void* instance_graph,
                                        uint32_t assume_template_is_archetype);
static construct_obj_func construct_obj_ptr;

void BL3Hook::find_construct_object(void) {
    static const Pattern CONSTRUCT_OBJECT_PATTERN{
        "\xE8\x00\x00\x00\x00\x41\x89\x3E\x4D\x8D\x46\x04",
        "\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 1};

    auto construct_obj_instr = sigscan(CONSTRUCT_OBJECT_PATTERN);
    construct_obj_ptr = read_offset<construct_obj_func>(construct_obj_instr);
    LOG(MISC, "StaticConstructObject: 0x%p", construct_obj_ptr);
}

UObject* BL3Hook::construct_object(UClass* cls,
                                   UObject* outer,
                                   const FName& name,
                                   decltype(UObject::ObjectFlags) flags,
                                   UObject* template_obj) const {
    return construct_obj_ptr(cls, outer, name, flags, 0, template_obj, 0 /* false */, nullptr,
                             0 /* false */);
}

#pragma endregion

}  // namespace unrealsdk::game

#endif
