#include <core/exec/syscalls.h>
#include <memory/vmm.h>
#include <utils/log.h>
#include <fs/ext2.h>
#include <core/exec/scheduler.h>
#include <stdint.h>
#include <lib/string.h>

ext2_fs_t ext2_fs;

// MSR addresses for syscall/sysret
#define IA32_STAR  0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084

// Syscall handler function
extern void syscall_entry(void);

// Read from an MSR
static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Write to an MSR
static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

// Initialize syscalls for x86_64
void syscalls_init(void) {
    // Set the STAR MSR:
    // - CS = 0x08 (kernel code segment)
    // - SS = 0x10 (kernel data segment)
    // - User CS = 0x18 | 3 (user code segment with RPL 3)
    // - User SS = 0x20 | 3 (user data segment with RPL 3)
    uint64_t star_value = ((uint64_t)0x18 << 48) | ((uint64_t)0x08 << 32);
    write_msr(IA32_STAR, star_value);

    // Set the LSTAR MSR to point to the syscall entry point
    write_msr(IA32_LSTAR, (uint64_t)syscall_entry);

    // Set the FMASK MSR to mask interrupts during syscall
    // - Mask interrupts (EFLAGS.IF) and direction flag (EFLAGS.DF)
    write_msr(IA32_FMASK, (1 << 9) | (1 << 10));

    // Enable the SYSCALL/SYSRET instructions by setting the EFER.SCE bit
    uint64_t efer = read_msr(0xC0000080); // EFER MSR
    efer |= (1 << 0); // Set the SCE (SYSCALL Enable) bit
    write_msr(0xC0000080, efer);

    LOG_INFO("Syscalls initialized");
}

// System call handler
long handle_syscall(long syscall_number, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6) {
    switch (syscall_number) {
        case SYS_READ:
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
        case SYS_WRITE:
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case SYS_OPEN:
            return sys_open((const char*)arg1, (int)arg2, (int)arg3);
        case SYS_CLOSE:
            return sys_close((int)arg1);
        case SYS_BRK:
            return sys_brk((void*)arg1);
        case SYS_EXIT:
            sys_exit((int)arg1);
            return 0; // Never reached
        case SYS_GETPID:
            return sys_getpid();
        case SYS_FORK:
            return sys_fork();
        case SYS_EXECVE:
            return sys_execve((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
        case SYS_WAITPID:
            return sys_waitpid((pid_t)arg1, (int*)arg2, (int)arg3);
        case SYS_MMAP:
            return sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (off_t)arg6);
        case SYS_MUNMAP:
            return sys_munmap((void*)arg1, (size_t)arg2);
        case SYS_GETDENTS:
            return sys_getdents((int)arg1, (struct linux_dirent64*)arg2, (unsigned int)arg3);
        case SYS_GETCWD:
            return sys_getcwd((char*)arg1, (size_t)arg2);
        case SYS_CHDIR:
            return sys_chdir((const char*)arg1);
        case SYS_FSTAT:
            return sys_fstat((int)arg1, (struct stat*)arg2);
        case SYS_LSEEK:
            return sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
        case SYS_MKDIR:
            return sys_mkdir((const char*)arg1, (int)arg2);
        case SYS_RMDIR:
            return sys_rmdir((const char*)arg1);
        case SYS_UNLINK:
            return sys_unlink((const char*)arg1);
        default:
            LOG_ERROR("Unknown syscall number: %ld", syscall_number);
            return -1; // Return -1 for unknown syscalls
    }
}

// Assembly syscall entry point
__asm__(
    ".global syscall_entry\n"
    "syscall_entry:\n"
    "    swapgs\n" // Switch to kernel GS base
    "    mov %rsp, %gs:0x10\n" // Save user stack pointer
    "    mov %gs:0x8, %rsp\n" // Load kernel stack pointer
    "    push %rcx\n" // Save user RIP
    "    push %r11\n" // Save user RFLAGS
    "    push %rax\n" // Save syscall number
    "    mov %r10, %rcx\n" // Move 4th argument to RCX (x86_64 calling convention)
    "    call handle_syscall\n" // Call the C handler
    "    pop %rcx\n" // Restore syscall number
    "    pop %r11\n" // Restore RFLAGS
    "    pop %rcx\n" // Restore RIP
    "    mov %gs:0x10, %rsp\n" // Restore user stack pointer
    "    swapgs\n" // Switch back to user GS base
    "    sysretq\n" // Return to user mode
);

// System call implementations

long sys_read(int fd, void *buf, size_t count) {
    if (fd < 0 || !buf || count == 0) {
        LOG_ERROR("Invalid arguments for sys_read");
        return -1;
    }
    ssize_t bytes_read = ext2_read(fd, buf, count);
    if (bytes_read < 0) {
        LOG_ERROR("Failed to read from file descriptor %d", fd);
        return -1;
    }
    return bytes_read;
}

long sys_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || !buf || count == 0) {
        LOG_ERROR("Invalid arguments for sys_write");
        return -1;
    }
    ssize_t bytes_written = ext2_write(fd, buf, count);
    if (bytes_written < 0) {
        LOG_ERROR("Failed to write to file descriptor %d", fd);
        return -1;
    }
    return bytes_written;
}

