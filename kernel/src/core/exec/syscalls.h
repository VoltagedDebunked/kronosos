#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <memory/vmm.h>
#include <fs/ext2.h>
#include <core/exec/scheduler.h>

typedef int32_t pid_t;
typedef int64_t off_t;

#define SEEK_SET 0  // Seek from the beginning of the file
#define SEEK_CUR 1  // Seek from the current position
#define SEEK_END 2  // Seek from the end of the file

typedef unsigned long long ino64_t;  // 64-bit inode number
typedef long long off64_t;           // 64-bit file offset

struct linux_dirent64 {
    ino64_t        d_ino;    // 64-bit inode number
    off64_t        d_off;    // 64-bit offset to the next entry
    unsigned short d_reclen; // Length of this record
    unsigned char  d_type;   // File type (DT_REG, DT_DIR, etc.)
    char           d_name[]; // Null-terminated filename
};

struct stat {
    uint32_t st_dev;     // ID of device containing file
    uint32_t st_ino;     // Inode number
    uint32_t st_mode;    // File type and mode
    uint32_t st_nlink;   // Number of hard links
    uint32_t st_uid;     // User ID of owner
    uint32_t st_gid;     // Group ID of owner
    uint32_t st_rdev;    // Device ID (if special file)
    uint32_t st_size;    // Total size, in bytes
    uint32_t st_blksize; // Block size for filesystem I/O
    uint32_t st_blocks;  // Number of 512B blocks allocated
    uint32_t st_atime;   // Time of last access
    uint32_t st_mtime;   // Time of last modification
    uint32_t st_ctime;   // Time of last status change
};

// System call numbers
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_BRK             12
#define SYS_EXIT            60
#define SYS_GETPID          39
#define SYS_FORK            57
#define SYS_EXECVE          59
#define SYS_WAITPID         61
#define SYS_MMAP            9
#define SYS_MUNMAP          11
#define SYS_GETDENTS        78
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_FSTAT           5
#define SYS_LSEEK           8
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_UNLINK          87

void syscalls_init(void);

// System call function prototypes
long sys_read(int fd, void *buf, size_t count);
long sys_write(int fd, const void *buf, size_t count);
long sys_open(const char *filename, int flags, int mode);
long sys_close(int fd);
long sys_brk(void *addr);
void sys_exit(int status);
long sys_getpid(void);
uint32_t sys_fork(void);
long sys_execve(const char *filename, char *const argv[], char *const envp[]);
long sys_waitpid(pid_t pid, int *status, int options);
long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
long sys_munmap(void *addr, size_t length);
long sys_getdents(int fd, struct linux_dirent64 *dirp, unsigned int count);
long sys_getcwd(char *buf, size_t size);
long sys_chdir(const char *path);
long sys_fstat(int fd, struct stat *statbuf);
long sys_lseek(int fd, off_t offset, int whence);
long sys_mkdir(const char *pathname, int mode);
long sys_rmdir(const char *pathname);
long sys_unlink(const char *pathname);

// System call handler
long handle_syscall(long syscall_number, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);

#endif // SYSCALLS_H