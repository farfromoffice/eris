// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/elf.hpp>
#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/process.hpp>
#include <eris/string.hpp>
#include <eris/thread.hpp>
#include <eris/vfs.hpp>

extern "C" void enter_user_mode(eris::u64 entry, eris::u64 stack);

namespace eris {
namespace {

constexpr usize max_processes = 8;
constexpr usize max_program_bytes = 1 << 20;

constinit Process processes[max_processes]{};
constinit usize process_total = 0;
constinit Process* running = nullptr;
constinit IrqSpinLock process_lock{};

struct ERIS_PACKED ProgramHeader {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
};

constexpr u32 program_load = 1;
constexpr u32 program_flag_execute = 1;
constexpr u32 program_flag_write = 2;

} // namespace

// Gives the scheduler and the fault handler a way to name whose program it is.
class ProcessTable {
public:
    static Process* allocate(const char* name);
    static bool load(Process& process, const u8* image, usize length);
    static bool map_stack(Process& process);
    static void activate(Process& process);
    static void release(Process& process);
};

Process* ProcessTable::allocate(const char* name)
{
    IrqGuard guard(process_lock);

    if (process_total >= max_processes)
        return nullptr;

    Process& process = processes[process_total];

    usize i = 0;
    while (i + 1 < Process::name_length && name[i] != '\0') {
        process.name_[i] = name[i];
        ++i;
    }
    process.name_[i] = '\0';

    process.id_ = static_cast<u32>(process_total + 1);
    process.state_ = ProcessState::Ready;
    process.exit_code_ = 0;
    process.page_count_ = 0;

    ++process_total;
    return &process;
}

// A process address space is the kernel one with a branch of its own, so a
// syscall does not have to switch tables to reach kernel memory.
bool ProcessTable::load(Process& process, const u8* image, usize length)
{
    if (length < sizeof(elf::Header))
        return false;

    const auto& header = *reinterpret_cast<const elf::Header*>(image);

    if (header.ident[0] != 0x7F || header.ident[1] != 'E' || header.ident[2] != 'L'
        || header.ident[3] != 'F' || header.machine != elf::machine_x86_64)
        return false;

    if (header.type != 2) {
        pr_err("proc: %s is not an executable\n", process.name_);
        return false;
    }

    const phys_addr root_frame = mm::alloc_page();
    if (root_frame == 0)
        return false;

    auto* root = reinterpret_cast<u64*>(mm::phys_to_virt(root_frame));
    memset(root, 0, page_size);

    // Everything the kernel maps lives under the first top level entry, and the
    // process shares it rather than keeping a copy.
    const auto* kernel_root = reinterpret_cast<const u64*>(
        mm::phys_to_virt(mm::AddressSpace::kernel().root()));
    root[0] = kernel_root[0];

    process.space_.adopt(root_frame);
    process.entry_ = header.entry;

    const auto* program_headers = reinterpret_cast<const ProgramHeader*>(
        image + header.program_header_offset);

    for (u16 i = 0; i < header.program_header_count; ++i) {
        const ProgramHeader& segment = program_headers[i];
        if (segment.type != program_load || segment.memory_size == 0)
            continue;

        if (segment.virtual_address < user_base) {
            pr_err("proc: %s wants to load at %lx, below the user base\n",
                   process.name_, segment.virtual_address);
            return false;
        }

        const virt_addr start = segment.virtual_address & ~(virt_addr{page_size} - 1);
        const usize pages = (segment.memory_size + (segment.virtual_address - start)
                             + page_size - 1) / page_size;

        mm::PageFlags flags = mm::PageFlags::User | mm::PageFlags::Write;
        if ((segment.flags & program_flag_execute) == 0)
            flags = flags | mm::PageFlags::NoExecute;

        for (usize page = 0; page < pages; ++page) {
            const phys_addr frame = mm::alloc_page();
            if (frame == 0)
                return false;

            memset(reinterpret_cast<void*>(mm::phys_to_virt(frame)), 0, page_size);

            if (!process.space_.map(start + page * page_size, frame, page_size, flags))
                return false;

            ++process.page_count_;
        }

        // The image is copied through the direct map, because the process
        // tables are not loaded yet.
        for (usize offset = 0; offset < segment.file_size; ++offset) {
            const virt_addr where = segment.virtual_address + offset;
            const phys_addr frame = process.space_.translate(where);
            if (frame == 0)
                return false;

            auto* target = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
            *target = image[segment.offset + offset];
        }

        const virt_addr end = segment.virtual_address + segment.memory_size;
        if (end > process.brk_)
            process.brk_ = end;

        // Read only segments lose write permission once they carry the image.
        if ((segment.flags & program_flag_write) == 0) {
            mm::PageFlags final_flags = mm::PageFlags::User;
            if ((segment.flags & program_flag_execute) == 0)
                final_flags = final_flags | mm::PageFlags::NoExecute;

            process.space_.protect(start, pages * page_size, final_flags);
        }
    }

    return true;
}

bool ProcessTable::map_stack(Process& process)
{
    const virt_addr base = user_stack_top - user_stack_pages * page_size;

    for (usize i = 0; i < user_stack_pages; ++i) {
        const phys_addr frame = mm::alloc_page();
        if (frame == 0)
            return false;

        memset(reinterpret_cast<void*>(mm::phys_to_virt(frame)), 0, page_size);

        if (!process.space_.map(base + i * page_size, frame, page_size,
                                mm::PageFlags::User | mm::PageFlags::Write
                                    | mm::PageFlags::NoExecute))
            return false;

        ++process.page_count_;
    }

    return true;
}

void ProcessTable::activate(Process& process)
{
    running = &process;
    process.state_ = ProcessState::Running;
    process.space_.activate();
}

void ProcessTable::release(Process& process)
{
    running = nullptr;
    mm::AddressSpace::kernel().activate();
}

bool Process::owns(virt_addr address, usize length) const
{
    if (address < user_base || length > max_program_bytes)
        return false;

    return space_.translate(address) != 0 && space_.translate(address + length - 1) != 0;
}

void Process::exit(int code)
{
    exit_code_ = code;
    state_ = ProcessState::Exited;
}

Process* current_process()
{
    return running;
}

usize process_count()
{
    return process_total;
}

Process* process_at(usize index)
{
    return index < process_total ? &processes[index] : nullptr;
}

// Reads the program, builds its address space and drops into ring 3. Returns
// when the program leaves, which today means it called exit or faulted.
int process_run(const char* path, int& exit_code)
{
    fs::Stat status{};
    if (fs::stat(path, status) != 0) {
        pr_err("proc: %s does not exist\n", path);
        return -1;
    }

    if (status.size == 0 || status.size > max_program_bytes) {
        pr_err("proc: %s is %lu bytes, which is not a program\n", path, status.size);
        return -1;
    }

    auto* image = static_cast<u8*>(kmalloc(static_cast<usize>(status.size)));
    if (image == nullptr)
        return -1;

    const isize read = fs::read_file(path, image, static_cast<usize>(status.size));
    if (read <= 0) {
        kfree(image);
        return -1;
    }

    const char* name = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/')
            name = p + 1;
    }

