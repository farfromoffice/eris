// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/export.hpp>
#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include "virtio_blk.hpp"

namespace eris::modules {
namespace {

constexpr u16 vendor_virtio = 0x1AF4;
constexpr u16 device_legacy_blk = 0x1001;
constexpr u16 device_modern_blk = 0x1042;

// Legacy virtio over port IO. The modern layout hides the same registers behind
// PCI capabilities, and this driver asks QEMU for the simple one.
constexpr u16 reg_device_features = 0x00;
constexpr u16 reg_driver_features = 0x04;
constexpr u16 reg_queue_address = 0x08;
constexpr u16 reg_queue_size = 0x0C;
constexpr u16 reg_queue_select = 0x0E;
constexpr u16 reg_queue_notify = 0x10;
constexpr u16 reg_status = 0x12;
constexpr u16 reg_isr = 0x13;
constexpr u16 reg_config = 0x14;

constexpr u8 status_acknowledge = 1;
constexpr u8 status_driver = 2;
constexpr u8 status_driver_ok = 4;
constexpr u8 status_failed = 128;

constexpr u16 descriptor_next = 1;
constexpr u16 descriptor_write = 2;

constexpr u32 request_in = 0;
constexpr u32 request_out = 1;

constexpr usize sector_size = 512;

struct ERIS_PACKED Descriptor {
    u64 address;
    u32 length;
    u16 flags;
    u16 next;
};

struct ERIS_PACKED Available {
    u16 flags;
    u16 index;
    u16 ring[];
};

struct ERIS_PACKED UsedElement {
    u32 id;
    u32 length;
};

struct ERIS_PACKED Used {
    u16 flags;
    u16 index;
    UsedElement ring[];
};

struct ERIS_PACKED RequestHeader {
    u32 type;
    u32 reserved;
    u64 sector;
};

// One virtqueue, laid out the way the legacy specification asks: descriptors,
// then the available ring, then the used ring on the next page.
class Queue {
public:
    bool setup(u16 port, u16 index);
    void teardown();

    bool submit(u32 type, u64 sector, void* buffer, usize length, bool write);

    u16 size() const { return size_; }

private:
    u16 port_ = 0;
    u16 index_ = 0;
    u16 size_ = 0;
    u16 next_descriptor_ = 0;
    u16 last_used_ = 0;

    u64 frames_ = 0;
    usize pages_ = 0;

