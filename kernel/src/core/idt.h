#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include <stdbool.h>

// IDT entry structure
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;        // Bits 0-2: IST, Bits 3-7: Reserved
    uint8_t type_attr;  // Type and attributes
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

// IDT pointer structure
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Define number of IDT entries
#define IDT_ENTRIES 256

// Exception and interrupt constants
#define INT_DIVIDE_BY_ZERO      0
#define INT_DEBUG               1
#define INT_NMI                 2
#define INT_BREAKPOINT          3
#define INT_OVERFLOW            4
#define INT_BOUND_RANGE         5
#define INT_INVALID_OPCODE      6
#define INT_DEVICE_NOT_AVAIL    7
#define INT_DOUBLE_FAULT        8
#define INT_COPROCESSOR_SEG     9
#define INT_INVALID_TSS        10
#define INT_SEGMENT_NOT_PRES   11
#define INT_STACK_SEGMENT      12
#define INT_GENERAL_PROTECT    13
#define INT_PAGE_FAULT         14
#define INT_RESERVED_15        15
#define INT_FPU_ERROR          16
#define INT_ALIGNMENT_CHECK    17
#define INT_MACHINE_CHECK      18
#define INT_SIMD_FP_EXCEPTION  19
#define INT_VIRT_EXCEPTION     20
#define INT_CONTROL_PROTECT    21
// 22-31 reserved for future use by Intel

// IRQ numbers
#define IRQ0                   32
#define IRQ1                   33
#define IRQ2                   34
#define IRQ3                   35
#define IRQ4                   36
#define IRQ5                   37
#define IRQ6                   38
#define IRQ7                   39
#define IRQ8                   40
#define IRQ9                   41
#define IRQ10                  42
#define IRQ11                  43
#define IRQ12                  44
#define IRQ13                  45
#define IRQ14                  46
#define IRQ15                  47

// Common device IRQs
#define IRQ_TIMER              IRQ0
#define IRQ_KEYBOARD           IRQ1
#define IRQ_CASCADE            IRQ2
#define IRQ_COM2_4             IRQ3
#define IRQ_COM1_3             IRQ4
#define IRQ_LPT2               IRQ5
#define IRQ_FLOPPY             IRQ6
#define IRQ_LPT1               IRQ7
#define IRQ_RTC                IRQ8
#define IRQ_ACPI               IRQ9
#define IRQ_AVAILABLE1         IRQ10
#define IRQ_AVAILABLE2         IRQ11
#define IRQ_MOUSE              IRQ12
#define IRQ_FPU                IRQ13
#define IRQ_PRIMARY_ATA        IRQ14
#define IRQ_SECONDARY_ATA      IRQ15

// Register structure passed to handlers
struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    
    uint64_t int_no;
    uint64_t error_code;
    
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

// Functions declarations
void idt_init(void);
bool idt_check_integrity(void);
void idt_reload(void);
bool idt_recover(void);
void idt_save_backup(void);

// Function to register an interrupt handler
typedef void (*interrupt_handler_t)(struct interrupt_frame *frame);
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

// Function to register an ISR handler
bool idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr);

// Hardware interrupt control
void interrupt_enable(void);
void interrupt_disable(void);
bool interrupt_state(void);

#endif // IDT_H