    Process* process = ProcessTable::allocate(name);
    if (process == nullptr) {
        kfree(image);
        return -1;
    }

    if (!ProcessTable::load(*process, image, static_cast<usize>(read))
        || !ProcessTable::map_stack(*process)) {
        pr_err("proc: %s could not be loaded\n", path);
        kfree(image);
        return -1;
    }

    kfree(image);

    pr_info("proc: %s starts at %lx with %lu pages\n",
            process->name(), process->entry(), process->pages());

    // A stack of its own for what the program asks for. Borrowing the frame
    // this function is running on puts the pushes straight through its locals.
    constexpr usize syscall_stack_pages = 4;
    const phys_addr syscall_stack = mm::alloc_pages(syscall_stack_pages);
    if (syscall_stack == 0)
        return -1;

    const virt_addr syscall_stack_top = mm::phys_to_virt(syscall_stack)
        + syscall_stack_pages * page_size;

    if (Thread* self = current_thread(); self != nullptr) {
        self->set_syscall_stack(syscall_stack_top);
        self->set_address_space(process->space_root());
    }

    arch::tss_set_kernel_stack(syscall_stack_top);

    // A tick that lands in ring 3 pushes its frame onto this thread's syscall
    // stack, so a switch saves and restores it like any other kernel context
    // and the program resumes through the same interrupt return.
    ProcessTable::activate(*process);
    enter_user_mode(process->entry(), user_stack_top - 64);

    ProcessTable::release(*process);

    if (Thread* self = current_thread(); self != nullptr) {
        self->set_syscall_stack(0);
        self->set_address_space(0);
    }

    mm::free_pages(syscall_stack, syscall_stack_pages);

    exit_code = process->exit_code();
    pr_info("proc: %s left with code %d\n", process->name(), exit_code);
    return 0;
}

} // namespace eris
