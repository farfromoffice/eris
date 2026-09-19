// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/lock.hpp>
#include <eris/module.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

extern "C" {
extern const eris::ModuleInfo __eris_modules_start[];
extern const eris::ModuleInfo __eris_modules_end[];
}

namespace eris {
namespace {

constexpr usize max_modules = 64;

constexpr const char* free_licenses[] = {
    "GPL-2.0-only",
    "GPL-2.0-or-later",
    "GPL-3.0-only",
    "GPL-3.0-or-later",
    "LGPL-2.1-or-later",
    "MIT",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "Apache-2.0",
};

constinit Module modules[max_modules]{};
constinit usize registered = 0;
constinit bool tainted = false;
constinit IrqSpinLock table_lock{};

int load_locked(Module& module, usize depth);

Module* find(const char* name)
{
    for (usize i = 0; i < registered; ++i) {
        if (strcmp(modules[i].info->name, name) == 0)
            return &modules[i];
    }
    return nullptr;
}

int load_dependencies(Module& module, usize depth)
{
    for (usize i = 0; i < module.info->dep_count; ++i) {
        const char* dep_name = module.info->deps[i];
        Module* dep = find(dep_name);
        if (dep == nullptr) {
            pr_err("module %s: missing dependency %s\n", module.info->name, dep_name);
            return -1;
        }

        const int result = load_locked(*dep, depth + 1);
        if (result != 0)
            return result;
    }
    return 0;
}

// References are taken only once the module is Ready and dropped by
// release_dependencies() on unload and no failure path has to undo them.
void acquire_dependencies(const Module& module)
{
    for (usize i = 0; i < module.info->dep_count; ++i)
        find(module.info->deps[i])->dependents.take();
}

int load_locked(Module& module, usize depth)
{
    if (depth > max_modules) {
        pr_err("module %s: dependency chain too deep\n", module.info->name);
        return -1;
    }

    switch (module.state) {
    case ModuleState::Ready:
        return 0;
    case ModuleState::Loading:
        pr_err("module %s: dependency cycle\n", module.info->name);
        return -1;
    case ModuleState::Failed:
        return module.error;
    case ModuleState::Registered:
        break;
    }

    module.state = ModuleState::Loading;

    if (load_dependencies(module, depth) != 0) {
        module.state = ModuleState::Failed;
        module.error = -1;
        return -1;
    }

    if (!module_license_is_free(module.info->license)) {
        tainted = true;
        pr_warn("module %s has license %s, kernel tainted\n",
                module.info->name,
                module.info->license != nullptr ? module.info->license : "unknown");
    }

    const int result = module.info->init != nullptr ? module.info->init() : 0;
    if (result != 0) {
        module.state = ModuleState::Failed;
        module.error = result;
        pr_err("module %s: init failed (%d)\n", module.info->name, result);
        return result;
    }

    acquire_dependencies(module);
    module.state = ModuleState::Ready;
    module.error = 0;
    pr_info("module %s %s loaded (%s)\n", module.info->name, module.info->version,
            module.info->license);
    return 0;
}

void release_dependencies(const Module& module)
{
    for (usize i = 0; i < module.info->dep_count; ++i) {
        Module* dep = find(module.info->deps[i]);
        if (dep == nullptr)
            continue;
        if (!dep->dependents.held())
            panic("module %s: no reference left to drop on %s", module.info->name, dep->info->name);
        dep->dependents.release();
    }
}

} // namespace

// A module built elsewhere joins the same table the builtin ones live in, once
// its ABI stamp says it was built against this kernel.
int module_register_loaded(const ModuleInfo* info, virt_addr base, usize pages)
{
    if (info == nullptr)
        return -1;

    if (info->abi != module_abi_version) {
        pr_err("module %s was built against abi %u, this kernel speaks %u\n",
               info->name != nullptr ? info->name : "?",
               info->abi,
               module_abi_version);
        return -1;
    }

    IrqGuard guard(table_lock);

    if (find(info->name) != nullptr) {
        pr_err("module %s is already in the table\n", info->name);
        return -1;
    }

    if (registered >= max_modules) {
        pr_err("module table full, %s refused\n", info->name);
        return -1;
    }

    Module& slot = modules[registered++];
    slot.info = info;
    slot.state = ModuleState::Registered;
    slot.error = 0;
    slot.image_base = base;
    slot.image_pages = pages;

    return 0;
}

void module_init_builtin()
{
    for (const ModuleInfo* info = __eris_modules_start; info != __eris_modules_end; ++info) {
        if (registered >= max_modules) {
            pr_warn("module table full, %s skipped\n", info->name);
            continue;
        }
        if (info->abi != module_abi_version) {
            pr_warn("builtin module %s carries abi %u, skipped\n", info->name, info->abi);
            continue;
        }

        Module& slot = modules[registered++];
        slot.info = info;
        slot.state = ModuleState::Registered;
        slot.error = 0;
        slot.image_base = 0;
        slot.image_pages = 0;
    }

    pr_info("%lu builtin modules registered\n", static_cast<u64>(registered));

    for (usize i = 0; i < registered; ++i)
        load_locked(modules[i], 0);
}

int module_load(const char* name)
{
    Module* module = find(name);
    if (module == nullptr)
        return -1;
    return load_locked(*module, 0);
}

int module_unload(const char* name)
{
    Module* module = find(name);
    if (module == nullptr)
        return -1;
    if (module->state != ModuleState::Ready)
        return 0;
    if (module->dependents.held() || module->users.held()) {
        pr_warn("module %s still in use, dependents=%u users=%u\n", name,
                module->dependents.value(), module->users.value());
        return -1;
    }

    if (module->info->exit != nullptr)
        module->info->exit();

    release_dependencies(*module);
    module->state = ModuleState::Registered;

    // A loaded image is only worth keeping while something in it can run.
    if (module->image_base != 0) {
        const virt_addr base = module->image_base;
        const usize pages = module->image_pages;

        module->image_base = 0;
        module->image_pages = 0;

        for (usize i = 0; i < registered; ++i) {
            if (&modules[i] == module) {
                modules[i] = modules[--registered];
                break;
            }
        }

        module_release_image(base, pages);
        pr_info("module %s unloaded, %lu KiB returned\n", name,
                static_cast<u64>(pages * page_size / 1024));
        return 0;
    }

    pr_info("module %s unloaded\n", name);
    return 0;
}

Module* module_find(const char* name)
{
    return find(name);
}

Module* module_at(usize index)
{
    return index < registered ? &modules[index] : nullptr;
}

usize module_count()
{
    return registered;
}

bool module_get(const char* name)
{
    IrqGuard guard(table_lock);

    Module* module = find(name);
    if (module == nullptr || module->state != ModuleState::Ready)
        return false;

    module->users.take();
    return true;
}

void module_put(const char* name)
{
    IrqGuard guard(table_lock);

    Module* module = find(name);
    if (module == nullptr || !module->users.held())
        panic("module_put(%s) without a matching module_get", name);
    module->users.release();
}

bool module_license_is_free(const char* license)
{
    if (license == nullptr)
        return false;

    for (const char* candidate : free_licenses) {
        if (strcmp(candidate, license) == 0)
            return true;
    }
    return false;
}

bool kernel_tainted()
{
    return tainted;
}

const char* module_state_name(ModuleState state)
{
    switch (state) {
    case ModuleState::Registered: return "registered";
    case ModuleState::Loading:    return "loading";
    case ModuleState::Ready:      return "ready";
    case ModuleState::Failed:     return "failed";
    }
    return "unknown";
}

} // namespace eris
