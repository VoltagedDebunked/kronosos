#include <memory/vmm.h>
#include <memory/pmm.h>
#include <utils/log.h>
#include <lib/string.h>
#include <lib/asm.h>
#include <core/idt.h>
#include <stdint.h>

// Limine HHDM (Higher Half Direct Mapping) reques
__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

// Limine Kernel Address request
__attribute__((used, section(".limine_requests")))
volatile struct limine_kernel_address_request kernel_addr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

// Maximum memory regions for dynamic allocation
#define MAX_MEMORY_AREAS 32

// Page table entry indices calculations
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

// VMM configuration and state
static vmm_config_t vmm_config;
static uint64_t hhdm_offset;
static uint64_t kernel_phys_base;
static uint64_t kernel_virt_base;
static uint64_t current_pml4_phys;

// Memory regions for dynamic allocation
static vmm_memory_region_t user_areas[MAX_MEMORY_AREAS];
static vmm_memory_region_t kernel_areas[MAX_MEMORY_AREAS];
static int user_area_count = 0;
static int kernel_area_count = 0;

// Statistics for memory usage
static struct {
    size_t pages_allocated;
    size_t pages_freed;
    size_t page_faults_handled;
} vmm_stats = {0};

// Forward declarations
static void* phys_to_virt(uint64_t phys);
static uint64_t virt_to_phys(void* virt);
static bool map_page_internal(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
static void register_memory_area(vmm_memory_region_t* areas, int* count, uint64_t base, size_t size, uint32_t flags);
static void page_fault_handler(struct interrupt_frame *frame);
static uint64_t create_page_table(void);

// Read CR3 register
static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Write CR3 register
static inline void write_cr3(uint64_t cr3) {
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Invalidate TLB entry
static inline void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

// Convert physical address to virtual using HHDM
static void* phys_to_virt(uint64_t phys) {
    if (phys == 0) return NULL;
    return (void*)(phys + hhdm_offset);
}

// Convert virtual address to physical
static uint64_t virt_to_phys(void* virt) {
    uint64_t addr = (uint64_t)virt;
    
    // Handle higher half direct mapping
    if (addr >= hhdm_offset) {
        return addr - hhdm_offset;
    }
    
    // For other addresses, walk the page tables
    uint64_t pml4_idx = PML4_INDEX(addr);
    uint64_t pdpt_idx = PDPT_INDEX(addr);
    uint64_t pd_idx = PD_INDEX(addr);
    uint64_t pt_idx = PT_INDEX(addr);
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4 || !(pml4[pml4_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // Check for 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        return (pdpt[pdpt_idx] & PAGE_ADDR_MASK) + (addr & 0x3FFFFFFF);
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd || !(pd[pd_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // Check for 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & PAGE_ADDR_MASK) + (addr & 0x1FFFFF);
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // 4KB page
    return (pt[pt_idx] & PAGE_ADDR_MASK) + (addr & 0xFFF);
}

// Create a new page table
static uint64_t create_page_table(void) {
    void *page = pmm_alloc_page();
    if (page == NULL) {
        LOG_ERROR("Failed to allocate page for page table");
        return 0;
    }
    
    uint64_t phys_addr = virt_to_phys(page);
    memset(page, 0, PAGE_SIZE_4K);
    
    return phys_addr;
}

// Register a memory area for dynamic allocation
static void register_memory_area(vmm_memory_region_t* areas, int* count, uint64_t base, size_t size, uint32_t flags) {
    if (*count >= MAX_MEMORY_AREAS) {
        LOG_ERROR("VMM: Too many memory areas");
        return;
    }
    
    areas[*count].base = base;
    areas[*count].size = size;
    areas[*count].flags = flags;
    areas[*count].is_used = false;
    (*count)++;
}

// Find a free memory area of the requested size
static vmm_memory_region_t* find_free_area(vmm_memory_region_t* areas, int count, size_t size, uint32_t flags) {
    for (int i = 0; i < count; i++) {
        if (!areas[i].is_used && areas[i].size >= size) {
            return &areas[i];
        }
    }
    return NULL;
}

// Map a page in the specified page table
static bool map_page_internal(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4_phys || !virt || !phys) {
        return false;
    }
    
    // Prevent mapping the null page as a security measure
    if ((virt & PAGE_ADDR_MASK) == 0) {
        LOG_ERROR("Attempted to map the null page - operation aborted");
        return false;
    }
    
    // Align addresses to page boundaries
    virt &= PAGE_ADDR_MASK;
    phys &= PAGE_ADDR_MASK;
    
    LOG_DEBUG("Mapping virt 0x%llX to phys 0x%llX with flags 0x%llX", virt, phys, flags);
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    if (!pml4) {
        return false;
    }
    
    // Get indices
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    // Ensure PDPT exists
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = create_page_table();
        if (pdpt_phys == 0) {
            LOG_ERROR("Failed to create PDPT for virtual address 0x%llX", virt);
            return false;
        }
        
        LOG_DEBUG("Created new PDPT at physical address 0x%llX", pdpt_phys);
        
        pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages (lower half)
        if (virt < 0x8000000000000000ULL) {
            pml4[pml4_idx] |= PAGE_USER;
        }
    }
    
    // Get PDPT
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt) {
        return false;
    }
    
    // Handle 1GB pages
    if ((flags & PAGE_HUGE) && ((virt & 0x3FFFFFFF) == 0) && ((phys & 0x3FFFFFFF) == 0)) {
        // 1GB alignment
        pdpt[pdpt_idx] = phys | flags | PAGE_HUGE;
        invlpg(virt);
        return true;
    }
    
    // Ensure PD exists
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uint64_t pd_phys = create_page_table();
        if (pd_phys == 0) {
            LOG_ERROR("Failed to create PD for virtual address 0x%llX", virt);
            return false;
        }
        
        LOG_DEBUG("Created new PD at physical address 0x%llX", pd_phys);
        
        pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages
        if (virt < 0x8000000000000000ULL) {
            pdpt[pdpt_idx] |= PAGE_USER;
        }
    }
    
    // Get PD
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd) {
        return false;
    }
    
    // Handle 2MB pages
    if ((flags & PAGE_HUGE) && ((virt & 0x1FFFFF) == 0) && ((phys & 0x1FFFFF) == 0)) {
        // 2MB alignment
        pd[pd_idx] = phys | flags | PAGE_HUGE;
        invlpg(virt);
        return true;
    }
    
    // Ensure PT exists
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t pt_phys = create_page_table();
        if (pt_phys == 0) {
            LOG_ERROR("Failed to create PT for virtual address 0x%llX", virt);
            return false;
        }
        
        LOG_DEBUG("Created new PT at physical address 0x%llX", pt_phys);
        
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages
        if (virt < 0x8000000000000000ULL) {
            pd[pd_idx] |= PAGE_USER;
        }
    }
    
    // Get PT
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt) {
        return false;
    }
    
    // Check if page is already mapped
    if (pt[pt_idx] & PAGE_PRESENT) {
        LOG_WARN("Page at virtual address 0x%llX is already mapped, overwriting", virt);
    }
    
    // Map the page
    pt[pt_idx] = phys | flags;
    
    // Invalidate TLB
    invlpg(virt);
    
    return true;
}

