// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/ramfs.hpp>
#include <eris/string.hpp>

namespace eris::fs {
namespace {

constexpr usize max_nodes = 32;
constexpr usize max_name = 32;

class RamFile final : public Inode {
public:
    void bind(const char* name)
    {
        usize i = 0;
        while (i + 1 < max_name && name[i] != '\0') {
            name_[i] = name[i];
            ++i;
        }
        name_[i] = '\0';
        used_ = true;
    }

    const char* file_name() const { return name_; }
    bool used() const { return used_; }

    NodeType type() const override { return NodeType::File; }
    u64 size() const override { return size_; }

    isize read(u64 offset, void* buffer, usize length) override
    {
        if (offset >= size_)
            return 0;

        const usize available = static_cast<usize>(size_ - offset);
        const usize chunk = length < available ? length : available;

        memcpy(buffer, data_ + offset, chunk);
        return static_cast<isize>(chunk);
    }

    isize write(u64 offset, const void* buffer, usize length) override
    {
        if (offset + length > capacity_ && !grow(offset + length))
            return -1;

        memcpy(data_ + offset, buffer, length);

        if (offset + length > size_)
            size_ = offset + length;

        return static_cast<isize>(length);
    }

    void release()
    {
        if (data_ != nullptr) {
            kfree(data_);
            data_ = nullptr;
        }

        size_ = 0;
        capacity_ = 0;
        used_ = false;
    }

private:
    bool grow(u64 wanted)
    {
        usize target = capacity_ == 0 ? 512 : capacity_;
        while (target < wanted)
            target *= 2;

        auto* fresh = static_cast<u8*>(kzalloc(target));
        if (fresh == nullptr)
            return false;

        if (data_ != nullptr) {
            memcpy(fresh, data_, static_cast<usize>(size_));
            kfree(data_);
        }

        data_ = fresh;
        capacity_ = target;
        return true;
    }

    char name_[max_name]{};
    u8* data_ = nullptr;
    u64 size_ = 0;
    usize capacity_ = 0;
    bool used_ = false;
};

class RamDirectory final : public Inode {
public:
    NodeType type() const override { return NodeType::Directory; }
    u64 size() const override { return 0; }

    isize read(u64, void*, usize) override { return -1; }

    Inode* lookup(const char* name) override;
    const char* entry_at(usize index) override;
};

class RamFs final : public FileSystem {
public:
    const char* name() const override { return "ramfs"; }
    Inode* root() override { return &root_; }

private:
    RamDirectory root_;
};

constinit RamFile files[max_nodes]{};
constinit RamFs filesystem{};
constinit IrqSpinLock ramfs_lock{};

RamFile* find(const char* name)
{
    for (auto& file : files) {
        if (file.used() && strcmp(file.file_name(), name) == 0)
            return &file;
    }
    return nullptr;
}

Inode* RamDirectory::lookup(const char* name)
{
    return find(name);
}

const char* RamDirectory::entry_at(usize index)
{
    usize seen = 0;

    for (auto& file : files) {
        if (!file.used())
            continue;

        if (seen == index)
            return file.file_name();

        ++seen;
    }

    return nullptr;
}

} // namespace

FileSystem* ramfs_create()
{
    return &filesystem;
}

Inode* ramfs_create_file(const char* name)
{
    IrqGuard guard(ramfs_lock);

    if (RamFile* existing = find(name); existing != nullptr)
        return existing;

    for (auto& file : files) {
        if (file.used())
            continue;

        file.bind(name);
        return &file;
    }

    pr_warn("ramfs: no room for %s\n", name);
    return nullptr;
}

bool ramfs_remove(const char* name)
{
    IrqGuard guard(ramfs_lock);

    RamFile* file = find(name);
    if (file == nullptr)
        return false;

    file->release();
    return true;
}

} // namespace eris::fs
