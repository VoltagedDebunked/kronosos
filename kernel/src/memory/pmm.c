#include <memory/pmm.h>
#include <utils/log.h>
#include <lib/string.h>
#include <lib/asm.h>

// Static PMM configuration
static pmm_config_t pmm_config = {0};

// Use a larger static bitmap for managing memory
#define STATIC_BITMAP_SIZE 8192  // 8KB = 64K pages = 256MB of memory
static uint8_t static_bitmap[STATIC_BITMAP_SIZE] = {0};
static uint8_t *page_bitmap = NULL;
static size_t bitmap_size = 0;

// Statistics tracking
static size_t total_allocations = 0;
static size_t failed_allocations = 0;

// Initialize the PMM with memory map data
void pmm_init(struct limine_memmap_response *memmap) {
    LOG_INFO_MSG("Initializing Physical Memory Manager");
    
    if (memmap == NULL) {
        LOG_CRITICAL_MSG("Memory map not available for PMM initialization");
        hcf();
    }
    
    // Fixed page size
    pmm_config.page_size = PMM_BLOCK_SIZE;
    
    // Calculate how many pages we can track with our static bitmap
    pmm_config.max_pages = STATIC_BITMAP_SIZE * 8;
    
    // Set up our bitmap to point to the static array
    page_bitmap = static_bitmap;
    bitmap_size = STATIC_BITMAP_SIZE;
    
    // Use the memory map to find a good starting address
    uint64_t start_address = 0x100000; // Default to 1MB if no better option
    size_t largest_usable_size = 0;
    
    // Count total memory and identify the largest contiguous usable region
    uint64_t total_memory = 0;
    
    LOG_INFO("Found %d memory map entries", memmap->entry_count);
    
    // First, dump all memory map entries to help diagnose issues
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        const char* type_str = "Unknown";
        switch (entry->type) {
            case LIMINE_MEMMAP_USABLE: type_str = "Usable"; break;
            case LIMINE_MEMMAP_RESERVED: type_str = "Reserved"; break;
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE: type_str = "ACPI Reclaimable"; break;
            case LIMINE_MEMMAP_ACPI_NVS: type_str = "ACPI NVS"; break;
            case LIMINE_MEMMAP_BAD_MEMORY: type_str = "Bad Memory"; break;
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: type_str = "Bootloader Reclaimable"; break;
            case LIMINE_MEMMAP_KERNEL_AND_MODULES: type_str = "Kernel and Modules"; break;
            case LIMINE_MEMMAP_FRAMEBUFFER: type_str = "Framebuffer"; break;
        }
        
        // Log only regions larger than 1MB to reduce log spam
        if (entry->length >= 1024*1024) {
            LOG_INFO("Memory Region %d: base=0x%X, length=%d MB, type=%s", 
                    i, entry->base, entry->length / (1024 * 1024), type_str);
        }
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_memory += entry->length;
            
            // Find the largest usable region bigger than 1MB that starts above 1MB
            if (entry->length > largest_usable_size && entry->base >= 0x100000) {
                largest_usable_size = entry->length;
                start_address = entry->base;
            }
        }
    }
    
    LOG_INFO("Total memory: %d MB", total_memory / (1024 * 1024));
    LOG_INFO("Found usable region at 0x%X (%d MB)", 
           start_address, largest_usable_size / (1024 * 1024));
    
    // Store the base address for our managed memory region
    pmm_config.kernel_start = start_address;
    
    // Calculate how much memory we can actually manage
    size_t manageable_size = pmm_config.max_pages * pmm_config.page_size;
    if (manageable_size > largest_usable_size) {
        // Adjust downward if we can't track that much memory
        pmm_config.max_pages = largest_usable_size / pmm_config.page_size;
        LOG_INFO("Adjusted max pages to %d to fit in available memory", 
               pmm_config.max_pages);
    }
    
    // Calculate the end address of our managed memory region
    pmm_config.kernel_end = start_address + (pmm_config.max_pages * pmm_config.page_size);
    
    // Initialize all memory as free (already zeroed in our static array)
    memset(page_bitmap, 0, bitmap_size);
    
    // Now mark specific regions as used based on memory map
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        // Skip usable regions and regions outside our management range
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            continue;
        }
        
        // Skip regions completely outside our managed range
        if (entry->base >= pmm_config.kernel_end || 
            entry->base + entry->length <= pmm_config.kernel_start) {
            continue;
        }
        
        // Calculate overlap with our managed memory
        uint64_t region_start = entry->base;
        uint64_t region_end = entry->base + entry->length;
        
        // Adjust to fit within our managed range
        if (region_start < pmm_config.kernel_start) {
            region_start = pmm_config.kernel_start;
        }
        
        if (region_end > pmm_config.kernel_end) {
            region_end = pmm_config.kernel_end;
        }
        
        // Mark the overlapping part as used
        for (uint64_t addr = region_start; addr < region_end; addr += pmm_config.page_size) {
            // Calculate offset from base
            uint64_t offset = addr - pmm_config.kernel_start;
            size_t page_index = offset / pmm_config.page_size;
            
            // Ensure we're within bounds of our bitmap
            if (page_index < pmm_config.max_pages) {
                // Mark as used in bitmap
                size_t byte_index = page_index / 8;
                uint8_t bit_mask = 1 << (page_index % 8);
                page_bitmap[byte_index] |= bit_mask;
            }
        }
    }
    
    // Also mark the first few pages as used to avoid conflicts with low memory
    for (size_t i = 0; i < 256 && i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        page_bitmap[byte_index] |= bit_mask;
    }
    
    // Store total memory size
    pmm_config.total_memory = total_memory;
    
    LOG_INFO("PMM managing memory from 0x%X to 0x%X (%d MB)", 
           pmm_config.kernel_start, pmm_config.kernel_end, 
           (unsigned int)((pmm_config.kernel_end - pmm_config.kernel_start) / (1024 * 1024)));
    
    LOG_INFO_MSG("Physical Memory Manager initialized");
}

