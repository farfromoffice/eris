// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/block.hpp>
#include <eris/compiler.hpp>
#include <eris/ext2.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris::fs {
namespace {

constexpr u16 ext2_magic = 0xEF53;
constexpr u64 superblock_offset = 1024;
constexpr usize max_open_inodes = 32;
constexpr usize root_inode = 2;

struct ERIS_PACKED Superblock {
    u32 inode_count;
    u32 block_count;
    u32 reserved_blocks;
    u32 free_blocks;
    u32 free_inodes;
    u32 first_data_block;
    u32 log_block_size;
    u32 log_fragment_size;
    u32 blocks_per_group;
    u32 fragments_per_group;
    u32 inodes_per_group;
    u32 mount_time;
    u32 write_time;
    u16 mount_count;
    u16 max_mount_count;
    u16 magic;
    u16 state;
    u16 errors;
    u16 minor_revision;
    u32 last_check;
    u32 check_interval;
    u32 creator_os;
    u32 revision;
    u16 reserved_uid;
    u16 reserved_gid;
    u32 first_inode;
    u16 inode_size;
};

struct ERIS_PACKED GroupDescriptor {
    u32 block_bitmap;
    u32 inode_bitmap;
    u32 inode_table;
    u16 free_blocks;
    u16 free_inodes;
    u16 directories;
    u16 padding;
    u32 reserved[3];
};

struct ERIS_PACKED DiskInode {
    u16 mode;
    u16 uid;
    u32 size;
    u32 access_time;
    u32 creation_time;
    u32 modification_time;
    u32 deletion_time;
    u16 gid;
    u16 link_count;
    u32 sectors;
    u32 flags;
    u32 os_specific;
    u32 direct[12];
    u32 indirect;
    u32 double_indirect;
    u32 triple_indirect;
};

struct ERIS_PACKED DirectoryEntry {
    u32 inode;
    u16 length;
    u8 name_length;
    u8 type;
};

constexpr u16 mode_directory = 0x4000;

class Ext2Inode;

// One mounted ext2 image. Read only: writing means allocation, bitmaps and a
// journal nobody needs yet.
class Ext2Fs final : public FileSystem {
public:
    bool mount(BlockDevice* device);

    const char* name() const override { return "ext2"; }
    Inode* root() override;

    bool read_block(u32 block, void* buffer);
    bool read_inode(u32 number, DiskInode& out);
    u32 block_size() const { return block_size_; }

    Ext2Inode* open(u32 number);

private:
    BlockDevice* device_ = nullptr;
    Superblock super_{};
    u32 block_size_ = 1024;
    u32 inode_size_ = 128;
    u32 group_count_ = 0;
};

class Ext2Inode final : public Inode {
public:
    void bind(Ext2Fs* filesystem, u32 number, const DiskInode& disk)
    {
        filesystem_ = filesystem;
        number_ = number;
        disk_ = disk;
        used_ = true;
    }

    bool used() const { return used_; }
    u32 number() const { return number_; }

    NodeType type() const override
    {
        return (disk_.mode & mode_directory) != 0 ? NodeType::Directory : NodeType::File;
    }

    u64 size() const override { return disk_.size; }

    isize read(u64 offset, void* buffer, usize length) override;
    Inode* lookup(const char* name) override;
    const char* entry_at(usize index) override;

private:
    u32 block_for(u32 index);

