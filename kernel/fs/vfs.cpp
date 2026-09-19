// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/lock.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>
#include <eris/vfs.hpp>

namespace eris::fs {
namespace {

constexpr usize max_mounts = 8;
constexpr usize max_path = 128;

struct Mount {
    char path[max_path];
    FileSystem* filesystem;
};

constinit Mount mounts[max_mounts]{};
constinit usize mount_total = 0;
constinit IrqSpinLock mount_lock{};

usize path_length(const char* path)
{
    usize length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        --length;
    return length;
}

// The mount whose path is the longest prefix of this one, so /mnt/disk wins
// over / for anything under it.
Mount* mount_for(const char* path, const char*& rest)
{
    Mount* best = nullptr;
    usize best_length = 0;

    for (usize i = 0; i < mount_total; ++i) {
        Mount& mount = mounts[i];
        const usize length = path_length(mount.path);

        if (memcmp(path, mount.path, length) != 0)
            continue;

        const char after = path[length];
        if (length > 1 && after != '\0' && after != '/')
            continue;

        if (best == nullptr || length > best_length) {
            best = &mount;
            best_length = length;
        }
    }

    if (best == nullptr)
        return nullptr;

    rest = path + (best_length > 1 ? best_length : 0);
    return best;
}

} // namespace

isize Inode::write(u64, const void*, usize)
{
    return -1;
}

Inode* Inode::lookup(const char*)
{
    return nullptr;
}

const char* Inode::entry_at(usize)
{
    return nullptr;
}

void init()
{
    mount_total = 0;
}

int mount(const char* path, FileSystem* filesystem)
{
    if (path == nullptr || filesystem == nullptr || path[0] != '/')
        return -1;

    IrqGuard guard(mount_lock);

    if (mount_total >= max_mounts) {
        pr_warn("vfs: mount table full, %s refused\n", path);
        return -1;
    }

    Mount& mount = mounts[mount_total];

    usize i = 0;
    while (i + 1 < max_path && path[i] != '\0') {
        mount.path[i] = path[i];
        ++i;
    }
    mount.path[i] = '\0';
    mount.filesystem = filesystem;

    ++mount_total;

    pr_info("vfs: %s mounted at %s\n", filesystem->name(), path);
    return 0;
}

int unmount(const char* path)
{
    IrqGuard guard(mount_lock);

    for (usize i = 0; i < mount_total; ++i) {
        if (strcmp(mounts[i].path, path) != 0)
            continue;

        mounts[i] = mounts[--mount_total];
        return 0;
    }

    return -1;
}

usize mount_count()
{
    return mount_total;
}

const char* mount_at(usize index, const char*& filesystem)
{
    if (index >= mount_total)
        return nullptr;

    filesystem = mounts[index].filesystem->name();
    return mounts[index].path;
}

// Walks a path one component at a time from the root of whatever is mounted
// closest to it.
Inode* resolve(const char* path)
{
    if (path == nullptr || path[0] != '/')
        return nullptr;

    const char* rest = nullptr;
    Mount* mount = mount_for(path, rest);
    if (mount == nullptr)
        return nullptr;

    Inode* node = mount->filesystem->root();

    while (node != nullptr && rest != nullptr && *rest != '\0') {
        while (*rest == '/')
            ++rest;

        if (*rest == '\0')
            break;

        char component[64];
        usize i = 0;
        while (i + 1 < sizeof(component) && rest[i] != '\0' && rest[i] != '/') {
            component[i] = rest[i];
            ++i;
        }
        component[i] = '\0';
        rest += i;

        node = node->lookup(component);
    }

    return node;
}

int stat(const char* path, Stat& out)
{
    Inode* node = resolve(path);
    if (node == nullptr)
        return -1;

    out.type = node->type();
    out.size = node->size();
    return 0;
}

isize read_file(const char* path, void* buffer, usize length)
{
    Inode* node = resolve(path);
    if (node == nullptr || node->type() == NodeType::Directory)
        return -1;

    return node->read(0, buffer, length);
}

isize write_file(const char* path, const void* buffer, usize length)
{
    Inode* node = resolve(path);
    if (node == nullptr || node->type() == NodeType::Directory)
        return -1;

    return node->write(0, buffer, length);
}

const char* list(const char* path, usize index)
{
    Inode* node = resolve(path);
    if (node == nullptr || node->type() != NodeType::Directory)
        return nullptr;

    return node->entry_at(index);
}

} // namespace eris::fs
