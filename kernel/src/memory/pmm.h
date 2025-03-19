#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

// PMM configuration structure
typedef struct {
    uint64_t page_size;        // Size of each memory page (4096 bytes)
    uint64_t kernel_start;     // Start of managed memory region
    uint64_t kernel_end;       // End of managed memory region
    uint32_t max_pages;        // Maximum number of pages we can manage
    uint64_t total_memory;     // Total physical memory in the system
} pmm_config_t;

// Define block size for the bitmap (4KiB)
#define PMM_BLOCK_SIZE 4096

// Function to initialize the physical memory manager
void pmm_init(struct limine_memmap_response *memmap);

// Allocate a single physical memory page
void *pmm_alloc_page(void);

// Allocate multiple contiguous physical memory pages
void *pmm_alloc_pages(size_t count);

// Free a previously allocated physical memory page
void pmm_free_page(void *page_addr);

// Free multiple previously allocated physical memory pages
void pmm_free_pages(void *page_addr, size_t count);

// Check if a page is free
bool pmm_is_page_free(void *page_addr);

// Get amount of free physical memory in bytes
uint64_t pmm_get_free_memory(void);

// Get amount of used physical memory in bytes
uint64_t pmm_get_used_memory(void);

// Get PMM configuration
void pmm_get_info(pmm_config_t *config);

// Print memory statistics
void pmm_print_stats(void);

#endif // PMM_H