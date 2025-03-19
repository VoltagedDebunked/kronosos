#include <utils/sysinfo.h>
#include <utils/log.h>
#include <limine.h>
#include <stddef.h>
#include <memory/pmm.h>

extern volatile struct limine_memmap_request memmap_request;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

extern volatile struct limine_kernel_address_request kernel_addr_request;

void sysinfo_init(void) {
    if (memmap_request.response == NULL) {
        LOG_WARN_MSG("Memory map information not available");
    }
    
    if (bootloader_request.response == NULL) {
        LOG_WARN_MSG("Bootloader information not available");
    }
    
    if (kernel_addr_request.response == NULL) {
        LOG_WARN_MSG("Kernel address information not available");
    }
}

void sysinfo_print(void) {
    LOG_INFO_MSG("System Information:");
    
    if (bootloader_request.response) {
        LOG_INFO("Bootloader: %s %s", 
                 bootloader_request.response->name,
                 bootloader_request.response->version);
    }
    
    if (kernel_addr_request.response) {
        LOG_INFO("Kernel: physical=0x%X, virtual=0x%X",
                 kernel_addr_request.response->physical_base,
                 kernel_addr_request.response->virtual_base);
    }
    
    if (memmap_request.response) {
        LOG_INFO("Memory map entries: %d", memmap_request.response->entry_count);
        
        uint64_t total_usable = 0;
        
        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_request.response->entries[i];
            
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                total_usable += entry->length;
            }
        }
        
        LOG_INFO("Total usable memory: %d MB", total_usable / (1024 * 1024));
    }
    
    // Add PMM statistics to system info
    pmm_config_t config;
    pmm_get_info(&config);
    
    LOG_INFO("Memory Management:");
    LOG_INFO("  Managed range: 0x%X - 0x%X", config.kernel_start, config.kernel_end);
    LOG_INFO("  Managed size: %d MB", (config.kernel_end - config.kernel_start) / (1024 * 1024));
    LOG_INFO("  Free Memory: %d MB", pmm_get_free_memory() / (1024 * 1024));
    LOG_INFO("  Used Memory: %d MB", pmm_get_used_memory() / (1024 * 1024));
}