static void uint64_to_hex(uint64_t value, char* buffer) {
    const char hex_digits[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 16; i > 1; i--) {
        buffer[i] = hex_digits[value & 0xF];
        value >>= 4;
    }
    buffer[17] = '\0';
 }
 
 static void page_fault_handler(struct interrupt_frame *frame) {
    uint64_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    uint64_t error_code = frame->error_code;
    
    char fault_addr_str[19];
    char rip_str[19];
    char phys_addr_str[19];
 
    uint64_to_hex(fault_addr, fault_addr_str);
    uint64_to_hex(frame->rip, rip_str);
 
    uint64_t phys_addr = vmm_get_physical_address(fault_addr);
    uint64_to_hex(phys_addr, phys_addr_str);
 
    LOG_ERROR("\n!!! PAGE FAULT !!!");
    LOG_ERROR("Page Fault Details:");
    LOG_ERROR("  Fault Address: %s", fault_addr_str);
    LOG_ERROR("  Error Code: 0x%X", (unsigned int)error_code);
    LOG_ERROR("  Instruction Pointer: %s", rip_str);
    LOG_ERROR("  Address mapped to physical: %s", phys_addr_str);
    LOG_ERROR("Page fault at %s, error code 0x%X", fault_addr_str, (unsigned int)error_code);

    while (1) {
        hcf();
    }
 }