long sys_open(const char *filename, int flags, int mode) {
    if (!filename) {
        LOG_ERROR("Invalid filename for sys_open");
        return -1;
    }
    int fd = ext2_open(filename, flags);
    if (fd < 0) {
        LOG_ERROR("Failed to open file: %s", filename);
        return -1;
    }
    return fd;
}

long sys_close(int fd) {
    if (fd < 0) {
        LOG_ERROR("Invalid file descriptor for sys_close");
        return -1;
    }
    if (!ext2_close(fd)) {
        LOG_ERROR("Failed to close file descriptor %d", fd);
        return -1;
    }
    return 0;
}

long sys_brk(void *addr) {
    void *new_brk = vmm_allocate((size_t)addr, VMM_FLAG_USER | VMM_FLAG_WRITABLE);
    if (!new_brk) {
        LOG_ERROR("Failed to allocate memory for brk");
        return -1;
    }
    return (long)new_brk;
}

void sys_exit(int status) {
    task_t *current_task = scheduler_get_current_task();
    if (current_task) {
        scheduler_terminate_task(current_task->tid, status);
    }
    LOG_INFO("Task terminated with exit code: %d", status);
    scheduler_yield(); // Switch to the next task
}

long sys_getpid(void) {
    task_t *current_task = scheduler_get_current_task();
    if (!current_task) {
        LOG_ERROR("No current task found");
        return -1;
    }
    return current_task->tid;
}

uint32_t sys_fork(void) {
    task_t* current_task = scheduler_get_current_task();
    if (!current_task) {
        return (uint32_t)-1; // No current task, return error
    }

    // Create a new task as a copy of the current task
    uint32_t new_tid = scheduler_create_task(
        NULL, // No ELF data (forked task is a copy of the current task)
        0,    // No ELF size
        "forked_task", // Name of the new task
        current_task->base_priority, // Inherit parent's priority
        current_task->argc, // Inherit parent's argc
        current_task->argv, // Inherit parent's argv
        current_task->envp  // Inherit parent's envp
    );

    if (new_tid == 0) {
        return (uint32_t)-1; // Failed to create task, return error
    }

    // Copy the parent's CPU context to the child task
    task_t* new_task = scheduler_get_task_by_id(new_tid);
    if (!new_task) {
        return (uint32_t)-1; // Failed to find new task, return error
    }

    memcpy(&new_task->context, &current_task->context, sizeof(cpu_context_t));

    // Set the child's return value to 0 (child process)
    new_task->context.rax = 0;

    // Return the child's TID to the parent
    return new_tid;
}

long sys_execve(const char *filename, char *const argv[], char *const envp[]) {
    if (!filename) {
        LOG_ERROR("Invalid filename for execve");
        return -1;
    }
    task_t *current_task = scheduler_get_current_task();
    if (!current_task) {
        LOG_ERROR("No current task found for execve");
        return -1;
    }
    if (!scheduler_execute_task(current_task->tid, 0, (char**)argv, (char**)envp)) {
        LOG_ERROR("Failed to execute task for execve");
        return -1;
    }
    return 0;
}

long sys_waitpid(pid_t pid, int *status, int options) {
    if (pid < 0 || !status) {
        LOG_ERROR("Invalid arguments for waitpid");
        return -1;
    }
    task_t *task = scheduler_get_task_by_id(pid);
    if (!task) {
        LOG_ERROR("Task with PID %d not found", pid);
        return -1;
    }
    while (task->state != TASK_STATE_TERMINATED) {
        scheduler_yield(); // Wait for the task to terminate
    }
    *status = task->exit_code;
    return pid;
}

long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!addr || length == 0) {
        LOG_ERROR("Invalid arguments for mmap");
        return -1;
    }
    void *mapped_addr = vmm_map_physical((uint64_t)addr, length, VMM_FLAG_USER | VMM_FLAG_WRITABLE);
    if (!mapped_addr) {
        LOG_ERROR("Failed to map memory for mmap");
        return -1;
    }
    return (long)mapped_addr;
}

long sys_munmap(void *addr, size_t length) {
    if (!addr || length == 0) {
        LOG_ERROR("Invalid arguments for munmap");
        return -1;
    }
    vmm_unmap_physical(addr, length);
    return 0;
}