// Allocate a single physical page
void *pmm_alloc_page(void) {
    if (!page_bitmap) {
        LOG_ERROR_MSG("PMM not initialized");
        return NULL;
    }
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        // Check if page is free
        if (!(page_bitmap[byte_index] & bit_mask)) {
            // Mark as used
            page_bitmap[byte_index] |= bit_mask;
            
            // Calculate physical address
            uint64_t phys_addr = pmm_config.kernel_start + (i * pmm_config.page_size);
            
            total_allocations++;
            LOG_DEBUG("PMM: Allocated page at 0x%X", phys_addr);
            return (void *)phys_addr;
        }
    }
    
    failed_allocations++;
    LOG_WARN_MSG("PMM: Failed to allocate page - no free pages");
    return NULL; // No free pages
}

// Allocate multiple consecutive physical pages
void *pmm_alloc_pages(size_t count) {
    if (!page_bitmap || count == 0) {
        return NULL;
    }
    
    // For small allocations, use the simple approach
    if (count == 1) {
        return pmm_alloc_page();
    }
    
    // For larger allocations, find a continuous run of free pages
    size_t found_pages = 0;
    size_t start_page = 0;
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (!(page_bitmap[byte_index] & bit_mask)) {
            // This page is free
            if (found_pages == 0) {
                // Start of a new run
                start_page = i;
            }
            
            found_pages++;
            
            if (found_pages == count) {
                // Found enough consecutive pages, mark them all as used
                for (size_t j = 0; j < count; j++) {
                    size_t page_idx = start_page + j;
                    size_t byte_idx = page_idx / 8;
                    uint8_t mask = 1 << (page_idx % 8);
                    page_bitmap[byte_idx] |= mask;
                }
                
                // Calculate physical address
                uint64_t phys_addr = pmm_config.kernel_start + (start_page * pmm_config.page_size);
                
                total_allocations++;
                LOG_DEBUG("PMM: Allocated %d contiguous pages at 0x%X", count, phys_addr);
                return (void *)phys_addr;
            }
        } else {
            // This page is used, reset counter
            found_pages = 0;
        }
    }
    
    failed_allocations++;
    LOG_WARN("PMM: Failed to allocate %d contiguous pages", count);
    return NULL; // Couldn't find enough consecutive pages
}

// Free a physical page
void pmm_free_page(void *page_addr) {
    uint64_t page = (uint64_t)page_addr;
    
    if (!page_bitmap) {
        return;
    }
    
    // Check if this page is in our managed range
    if (page < pmm_config.kernel_start || 
        page >= pmm_config.kernel_end ||
        (page % pmm_config.page_size) != 0) {
        LOG_WARN("PMM: Attempted to free invalid page address: 0x%X", page);
        return;
    }
    
    // Calculate the page index
    size_t page_index = (page - pmm_config.kernel_start) / pmm_config.page_size;
    
    if (page_index >= pmm_config.max_pages) {
        LOG_WARN("PMM: Page index out of range: %d", page_index);
        return;
    }
    
    size_t byte_index = page_index / 8;
    uint8_t bit_mask = 1 << (page_index % 8);
    
    // Check if the page is already marked as free
    if (!(page_bitmap[byte_index] & bit_mask)) {
        LOG_WARN("PMM: Attempted to free already free page at 0x%X", page);
        return;
    }
    
    // Mark as free
    page_bitmap[byte_index] &= ~bit_mask;
    LOG_DEBUG("PMM: Freed page at 0x%X", page);
}