// Initialize the virtual memory manager
void vmm_init(struct limine_memmap_response *memmap) {
    LOG_INFO("Initializing VMM");
    
    // Get HHDM from Limine
    if (hhdm_request.response) {
        hhdm_offset = hhdm_request.response->offset;
        LOG_INFO("HHDM offset: 0x%llX", hhdm_offset);
    } else {
        LOG_WARN("HHDM response not available, using default");
        hhdm_offset = 0xffff800000000000ULL;
    }
    
    // Get kernel address info
    if (kernel_addr_request.response) {
        kernel_phys_base = kernel_addr_request.response->physical_base;
        kernel_virt_base = kernel_addr_request.response->virtual_base;
        LOG_INFO("Kernel physical base: 0x%llX", kernel_phys_base);
        LOG_INFO("Kernel virtual base: 0x%llX", kernel_virt_base);
    } else {
        LOG_WARN("Kernel address response not available");
        kernel_phys_base = 0x100000; // 1MB default
        kernel_virt_base = hhdm_offset + kernel_phys_base;
    }
    
    // Get CR3 (physical address of PML4)
    current_pml4_phys = read_cr3();
    LOG_INFO("Current PML4 physical address: 0x%llX", current_pml4_phys);
    
    // Store configuration
    vmm_config.kernel_pml4 = current_pml4_phys;
    vmm_config.kernel_virtual_base = kernel_virt_base;
    vmm_config.kernel_virtual_size = 0x10000000; // 256MB by default
    vmm_config.hhdm_offset = hhdm_offset;
    
    // Check for NX bit support
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" 
                : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                : "a"(0x80000001));
    vmm_config.using_nx = (edx & (1 << 20)) != 0;
    LOG_INFO("NX bit %s", vmm_config.using_nx ? "supported" : "not supported");
    
    // Register page fault handler
    idt_register_handler(14, page_fault_handler);
    
    // Set up memory areas for kernel
    register_memory_area(kernel_areas, &kernel_area_count, 
                        hhdm_offset + 0x10000000, // Start at 256MB physical
                        0x10000000, // 256MB size
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    
    // Set up memory areas for user mode
    register_memory_area(user_areas, &user_area_count,
                        0x400000, // Start at 4MB
                        0x10000000, // 256MB size
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    
    LOG_INFO("VMM initialized successfully");
}

// Map a physical page to a virtual address with specified flags
bool vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (virt_addr == 0) {
        LOG_ERROR("Cannot map null virtual address");
        return false;
    }
    
    if (phys_addr == 0) {
        LOG_ERROR("Cannot map null physical address");
        return false;
    }
    
    // Align addresses to page boundaries
    virt_addr &= PAGE_ADDR_MASK;
    phys_addr &= PAGE_ADDR_MASK;
    
    LOG_DEBUG("Mapping virt 0x%lX to phys 0x%lX with flags 0x%lX", virt_addr, phys_addr, flags);
    
    // Get the PML4
    uint64_t* pml4 = phys_to_virt(current_pml4_phys);
    if (!pml4) {
        LOG_ERROR("Cannot access PML4");
        return false;
    }
    
    // Get indices for page tables
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;
    
    LOG_DEBUG("Indices: PML4=%lu, PDPT=%lu, PD=%lu, PT=%lu", 
             pml4_idx, pdpt_idx, pd_idx, pt_idx);
    
    // Convert VMM flags to hardware flags
    uint64_t hw_flags = PAGE_PRESENT;
    if (flags & VMM_FLAG_WRITABLE) hw_flags |= PAGE_WRITABLE;
    if (flags & VMM_FLAG_USER) hw_flags |= PAGE_USER;
    if (flags & VMM_FLAG_WRITETHROUGH) hw_flags |= PAGE_WRITETHROUGH;
    if (flags & VMM_FLAG_NOCACHE) hw_flags |= PAGE_CACHE_DISABLE;
    if (flags & VMM_FLAG_GLOBAL) hw_flags |= PAGE_GLOBAL;
    if (flags & VMM_FLAG_HUGE) hw_flags |= PAGE_HUGE;
    if ((flags & VMM_FLAG_NO_EXECUTE) && vmm_config.using_nx) hw_flags |= PAGE_NO_EXECUTE;
    
    // Ensure PDPT exists
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = (uint64_t)pmm_alloc_page();
        if (!pdpt_phys) {
            LOG_ERROR("Failed to allocate PDPT");
            return false;
        }
        memset(phys_to_virt(pdpt_phys), 0, PAGE_SIZE_4K);
        pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        // Propagate user access if in user space
        if (virt_addr < 0x8000000000000000ULL) pml4[pml4_idx] |= PAGE_USER;
        LOG_DEBUG("Created new PDPT at 0x%lX", pdpt_phys);
    }
    
    // Get PDPT
    uint64_t pdpt_phys = pml4[pml4_idx] & PAGE_ADDR_MASK;
    uint64_t* pdpt = phys_to_virt(pdpt_phys);
    if (!pdpt) {
        LOG_ERROR("Cannot access PDPT");
        return false;
    }
    
    // Ensure PD exists
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uint64_t pd_phys = (uint64_t)pmm_alloc_page();
        if (!pd_phys) {
            LOG_ERROR("Failed to allocate PD");
            return false;
        }
        memset(phys_to_virt(pd_phys), 0, PAGE_SIZE_4K);
        pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
        // Propagate user access if in user space
        if (virt_addr < 0x8000000000000000ULL) pdpt[pdpt_idx] |= PAGE_USER;
        LOG_DEBUG("Created new PD at 0x%lX", pd_phys);
    }
    
    // Get PD
    uint64_t pd_phys = pdpt[pdpt_idx] & PAGE_ADDR_MASK;
    uint64_t* pd = phys_to_virt(pd_phys);
    if (!pd) {
        LOG_ERROR("Cannot access PD");
        return false;
    }
    
    // Ensure PT exists
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t pt_phys = (uint64_t)pmm_alloc_page();
        if (!pt_phys) {
            LOG_ERROR("Failed to allocate PT");
            return false;
        }
        memset(phys_to_virt(pt_phys), 0, PAGE_SIZE_4K);
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        // Propagate user access if in user space
        if (virt_addr < 0x8000000000000000ULL) pd[pd_idx] |= PAGE_USER;
        LOG_DEBUG("Created new PT at 0x%lX", pt_phys);
    }
    
    // Get PT
    uint64_t pt_phys = pd[pd_idx] & PAGE_ADDR_MASK;
    uint64_t* pt = phys_to_virt(pt_phys);
    if (!pt) {
        LOG_ERROR("Cannot access PT");
        return false;
    }
    
    // Check if already mapped
    if (pt[pt_idx] & PAGE_PRESENT) {
        LOG_WARN("0x%lX is already mapped to 0x%lX - overwriting", 
                virt_addr, pt[pt_idx] & PAGE_ADDR_MASK);
    }
    
    // Set the page table entry
    pt[pt_idx] = phys_addr | hw_flags;
    
    // Invalidate TLB entry
    asm volatile("invlpg (%0)" : : "r" (virt_addr) : "memory");
    
    LOG_DEBUG("Successfully mapped 0x%lX to 0x%lX", virt_addr, phys_addr);
    return true;
}

