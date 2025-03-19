#include "ext2.h"
#include <drivers/ata/ata.h>
#include <lib/string.h>
#include <utils/log.h>
#include <lib/stdio.h>
#include <memory/pmm.h>

// Global state
ext2_fs_t fs;
static bool initialized = false;
static bool mounted = false;
static uint8_t *io_buffer = NULL;

// Initialize filesystem driver
bool ext2_init(void) {
    LOG_INFO_MSG("Initializing EXT2 filesystem driver");
    
    // Initialize global state
    memset(&fs, 0, sizeof(ext2_fs_t));
    
    // Init cache
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        fs.cache[i].valid = false;
        fs.cache[i].dirty = false;
    }
    
    // Init file handles
    for (int i = 0; i < EXT2_MAX_FILES; i++) {
        fs.open_files[i].is_open = false;
    }
    
    // Allocate I/O buffer (8KB)
    io_buffer = pmm_alloc_pages(2);
    if (!io_buffer) {
        LOG_ERROR_MSG("Failed to allocate I/O buffer");
        return false;
    }
    
    strcpy(fs.current_dir, "/");
    initialized = true;
    
    LOG_INFO_MSG("EXT2 filesystem driver initialized");
    return true;
}

// Disk I/O helpers
static bool read_sector(uint8_t drive, uint32_t sector, void *buffer) {
    return ata_read_sectors(drive, sector, 1, buffer);
}

static bool write_sector(uint8_t drive, uint32_t sector, void *buffer) {
    return ata_write_sectors(drive, sector, 1, buffer);
}

// Read a block from disk
bool ext2_read_block(uint8_t drive_index, uint32_t block_no, void *buffer) {
    if (!buffer) return false;
    
    // Check cache first
    if (mounted) {
        for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
            if (fs.cache[i].valid && fs.cache[i].block_no == block_no) {
                memcpy(buffer, fs.cache[i].data, fs.block_size);
                return true;
            }
        }
    }
    
    // Cache miss, read from disk
    uint32_t sectors_per_block = fs.block_size / 512;
    uint32_t start_sector = block_no * sectors_per_block;
    
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!read_sector(drive_index, start_sector + i, (uint8_t*)buffer + (i * 512))) {
            LOG_ERROR("Failed to read sector %u", start_sector + i);
            return false;
        }
    }
    
    // Update cache
    if (mounted) {
        // Find free cache entry or replace oldest
        int cache_idx = 0;
        for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
            if (!fs.cache[i].valid) {
                cache_idx = i;
                break;
            }
        }
        
        // Allocate cache data if needed
        if (!fs.cache[cache_idx].data) {
            fs.cache[cache_idx].data = pmm_alloc_page();
        }
        
        if (fs.cache[cache_idx].data) {
            fs.cache[cache_idx].block_no = block_no;
            fs.cache[cache_idx].valid = true;
            fs.cache[cache_idx].dirty = false;
            memcpy(fs.cache[cache_idx].data, buffer, fs.block_size);
        }
    }
    
    return true;
}

// Write a block to disk
bool ext2_write_block(uint8_t drive_index, uint32_t block_no, void *buffer) {
    if (!buffer) return false;
    
    // Update cache if present
    if (mounted) {
        for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
            if (fs.cache[i].valid && fs.cache[i].block_no == block_no) {
                memcpy(fs.cache[i].data, buffer, fs.block_size);
                fs.cache[i].dirty = true;
            }
        }
    }
    
    // Write to disk
    uint32_t sectors_per_block = fs.block_size / 512;
    uint32_t start_sector = block_no * sectors_per_block;
    
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (!write_sector(drive_index, start_sector + i, (uint8_t*)buffer + (i * 512))) {
            LOG_ERROR("Failed to write sector %u", start_sector + i);
            return false;
        }
    }
    
    return true;
}

// Read an inode from disk
bool ext2_read_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode) {
    if (!inode || inode_no == 0) return false;
    
    // Calculate block group & offset
    uint32_t block_group = (inode_no - 1) / fs.inodes_per_group;
    uint32_t index = (inode_no - 1) % fs.inodes_per_group;
    
    if (block_group >= fs.groups_count) {
        LOG_ERROR("Invalid block group %u for inode %u", block_group, inode_no);
        return false;
    }
    
    // Get inode table block
    uint32_t inode_table = fs.group_descs[block_group].bg_inode_table;
    
    // Calculate block offset
    uint32_t inodes_per_block = fs.block_size / fs.inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = index % inodes_per_block;
    
    // Read the block
    uint8_t *block_data = io_buffer;
    if (!ext2_read_block(drive_index, inode_table + block_offset, block_data)) {
        LOG_ERROR("Failed to read inode block");
        return false;
    }
    
    // Copy the inode
    memcpy(inode, block_data + (inode_offset * fs.inode_size), sizeof(ext2_inode_t));
    
    return true;
}

// Write an inode to disk
bool ext2_write_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode) {
    if (!inode || inode_no == 0) return false;
    
    // Calculate block group & offset
    uint32_t block_group = (inode_no - 1) / fs.inodes_per_group;
    uint32_t index = (inode_no - 1) % fs.inodes_per_group;
    
    if (block_group >= fs.groups_count) {
        LOG_ERROR("Invalid block group %u for inode %u", block_group, inode_no);
        return false;
    }
    
    // Get inode table block
    uint32_t inode_table = fs.group_descs[block_group].bg_inode_table;
    
    // Calculate block offset
    uint32_t inodes_per_block = fs.block_size / fs.inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = index % inodes_per_block;
    
    // Read the block
    uint8_t *block_data = io_buffer;
    if (!ext2_read_block(drive_index, inode_table + block_offset, block_data)) {
        LOG_ERROR("Failed to read inode block");
        return false;
    }
    
    // Update the inode
    memcpy(block_data + (inode_offset * fs.inode_size), inode, sizeof(ext2_inode_t));
    
    // Write the block back
    if (!ext2_write_block(drive_index, inode_table + block_offset, block_data)) {
        LOG_ERROR("Failed to write inode block");
        return false;
    }
    
    return true;
}