    Ext2Fs* filesystem_ = nullptr;
    u32 number_ = 0;
    DiskInode disk_{};
    bool used_ = false;
    char entry_name_[64]{};
};

constinit Ext2Fs filesystem{};
constinit Ext2Inode inodes[max_open_inodes]{};
constinit usize inode_total = 0;

bool Ext2Fs::read_block(u32 block, void* buffer)
{
    if (device_ == nullptr)
        return false;

    return device_->read_bytes(static_cast<u64>(block) * block_size_, buffer, block_size_);
}

bool Ext2Fs::read_inode(u32 number, DiskInode& out)
{
    if (number == 0 || device_ == nullptr)
        return false;

    const u32 group = (number - 1) / super_.inodes_per_group;
    const u32 index = (number - 1) % super_.inodes_per_group;

    // The group descriptors sit in the block right after the superblock.
    const u32 descriptor_block = block_size_ == 1024 ? 2 : 1;
    const u64 descriptor_offset = static_cast<u64>(descriptor_block) * block_size_
        + group * sizeof(GroupDescriptor);

    GroupDescriptor descriptor{};
    if (!device_->read_bytes(descriptor_offset, &descriptor, sizeof(descriptor)))
        return false;

    const u64 offset = static_cast<u64>(descriptor.inode_table) * block_size_
        + static_cast<u64>(index) * inode_size_;

    return device_->read_bytes(offset, &out, sizeof(out));
}

bool Ext2Fs::mount(BlockDevice* device)
{
    device_ = device;

    if (!device->read_bytes(superblock_offset, &super_, sizeof(super_))) {
        device_ = nullptr;
        return false;
    }

    if (super_.magic != ext2_magic) {
        device_ = nullptr;
        return false;
    }

    block_size_ = 1024u << super_.log_block_size;
    inode_size_ = super_.revision >= 1 && super_.inode_size != 0 ? super_.inode_size : 128;
    group_count_ = (super_.block_count + super_.blocks_per_group - 1) / super_.blocks_per_group;

    pr_info("ext2: %u blocks of %u bytes, %u inodes, %u group%s\n",
            super_.block_count,
            block_size_,
            super_.inode_count,
            group_count_,
            group_count_ == 1 ? "" : "s");
    return true;
}

Ext2Inode* Ext2Fs::open(u32 number)
{
    for (usize i = 0; i < inode_total; ++i) {
        if (inodes[i].used() && inodes[i].number() == number)
            return &inodes[i];
    }

    if (inode_total >= max_open_inodes)
        return nullptr;

    DiskInode disk{};
    if (!read_inode(number, disk))
        return nullptr;

    Ext2Inode& node = inodes[inode_total++];
    node.bind(this, number, disk);
    return &node;
}

Inode* Ext2Fs::root()
{
    return open(root_inode);
}

// Direct blocks cover the first twelve, then one level of indirection. That is
// enough for anything this kernel puts on a disk today.
u32 Ext2Inode::block_for(u32 index)
{
    if (index < 12)
        return disk_.direct[index];

    const u32 per_block = filesystem_->block_size() / sizeof(u32);
    const u32 indirect_index = index - 12;

    if (indirect_index >= per_block || disk_.indirect == 0)
        return 0;

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return 0;

    auto* table = reinterpret_cast<u32*>(mm::phys_to_virt(frame));
    u32 block = 0;

    if (filesystem_->read_block(disk_.indirect, table))
        block = table[indirect_index];

    mm::free_page(frame);
    return block;
}

isize Ext2Inode::read(u64 offset, void* buffer, usize length)
{
    if (filesystem_ == nullptr || offset >= disk_.size)
        return 0;

    const u64 available = disk_.size - offset;
    if (length > available)
        length = static_cast<usize>(available);

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return -1;

    auto* scratch = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
    auto* out = static_cast<u8*>(buffer);
    usize done = 0;

    while (done < length) {
        const u32 block_size = filesystem_->block_size();
        const u32 index = static_cast<u32>((offset + done) / block_size);
        const u32 inside = static_cast<u32>((offset + done) % block_size);

        const u32 block = block_for(index);
        if (block == 0)
            break;

        if (!filesystem_->read_block(block, scratch))
            break;

        const usize chunk = length - done < block_size - inside ? length - done
                                                                : block_size - inside;
        memcpy(out + done, scratch + inside, chunk);
        done += chunk;
    }

    mm::free_page(frame);
    return static_cast<isize>(done);
}

Inode* Ext2Inode::lookup(const char* name)
{
    if (type() != NodeType::Directory)
        return nullptr;

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return nullptr;

    auto* block = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
    Inode* found = nullptr;
    const u32 block_size = filesystem_->block_size();

    for (u32 index = 0; index * block_size < disk_.size && found == nullptr; ++index) {
        const u32 number = block_for(index);
        if (number == 0 || !filesystem_->read_block(number, block))
            break;

        u32 offset = 0;
        while (offset + sizeof(DirectoryEntry) < block_size) {
            const auto* entry = reinterpret_cast<const DirectoryEntry*>(block + offset);
            if (entry->length == 0)
                break;

            if (entry->inode != 0) {
                const char* entry_name = reinterpret_cast<const char*>(entry + 1);
                if (strlen(name) == entry->name_length
                    && memcmp(entry_name, name, entry->name_length) == 0) {
                    found = filesystem_->open(entry->inode);
                    break;
                }
            }

            offset += entry->length;
        }
    }

    mm::free_page(frame);
    return found;
}

const char* Ext2Inode::entry_at(usize index)
{
    if (type() != NodeType::Directory)
        return nullptr;

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return nullptr;

    auto* block = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
    const u32 block_size = filesystem_->block_size();
    const char* result = nullptr;
    usize seen = 0;

    for (u32 block_index = 0; block_index * block_size < disk_.size && result == nullptr;
         ++block_index) {
        const u32 number = block_for(block_index);
        if (number == 0 || !filesystem_->read_block(number, block))
            break;

        u32 offset = 0;
        while (offset + sizeof(DirectoryEntry) < block_size) {
            const auto* entry = reinterpret_cast<const DirectoryEntry*>(block + offset);
            if (entry->length == 0)
                break;

            if (entry->inode != 0) {
                if (seen == index) {
                    const char* name = reinterpret_cast<const char*>(entry + 1);
                    usize i = 0;
                    while (i + 1 < sizeof(entry_name_) && i < entry->name_length) {
                        entry_name_[i] = name[i];
                        ++i;
                    }
                    entry_name_[i] = '\0';
                    result = entry_name_;
                    break;
                }
                ++seen;
            }

            offset += entry->length;
        }
    }

    mm::free_page(frame);
    return result;
}

} // namespace

FileSystem* ext2_mount(BlockDevice* device)
{
    if (device == nullptr)
        return nullptr;

    if (!filesystem.mount(device))
        return nullptr;

    return &filesystem;
}

} // namespace eris::fs