// Unmap a virtual page
bool vmm_unmap_page(uint64_t virt_addr) {
    if (virt_addr == 0) {
        LOG_ERROR("Cannot unmap null address");
        return false;
    }
    
    // Align address to page boundary
    virt_addr &= PAGE_ADDR_MASK;
    
    // Get the PML4
    uint64_t* pml4 = phys_to_virt(current_pml4_phys);
    if (!pml4) {
        LOG_ERROR("Cannot access PML4");
        return false;
    }
    
    // Get indices for page tables
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;
    
    // Check if PML4 entry exists
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        LOG_WARN("Address 0x%lX not mapped at PML4 level", virt_addr);
        return false;
    }
    
    // Get PDPT
    uint64_t pdpt_phys = pml4[pml4_idx] & PAGE_ADDR_MASK;
    uint64_t* pdpt = phys_to_virt(pdpt_phys);
    if (!pdpt) {
        LOG_ERROR("Cannot access PDPT for 0x%lX", virt_addr);
        return false;
    }
    
    // Check if PDPT entry exists
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        LOG_WARN("Address 0x%lX not mapped at PDPT level", virt_addr);
        return false;
    }
    
    // Check for 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        LOG_DEBUG("Unmapping 1GB page at 0x%lX", virt_addr);
        pdpt[pdpt_idx] = 0;
        asm volatile("invlpg (%0)" : : "r" (virt_addr) : "memory");
        return true;
    }
    
    // Get PD
    uint64_t pd_phys = pdpt[pdpt_idx] & PAGE_ADDR_MASK;
    uint64_t* pd = phys_to_virt(pd_phys);
    if (!pd) {
        LOG_ERROR("Cannot access PD for 0x%lX", virt_addr);
        return false;
    }
    
    // Check if PD entry exists
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        LOG_WARN("Address 0x%lX not mapped at PD level", virt_addr);
        return false;
    }
    
    // Check for 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        LOG_DEBUG("Unmapping 2MB page at 0x%lX", virt_addr);
        pd[pd_idx] = 0;
        asm volatile("invlpg (%0)" : : "r" (virt_addr) : "memory");
        return true;
    }
    
    // Get PT
    uint64_t pt_phys = pd[pd_idx] & PAGE_ADDR_MASK;
    uint64_t* pt = phys_to_virt(pt_phys);
    if (!pt) {
        LOG_ERROR("Cannot access PT for 0x%lX", virt_addr);
        return false;
    }
    
    // Check if page is mapped
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        LOG_WARN("Address 0x%lX not mapped at PT level", virt_addr);
        return false;
    }
    
    // Unmap the page
    pt[pt_idx] = 0;
    
    // Invalidate TLB entry
    asm volatile("invlpg (%0)" : : "r" (virt_addr) : "memory");
    
    LOG_DEBUG("Successfully unmapped 0x%lX", virt_addr);
    return true;
}

