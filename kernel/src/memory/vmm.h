#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include <memory/pmm.h>

// Limine HHDM request declaration
extern volatile struct limine_hhdm_request hhdm_request;

// Page size definitions
#define PAGE_SIZE_4K  0x1000UL
#define PAGE_SIZE_2M  0x200000UL
#define PAGE_SIZE_1G  0x40000000UL

// Hardware page flags (used internally)
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITETHROUGH   (1ULL << 3)
#define PAGE_CACHE_DISABLE  (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NO_EXECUTE     (1ULL << 63)

// Public VMM flags for mapping
#define VMM_FLAG_PRESENT       (1ULL << 0)
#define VMM_FLAG_WRITABLE      (1ULL << 1)
#define VMM_FLAG_USER          (1ULL << 2)
#define VMM_FLAG_WRITETHROUGH  (1ULL << 3)
#define VMM_FLAG_NOCACHE       (1ULL << 4)
#define VMM_FLAG_GLOBAL        (1ULL << 8)
#define VMM_FLAG_NO_EXECUTE    (1ULL << 9)
#define VMM_FLAG_HUGE          (1ULL << 10)

// Address mask for page tables
#define PAGE_ADDR_MASK ~0xFFFULL

// VMM configuration structure
typedef struct {
    uint64_t kernel_pml4;           // Physical address of kernel PML4
    uint64_t kernel_virtual_base;   // Virtual base address of kernel
    uint64_t kernel_virtual_size;   // Size of kernel virtual space
    bool using_nx;                  // Whether NX bit is supported
    uint64_t hhdm_offset;           // HHDM offset from Limine
} vmm_config_t;

// Page table types
typedef uint64_t* pml4_t;          // Page Map Level 4 (512 entries)
typedef uint64_t* pdpt_t;          // Page Directory Pointer Table
typedef uint64_t* pd_t;            // Page Directory
typedef uint64_t* pt_t;            // Page Table

// Structure for address space
typedef struct {
    pml4_t pml4;                   // Virtual address of PML4
    uint64_t pml4_phys;            // Physical address of PML4 (for CR3)
} vmm_address_space_t;

// Memory region descriptor
typedef struct {
    uint64_t base;                 // Base virtual address
    uint64_t size;                 // Size in bytes
    uint32_t flags;                // Region flags
    bool is_used;                  // Whether region is in use
} vmm_memory_region_t;

// Initialize the virtual memory manager
void vmm_init(struct limine_memmap_response *memmap);

// Map a physical page to a virtual address with specified flags
bool vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

// Map multiple pages at once
bool vmm_map_pages(uint64_t virt_addr, uint64_t phys_addr, size_t count, uint64_t flags);

// Unmap a page at the specified virtual address
bool vmm_unmap_page(uint64_t virt_addr);

// Unmap multiple pages
bool vmm_unmap_pages(uint64_t virt_addr, size_t count);

// Get the physical address of a virtual address
uint64_t vmm_get_physical_address(uint64_t virt_addr);

// Check if a virtual address is mapped
bool vmm_is_mapped(uint64_t virt_addr);

// Create a new address space
uint64_t vmm_create_address_space(void);

// Delete an address space
void vmm_delete_address_space(uint64_t pml4_phys);

// Switch to a different address space
void vmm_switch_address_space(uint64_t pml4_phys);

// Get current address space
uint64_t vmm_get_current_address_space(void);

// Allocate virtual memory
void* vmm_allocate(size_t size, uint64_t flags);

// Free allocated memory
void vmm_free(void* addr, size_t size);

// Map physical memory to virtual address space
void* vmm_map_physical(uint64_t phys_addr, size_t size, uint64_t flags);

// Unmap previously mapped physical memory
void vmm_unmap_physical(void* virt_addr, size_t size);

// Handle page fault
bool vmm_handle_page_fault(uint64_t fault_addr, uint32_t error_code);

// Flush TLB for a specific address
void vmm_flush_tlb_page(uint64_t virt_addr);

// Flush entire TLB
void vmm_flush_tlb_full(void);

// Get VMM configuration
void vmm_get_config(vmm_config_t *config);

// Dump page tables for debugging
void vmm_dump_page_tables(uint64_t virt_addr);
void vmm_dump_page_flags(uint64_t entry);

#endif // VMM_H