// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cmdline.hpp>
#include <eris/compiler.hpp>
#include <eris/console.hpp>
#include <eris/cpu.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/acpi.hpp>
#include <eris/block.hpp>
#include <eris/devfs.hpp>
#include <eris/ext2.hpp>
#include <eris/ramfs.hpp>
#include <eris/vfs.hpp>
#include <eris/elf.hpp>
#include <eris/export.hpp>
#include <eris/initrd.hpp>
#include <eris/atomic.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/pci.hpp>
#include <eris/module.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/process.hpp>
#include <eris/string.hpp>
#include <eris/serial.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>
#include <eris/work.hpp>
#include <eris/version.hpp>

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info);

namespace eris {
namespace {



void print_banner()
{
    printk(LogLevel::Info, "eris %s \"%s\" (%s, %s)\n",
           version_string,
           version_name,
           version_arch,
           version_language);
}

// Recursion the optimiser cannot turn into a loop, so the stack really grows.
// The warning about it is the point of the function.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
ERIS_NOINLINE u64 overflow_stack(u64 depth)
{
    volatile u8 filler[256];
    filler[0] = static_cast<u8>(depth);

    // Handing the frame address to the compiler as an opaque value stops it
    // from rewriting this into a loop that never grows the stack.
    asm volatile("" : : "r"(&filler[0]) : "memory");

    return filler[0] + overflow_stack(depth + 1);
}
#pragma GCC diagnostic pop

// Drives the panic path on purpose, so the machinery that reports a fault is
// tested rather than assumed. Selected with fault=<kind> on the command line.
void inject_fault(const char* kind)
{
    pr_warn("injecting the %s fault\n", kind);

    if (strcmp(kind, "unmapped") == 0) {
        auto* target = reinterpret_cast<volatile u64*>(0xFFFF800000000000);
        *target = 1;
    } else if (strcmp(kind, "opcode") == 0) {
        asm volatile("ud2");
    } else if (strcmp(kind, "divide") == 0) {
        asm volatile("xor %%edx, %%edx\n"
                     "mov $1, %%eax\n"
                     "xor %%ecx, %%ecx\n"
                     "div %%ecx\n"
                     :
                     :
                     : "eax", "ecx", "edx");
    } else if (strcmp(kind, "doublefault") == 0) {
        // Point the stack at unmapped memory, then fault. The page fault
        // cannot be delivered because pushing its frame faults as well, which
        // is exactly what the double fault stack exists for.
        asm volatile("mov $0xdeadbe000, %rsp\n"
                     "push $0\n");
    } else if (strcmp(kind, "text") == 0) {
        // W^X means this store has to fault even in ring 0, which only holds
        // while CR0.WP is set.
        auto* code = reinterpret_cast<volatile u8*>(&kernel_main);
        *code = 0xCC;
    } else if (strcmp(kind, "rodata") == 0) {
        auto* constant = const_cast<volatile char*>(version_name);
        *constant = 'x';
    } else if (strcmp(kind, "stack") == 0) {
        overflow_stack(0);
    } else if (strcmp(kind, "panic") == 0) {
        panic("fault injection asked for a panic");
    } else {
        pr_err("unknown fault kind %s\n", kind);
    }
}

// Proves the heap really commits new pages instead of living inside whatever
// the first reservation happened to cover.
void heap_selftest()
{
    constexpr usize chunk = 512 * 1024;
    constexpr usize chunks = 8;

    void* blocks[chunks]{};
    const usize before = heap_capacity();

    for (usize i = 0; i < chunks; ++i) {
        blocks[i] = kmalloc(chunk);
        if (blocks[i] == nullptr) {
            pr_err("heap selftest: allocation %lu failed\n", static_cast<u64>(i));
            return;
        }
        memset(blocks[i], 0x5A, chunk);
    }

    const usize peak = heap_capacity();

    for (usize i = 0; i < chunks; ++i)
        kfree(blocks[i]);

    pr_info("heap selftest: %lu KiB committed, grew from %lu to %lu KiB, %lu KiB in use\n",
            static_cast<u64>((peak - before) / 1024),
            static_cast<u64>(before / 1024),
            static_cast<u64>(peak / 1024),
            static_cast<u64>(heap_used() / 1024));
}

constinit volatile bool timer_fired = false;
constinit volatile u64 timer_fired_at = 0;

void mark_fired(void*)
{
    timer_fired = true;
    timer_fired_at = monotonic_ns();
}

// Checks that the clock moves at the rate it claims and that a deadline lands
// where it was asked to.
void time_selftest()
{
    const u64 start = monotonic_ns();
    mdelay(200);
    const u64 elapsed = monotonic_ns() - start;

    pr_info("time selftest: 200 ms delay measured %lu us on the %s clock\n",
            (elapsed + 500) / 1000,
            clock_source());

    const u64 armed = monotonic_ns();
    timer_after(50000000, mark_fired, nullptr);

    while (!timer_fired && monotonic_ns() - armed < 500000000)
        arch::hlt();

    if (!timer_fired) {
        pr_err("time selftest: the 50 ms timer never fired\n");
        return;
    }

    pr_info("time selftest: 50 ms timer fired after %lu us, jiffies at %lu\n",
            (timer_fired_at - armed + 500) / 1000,
            ticks());
}

constinit Atomic<u64> smp_hits{0};
constinit Atomic<u32> smp_workers_done{0};

// Every core hammers the same refcount and the same allocator, which is the
// cheapest way to find out whether the locks added in this phase are real.
void smp_worker()
{
    constexpr u64 rounds = 20000;

    for (u64 i = 0; i < rounds; ++i) {
        if (module_get("vga")) {
            module_put("vga");
            smp_hits.fetch_add(1);
        }

        if (void* block = kmalloc(64); block != nullptr)
            kfree(block);
    }
}

void smp_worker_thread(void*)
{
    smp_worker();
    smp_workers_done.fetch_add(1);
}

void smp_selftest()
{
    const Module* vga = module_find("vga");
    const u32 before = vga != nullptr ? vga->users.value() : 0;

    // One worker per core, handed to the scheduler rather than to the idle
    // loops, because a core running a thread is not watching for a job.
    const usize workers = arch::cpu_online_count();
    smp_workers_done.store(0);

    for (usize i = 0; i < workers; ++i)
        Thread::spawn("stress", smp_worker_thread, nullptr);

    const u64 deadline = monotonic_ns() + 10000000000ULL;
    while (smp_workers_done.load() < workers && monotonic_ns() < deadline)
        yield();

    const bool answered = smp_workers_done.load() >= workers;
    const u32 after = vga != nullptr ? vga->users.value() : 0;

    pr_info("smp selftest: %lu cores, %lu refcount round trips, vga users %u then %u%s\n",
            static_cast<u64>(workers),
            smp_hits.load(),
            before,
            after,
            answered ? "" : ", a worker never finished");

    if (before != after)
        pr_err("smp selftest: the refcount did not come back to where it started\n");
}

constinit Atomic<u64> worker_rounds{0};
constinit Atomic<u32> workers_finished{0};
constinit WaitQueue gate{};
constinit Atomic<bool> gate_opened{false};

void counting_worker(void* argument)
{
    const auto rounds = reinterpret_cast<u64>(argument);

    for (u64 i = 0; i < rounds; ++i) {
        worker_rounds.fetch_add(1);
        if ((i % 64) == 0)
            yield();
    }

    workers_finished.fetch_add(1);
}

void sleeping_worker(void* argument)
{
    const auto milliseconds = reinterpret_cast<u64>(argument);
    const u64 start = monotonic_ns();

    thread_sleep_ms(milliseconds);

    const u64 slept = (monotonic_ns() - start) / 1000000;
    pr_info("thread selftest: %s asked for %lu ms and slept %lu ms\n",
            current_thread()->name(), milliseconds, slept);

    workers_finished.fetch_add(1);
}

void blocked_worker(void*)
{
    gate.wait();

    if (!gate_opened.load())
        pr_err("thread selftest: a thread woke before the gate opened\n");

    workers_finished.fetch_add(1);
}

// Threads that do nothing prove nothing: these count, sleep and block, and the
// numbers have to add up at the end.
void thread_selftest()
{
    constexpr u64 rounds = 20000;
    constexpr u32 counters = 4;

    workers_finished.store(0);
    worker_rounds.store(0);

    for (u32 i = 0; i < counters; ++i)
        Thread::spawn("counter", counting_worker, reinterpret_cast<void*>(rounds));

    Thread::spawn("sleeper", sleeping_worker, reinterpret_cast<void*>(u64{50}));
    Thread::spawn("waiter", blocked_worker, nullptr);

    const u64 deadline = monotonic_ns() + 3000000000ULL;
    while (workers_finished.load() < counters + 1 && monotonic_ns() < deadline)
        yield();

    gate_opened.store(true);
    gate.wake_all();

    while (workers_finished.load() < counters + 2 && monotonic_ns() < deadline)
        yield();

    pr_info("thread selftest: %u of %u threads finished, %lu rounds counted, %lu threads alive\n",
            workers_finished.load(),
            counters + 2,
            worker_rounds.load(),
            static_cast<u64>(thread_count()));

    if (worker_rounds.load() != rounds * counters)
        pr_err("thread selftest: lost %lu rounds\n", rounds * counters - worker_rounds.load());
}

void report_modules();
void load_initrd_modules();
void mount_root();
void mount_storage();
void start_init();
void module_selftest();
void block_selftest();
void fs_selftest();
void time_selftest();
void smp_selftest();
void thread_selftest();
void inject_fault(const char* kind);

// The second half of the boot sequence, running as the first kernel thread.
void kernel_init(void*)
{
    if (cmdline_has("timetest"))
        time_selftest();

    module_init_builtin();

    // The bus is walked before the loadable modules arrive, so a driver finds
    // its device the moment its init runs.
    pci::init();

    // The root comes up before the loadable modules, because a module that
    // needs a file, like the desktop and its fonts, has to find one.
    mount_root();
    load_initrd_modules();
    mount_storage();

    report_modules();
    start_init();

    if (cmdline_has("smptest"))
        smp_selftest();

    if (cmdline_has("threadtest"))
        thread_selftest();

    if (cmdline_has("kotest"))
        module_selftest();

    if (cmdline_has("blktest"))
        block_selftest();

    if (cmdline_has("fstest"))
        fs_selftest();

    if (cmdline_has("spintest")) {
        int code = 0;
        syscall_init();

        const u64 before = ticks();
        process_run("/spin", code);
        pr_info("spintest: %lu timer ticks passed while it ran, exit code %d\n",
                ticks() - before, code);
    }

    if (cmdline_has("crashtest")) {
        int code = 0;
        syscall_init();
        process_run("/crash", code);
        pr_info("crashtest: the kernel is still running after the program died\n");
    }

    if (cmdline_has("test_exit")) {
        pr_info("selftests finished, leaving\n");
        arch::outb(0xF4, 0x10);
    }

    if (const char* kind = cmdline_value("fault"); kind != nullptr)
        inject_fault(kind);
}

ERIS_NORETURN void idle_loop()
{
    for (;;) {
        work_run_pending();
        yield();
        arch::hlt();
    }
}

bool blacklisted(const char* name)
{
    const char* list = cmdline_value("modules.blacklist");
    if (list == nullptr)
        return false;

    const usize length = strlen(name);
    for (const char* p = list; *p != '\0';) {
        const char* start = p;
        while (*p != '\0' && *p != ',')
            ++p;

        if (static_cast<usize>(p - start) == length && memcmp(start, name, length) == 0)
            return true;

        if (*p == ',')
            ++p;
    }

    return false;
}

// Everything the initrd carries is loaded unless the command line says
// otherwise, and then whatever registered gets its init run.
void load_initrd_modules()
{
    if (!initrd_available() || cmdline_has("modules.noload"))
        return;

    constexpr usize max_images = 64;

    const void* images[max_images]{};
    usize lengths[max_images]{};
    const char* names[max_images]{};
    usize image_count = 0;

    for (usize i = 0; i < initrd_file_count() && image_count < max_images; ++i) {
        const char* name = nullptr;
        usize length = 0;

        const void* data = initrd_data_at(i, name, length);
        if (data == nullptr || length == 0 || name == nullptr)
            continue;

        // The archive carries programs as well, and those are not modules.
        if (memcmp(name, "modules/", 8) != 0)
            continue;

        images[image_count] = data;
        lengths[image_count] = length;
        names[image_count] = name;
        ++image_count;
    }

    // An image is relocated against the symbols already exported, so one that
    // calls into another module has to arrive after it. Rather than depend on
    // the order the archive happens to have, the pass repeats while it is
    // still making progress.
    for (;;) {
        usize mapped = 0;

        for (usize i = 0; i < image_count; ++i) {
            if (images[i] == nullptr)
                continue;

            if (module_load_image(images[i], lengths[i], names[i]) != 0)
                continue;

            images[i] = nullptr;
            ++mapped;
        }

        if (mapped == 0)
            break;
    }

    for (usize i = 0; i < module_count(); ++i) {
        Module* module = module_at(i);
        if (module == nullptr || module->state != ModuleState::Registered)
            continue;

        if (blacklisted(module->info->name)) {
            pr_info("module %s is blacklisted, left alone\n", module->info->name);
            continue;
        }

        module_load(module->info->name);
    }
}

// Loads, unloads and reloads a module that was never linked into the image,
// then checks the pages came back and that a stale ABI stamp is refused.
void module_selftest()
{
    // A leaf is the only honest guinea pig here: a module something else
    // depends on refuses to unload, and rightly so.
    const char* name = "virtio_blk";

    const char* file = "virtio_blk.ko";
    usize length = 0;
    const void* image = initrd_find("virtio_blk", length);

    if (image == nullptr) {
        pr_err("module selftest: the initrd carries nothing to load\n");
        return;
    }

    const Module* module = module_find(name);
    if (module == nullptr || module->state != ModuleState::Ready) {
        pr_err("module selftest: %s did not load from the initrd\n", name);
        return;
    }

    const usize before_unload = mm::free_pages_count();
    if (module_unload(name) != 0) {
        pr_err("module selftest: %s refused to unload\n", name);
        return;
    }

    const usize after_unload = mm::free_pages_count();
    if (module_find(name) != nullptr)
        pr_err("module selftest: %s is still in the table after unloading\n", name);

    pr_info("module selftest: unloading %s returned %lu pages\n",
            name, static_cast<u64>(after_unload - before_unload));

    // A copy with the wrong stamp has to be refused rather than run.
    auto* copy = static_cast<u8*>(kmalloc(length));
    if (copy != nullptr) {
        memcpy(copy, image, length);
        const auto* header = reinterpret_cast<const elf::Header*>(copy);
        const auto* sections = reinterpret_cast<const elf::SectionHeader*>(
            copy + header->section_header_offset);

        for (u16 i = 0; i < header->section_header_count; ++i) {
            if (sections[i].size >= sizeof(ModuleInfo) && sections[i].type == elf::section_progbits
                && sections[i].size % sizeof(ModuleInfo) == 0) {
                auto* candidate = reinterpret_cast<u32*>(copy + sections[i].offset);
                if (*candidate == module_abi_version)
                    *candidate = module_abi_version + 1;
            }
        }

        if (module_load_image(copy, length, "a module from the future") == 0)
            pr_err("module selftest: an image with the wrong abi was accepted\n");
        else
            pr_info("module selftest: an image with the wrong abi was refused\n");

        kfree(copy);
    }

    if (module_load_image(image, length, file) != 0 || module_load(name) != 0) {
        pr_err("module selftest: %s did not come back\n", name);
        return;
    }

    const usize after_reload = mm::free_pages_count();
    pr_info("module selftest: %s reloaded, %lu pages free before and %lu after the round trip\n",
            name,
            static_cast<u64>(before_unload),
            static_cast<u64>(after_reload));
}

// Calls a module through the export table, which is the only way the kernel is
// allowed to reach code it was not linked with.
void block_selftest()
{
    using ReadFn = bool (*)(u64, void*, usize);
    using WriteFn = bool (*)(u64, const void*, usize);
    using CapacityFn = u64 (*)();

    auto read = reinterpret_cast<ReadFn>(symbol_lookup("virtio_blk_read"));
    auto write = reinterpret_cast<WriteFn>(symbol_lookup("virtio_blk_write"));
    auto capacity = reinterpret_cast<CapacityFn>(symbol_lookup("virtio_blk_capacity"));

    if (read == nullptr || write == nullptr || capacity == nullptr) {
        pr_err("block selftest: the driver exports nothing to call\n");
        return;
    }

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return;

    auto* buffer = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
    memset(buffer, 0, page_size);

    if (!read(0, buffer, 1)) {
        pr_err("block selftest: reading sector 0 failed\n");
        mm::free_page(frame);
        return;
    }

    buffer[31] = '\0';
    pr_info("block selftest: %lu sectors, sector 0 reads \"%s\"\n", capacity(), buffer);

    // Write a sector and read it back, which is the only way to know the
    // descriptor chain is right in both directions.
    constexpr const char* marker = "written by the block selftest";
    memset(buffer, 0, page_size);
    memcpy(buffer, marker, strlen(marker) + 1);

    if (!write(4, buffer, 1)) {
        pr_err("block selftest: writing sector 4 failed\n");
        mm::free_page(frame);
        return;
    }

    memset(buffer, 0, page_size);
    if (!read(4, buffer, 1)) {
        pr_err("block selftest: reading sector 4 back failed\n");
        mm::free_page(frame);
        return;
    }

    if (strcmp(reinterpret_cast<const char*>(buffer), marker) != 0)
        pr_err("block selftest: the sector came back as \"%s\"\n", buffer);
    else
        pr_info("block selftest: sector 4 came back byte for byte\n");

    mm::free_page(frame);
}

// The root is in memory and holds whatever the boot loader handed over, so it
// exists before anything that might want to read a file.
void mount_root()
{
    fs::init();
    fs::mount("/", fs::ramfs_create());

    for (usize i = 0; i < initrd_file_count(); ++i) {
        const char* name = nullptr;
        usize length = 0;

        const void* image = initrd_data_at(i, name, length);
        if (image == nullptr || name == nullptr || length == 0)
            continue;

        const char* base = name;
        for (const char* p = name; *p != '\0'; ++p) {
            if (*p == '/')
                base = p + 1;
        }

        const bool program = memcmp(name, "bin/", 4) == 0;
        const bool font = memcmp(name, "fonts/", 6) == 0;

        if (!program && !font)
            continue;

        if (fs::Inode* file = fs::ramfs_create_file(base); file != nullptr)
            file->write(0, image, length);
    }
}

// Storage arrives through modules, so this runs after they have loaded: adopt
// whatever they registered, then put a file system on top of it.
void mount_storage()
{
    block_register_module("vda", "virtio_blk_read", "virtio_blk_write", "virtio_blk_capacity");

    fs::mount("/dev", fs::devfs_create());

    if (BlockDevice* disk = block_device_at(0); disk != nullptr) {
        if (fs::FileSystem* ext2 = fs::ext2_mount(disk); ext2 != nullptr)
            fs::mount("/mnt", ext2);
    }

    for (usize i = 0; i < fs::mount_count(); ++i) {
        const char* kind = nullptr;
        const char* path = fs::mount_at(i, kind);
        if (path != nullptr)
            pr_info("vfs: %s holds %s\n", path, kind);
    }
}

// Exercises all three mounts: a file in memory, a device node and a real file
// read off a disk through the driver that was loaded from the initrd.
void fs_selftest()
{
    char buffer[128]{};

    if (fs::Inode* note = fs::ramfs_create_file("notes.txt"); note != nullptr) {
        constexpr const char* text = "written into ramfs";
        note->write(0, text, strlen(text));
    }

    const isize read_back = fs::read_file("/notes.txt", buffer, sizeof(buffer) - 1);
    if (read_back > 0) {
        buffer[read_back] = '\0';
        pr_info("fs selftest: /notes.txt reads \"%s\"\n", buffer);
    } else {
        pr_err("fs selftest: the ramfs file did not come back\n");
    }

    for (usize i = 0; i < 8; ++i) {
        const char* entry = fs::list("/dev", i);
        if (entry == nullptr)
            break;
        pr_info("fs selftest: /dev holds %s\n", entry);
    }

    fs::Stat status{};
    if (fs::stat("/mnt/hello.txt", status) != 0) {
        pr_info("fs selftest: no disk file to read, the mount is empty\n");
        return;
    }

    memset(buffer, 0, sizeof(buffer));
    const isize from_disk = fs::read_file("/mnt/hello.txt", buffer, sizeof(buffer) - 1);

    if (from_disk <= 0) {
        pr_err("fs selftest: reading /mnt/hello.txt failed\n");
        return;
    }

    for (isize i = 0; i < from_disk; ++i) {
        if (buffer[i] == '\n')
            buffer[i] = '\0';
    }

    pr_info("fs selftest: /mnt/hello.txt is %lu bytes and reads \"%s\"\n", status.size, buffer);

    for (usize i = 0; i < 8; ++i) {
        const char* entry = fs::list("/mnt", i);
        if (entry == nullptr)
            break;
        pr_info("fs selftest: /mnt holds %s\n", entry);
    }

    constexpr const char* through_console = "fs selftest: this line went through /dev/console\n";
    fs::write_file("/dev/console", through_console, strlen(through_console));
}

// The first program lives in the root like everything else, so starting it is
// a path lookup rather than a special case.
void start_init()
{
    const char* path = cmdline_value("init");
    if (path == nullptr)
        path = "/init";

    if (cmdline_has("noinit"))
        return;

    fs::Stat status{};
    if (fs::stat(path, status) != 0) {
        pr_info("init: %s is not there, staying in the kernel\n", path);
        return;
    }

    syscall_init();

    int code = 0;
    if (process_run(path, code) != 0)
        pr_err("init: %s did not run\n", path);
}

void report_memory()
{
    const auto free = mm::free_pages_count();
    pr_info("memory: %lu pages total, %lu free (%lu MiB)\n",
            static_cast<u64>(mm::total_pages()),
            static_cast<u64>(free),
            static_cast<u64>(free * page_size / (1024 * 1024)));
    pr_info("heap: %lu KiB\n", static_cast<u64>(heap_capacity() / 1024));
}

void report_modules()
{
    pr_info("modules: %lu registered, %lu symbols exported\n",
            static_cast<u64>(module_count()),
            static_cast<u64>(symbol_count()));

    for (usize i = 0; i < module_count(); ++i) {
        const Module* module = module_at(i);
        pr_info("  %s %s [%s] dependents=%u users=%u license=%s\n",
                module->info->name,
                module->info->version,
                module_state_name(module->state),
                module->dependents.value(),
                module->users.value(),
                module->info->license);
    }

    if (kernel_tainted())
        pr_warn("kernel is tainted by a non free module\n");
}

} // namespace

void start_kernel(u32 multiboot_magic, u64 multiboot_info)
{
    arch::percpu_setup(0, 0);

    serial_init();
    print_banner();

    cmdline_init(multiboot_magic, multiboot_info);

    arch::gdt_init();
    arch::tss_init();
    arch::idt_init();

    mm::page_alloc_init(multiboot_magic, multiboot_info);
    mm::paging_init();
    mm::heap_init();

    initrd_init(multiboot_magic, multiboot_info);
    acpi::init();
    arch::irq_init();
    clock_init();
    report_memory();

    if (cmdline_has("mmtest"))
        heap_selftest();

    timers_init();
    arch::sti();

    arch::smp_init();
    sched_init();

    // Everything from here runs in a thread, so a module init is allowed to
    // sleep on hardware instead of spinning on it.
    work_start();

    if (Thread::spawn("kinit", kernel_init, nullptr) == nullptr)
        panic("kinit: no thread to finish booting in");

    idle_loop();
}

} // namespace eris

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info)
{
    eris::start_kernel(multiboot_magic, multiboot_info);
}