// Map multiple pages
bool vmm_map_pages(uint64_t virt_addr, uint64_t phys_addr, size_t count, uint64_t flags) {
    // Check if we can use huge pages
    if ((flags & VMM_FLAG_HUGE) && 
        (virt_addr & 0x1FFFFF) == 0 && 
        (phys_addr & 0x1FFFFF) == 0 && 
        count >= 512) {
        
        // Use 2MB pages
        size_t huge_pages = count / 512;
        size_t remaining = count % 512;
        
        // Map huge pages
        for (size_t i = 0; i < huge_pages; i++) {
            if (!vmm_map_page(virt_addr + i * PAGE_SIZE_2M, 
                              phys_addr + i * PAGE_SIZE_2M, 
                              flags)) {
                // Clean up on failure
                for (size_t j = 0; j < i; j++) {
                    vmm_unmap_page(virt_addr + j * PAGE_SIZE_2M);
                }
                return false;
            }
        }
        
        // Map remaining regular pages if any
        if (remaining > 0) {
            uint64_t start_virt = virt_addr + huge_pages * PAGE_SIZE_2M;
            uint64_t start_phys = phys_addr + huge_pages * PAGE_SIZE_2M;
            
            for (size_t i = 0; i < remaining; i++) {
                if (!vmm_map_page(start_virt + i * PAGE_SIZE_4K,
                                 start_phys + i * PAGE_SIZE_4K,
                                 flags & ~VMM_FLAG_HUGE)) {
                    // Clean up on failure
                    for (size_t j = 0; j < i; j++) {
                        vmm_unmap_page(start_virt + j * PAGE_SIZE_4K);
                    }
                    for (size_t j = 0; j < huge_pages; j++) {
                        vmm_unmap_page(virt_addr + j * PAGE_SIZE_2M);
                    }
                    return false;
                }
            }
        }
        
        return true;
    }
    
    // Use regular 4KB pages
    for (size_t i = 0; i < count; i++) {
        if (!vmm_map_page(virt_addr + i * PAGE_SIZE_4K, 
                         phys_addr + i * PAGE_SIZE_4K, 
                         flags & ~VMM_FLAG_HUGE)) {
            // Clean up on failure
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(virt_addr + j * PAGE_SIZE_4K);
            }
            return false;
        }
    }
    
    return true;
}

