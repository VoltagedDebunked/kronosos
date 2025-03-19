#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Define ssize_t
typedef int64_t ssize_t;

// EXT2 Constants
#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_ROOT_INO    2

// File types
#define EXT2_S_IFREG  0x8000  // Regular file
#define EXT2_S_IFDIR  0x4000  // Directory
#define EXT2_S_IFCHR  0x2000  // Character device
#define EXT2_S_IFBLK  0x6000  // Block device
#define EXT2_S_IFLNK  0xA000  // Symbolic link
#define EXT2_S_IFSOCK 0xC000  // Socket
#define EXT2_S_IFIFO  0x1000  // FIFO

// Type checking macros
#define EXT2_S_ISREG(m)  (((m) & 0xF000) == EXT2_S_IFREG)
#define EXT2_S_ISDIR(m)  (((m) & 0xF000) == EXT2_S_IFDIR)
#define EXT2_S_ISCHR(m)  (((m) & 0xF000) == EXT2_S_IFCHR)
#define EXT2_S_ISBLK(m)  (((m) & 0xF000) == EXT2_S_IFBLK)
#define EXT2_S_ISLNK(m)  (((m) & 0xF000) == EXT2_S_IFLNK)

// Permission bits
#define EXT2_S_IRWXU 0x01C0  // Owner: rwx
#define EXT2_S_IRUSR 0x0100  // Owner: r--
#define EXT2_S_IWUSR 0x0080  // Owner: -w-
#define EXT2_S_IXUSR 0x0040  // Owner: --x
#define EXT2_S_IRWXG 0x0038  // Group: rwx
#define EXT2_S_IRGRP 0x0020  // Group: r--
#define EXT2_S_IWGRP 0x0010  // Group: -w-
#define EXT2_S_IXGRP 0x0008  // Group: --x
#define EXT2_S_IRWXO 0x0007  // Others: rwx
#define EXT2_S_IROTH 0x0004  // Others: r--
#define EXT2_S_IWOTH 0x0002  // Others: -w-
#define EXT2_S_IXOTH 0x0001  // Others: --x

// File operations
#define EXT2_O_RDONLY    0x0001
#define EXT2_O_WRONLY    0x0002
#define EXT2_O_RDWR      0x0003
#define EXT2_O_CREAT     0x0100
#define EXT2_O_EXCL      0x0200
#define EXT2_O_TRUNC     0x0400

// Block pointers in inode
#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK   12
#define EXT2_DIND_BLOCK  13
#define EXT2_TIND_BLOCK  14
#define EXT2_N_BLOCKS    15

// Directory entry types
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

// Limits
#define EXT2_NAME_LEN    255
#define EXT2_MAX_FILES   64
#define EXT2_CACHE_SIZE  32
#define EXT2_MAX_PATH    256

// Structures
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    
    // Revision specific fields
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_padding[820];  // Padding to 1024 bytes
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[EXT2_NAME_LEN];
} __attribute__((packed)) ext2_dir_entry_t;

typedef struct {
    uint32_t inode_num;
    ext2_inode_t inode;
    uint32_t flags;
    size_t position;
    bool is_open;
} ext2_file_t;

typedef struct {
    uint8_t drive_index;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t groups_count;
    uint32_t blocks_count;
    uint32_t inodes_count;
    ext2_superblock_t *superblock;
    ext2_group_desc_t *group_descs;
    ext2_file_t open_files[EXT2_MAX_FILES];
    char current_dir[EXT2_MAX_PATH];
    
    struct {
        uint32_t block_no;
        void *data;
        bool dirty;
        bool valid;
    } cache[EXT2_CACHE_SIZE];
} ext2_fs_t;

// Core functions
bool ext2_init(void);
bool ext2_mount(uint8_t drive_index);
bool ext2_unmount(void);

// File operations
int ext2_open(const char *path, uint32_t flags);
bool ext2_close(int fd);
ssize_t ext2_read(int fd, void *buffer, size_t size);
ssize_t ext2_write(int fd, const void *buffer, size_t size);

// Directory operations
bool ext2_mkdir(const char *path, uint32_t mode);
bool ext2_rmdir(const char *path);
bool ext2_unlink(const char *path);

// Device operations
bool ext2_create_device(uint8_t drive_index, const char *path, uint32_t mode, uint32_t dev);

// Helper functions
bool ext2_read_block(uint8_t drive_index, uint32_t block_no, void *buffer);
bool ext2_write_block(uint8_t drive_index, uint32_t block_no, void *buffer);
bool ext2_read_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode);
bool ext2_write_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode);
uint32_t ext2_allocate_block(uint8_t drive_index);
uint32_t ext2_allocate_inode(uint8_t drive_index);
uint32_t ext2_lookup_path(uint8_t drive_index, const char *path);
char *ext2_normalize_path(const char *path);

#endif // EXT2_H