// Free multiple consecutive physical pages
void pmm_free_pages(void *page_addr, size_t count) {
    uint64_t page = (uint64_t)page_addr;
    
    if (!page_bitmap || count == 0) {
        return;
    }
    
    // For single page, use the simple approach
    if (count == 1) {
        pmm_free_page(page_addr);
        return;
    }
    
    // Check if this page range is in our managed range
    if (page < pmm_config.kernel_start || 
        page >= pmm_config.kernel_end ||
        (page % pmm_config.page_size) != 0) {
        LOG_WARN("PMM: Attempted to free invalid page range starting at: 0x%X", page);
        return;
    }
    
    uint64_t end_page = page + (count * pmm_config.page_size);
    if (end_page > pmm_config.kernel_end) {
        LOG_WARN("PMM: Page range extends beyond managed memory: 0x%X-0x%X", page, end_page);
        count = (pmm_config.kernel_end - page) / pmm_config.page_size;
        LOG_WARN("PMM: Adjusting to free only %d pages", count);
    }
    
    // Free all pages in the range
    for (size_t i = 0; i < count; i++) {
        pmm_free_page((void *)(page + (i * pmm_config.page_size)));
    }
    
    LOG_DEBUG("PMM: Freed %d pages starting at 0x%X", count, page);
}

// Check if a page is free
bool pmm_is_page_free(void *page_addr) {
    uint64_t page = (uint64_t)page_addr;
    
    if (!page_bitmap) {
        return false;
    }
    
    // Check if this page is in our managed range
    if (page < pmm_config.kernel_start || 
        page >= pmm_config.kernel_end ||
        (page % pmm_config.page_size) != 0) {
        return false;
    }
    
    // Calculate the page index
    size_t page_index = (page - pmm_config.kernel_start) / pmm_config.page_size;
    
    if (page_index >= pmm_config.max_pages) {
        return false;
    }
    
    size_t byte_index = page_index / 8;
    uint8_t bit_mask = 1 << (page_index % 8);
    
    return !(page_bitmap[byte_index] & bit_mask);
}

// Get total free memory
uint64_t pmm_get_free_memory(void) {
    if (!page_bitmap) {
        return 0;
    }
    
    size_t free_pages = 0;
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (!(page_bitmap[byte_index] & bit_mask)) {
            free_pages++;
        }
    }
    
    return free_pages * pmm_config.page_size;
}

// Get total used memory
uint64_t pmm_get_used_memory(void) {
    if (!page_bitmap) {
        return 0;
    }
    
    size_t used_pages = 0;
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (page_bitmap[byte_index] & bit_mask) {
            used_pages++;
        }
    }
    
    return used_pages * pmm_config.page_size;
}

// Get PMM configuration
void pmm_get_info(pmm_config_t *config) {
    if (config) {
        *config = pmm_config;
    }
}

// Print memory statistics
void pmm_print_stats(void) {
    if (!page_bitmap) {
        LOG_WARN_MSG("PMM not initialized");
        return;
    }
    
    size_t free_pages = 0;
    size_t used_pages = 0;
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (page_bitmap[byte_index] & bit_mask) {
            used_pages++;
        } else {
            free_pages++;
        }
    }
    
    LOG_INFO("PMM Statistics:");
    LOG_INFO("  Total pages: %d", pmm_config.max_pages);
    LOG_INFO("  Used pages: %d (%d MB)", used_pages, (used_pages * pmm_config.page_size) / (1024 * 1024));
    LOG_INFO("  Free pages: %d (%d MB)", free_pages, (free_pages * pmm_config.page_size) / (1024 * 1024));
    LOG_INFO("  Total allocations: %d", total_allocations);
    LOG_INFO("  Failed allocations: %d", failed_allocations);
    LOG_INFO("  Memory range: 0x%X - 0x%X", pmm_config.kernel_start, pmm_config.kernel_end);
}