// Find free bit in bitmap
static int find_free_bit(uint8_t *bitmap, int size) {
    for (int i = 0; i < size; i++) {
        uint8_t byte = bitmap[i];
        if (byte == 0xFF) continue; // All bits used
        
        // Find first zero bit
        for (int j = 0; j < 8; j++) {
            if ((byte & (1 << j)) == 0) {
                return i * 8 + j;
            }
        }
    }
    return -1; // No free bits
}

// Allocate a block
uint32_t ext2_allocate_block(uint8_t drive_index) {
    if (!mounted) return 0;
    
    // Check for free blocks
    if (fs.superblock->s_free_blocks_count == 0) {
        LOG_ERROR_MSG("No free blocks available");
        return 0;
    }
    
    // Search through block groups
    for (uint32_t bg = 0; bg < fs.groups_count; bg++) {
        if (fs.group_descs[bg].bg_free_blocks_count == 0) continue;
        
        // Read block bitmap
        uint32_t bitmap_block = fs.group_descs[bg].bg_block_bitmap;
        uint8_t *bitmap = io_buffer;
        
        if (!ext2_read_block(drive_index, bitmap_block, bitmap)) {
            LOG_ERROR("Failed to read block bitmap");
            continue;
        }
        
        // Find a free bit
        int bit = find_free_bit(bitmap, fs.block_size);
        if (bit == -1) continue;
        
        // Mark block as used
        bitmap[bit / 8] |= (1 << (bit % 8));
        
        // Write bitmap back
        if (!ext2_write_block(drive_index, bitmap_block, bitmap)) {
            LOG_ERROR("Failed to write block bitmap");
            return 0;
        }
        
        // Calculate actual block number
        uint32_t block_no = bg * fs.blocks_per_group + bit + fs.superblock->s_first_data_block;
        
        // Update counters
        fs.superblock->s_free_blocks_count--;
        fs.group_descs[bg].bg_free_blocks_count--;
        
        // Zero the block
        memset(bitmap, 0, fs.block_size);
        ext2_write_block(drive_index, block_no, bitmap);
        
        LOG_DEBUG("Allocated block %u", block_no);
        return block_no;
    }
    
    LOG_ERROR_MSG("No free blocks found in bitmaps");
    return 0;
}

// Allocate an inode
uint32_t ext2_allocate_inode(uint8_t drive_index) {
    if (!mounted) return 0;
    
    // Check for free inodes
    if (fs.superblock->s_free_inodes_count == 0) {
        LOG_ERROR_MSG("No free inodes available");
        return 0;
    }
    
    // Search through block groups
    for (uint32_t bg = 0; bg < fs.groups_count; bg++) {
        if (fs.group_descs[bg].bg_free_inodes_count == 0) continue;
        
        // Read inode bitmap
        uint32_t bitmap_block = fs.group_descs[bg].bg_inode_bitmap;
        uint8_t *bitmap = io_buffer;
        
        if (!ext2_read_block(drive_index, bitmap_block, bitmap)) {
            LOG_ERROR("Failed to read inode bitmap");
            continue;
        }
        
        // Find a free bit
        int bit = find_free_bit(bitmap, fs.block_size);
        if (bit == -1) continue;
        
        // Mark inode as used
        bitmap[bit / 8] |= (1 << (bit % 8));
        
        // Write bitmap back
        if (!ext2_write_block(drive_index, bitmap_block, bitmap)) {
            LOG_ERROR("Failed to write inode bitmap");
            return 0;
        }
        
        // Calculate actual inode number (1-based)
        uint32_t inode_no = bg * fs.inodes_per_group + bit + 1;
        
        // Update counters
        fs.superblock->s_free_inodes_count--;
        fs.group_descs[bg].bg_free_inodes_count--;
        
        // Initialize inode
        ext2_inode_t inode;
        memset(&inode, 0, sizeof(ext2_inode_t));
        
        // Set times
        uint32_t current_time = 0;
        inode.i_ctime = current_time;
        inode.i_atime = current_time;
        inode.i_mtime = current_time;
        
        // Write inode
        ext2_write_inode(drive_index, inode_no, &inode);
        
        return inode_no;
    }
    
    LOG_ERROR_MSG("No free inodes found in bitmaps");
    return 0;
}

// Get block from inode
static bool get_block_from_inode(ext2_inode_t *inode, uint32_t block_idx, uint32_t *block_no) {
    if (!inode || !block_no) return false;
    
    // Check size
    uint32_t max_blocks = (inode->i_size + fs.block_size - 1) / fs.block_size;
    if (block_idx >= max_blocks) {
        *block_no = 0;
        return false;
    }
    
    // Direct blocks
    if (block_idx < EXT2_NDIR_BLOCKS) {
        *block_no = inode->i_block[block_idx];
        return *block_no != 0;
    }
    
    // Indirect blocks
    uint32_t *block_ptrs = (uint32_t *)io_buffer;
    block_idx -= EXT2_NDIR_BLOCKS;
    uint32_t ptrs_per_block = fs.block_size / sizeof(uint32_t);
    
    // Single indirect
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, inode->i_block[EXT2_IND_BLOCK], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        *block_no = block_ptrs[block_idx];
        return *block_no != 0;
    }
    
    // Double indirect
    block_idx -= ptrs_per_block;
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        uint32_t ind_idx = block_idx / ptrs_per_block;
        uint32_t ind_off = block_idx % ptrs_per_block;
        
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, inode->i_block[EXT2_DIND_BLOCK], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        if (block_ptrs[ind_idx] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, block_ptrs[ind_idx], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        *block_no = block_ptrs[ind_off];
        return *block_no != 0;
    }
    
    // Triple indirect (very rare)
    block_idx -= ptrs_per_block * ptrs_per_block;
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        uint32_t dbl_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t remain = block_idx % (ptrs_per_block * ptrs_per_block);
        uint32_t ind_idx = remain / ptrs_per_block;
        uint32_t ind_off = remain % ptrs_per_block;
        
        if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, inode->i_block[EXT2_TIND_BLOCK], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        if (block_ptrs[dbl_idx] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, block_ptrs[dbl_idx], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        if (block_ptrs[ind_idx] == 0) {
            *block_no = 0;
            return false;
        }
        
        if (!ext2_read_block(fs.drive_index, block_ptrs[ind_idx], block_ptrs)) {
            *block_no = 0;
            return false;
        }
        
        *block_no = block_ptrs[ind_off];
        return *block_no != 0;
    }
    
    *block_no = 0;
    return false;
}

