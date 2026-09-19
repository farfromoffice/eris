// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/atomic.hpp>
#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris {

enum class ModuleState : u8 {
    Registered,
    Loading,
    Ready,
    Failed,
};

using ModuleInitFn = int (*)();
using ModuleExitFn = void (*)();

// Bumped whenever the shape of anything a module links against changes, so a
// stale image is refused instead of faulting on the first call.
inline constexpr u32 module_abi_version = 1;

struct ModuleInfo {
    u32 abi;
    const char* name;
    const char* version;
    const char* author;
    const char* license;
    ModuleInitFn init;
    ModuleExitFn exit;
    const char* const* deps;
    usize dep_count;
};

struct Module {
    const ModuleInfo* info;
    ModuleState state;
    RefCount dependents; // Ready modules that list this one as a dependency
    RefCount users;      // module_get calls not yet matched by module_put
    int error;
    virt_addr image_base; // zero for a module built into the image
    usize image_pages;
};

void module_init_builtin();

// Loading an image that was built separately, which is what the module
// framework exists for.
int module_load_image(const void* data, usize length, const char* origin);
int module_register_loaded(const ModuleInfo* info, virt_addr base, usize pages);
void module_release_image(virt_addr base, usize pages);
bool module_license_is_free(const char* license);
bool kernel_tainted();
int module_load(const char* name);
int module_unload(const char* name);

Module* module_find(const char* name);
Module* module_at(usize index);
usize module_count();

bool module_get(const char* name);
void module_put(const char* name);

const char* module_state_name(ModuleState state);

} // namespace eris

#define ERIS_MODULE_SECTION __attribute__((section(".eris_modules"), used, aligned(8)))

#define ERIS_MODULE(NAME, VERSION, AUTHOR, LICENSE, INIT, EXIT, ...)                       \
    namespace {                                                                   \
    const char* const eris_module_deps_table[] = { __VA_ARGS__ __VA_OPT__(, ) nullptr }; \
    const ::eris::ModuleInfo eris_module_descriptor ERIS_MODULE_SECTION = {       \
        ::eris::module_abi_version,                                               \
        NAME,                                                                     \
        VERSION,                                                                  \
        AUTHOR,                                                                   \
        LICENSE,                                                                  \
        INIT,                                                                     \
        EXIT,                                                                     \
        eris_module_deps_table,                                                   \
        (sizeof(eris_module_deps_table) / sizeof(eris_module_deps_table[0])) - 1, \
    };                                                                            \
    }