// Unmap multiple pages
bool vmm_unmap_pages(uint64_t virt_addr, size_t count) {
    // Check each page since we might have mixed page sizes
    for (size_t i = 0; i < count; i++) {
        vmm_unmap_page(virt_addr + i * PAGE_SIZE_4K);
    }
    return true;
}

// Get physical address for a virtual address
uint64_t vmm_get_physical_address(uint64_t virt_addr) {
    return virt_to_phys((void*)virt_addr);
}

// Check if address is mapped
bool vmm_is_mapped(uint64_t virt_addr) {
    // Handle direct mapping range
    if (virt_addr >= hhdm_offset) {
        return true;
    }
    
    // Navigate the page tables
    uint64_t pml4_idx = PML4_INDEX(virt_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virt_addr);
    uint64_t pd_idx = PD_INDEX(virt_addr);
    uint64_t pt_idx = PT_INDEX(virt_addr);
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4 || !(pml4[pml4_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Check for 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        return true;
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd || !(pd[pd_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Check for 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        return true;
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    return true;
}

// Create a new address space
uint64_t vmm_create_address_space(void) {
    // Allocate a physical page for the PML4
    uint64_t pml4_phys = create_page_table();
    if (pml4_phys == 0) {
        return 0;
    }
    
    // Get current PML4
    uint64_t* src_pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    uint64_t* new_pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    
    // Copy kernel space mappings (higher half)
    for (size_t i = 256; i < 512; i++) {
        new_pml4[i] = src_pml4[i];
    }
    
    return pml4_phys;
}

// Delete an address space
void vmm_delete_address_space(uint64_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == current_pml4_phys) {
        return;
    }
    
    // Get the PML4 table
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    
    // Free user space page tables (lower half)
    for (size_t pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (pml4[pml4_idx] & PAGE_PRESENT) {
            uint64_t pdpt_phys = pml4[pml4_idx] & PAGE_ADDR_MASK;
            uint64_t* pdpt = (uint64_t*)phys_to_virt(pdpt_phys);
            
            // Free PDPTs
            for (size_t pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
                if ((pdpt[pdpt_idx] & PAGE_PRESENT) && !(pdpt[pdpt_idx] & PAGE_HUGE)) {
                    uint64_t pd_phys = pdpt[pdpt_idx] & PAGE_ADDR_MASK;
                    uint64_t* pd = (uint64_t*)phys_to_virt(pd_phys);
                    
                    // Free PDs
                    for (size_t pd_idx = 0; pd_idx < 512; pd_idx++) {
                        if ((pd[pd_idx] & PAGE_PRESENT) && !(pd[pd_idx] & PAGE_HUGE)) {
                            uint64_t pt_phys = pd[pd_idx] & PAGE_ADDR_MASK;
                            
                            // Free the page table
                            pmm_free_page((void*)pt_phys);
                        }
                    }
                    
                    // Free the page directory
                    pmm_free_page((void*)pd_phys);
                }
            }
            
            // Free the PDPT
            pmm_free_page((void*)pdpt_phys);
        }
    }
    
    // Free the PML4 itself
    pmm_free_page((void*)pml4_phys);
}

// Switch to a different address space
void vmm_switch_address_space(uint64_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == current_pml4_phys) {
        return;
    }
    
    // Update our tracking
    current_pml4_phys = pml4_phys;
    
    // Load the new CR3
    write_cr3(pml4_phys);
}

// Get current address space
uint64_t vmm_get_current_address_space(void) {
    return current_pml4_phys;
}

// Allocate virtual memory
void* vmm_allocate(size_t size, uint64_t flags) {
    if (size == 0) {
        return NULL;
    }
    
    // Round up to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    size_t page_count = size / PAGE_SIZE_4K;
    
    // Choose appropriate areas based on flags
    vmm_memory_region_t* areas;
    int* area_count;
    
    if (flags & VMM_FLAG_USER) {
        areas = user_areas;
        area_count = &user_area_count;
    } else {
        areas = kernel_areas;
        area_count = &kernel_area_count;
    }
    
    // Find a free area
    vmm_memory_region_t* area = find_free_area(areas, *area_count, size, flags);
    if (!area) {
        LOG_ERROR("VMM: No free memory area for allocation of size %zu", size);
        return NULL;
    }
    
    // Mark the area as used
    area->is_used = true;
    
    // Allocate physical pages and map them
    for (size_t i = 0; i < page_count; i++) {
        void* phys = pmm_alloc_page();
        if (phys == NULL) {
            // Out of physical memory, clean up
            for (size_t j = 0; j < i; j++) {
                uint64_t addr = area->base + j * PAGE_SIZE_4K;
                uint64_t page_phys = vmm_get_physical_address(addr);
                vmm_unmap_page(addr);
                pmm_free_page((void*)page_phys);
            }
            area->is_used = false;
            return NULL;
        }
        
        uint64_t phys_addr = (uint64_t)phys;
        
        // Apply additional flags
        uint64_t combined_flags = flags | area->flags;
        
        // Map the page
        if (!vmm_map_page(area->base + i * PAGE_SIZE_4K, phys_addr, combined_flags)) {
            // Failed to map, clean up
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uint64_t addr = area->base + j * PAGE_SIZE_4K;
                uint64_t page_phys = vmm_get_physical_address(addr);
                vmm_unmap_page(addr);
                pmm_free_page((void*)page_phys);
            }
            area->is_used = false;
            return NULL;
        }
        
        // Zero the memory
        memset(phys_to_virt(phys_addr), 0, PAGE_SIZE_4K);
    }
    
    // Update statistics
    vmm_stats.pages_allocated += page_count;
    
    return (void*)area->base;
}

// Free allocated memory
void vmm_free(void* addr, size_t size) {
    if (!addr || size == 0) {
        return;
    }
    
    uint64_t virt_addr = (uint64_t)addr;
    
    // Round up to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    size_t page_count = size / PAGE_SIZE_4K;
    
    // Free each page
    for (size_t i = 0; i < page_count; i++) {
        uint64_t page_addr = virt_addr + i * PAGE_SIZE_4K;
        uint64_t phys_addr = vmm_get_physical_address(page_addr);
        
        if (phys_addr) {
            vmm_unmap_page(page_addr);
            pmm_free_page((void*)phys_addr);
        }
    }
    
    // Find and free the memory area
    vmm_memory_region_t* areas;
    int count;
    
    if (virt_addr >= hhdm_offset) {
        areas = kernel_areas;
        count = kernel_area_count;
    } else {
        areas = user_areas;
        count = user_area_count;
    }
    
    for (int i = 0; i < count; i++) {
        if (areas[i].base == virt_addr) {
            areas[i].is_used = false;
            break;
        }
    }
    
    // Update statistics
    vmm_stats.pages_freed += page_count;
}

// Map physical memory to virtual address space
void* vmm_map_physical(uint64_t phys_addr, size_t size, uint64_t flags) {
    if (phys_addr == 0 || size == 0) {
        return NULL;
    }
    
    // Round to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    
    // For physical addresses already in HHDM range, just return the corresponding virtual address
    if (phys_addr < 0x100000000ULL) {
        void* virt_addr = phys_to_virt(phys_addr);
        return virt_addr;
    }
    
    // For high physical addresses, we need a custom mapping
    // Find a free memory area
    vmm_memory_region_t* area = find_free_area(kernel_areas, kernel_area_count, size, flags);
    if (!area) {
        LOG_ERROR("VMM: No free memory area for physical mapping of size %zu", size);
        return NULL;
    }
    
    // Mark the area as used
    area->is_used = true;
    void* virt_addr = (void*)area->base;
    
    // Map each page
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE_4K) {
        if (!vmm_map_page((uint64_t)virt_addr + offset, phys_addr + offset, flags)) {
            // Clean up on failure
            for (size_t i = 0; i < offset; i += PAGE_SIZE_4K) {
                vmm_unmap_page((uint64_t)virt_addr + i);
            }
            area->is_used = false;
            return NULL;
        }
    }
    
    return virt_addr;
}