// Set block in inode (allocate indirect blocks if needed)
static bool set_block_in_inode(ext2_inode_t *inode, uint32_t block_idx, uint32_t block_no) {
    if (!inode) return false;
    
    // Direct blocks
    if (block_idx < EXT2_NDIR_BLOCKS) {
        inode->i_block[block_idx] = block_no;
        return true;
    }
    
    // Indirect blocks
    uint32_t *block_ptrs = (uint32_t *)io_buffer;
    block_idx -= EXT2_NDIR_BLOCKS;
    uint32_t ptrs_per_block = fs.block_size / sizeof(uint32_t);
    
    // Single indirect
    if (block_idx < ptrs_per_block) {
        // Allocate indirect block if needed
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            inode->i_block[EXT2_IND_BLOCK] = ext2_allocate_block(fs.drive_index);
            if (inode->i_block[EXT2_IND_BLOCK] == 0) {
                return false;
            }
            
            memset(block_ptrs, 0, fs.block_size);
            ext2_write_block(fs.drive_index, inode->i_block[EXT2_IND_BLOCK], block_ptrs);
        }
        
        // Read indirect block
if (!ext2_read_block(fs.drive_index, inode->i_block[EXT2_IND_BLOCK], block_ptrs)) {
    return false;
}

// Update block pointer
block_ptrs[block_idx] = block_no;

// Write back
return ext2_write_block(fs.drive_index, inode->i_block[EXT2_IND_BLOCK], block_ptrs);
}

// Only implement double/triple indirect if needed
return false;
}

