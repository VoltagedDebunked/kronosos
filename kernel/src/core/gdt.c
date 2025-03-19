#include <stdint.h>
#include <stdbool.h>
#include <lib/string.h>
#include <lib/asm.h>
#include "gdt.h"

// The GDT entries
static struct gdt_entry gdt[GDT_REAL_ENTRIES_COUNT];

// The GDT pointer
static struct gdt_ptr gdt_pointer;

// Backup of the GDT for recovery
static struct gdt_entry gdt_backup[GDT_REAL_ENTRIES_COUNT];
static struct gdt_ptr gdt_pointer_backup;

// TSS entry
static struct tss_entry tss;

// External assembly function to load the GDT
extern void gdt_load(struct gdt_ptr* gdt_ptr);

// External assembly function to load the TSS
extern void tss_load(uint16_t tss_segment);

// Setup a GDT entry
static void gdt_set_gate(uint8_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    // Setup the descriptor base address
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    // Setup the descriptor limits
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    // Finally, set up the granularity and access flags
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

// Set up the 64-bit TSS entry
static void gdt_set_tss(uint8_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    // Set up the standard descriptor
    gdt_set_gate(num, base, limit, access, gran);
    
    // The TSS descriptor in 64-bit mode is 16 bytes (spanning two 8-byte GDT entries)
    // We need to set up the high part (base bits 32:63)
    gdt[num + 1].limit_low = 0;
    gdt[num + 1].base_low = 0;
    gdt[num + 1].base_middle = 0;
    gdt[num + 1].access = 0;
    gdt[num + 1].granularity = 0;
    gdt[num + 1].base_high = 0;
    
    // Upper 32 bits of the TSS base address will be set up separately
    // in the gdt_init() function to avoid complexity here
}

// Initialize the TSS
static void tss_init(void) {
    // Clear the TSS
    memset(&tss, 0, sizeof(tss));
    
    // Set the IOPB offset beyond the end of the TSS to effectively disable it
    tss.iopb_offset = sizeof(tss);
    
    // We'll set up the stack pointers later when we know them
}

// Initialize the GDT
void gdt_init(void) {
    // Initialize TSS
    tss_init();
    
    // Set up the GDT pointer
    gdt_pointer.limit = (sizeof(struct gdt_entry) * GDT_REAL_ENTRIES_COUNT) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
    // NULL descriptor (required)
    gdt_set_gate(GDT_NULL, 0, 0, 0, 0);
    
    // Kernel code segment (ring 0)
    // Access: 0x9A = 1001 1010
    // Present(1) | DPL(00) | S(1) | Type(1010 = Code, Readable, Non-conforming, Accessed=0)
    // Granularity: 0xA0 = 1010 0000
    // G=1 (4KiB blocks) | D/B=0 (default op size is not 32bit) | L=1 (64-bit) | AVL=0
    gdt_set_gate(GDT_KERNEL_CODE, 0, 0xFFFFF, 0x9A, 0xA0);
    
    // Kernel data segment (ring 0)
    // Access: 0x92 = 1001 0010
    // Present(1) | DPL(00) | S(1) | Type(0010 = Data, Writable, Expand-up, Accessed=0)
    // Granularity: 0x80 = 1000 0000
    // G=1 | D/B=0 | L=0 (not a code segment) | AVL=0
    gdt_set_gate(GDT_KERNEL_DATA, 0, 0xFFFFF, 0x92, 0x80);
    
    // User code segment (ring 3)
    // Access: 0xFA = 1111 1010
    // Present(1) | DPL(11) | S(1) | Type(1010 = Code, Readable, Non-conforming, Accessed=0)
    // Granularity: 0xA0 = 1010 0000
    // G=1 | D/B=0 | L=1 (64-bit) | AVL=0
    gdt_set_gate(GDT_USER_CODE, 0, 0xFFFFF, 0xFA, 0xA0);
    
    // User data segment (ring 3)
    // Access: 0xF2 = 1111 0010
    // Present(1) | DPL(11) | S(1) | Type(0010 = Data, Writable, Expand-up, Accessed=0)
    // Granularity: 0x80 = 1000 0000
    // G=1 | D/B=0 | L=0 (not a code segment) | AVL=0
    gdt_set_gate(GDT_USER_DATA, 0, 0xFFFFF, 0xF2, 0x80);
    
    // TSS
    // Access: 0x89 = 1000 1001
    // Present(1) | DPL(00) | Type(1001 = 64-bit TSS, Available)
    // Granularity: 0x00 (no special flags needed for TSS)
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    gdt_set_tss(GDT_TSS, tss_base, tss_limit, 0x89, 0x00);
    
    // Set the upper 32 bits of the TSS base address in the second half of the descriptor
    // These need to be split across the 8-byte GDT entry as follows:
    gdt[GDT_TSS + 1].limit_low = (tss_base >> 32) & 0xFFFF;
    gdt[GDT_TSS + 1].base_low = (tss_base >> 48) & 0xFFFF;
    
    // Create a backup of the GDT
    gdt_save_backup();
    
    // Load the GDT
    gdt_load(&gdt_pointer);
    
    // Load the TSS
    // The selector is GDT_TSS * 8 (each GDT entry is 8 bytes)
    tss_load(GDT_TSS * 8);
}

// Save the current GDT to backup storage
void gdt_save_backup(void) {
    // Copy the GDT contents
    memcpy(gdt_backup, gdt, sizeof(gdt));
    
    // Copy the GDT pointer
    gdt_pointer_backup.limit = gdt_pointer.limit;
    gdt_pointer_backup.base = gdt_pointer.base;
}

// Check if the GDT is still valid
bool gdt_check_integrity(void) {
    // Check if the GDT pointer is still pointing to our GDT
    if (gdt_pointer.base != (uint64_t)&gdt) {
        return false;
    }
    
    // Check if the limit is correct
    if (gdt_pointer.limit != (sizeof(struct gdt_entry) * GDT_REAL_ENTRIES_COUNT) - 1) {
        return false;
    }
    
    // Verify all entries in the GDT against their backups
    if (memcmp(gdt, gdt_backup, sizeof(gdt)) != 0) {
        return false;
    }
    
    return true;
}

// Reload the GDT
void gdt_reload(void) {
    gdt_load(&gdt_pointer);
    tss_load(GDT_TSS * 8);
}

// Recover the GDT from backup
bool gdt_recover(void) {
    // Restore the GDT contents from backup
    memcpy(gdt, gdt_backup, sizeof(gdt));
    
    // Restore the GDT pointer
    gdt_pointer.limit = gdt_pointer_backup.limit;
    gdt_pointer.base = gdt_pointer_backup.base;
    
    // Reload the GDT
    gdt_reload();
    
    // Check if the recovery worked
    return gdt_check_integrity();
}

// Set the kernel stack in the TSS
void gdt_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}