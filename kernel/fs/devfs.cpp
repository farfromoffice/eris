// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/block.hpp>
#include <eris/console.hpp>
#include <eris/devfs.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris::fs {
namespace {

constexpr usize max_nodes = 16;
constexpr usize max_name = 24;

// A block device seen as a file: offsets are bytes, the driver still moves
// whole sectors underneath.
class BlockNode final : public Inode {
public:
    void bind(BlockDevice* device)
    {
        device_ = device;

        usize i = 0;
        while (i + 1 < max_name && device->name()[i] != '\0') {
            name_[i] = device->name()[i];
            ++i;
        }
        name_[i] = '\0';
    }

    const char* node_name() const { return name_; }
    bool used() const { return device_ != nullptr; }

    NodeType type() const override { return NodeType::Device; }
    u64 size() const override { return device_->sector_count() * sector_size; }

    isize read(u64 offset, void* buffer, usize length) override
    {
        if (device_ == nullptr || !device_->read_bytes(offset, buffer, length))
            return -1;
        return static_cast<isize>(length);
    }

    isize write(u64 offset, const void* buffer, usize length) override
    {
        if (device_ == nullptr || offset % sector_size != 0 || length % sector_size != 0)
            return -1;

        if (!device_->write(offset / sector_size, buffer, length / sector_size))
            return -1;

        return static_cast<isize>(length);
    }

private:
    char name_[max_name]{};
    BlockDevice* device_ = nullptr;
};

// Writing here prints, which makes the console reachable through the same
// interface as everything else.
class ConsoleNode final : public Inode {
public:
    NodeType type() const override { return NodeType::Device; }
    u64 size() const override { return 0; }

    isize read(u64, void*, usize) override { return 0; }

    isize write(u64, const void* buffer, usize length) override
    {
        const auto* text = static_cast<const char*>(buffer);
        for (usize i = 0; i < length; ++i)
            console_put(text[i]);
        return static_cast<isize>(length);
    }
};

class DevDirectory final : public Inode {
public:
    NodeType type() const override { return NodeType::Directory; }
    u64 size() const override { return 0; }

    isize read(u64, void*, usize) override { return -1; }

    Inode* lookup(const char* name) override;
    const char* entry_at(usize index) override;
};

class DevFs final : public FileSystem {
public:
    const char* name() const override { return "devfs"; }
    Inode* root() override { return &root_; }

private:
    DevDirectory root_;
};

constinit BlockNode block_nodes[max_nodes]{};
constinit usize block_node_total = 0;
constinit ConsoleNode console_node{};
constinit DevFs filesystem{};

Inode* DevDirectory::lookup(const char* name)
{
    if (strcmp(name, "console") == 0)
        return &console_node;

    for (usize i = 0; i < block_node_total; ++i) {
        if (strcmp(block_nodes[i].node_name(), name) == 0)
            return &block_nodes[i];
    }

    return nullptr;
}

const char* DevDirectory::entry_at(usize index)
{
    if (index == 0)
        return "console";

    const usize block_index = index - 1;
    return block_index < block_node_total ? block_nodes[block_index].node_name() : nullptr;
}

} // namespace

FileSystem* devfs_create()
{
    devfs_refresh();
    return &filesystem;
}

// Called after a driver registers, because a device that shows up later still
// deserves a node.
void devfs_refresh()
{
    block_node_total = 0;

    for (usize i = 0; i < block_device_count() && block_node_total < max_nodes; ++i) {
        BlockDevice* device = block_device_at(i);
        if (device == nullptr)
            continue;

        block_nodes[block_node_total++].bind(device);
    }
}

} // namespace eris::fs