// Normalize a path
char *ext2_normalize_path(const char *path) {
    static char normalized[EXT2_MAX_PATH];
    char temp[EXT2_MAX_PATH];
    
    if (!path) return NULL;
    
    // Handle relative paths
    if (path[0] != '/') {
        if (mounted) {
            snprintf(temp, sizeof(temp), "%s/%s", fs.current_dir, path);
        } else {
            snprintf(temp, sizeof(temp), "/%s", path);
        }
    } else {
        strncpy(temp, path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
    }
    
    // Process path components
    char *out = normalized;
    *out = '\0';
    
    char *token, *saveptr;
    token = strtok_r(temp, "/", &saveptr);
    
    while (token) {
        // Skip "." (current directory)
        if (strcmp(token, ".") == 0) {
            token = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        
        // Handle ".." (parent directory)
        if (strcmp(token, "..") == 0) {
            // Remove last directory
            char *last_slash = strrchr(normalized, '/');
            if (last_slash && last_slash != normalized) {
                *last_slash = '\0';
            }
            token = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        
        // Add component to path
        strcat(normalized, "/");
        strcat(normalized, token);
        
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    // Handle empty path (root)
    if (normalized[0] == '\0') {
        normalized[0] = '/';
        normalized[1] = '\0';
    }
    
    return normalized;
}

// Find a file in a directory
static uint32_t find_file_in_dir(uint8_t drive_index, uint32_t dir_ino, const char *name) {
    if (!name || dir_ino == 0) return 0;
    
    // Read directory inode
    ext2_inode_t dir_inode;
    if (!ext2_read_inode(drive_index, dir_ino, &dir_inode)) return 0;
    
    // Check if it's a directory
    if (!EXT2_S_ISDIR(dir_inode.i_mode)) return 0;
    
    // Scan directory blocks
    uint32_t offset = 0;
    uint32_t block_idx = 0;
    
    while (offset < dir_inode.i_size) {
        // Get block
        uint32_t block_no;
        if (!get_block_from_inode(&dir_inode, block_idx, &block_no) || block_no == 0) break;
        
        // Read block
        uint8_t *block_data = io_buffer;
        if (!ext2_read_block(drive_index, block_no, block_data)) break;
        
        // Scan entries
        uint32_t block_offset = 0;
        while (block_offset < fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t*)(block_data + block_offset);
            
            if (entry->rec_len == 0) break;  // End of entries
            
            // Check name
            if (entry->inode != 0 && 
                entry->name_len == strlen(name) && 
                strncmp(entry->name, name, entry->name_len) == 0) {
                return entry->inode;  // Found it
            }
            
            // Next entry
            block_offset += entry->rec_len;
        }
        
        // Next block
        offset += fs.block_size;
        block_idx++;
    }
    
    return 0;  // Not found
}

// Lookup a path to find its inode
uint32_t ext2_lookup_path(uint8_t drive_index, const char *path) {
    if (!path) return 0;
    
    // Normalize path
    char *norm_path = ext2_normalize_path(path);
    
    // Handle root
    if (strcmp(norm_path, "/") == 0) return EXT2_ROOT_INO;
    
    // Start at root
    uint32_t current_ino = EXT2_ROOT_INO;
    
    // Skip leading slash
    char *p = norm_path;
    if (*p == '/') p++;
    
    // Process path components
    char component[EXT2_NAME_LEN + 1];
    char *start = p;
    
    while (*p) {
        if (*p == '/') {
            // Extract component
            size_t len = p - start;
            if (len > 0) {
                if (len > EXT2_NAME_LEN) len = EXT2_NAME_LEN;
                strncpy(component, start, len);
                component[len] = '\0';
                
                // Find in directory
                current_ino = find_file_in_dir(drive_index, current_ino, component);
                if (current_ino == 0) return 0;  // Not found
            }
            start = p + 1;
        }
        p++;
    }
    
    // Handle last component
    if (start < p) {
        size_t len = p - start;
        if (len > EXT2_NAME_LEN) len = EXT2_NAME_LEN;
        strncpy(component, start, len);
        component[len] = '\0';
        
        current_ino = find_file_in_dir(drive_index, current_ino, component);
    }
    
    return current_ino;
}

// Add entry to directory
static bool add_dir_entry(uint8_t drive_index, uint32_t dir_ino, 
                          const char *name, uint32_t ino, uint8_t type) {
    if (!name || dir_ino == 0 || ino == 0) return false;
    
    size_t name_len = strlen(name);
    if (name_len > EXT2_NAME_LEN) return false;
    
    // Read directory inode
    ext2_inode_t dir_inode;
    if (!ext2_read_inode(drive_index, dir_ino, &dir_inode)) return false;
    
    // Check if it's a directory
    if (!EXT2_S_ISDIR(dir_inode.i_mode)) return false;
    
    // Calculate entry size (8-byte aligned)
    uint16_t entry_size = 8 + name_len;
    entry_size = (entry_size + 7) & ~7;
    
    // Try to find space in existing blocks
    uint32_t offset = 0;
    uint32_t block_idx = 0;
    bool entry_added = false;
    
    while (offset < dir_inode.i_size && !entry_added) {
        // Get block
        uint32_t block_no;
        if (!get_block_from_inode(&dir_inode, block_idx, &block_no) || block_no == 0) break;
        
        // Read block
        uint8_t *block_data = io_buffer;
        if (!ext2_read_block(drive_index, block_no, block_data)) break;
        
        // Scan entries for space
        uint32_t block_offset = 0;
        while (block_offset < fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t*)(block_data + block_offset);
            
            // End of entries or unused entry
            if (entry->rec_len == 0 || (entry->inode == 0 && entry->rec_len >= entry_size)) {
                entry->inode = ino;
                entry->rec_len = entry_size;
                entry->name_len = name_len;
                entry->file_type = type;
                strncpy(entry->name, name, name_len);
                
                if (!ext2_write_block(drive_index, block_no, block_data)) return false;
                
                entry_added = true;
                break;
            }
            
            // Check for space after existing entry
            uint16_t actual_len = 8 + entry->name_len;
            actual_len = (actual_len + 7) & ~7;
            
            if (entry->rec_len - actual_len >= entry_size) {
                // Split existing entry
                uint16_t new_rec_len = entry->rec_len - actual_len;
                entry->rec_len = actual_len;
                
                // Create new entry
                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t*)(block_data + block_offset + actual_len);
                new_entry->inode = ino;
                new_entry->rec_len = new_rec_len;
                new_entry->name_len = name_len;
                new_entry->file_type = type;
                strncpy(new_entry->name, name, name_len);
                
                if (!ext2_write_block(drive_index, block_no, block_data)) return false;
                
                entry_added = true;
                break;
            }
            
            // Next entry
            block_offset += entry->rec_len;
        }
        
        // Next block
        offset += fs.block_size;
        block_idx++;
    }
    
    // Need to allocate a new block
    if (!entry_added) {
        uint32_t block_no = ext2_allocate_block(drive_index);
        if (block_no == 0) return false;
        
        // Initialize block
        uint8_t *block_data = io_buffer;
        memset(block_data, 0, fs.block_size);
        
        // Create entry
        ext2_dir_entry_t *entry = (ext2_dir_entry_t*)block_data;
        entry->inode = ino;
        entry->rec_len = fs.block_size;  // Use whole block
        entry->name_len = name_len;
        entry->file_type = type;
        strncpy(entry->name, name, name_len);
        
        if (!ext2_write_block(drive_index, block_no, block_data)) return false;
        
        // Update inode
        if (!set_block_in_inode(&dir_inode, block_idx, block_no)) return false;
        
        // Update size and block count
        dir_inode.i_size += fs.block_size;
        dir_inode.i_blocks += fs.block_size / 512;
        
        // Update modification time
        dir_inode.i_mtime = 0;
        
        if (!ext2_write_inode(drive_index, dir_ino, &dir_inode)) return false;
    }
    
    return true;
}

// Mount an EXT2 filesystem
bool ext2_mount(uint8_t drive_index) {
    if (!initialized || mounted) return false;
    
    LOG_INFO("Mounting EXT2 filesystem on drive %u", drive_index);
    
    // Check drive
    if (!ata_drive_present(drive_index)) {
        LOG_ERROR("Drive %u not present", drive_index);
        return false;
    }
    
    fs.drive_index = drive_index;
    
    // Read superblock (sector 2 for 1K blocks)
    uint8_t *sb_buffer = io_buffer;
    if (!read_sector(drive_index, 2, sb_buffer)) {
        LOG_ERROR("Failed to read superblock");
        return false;
    }
    
    // Check magic
    ext2_superblock_t *sb = (ext2_superblock_t*)sb_buffer;
    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        LOG_ERROR("Invalid EXT2 magic: 0x%X", sb->s_magic);
        return false;
    }
    
    // Store FS parameters
    fs.block_size = 1024 << sb->s_log_block_size;
    fs.blocks_per_group = sb->s_blocks_per_group;
    fs.inodes_per_group = sb->s_inodes_per_group;
    fs.inode_size = sb->s_inode_size > 0 ? sb->s_inode_size : 128;
    fs.groups_count = (sb->s_blocks_count + fs.blocks_per_group - 1) / fs.blocks_per_group;
    
    // Copy superblock
    fs.superblock = (ext2_superblock_t*)pmm_alloc_page();
    if (!fs.superblock) {
        LOG_ERROR("Failed to allocate memory for superblock");
        return false;
    }
    memcpy(fs.superblock, sb, sizeof(ext2_superblock_t));
    
    // Read block group descriptors
    uint32_t bg_desc_block = sb->s_first_data_block + 1;
    uint32_t bg_desc_size = sizeof(ext2_group_desc_t) * fs.groups_count;
    uint32_t bg_desc_blocks = (bg_desc_size + fs.block_size - 1) / fs.block_size;
    
    // Allocate group descriptors
    fs.group_descs = (ext2_group_desc_t*)pmm_alloc_pages((bg_desc_blocks * fs.block_size + 4095) / 4096);
    if (!fs.group_descs) {
        LOG_ERROR("Failed to allocate memory for group descriptors");
        pmm_free_page(fs.superblock);
        return false;
    }
    
    // Read group descriptors
    for (uint32_t i = 0; i < bg_desc_blocks; i++) {
        if (!ext2_read_block(drive_index, bg_desc_block + i, 
                          (uint8_t*)fs.group_descs + (i * fs.block_size))) {
            LOG_ERROR("Failed to read block group descriptors");
            pmm_free_page(fs.group_descs);
            pmm_free_page(fs.superblock);
            return false;
        }
    }
    
    // Initialize cache memory
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (!fs.cache[i].data) {
            fs.cache[i].data = pmm_alloc_page();
        }
    }
    
    mounted = true;
    strcpy(fs.current_dir, "/");
    
    fs.blocks_count = fs.superblock->s_blocks_count;
    fs.inodes_count = fs.superblock->s_inodes_count;
    
    // Then the LOG_INFO statement will work correctly:
    LOG_INFO("EXT2 filesystem mounted: blocks=%u, inodes=%u, block_size=%u",
             fs.blocks_count, fs.inodes_count, fs.block_size);
    
    return true;
}

