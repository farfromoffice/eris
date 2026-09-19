// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::fs {

enum class NodeType : u8 {
    File,
    Directory,
    Device,
};

struct Stat {
    NodeType type;
    u64 size;
};

// One file or directory. A file system hands these out, the VFS walks them and
// nothing above either cares which is which.
class Inode {
public:
    virtual ~Inode() = default;

    virtual NodeType type() const = 0;
    virtual u64 size() const = 0;

    virtual isize read(u64 offset, void* buffer, usize length) = 0;
    virtual isize write(u64 offset, const void* buffer, usize length);

    // Directory walk. A file returns null for both.
    virtual Inode* lookup(const char* name);
    virtual const char* entry_at(usize index);
};

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual const char* name() const = 0;
    virtual Inode* root() = 0;
};

void init();

int mount(const char* path, FileSystem* filesystem);
int unmount(const char* path);

usize mount_count();
const char* mount_at(usize index, const char*& filesystem);

Inode* resolve(const char* path);
int stat(const char* path, Stat& out);

isize read_file(const char* path, void* buffer, usize length);
isize write_file(const char* path, const void* buffer, usize length);

// Directory listing, one entry at a time, so nothing has to allocate.
const char* list(const char* path, usize index);

} // namespace eris::fs