// Unmap previously mapped physical memory
void vmm_unmap_physical(void* virt_addr, size_t size) {
    if (!virt_addr || size == 0) {
        return;
    }
    
    // Check if this is in the HHDM range
    if ((uint64_t)virt_addr >= hhdm_offset && (uint64_t)virt_addr < hhdm_offset + 0x100000000ULL) {
        // This is in the direct mapping range, don't unmap it
        return;
    }
    
    // Round to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    
    // Unmap each page
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE_4K) {
        vmm_unmap_page((uint64_t)virt_addr + offset);
    }
    
    // Find and free the memory area
    for (int i = 0; i < kernel_area_count; i++) {
        if (kernel_areas[i].base == (uint64_t)virt_addr) {
            kernel_areas[i].is_used = false;
            break;
        }
    }
}

// Handle page fault (minimal implementation)
bool vmm_handle_page_fault(uint64_t fault_addr, uint32_t error_code) {
    // For now, we don't handle any page faults
    // This will be expanded in future implementations
    LOG_ERROR("Page fault at 0x%llX, error code 0x%X", fault_addr, error_code);
    return false;
}

// Flush TLB for a specific address
void vmm_flush_tlb_page(uint64_t virt_addr) {
    invlpg(virt_addr);
}

// Flush entire TLB
void vmm_flush_tlb_full(void) {
    write_cr3(read_cr3());
}