// Unmount filesystem
bool ext2_unmount(void) {
    if (!mounted) return false;
    
    LOG_INFO_MSG("Unmounting EXT2 filesystem");
    
    // Flush dirty cache blocks
    for (int i = 0; i < EXT2_CACHE_SIZE; i++) {
        if (fs.cache[i].valid && fs.cache[i].dirty) {
            ext2_write_block(fs.drive_index, fs.cache[i].block_no, fs.cache[i].data);
        }
        
        // Free cache memory
        if (fs.cache[i].data) {
            pmm_free_page(fs.cache[i].data);
            fs.cache[i].data = NULL;
        }
        
        fs.cache[i].valid = false;
        fs.cache[i].dirty = false;
    }
    
    // Close open files
    for (int i = 0; i < EXT2_MAX_FILES; i++) {
        fs.open_files[i].is_open = false;
    }
    
    // Free resources
    if (fs.superblock) {
        pmm_free_page(fs.superblock);
        fs.superblock = NULL;
    }
    
    if (fs.group_descs) {
        pmm_free_page(fs.group_descs);
        fs.group_descs = NULL;
    }
    
    mounted = false;
    LOG_INFO_MSG("EXT2 filesystem unmounted");
    
    return true;
}

// Simple file creation function
static bool create_file(uint8_t drive_index, const char *path, uint32_t mode, uint8_t type) {
    if (!path) return false;
    
    // Normalize path
    char *norm_path = ext2_normalize_path(path);
    
    // Extract directory path and filename
    char dir_path[EXT2_MAX_PATH];
    char filename[EXT2_NAME_LEN + 1];
    
    // Find last slash
    char *last_slash = strrchr(norm_path, '/');
    if (!last_slash) return false;
    
    // Extract directory path
    size_t dir_len = last_slash - norm_path;
    strncpy(dir_path, norm_path, dir_len);
    dir_path[dir_len] = '\0';
    
    // Handle root directory
    if (dir_len == 0) strcpy(dir_path, "/");
    
    // Extract filename (skip slash)
    strncpy(filename, last_slash + 1, EXT2_NAME_LEN);
    filename[EXT2_NAME_LEN] = '\0';
    
    // Find parent directory
    uint32_t dir_ino = ext2_lookup_path(drive_index, dir_path);
    if (dir_ino == 0) return false;
    
    // Check if file already exists
    if (find_file_in_dir(drive_index, dir_ino, filename) != 0) {
        return false;  // Already exists
    }
    
    // Allocate inode
    uint32_t ino = ext2_allocate_inode(drive_index);
    if (ino == 0) return false;
    
    // Initialize inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(ext2_inode_t));
    
    // Set mode (type + permissions)
    uint16_t file_type;
    uint8_t dir_type;
    
    switch (type) {
        case EXT2_FT_REG_FILE:
            file_type = EXT2_S_IFREG;
            dir_type = EXT2_FT_REG_FILE;
            break;
        case EXT2_FT_DIR:
            file_type = EXT2_S_IFDIR;
            dir_type = EXT2_FT_DIR;
            break;
        case EXT2_FT_CHRDEV:
            file_type = EXT2_S_IFCHR;
            dir_type = EXT2_FT_CHRDEV;
            break;
        case EXT2_FT_BLKDEV:
            file_type = EXT2_S_IFBLK;
            dir_type = EXT2_FT_BLKDEV;
            break;
        default:
            return false;
    }
    
    inode.i_mode = file_type | (mode & 0x1FF);
    
    // Set times
    inode.i_ctime = inode.i_atime = inode.i_mtime = 0;
    
    // Set link count
    inode.i_links_count = 1;
    
    // Write inode
    if (!ext2_write_inode(drive_index, ino, &inode)) {
        return false;
    }
    
    // Add directory entry
    return add_dir_entry(drive_index, dir_ino, filename, ino, dir_type);
}

// Create a device (character or block)
bool ext2_create_device(uint8_t drive_index, const char *path, uint32_t mode, uint32_t dev) {
    if (!mounted || !path) return false;
    
    // Determine device type
    uint8_t type;
    
    if ((mode & EXT2_S_IFCHR) == EXT2_S_IFCHR) {
        type = EXT2_FT_CHRDEV;
    } 
    else if ((mode & EXT2_S_IFBLK) == EXT2_S_IFBLK) {
        type = EXT2_FT_BLKDEV;
    }
    else {
        return false;  // Invalid type
    }
    
    // Create the file
    if (!create_file(drive_index, path, mode, type)) {
        return false;
    }
    
    // Update device ID
    uint32_t ino = ext2_lookup_path(drive_index, path);
    if (ino == 0) return false;
    
    ext2_inode_t inode;
    if (!ext2_read_inode(drive_index, ino, &inode)) {
        return false;
    }
    
    // Store device ID in first block pointer
    inode.i_block[0] = dev;
    
    // Write inode
    return ext2_write_inode(drive_index, ino, &inode);
}