long sys_getdents(int fd, struct linux_dirent64 *dirp, unsigned int count) {
    if (fd < 0 || !dirp || count == 0) {
        LOG_ERROR("Invalid arguments for getdents");
        return -1;
    }
    ext2_file_t *file = &ext2_fs.open_files[fd];
    if (!file->is_open || !EXT2_S_ISDIR(file->inode.i_mode)) {
        LOG_ERROR("File descriptor %d is not a directory", fd);
        return -1;
    }
    // Read directory entries
    uint32_t bytes_read = 0;
    while (bytes_read < count) {
        ext2_dir_entry_t entry;
        if (!ext2_read(fd, &entry, sizeof(entry))) {
            break;
        }
        if (entry.inode == 0) {
            continue; // Skip unused entries
        }
        struct linux_dirent64 *ldirp = (struct linux_dirent64*)((char*)dirp + bytes_read);
        ldirp->d_ino = entry.inode;
        ldirp->d_off = entry.rec_len;
        ldirp->d_reclen = entry.rec_len;
        strncpy(ldirp->d_name, entry.name, entry.name_len);
        ldirp->d_name[entry.name_len] = '\0';
        bytes_read += entry.rec_len;
    }
    return bytes_read;
}

long sys_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) {
        LOG_ERROR("Invalid arguments for getcwd");
        return -1;
    }
    strncpy(buf, ext2_fs.current_dir, size);
    buf[size - 1] = '\0'; // Ensure null termination
    return strlen(buf);
}

long sys_chdir(const char *path) {
    if (!path) {
        LOG_ERROR("Invalid path for chdir");
        return -1;
    }
    uint32_t inode_num = ext2_lookup_path(ext2_fs.drive_index, path);
    if (inode_num == 0) {
        LOG_ERROR("Path not found: %s", path);
        return -1;
    }
    ext2_inode_t inode;
    if (!ext2_read_inode(ext2_fs.drive_index, inode_num, &inode)) {
        LOG_ERROR("Failed to read inode for path: %s", path);
        return -1;
    }
    if (!EXT2_S_ISDIR(inode.i_mode)) {
        LOG_ERROR("Path is not a directory: %s", path);
        return -1;
    }
    strncpy(ext2_fs.current_dir, path, EXT2_MAX_PATH);
    ext2_fs.current_dir[EXT2_MAX_PATH - 1] = '\0';
    return 0;
}

long sys_fstat(int fd, struct stat *statbuf) {
    if (fd < 0 || !statbuf) {
        LOG_ERROR("Invalid arguments for fstat");
        return -1;
    }
    ext2_file_t *file = &ext2_fs.open_files[fd];
    if (!file->is_open) {
        LOG_ERROR("File descriptor %d is not open", fd);
        return -1;
    }
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_ino = file->inode_num;
    statbuf->st_mode = file->inode.i_mode;
    statbuf->st_size = file->inode.i_size;
    statbuf->st_blocks = file->inode.i_blocks;
    statbuf->st_blksize = ext2_fs.block_size;
    return 0;
}

long sys_lseek(int fd, off_t offset, int whence) {
    if (fd < 0) {
        LOG_ERROR("Invalid file descriptor for lseek");
        return -1;
    }
    ext2_file_t *file = &ext2_fs.open_files[fd];
    if (!file->is_open) {
        LOG_ERROR("File descriptor %d is not open", fd);
        return -1;
    }
    switch (whence) {
        case SEEK_SET:
            file->position = offset;
            break;
        case SEEK_CUR:
            file->position += offset;
            break;
        case SEEK_END:
            file->position = file->inode.i_size + offset;
            break;
        default:
            LOG_ERROR("Invalid whence value: %d", whence);
            return -1;
    }
    return file->position;
}

long sys_mkdir(const char *pathname, int mode) {
    if (!pathname) {
        LOG_ERROR("Invalid pathname for mkdir");
        return -1;
    }
    if (!ext2_mkdir(pathname, mode)) {
        LOG_ERROR("Failed to create directory: %s", pathname);
        return -1;
    }
    return 0;
}

long sys_rmdir(const char *pathname) {
    if (!pathname) {
        LOG_ERROR("Invalid pathname for rmdir");
        return -1;
    }
    if (!ext2_rmdir(pathname)) {
        LOG_ERROR("Failed to remove directory: %s", pathname);
        return -1;
    }
    return 0;
}

long sys_unlink(const char *pathname) {
    if (!pathname) {
        LOG_ERROR("Invalid pathname for unlink");
        return -1;
    }
    if (!ext2_unlink(pathname)) {
        LOG_ERROR("Failed to unlink file: %s", pathname);
        return -1;
    }
    return 0;
}