// Get VMM configuration
void vmm_get_config(vmm_config_t *config) {
    if (config) {
        *config = vmm_config;
    }
}

// Dump page tables for debugging
void vmm_dump_page_tables(uint64_t virt_addr) {
    LOG_INFO("Page table info for address 0x%llX:", virt_addr);
    
    // Get indices
    uint64_t pml4_idx = PML4_INDEX(virt_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virt_addr);
    uint64_t pd_idx = PD_INDEX(virt_addr);
    uint64_t pt_idx = PT_INDEX(virt_addr);
    
    LOG_INFO("Indices: PML4=%llu, PDPT=%llu, PD=%llu, PT=%llu", 
           pml4_idx, pdpt_idx, pd_idx, pt_idx);
    
    // Navigate the page tables
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4) {
        LOG_ERROR("Cannot access PML4!");
        return;
    }
    
    LOG_INFO("PML4 entry: 0x%llX", pml4[pml4_idx]);
    
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        LOG_INFO("PML4 entry not present");
        return;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt) {
        LOG_ERROR("Cannot access PDPT!");
        return;
    }
    
    LOG_INFO("PDPT entry: 0x%llX", pdpt[pdpt_idx]);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        LOG_INFO("PDPT entry not present");
        return;
    }
    
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        LOG_INFO("1GB page at physical address 0x%llX", pdpt[pdpt_idx] & PAGE_ADDR_MASK);
        vmm_dump_page_flags(pdpt[pdpt_idx]);
        return;
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd) {
        LOG_ERROR("Cannot access PD!");
        return;
    }
    
    LOG_INFO("PD entry: 0x%llX", pd[pd_idx]);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        LOG_INFO("PD entry not present");
        return;
    }
    
    if (pd[pd_idx] & PAGE_HUGE) {
        LOG_INFO("2MB page at physical address 0x%llX", pd[pd_idx] & PAGE_ADDR_MASK);
        vmm_dump_page_flags(pd[pd_idx]);
        return;
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt) {
        LOG_ERROR("Cannot access PT!");
        return;
    }
    
    LOG_INFO("PT entry: 0x%llX", pt[pt_idx]);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        LOG_INFO("PT entry not present");
        return;
    }
    
    LOG_INFO("4KB page at physical address 0x%llX", pt[pt_idx] & PAGE_ADDR_MASK);
    vmm_dump_page_flags(pt[pt_idx]);
}

// Helper function to print page flags
void vmm_dump_page_flags(uint64_t entry) {
    LOG_INFO("Flags: %s%s%s%s%s%s%s%s%s",
           (entry & PAGE_PRESENT) ? "PRESENT " : "",
           (entry & PAGE_WRITABLE) ? "WRITABLE " : "",
           (entry & PAGE_USER) ? "USER " : "",
           (entry & PAGE_WRITETHROUGH) ? "WRITETHROUGH " : "",
           (entry & PAGE_CACHE_DISABLE) ? "NOCACHE " : "",
           (entry & PAGE_ACCESSED) ? "ACCESSED " : "",
           (entry & PAGE_DIRTY) ? "DIRTY " : "",
           (entry & PAGE_HUGE) ? "HUGE " : "",
           (entry & PAGE_GLOBAL) ? "GLOBAL " : "");
}