// Create a directory
bool ext2_mkdir(const char *path, uint32_t mode) {
    if (!mounted || !path) return false;
    
    // Create the directory
    if (!create_file(fs.drive_index, path, mode, EXT2_FT_DIR)) {
        return false;
    }
    
    // Get the inode
    uint32_t ino = ext2_lookup_path(fs.drive_index, path);
    if (ino == 0) return false;
    
    // Get parent directory
    char *norm_path = ext2_normalize_path(path);
    char *last_slash = strrchr(norm_path, '/');
    if (!last_slash) return false;
    
    char dir_path[EXT2_MAX_PATH];
    size_t dir_len = last_slash - norm_path;
    
    if (dir_len > 0) {
        strncpy(dir_path, norm_path, dir_len);
        dir_path[dir_len] = '\0';
    } else {
        strcpy(dir_path, "/");
    }
    
    uint32_t parent_ino = ext2_lookup_path(fs.drive_index, dir_path);
    if (parent_ino == 0) return false;
    
    // Create "." and ".." entries
    ext2_inode_t inode;
    if (!ext2_read_inode(fs.drive_index, ino, &inode)) {
        return false;
    }
    
    // Allocate first block
    uint32_t block_no = ext2_allocate_block(fs.drive_index);
    if (block_no == 0) return false;
    
    // Initialize with "." and ".." entries
    uint8_t *block_data = io_buffer;
    memset(block_data, 0, fs.block_size);
    
    ext2_dir_entry_t *dot = (ext2_dir_entry_t*)block_data;
    dot->inode = ino;  // Self
    dot->rec_len = 12;  // Size of entry (8 + name + alignment)
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';
    
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t*)(block_data + 12);
    dotdot->inode = parent_ino;  // Parent
    dotdot->rec_len = fs.block_size - 12;  // Rest of block
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    
    // Write the block
    if (!ext2_write_block(fs.drive_index, block_no, block_data)) {
        return false;
    }
    
    // Update directory inode
    inode.i_block[0] = block_no;
    inode.i_size = fs.block_size;
    inode.i_blocks = fs.block_size / 512;
    inode.i_links_count = 2;  // . entry + parent dir
    
    // Write updated inode
    if (!ext2_write_inode(fs.drive_index, ino, &inode)) {
        return false;
    }
    
    // Update parent's link count (for the .. entry)
    ext2_inode_t parent;
    if (!ext2_read_inode(fs.drive_index, parent_ino, &parent)) {
        return false;
    }
    
    parent.i_links_count++;
    
    // Update directory count
    uint32_t bg = (ino - 1) / fs.inodes_per_group;
    fs.group_descs[bg].bg_used_dirs_count++;
    
    return ext2_write_inode(fs.drive_index, parent_ino, &parent);
}

// File open function
int ext2_open(const char *path, uint32_t flags) {
    if (!mounted || !path) return -1;
    
    // Find available file handle
    int fd = -1;
    for (int i = 0; i < EXT2_MAX_FILES; i++) {
        if (!fs.open_files[i].is_open) {
            fd = i;
            break;
        }
    }
    
    if (fd == -1) {
        LOG_ERROR_MSG("Too many open files");
        return -1;
    }
    
    // Try to find the file
    uint32_t inode_no = ext2_lookup_path(fs.drive_index, path);
    
    // Create if it doesn't exist and O_CREAT is set
    if (inode_no == 0 && (flags & EXT2_O_CREAT)) {
        if (!create_file(fs.drive_index, path, 0644, EXT2_FT_REG_FILE)) {
            return -1;
        }
        
        inode_no = ext2_lookup_path(fs.drive_index, path);
        if (inode_no == 0) return -1;
    } 
    else if (inode_no == 0) {
        LOG_ERROR("File not found: %s", path);
        return -1;
    }
    
    // Read inode
    ext2_inode_t inode;
    if (!ext2_read_inode(fs.drive_index, inode_no, &inode)) {
        return -1;
    }
    
    // Check file type
    if (EXT2_S_ISDIR(inode.i_mode) && (flags & (EXT2_O_WRONLY | EXT2_O_RDWR))) {
        LOG_ERROR("Cannot open directory for writing");
        return -1;
    }
    
    // Truncate if requested
    if ((flags & EXT2_O_TRUNC) && (flags & (EXT2_O_WRONLY | EXT2_O_RDWR))) {
        // TODO: implement truncate
    }
    
    // Initialize file handle
    fs.open_files[fd].inode_num = inode_no;
    fs.open_files[fd].inode = inode;
    fs.open_files[fd].flags = flags;
    fs.open_files[fd].position = 0;
    fs.open_files[fd].is_open = true;
    
    return fd;
}

// Close file
bool ext2_close(int fd) {
    if (!mounted || fd < 0 || fd >= EXT2_MAX_FILES || !fs.open_files[fd].is_open) {
        return false;
    }
    
    fs.open_files[fd].is_open = false;
    return true;
}

// Read from file
ssize_t ext2_read(int fd, void *buffer, size_t size) {
    if (!mounted || !buffer || fd < 0 || fd >= EXT2_MAX_FILES || !fs.open_files[fd].is_open) {
        return -1;
    }
    
    // Check if file is readable
    if ((fs.open_files[fd].flags & EXT2_O_WRONLY) && 
        !(fs.open_files[fd].flags & EXT2_O_RDWR)) {
        LOG_ERROR("File not opened for reading");
        return -1;
    }
    
    ext2_file_t *file = &fs.open_files[fd];
    
    // Check if at end of file
    if (file->position >= file->inode.i_size) {
        return 0;
    }
    
    // Limit read size to file size
    if (file->position + size > file->inode.i_size) {
        size = file->inode.i_size - file->position;
    }
    
    // Calculate block positions
    uint32_t start_block = file->position / fs.block_size;
    uint32_t block_offset = file->position % fs.block_size;
    
    // Read data
    uint8_t *buf = (uint8_t*)buffer;
    size_t bytes_read = 0;
    size_t remaining = size;
    
    while (remaining > 0) {
        // Get block number
        uint32_t block_no;
        if (!get_block_from_inode(&file->inode, start_block, &block_no) || block_no == 0) {
            break;
        }
        
        // Read the block
        if (!ext2_read_block(fs.drive_index, block_no, io_buffer)) {
            break;
        }
        
        // Calculate bytes to copy
        size_t to_copy = fs.block_size - block_offset;
        if (to_copy > remaining) {
            to_copy = remaining;
        }
        
        // Copy data
        memcpy(buf + bytes_read, io_buffer + block_offset, to_copy);
        
        // Update counters
        bytes_read += to_copy;
        remaining -= to_copy;
        start_block++;
        block_offset = 0;
    }
    
    // Update file position
    file->position += bytes_read;
    
    // Update access time
    file->inode.i_atime = 0;
    ext2_write_inode(fs.drive_index, file->inode_num, &file->inode);
    
    return bytes_read;
}