    Descriptor* descriptors_ = nullptr;
    Available* available_ = nullptr;
    Used* used_ = nullptr;
};

constinit Queue queue{};
constinit u16 io_base = 0;
constinit u64 capacity_sectors = 0;
constinit bool attached = false;

usize align_up(usize value, usize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

bool Queue::setup(u16 port, u16 index)
{
    port_ = port;
    index_ = index;

    eris_outw(port_ + reg_queue_select, index_);
    size_ = eris_inw(port_ + reg_queue_size);

    if (size_ == 0) {
        pr_module_err("virtio_blk: queue %u does not exist\n", index_);
        return false;
    }

    const usize descriptor_bytes = sizeof(Descriptor) * size_;
    const usize available_bytes = sizeof(u16) * (3 + size_);
    const usize used_offset = align_up(descriptor_bytes + available_bytes, 4096);
    const usize used_bytes = sizeof(u16) * 3 + sizeof(UsedElement) * size_;

    pages_ = (used_offset + align_up(used_bytes, 4096)) / 4096;
    frames_ = eris_alloc_pages(pages_);
    if (frames_ == 0) {
        pr_module_err("virtio_blk: no memory for a %u entry queue\n", size_);
        return false;
    }

    auto* base = reinterpret_cast<u8*>(eris_phys_to_virt(frames_));
    for (usize i = 0; i < pages_ * 4096; ++i)
        base[i] = 0;

    descriptors_ = reinterpret_cast<Descriptor*>(base);
    available_ = reinterpret_cast<Available*>(base + descriptor_bytes);
    used_ = reinterpret_cast<Used*>(base + used_offset);

    // The device is told where the ring lives as a page frame number.
    eris_outl(port_ + reg_queue_address, static_cast<u32>(frames_ / 4096));

    next_descriptor_ = 0;
    last_used_ = 0;
    return true;
}

void Queue::teardown()
{
    if (frames_ == 0)
        return;

    eris_outw(port_ + reg_queue_select, index_);
    eris_outl(port_ + reg_queue_address, 0);

    eris_free_pages(frames_, pages_);
    frames_ = 0;
    pages_ = 0;
}

// Header, payload and status byte, chained, then wait for the device to hand
// the chain back on the used ring.
bool Queue::submit(u32 type, u64 sector, void* buffer, usize length, bool write)
{
    if (size_ < 3)
        return false;

    const u64 header_frame = eris_alloc_pages(1);
    if (header_frame == 0)
        return false;

    auto* header = reinterpret_cast<RequestHeader*>(eris_phys_to_virt(header_frame));
    header->type = type;
    header->reserved = 0;
    header->sector = sector;

    auto* status = reinterpret_cast<u8*>(eris_phys_to_virt(header_frame) + 256);
    *status = 0xFF;

    const u64 payload = eris_virt_to_phys(reinterpret_cast<u64>(buffer));

    const u16 first = next_descriptor_;
    const u16 second = static_cast<u16>((first + 1) % size_);
    const u16 third = static_cast<u16>((first + 2) % size_);
    next_descriptor_ = static_cast<u16>((first + 3) % size_);

    descriptors_[first] = Descriptor{header_frame, sizeof(RequestHeader), descriptor_next, second};
    descriptors_[second] = Descriptor{
        payload,
        static_cast<u32>(length),
        static_cast<u16>(descriptor_next | (write ? 0 : descriptor_write)),
        third,
    };
    descriptors_[third] = Descriptor{header_frame + 256, 1, descriptor_write, 0};

    available_->ring[available_->index % size_] = first;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    available_->index = static_cast<u16>(available_->index + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    eris_outw(port_ + reg_queue_notify, index_);

    const u64 deadline = eris_monotonic_ns() + 2000000000ULL;
    while (used_->index == last_used_) {
        if (eris_monotonic_ns() > deadline) {
            pr_module_err("virtio_blk: the device did not answer in two seconds\n");
            eris_free_pages(header_frame, 1);
            return false;
        }
        eris_yield();
    }

    last_used_ = used_->index;
    const bool ok = *status == 0;

    if (!ok)
        pr_module_err("virtio_blk: request failed with status %u\n", *status);

    eris_free_pages(header_frame, 1);
    return ok;
}

bool find_device(usize& index, u16& port)
{
    for (usize i = 0; i < eris_pci_device_count(); ++i) {
        u16 vendor = 0;
        u16 device = 0;

        if (!eris_pci_device_at(i, &vendor, &device, nullptr, nullptr, nullptr))
            continue;

        if (vendor != vendor_virtio)
            continue;

        if (device != device_legacy_blk && device != device_modern_blk)
            continue;

        u64 length = 0;
        bool memory = true;
        const u64 bar = eris_pci_bar(i, 0, &length, &memory);

        if (memory || bar == 0) {
            pr_module_err("virtio_blk: %x:%x is not the legacy port layout\n", vendor, device);
            continue;
        }

        index = i;
        port = static_cast<u16>(bar);
        return true;
    }

    return false;
}

int virtio_blk_init()
{
    usize index = 0;
    if (!find_device(index, io_base)) {
        // A driver without hardware is idle, not broken. Staying loaded means
        // it is ready if the device shows up on the next boot.
        pr_module_info("virtio_blk: no device on this machine\n");
        return 0;
    }

    eris_pci_enable(index);

    eris_outb(io_base + reg_status, 0);
    eris_outb(io_base + reg_status, status_acknowledge);
    eris_outb(io_base + reg_status, status_acknowledge | status_driver);

    // Nothing optional is needed for reading and writing whole sectors.
    const u32 offered = eris_inl(io_base + reg_device_features);
    eris_outl(io_base + reg_driver_features, 0);

    if (!queue.setup(io_base, 0)) {
        eris_outb(io_base + reg_status, status_failed);
        return -1;
    }

    eris_outb(io_base + reg_status, status_acknowledge | status_driver | status_driver_ok);

    const u32 capacity_low = eris_inl(io_base + reg_config);
    const u32 capacity_high = eris_inl(io_base + reg_config + 4);
    capacity_sectors = (static_cast<u64>(capacity_high) << 32) | capacity_low;

    attached = true;

    pr_module_info("virtio_blk: %lu sectors, %lu MiB, queue of %u, features %x\n",
                   capacity_sectors,
                   capacity_sectors * sector_size / (1024 * 1024),
                   queue.size(),
                   offered);
    return 0;
}

void virtio_blk_exit()
{
    if (!attached)
        return;

    eris_outb(io_base + reg_status, 0);
    queue.teardown();
    attached = false;
}

} // namespace
} // namespace eris::modules

extern "C" {

bool virtio_blk_present()
{
    return eris::modules::attached;
}

eris::u64 virtio_blk_capacity()
{
    return eris::modules::capacity_sectors;
}

bool virtio_blk_read(eris::u64 sector, void* buffer, eris::usize sectors)
{
    using namespace eris::modules;

    if (!attached || sectors == 0)
        return false;

    return queue.submit(request_in, sector, buffer, sectors * sector_size, false);
}

bool virtio_blk_write(eris::u64 sector, const void* buffer, eris::usize sectors)
{
    using namespace eris::modules;

    if (!attached || sectors == 0)
        return false;

    return queue.submit(request_out, sector, const_cast<void*>(buffer),
                        sectors * sector_size, true);
}

}

ERIS_EXPORT_SYMBOL(virtio_blk_present);
ERIS_EXPORT_SYMBOL(virtio_blk_capacity);
ERIS_EXPORT_SYMBOL(virtio_blk_read);
ERIS_EXPORT_SYMBOL(virtio_blk_write);

ERIS_MODULE("virtio_blk", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::virtio_blk_init, eris::modules::virtio_blk_exit);
