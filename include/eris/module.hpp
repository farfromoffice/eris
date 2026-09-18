// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

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

struct ModuleInfo {
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
    u32 refcount;
    int error;
};

void module_init_builtin();
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