// Write to file
ssize_t ext2_write(int fd, const void *buffer, size_t size) {
    if (!mounted || !buffer || fd < 0 || fd >= EXT2_MAX_FILES || !fs.open_files[fd].is_open) {
        return -1;
    }
    
    // Check if file is writable
    if (!(fs.open_files[fd].flags & (EXT2_O_WRONLY | EXT2_O_RDWR))) {
        LOG_ERROR("File not opened for writing");
        return -1;
    }
    
    ext2_file_t *file = &fs.open_files[fd];
    
    // Calculate block positions
    uint32_t start_block = file->position / fs.block_size;
    uint32_t block_offset = file->position % fs.block_size;
    
    // Write data
    const uint8_t *buf = (const uint8_t*)buffer;
    size_t bytes_written = 0;
    size_t remaining = size;
    
    while (remaining > 0) {
        // Get block number
        uint32_t block_no;
        if (!get_block_from_inode(&file->inode, start_block, &block_no)) {
            // Need to allocate a new block
            block_no = ext2_allocate_block(fs.drive_index);
            if (block_no == 0) break;
            
            // Zero the block
            memset(io_buffer, 0, fs.block_size);
            ext2_write_block(fs.drive_index, block_no, io_buffer);
            
            // Add to inode
            if (!set_block_in_inode(&file->inode, start_block, block_no)) {
                break;
            }
            
            // Update inode blocks
            file->inode.i_blocks += fs.block_size / 512;
        }
        
        // If we're not writing a full block, read existing data first
        if (block_offset > 0 || remaining < fs.block_size) {
            if (!ext2_read_block(fs.drive_index, block_no, io_buffer)) {
                break;
            }
        }
        
        // Calculate bytes to copy
        size_t to_copy = fs.block_size - block_offset;
        if (to_copy > remaining) {
            to_copy = remaining;
        }
        
        // Copy data
        memcpy(io_buffer + block_offset, buf + bytes_written, to_copy);
        
        // Write the block
        if (!ext2_write_block(fs.drive_index, block_no, io_buffer)) {
            break;
        }
        
        // Update counters
        bytes_written += to_copy;
        remaining -= to_copy;
        start_block++;
        block_offset = 0;
    }
    
    // Update file position
    file->position += bytes_written;
    
    // Update file size if needed
    if (file->position > file->inode.i_size) {
        file->inode.i_size = file->position;
    }
    
    // Update modification time
    file->inode.i_mtime = 0;
    
    // Write back inode
    ext2_write_inode(fs.drive_index, file->inode_num, &file->inode);
    
    return bytes_written;
 }
 
 // Remove a file
 bool ext2_unlink(const char *path) {
    if (!mounted || !path) return false;
    
    // Normalize path
    char *norm_path = ext2_normalize_path(path);
    
    // Get parent directory path and filename
    char dir_path[EXT2_MAX_PATH];
    char filename[EXT2_NAME_LEN + 1];
    
    char *last_slash = strrchr(norm_path, '/');
    if (!last_slash) return false;
    
    size_t dir_len = last_slash - norm_path;
    if (dir_len > 0) {
        strncpy(dir_path, norm_path, dir_len);
        dir_path[dir_len] = '\0';
    } else {
        strcpy(dir_path, "/");
    }
    
    strncpy(filename, last_slash + 1, EXT2_NAME_LEN);
    filename[EXT2_NAME_LEN] = '\0';
    
    // Get parent directory inode
    uint32_t dir_ino = ext2_lookup_path(fs.drive_index, dir_path);
    if (dir_ino == 0) return false;
    
    // Find file in directory
    uint32_t file_ino = find_file_in_dir(fs.drive_index, dir_ino, filename);
    if (file_ino == 0) return false;
    
    // Read file inode
    ext2_inode_t inode;
    if (!ext2_read_inode(fs.drive_index, file_ino, &inode)) {
        return false;
    }
    
    // Check if it's a directory
    if (EXT2_S_ISDIR(inode.i_mode)) {
        LOG_ERROR("Cannot unlink directory: %s", path);
        return false;
    }
    
    // Remove from directory
    ext2_inode_t dir_inode;
    if (!ext2_read_inode(fs.drive_index, dir_ino, &dir_inode)) {
        return false;
    }
    
    // Scan directory blocks
    uint32_t offset = 0;
    uint32_t block_idx = 0;
    bool entry_removed = false;
    
    while (offset < dir_inode.i_size && !entry_removed) {
        // Get block
        uint32_t block_no;
        if (!get_block_from_inode(&dir_inode, block_idx, &block_no) || block_no == 0) {
            break;
        }
        
        // Read block
        uint8_t *block_data = io_buffer;
        if (!ext2_read_block(fs.drive_index, block_no, block_data)) {
            break;
        }
        
        // Scan entries
        uint32_t block_offset = 0;
        ext2_dir_entry_t *prev_entry = NULL;
        
        while (block_offset < fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t*)(block_data + block_offset);
            
            if (entry->rec_len == 0) break;
            
            // Check if this is the target entry
            if (entry->inode == file_ino &&
                entry->name_len == strlen(filename) &&
                strncmp(entry->name, filename, entry->name_len) == 0) {
                
                // Found the entry to remove
                if (prev_entry) {
                    // Extend previous entry to cover this one
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    // Mark this entry as unused
                    entry->inode = 0;
                }
                
                // Write the block back
                if (!ext2_write_block(fs.drive_index, block_no, block_data)) {
                    return false;
                }
                
                entry_removed = true;
                break;
            }
            
            prev_entry = entry;
            block_offset += entry->rec_len;
        }
        
        offset += fs.block_size;
        block_idx++;
    }
    
    if (!entry_removed) {
        LOG_ERROR("Failed to remove directory entry for: %s", path);
        return false;
    }
    
    // Decrement link count
    inode.i_links_count--;
    
    // If no more links, mark for deletion
    if (inode.i_links_count == 0) {
        uint32_t current_time = 0;
        inode.i_dtime = current_time;
    }
    
    // Write inode back
    return ext2_write_inode(fs.drive_index, file_ino, &inode);
 }
 
 // Remove a directory
 bool ext2_rmdir(const char *path) {
    if (!mounted || !path) return false;
    
    // Normalize path
    char *norm_path = ext2_normalize_path(path);
    
    // Get parent directory path and dirname
    char parent_path[EXT2_MAX_PATH];
    char dirname[EXT2_NAME_LEN + 1];
    
    char *last_slash = strrchr(norm_path, '/');
    if (!last_slash) return false;
    
    size_t parent_len = last_slash - norm_path;
    if (parent_len > 0) {
        strncpy(parent_path, norm_path, parent_len);
        parent_path[parent_len] = '\0';
    } else {
        strcpy(parent_path, "/");
    }
    
    strncpy(dirname, last_slash + 1, EXT2_NAME_LEN);
    dirname[EXT2_NAME_LEN] = '\0';
    
    // Cannot remove root
    if (strcmp(norm_path, "/") == 0) {
        LOG_ERROR("Cannot remove root directory");
        return false;
    }
    
    // Get parent directory inode
    uint32_t parent_ino = ext2_lookup_path(fs.drive_index, parent_path);
    if (parent_ino == 0) return false;
    
    // Find directory in parent
    uint32_t dir_ino = find_file_in_dir(fs.drive_index, parent_ino, dirname);
    if (dir_ino == 0) return false;
    
    // Read directory inode
    ext2_inode_t dir_inode;
    if (!ext2_read_inode(fs.drive_index, dir_ino, &dir_inode)) {
        return false;
    }
    
    // Check if it's a directory
    if (!EXT2_S_ISDIR(dir_inode.i_mode)) {
        LOG_ERROR("Not a directory: %s", path);
        return false;
    }
    
    // Check if directory is empty (except for . and ..)
    bool is_empty = true;
    
    // Scan all blocks
    uint32_t offset = 0;
    uint32_t block_idx = 0;
    
    while (offset < dir_inode.i_size && is_empty) {
        // Get block
        uint32_t block_no;
        if (!get_block_from_inode(&dir_inode, block_idx, &block_no) || block_no == 0) {
            break;
        }
        
        // Read block
        uint8_t *block_data = io_buffer;
        if (!ext2_read_block(fs.drive_index, block_no, block_data)) {
            break;
        }
        
        // Scan entries
        uint32_t block_offset = 0;
        
        while (block_offset < fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t*)(block_data + block_offset);
            
            if (entry->rec_len == 0) break;
            
            // Skip . and .. entries
            if (entry->inode != 0 && 
                !(entry->name_len == 1 && entry->name[0] == '.') &&
                !(entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                
                is_empty = false;
                break;
            }
            
            block_offset += entry->rec_len;
        }
        
        offset += fs.block_size;
        block_idx++;
    }
    
    if (!is_empty) {
        LOG_ERROR("Directory not empty: %s", path);
        return false;
    }
    
    // Remove from parent directory
    ext2_inode_t parent_inode;
    if (!ext2_read_inode(fs.drive_index, parent_ino, &parent_inode)) {
        return false;
    }
    
    // Same code as unlink to remove from parent
    offset = 0;
    block_idx = 0;
    bool entry_removed = false;
    
    while (offset < parent_inode.i_size && !entry_removed) {
        uint32_t block_no;
        if (!get_block_from_inode(&parent_inode, block_idx, &block_no) || block_no == 0) {
            break;
        }
        
        uint8_t *block_data = io_buffer;
        if (!ext2_read_block(fs.drive_index, block_no, block_data)) {
            break;
        }
        
        uint32_t block_offset = 0;
        ext2_dir_entry_t *prev_entry = NULL;
        
        while (block_offset < fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t*)(block_data + block_offset);
            
            if (entry->rec_len == 0) break;
            
            if (entry->inode == dir_ino &&
                entry->name_len == strlen(dirname) &&
                strncmp(entry->name, dirname, entry->name_len) == 0) {
                
                if (prev_entry) {
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    entry->inode = 0;
                }
                
                if (!ext2_write_block(fs.drive_index, block_no, block_data)) {
                    return false;
                }
                
                entry_removed = true;
                break;
            }
            
            prev_entry = entry;
            block_offset += entry->rec_len;
        }
        
        offset += fs.block_size;
        block_idx++;
    }
    
    if (!entry_removed) {
        LOG_ERROR("Failed to remove directory entry for: %s", path);
        return false;
    }
    
    // Free directory blocks
    for (block_idx = 0; block_idx < EXT2_NDIR_BLOCKS; block_idx++) {
        if (dir_inode.i_block[block_idx] != 0) {
            // TODO: implement block freeing
        }
    }
    
    // Update parent inode (decrease link count for ..)
    parent_inode.i_links_count--;
    
    // Update parent's directory count in block group
    uint32_t bg = (dir_ino - 1) / fs.inodes_per_group;
    fs.group_descs[bg].bg_used_dirs_count--;
    
    // Write parent inode
    if (!ext2_write_inode(fs.drive_index, parent_ino, &parent_inode)) {
        return false;
    }
    
    // Mark directory inode as deleted
    dir_inode.i_links_count = 0;
    dir_inode.i_dtime = 0;
    
    // Write directory inode
    return ext2_write_inode(fs.drive_index, dir_ino, &dir_inode);
 }