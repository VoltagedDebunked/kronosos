#ifndef GDT_H
#define GDT_H

#include <stdint.h>
#include <stdbool.h>

// GDT entry structure
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

// GDT pointer structure
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// TSS structure
struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

// Define GDT entries
enum gdt_selector {
    GDT_NULL = 0,
    GDT_KERNEL_CODE = 1,
    GDT_KERNEL_DATA = 2,
    GDT_USER_CODE = 3,
    GDT_USER_DATA = 4,
    GDT_TSS = 5,
    GDT_ENTRIES_COUNT
};

// TSS descriptor is 16 bytes (2 gdt entries)
#define GDT_REAL_ENTRIES_COUNT (GDT_ENTRIES_COUNT + 1)

// Functions declarations
void gdt_init(void);
bool gdt_check_integrity(void);
void gdt_reload(void);
bool gdt_recover(void);
void gdt_save_backup(void);
void gdt_set_kernel_stack(uint64_t stack);

#endif // GDT_H