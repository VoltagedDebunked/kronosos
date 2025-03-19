#include <core/idt.h>
#include <lib/string.h>
#include <lib/asm.h>
#include <utils/log.h>
#include <drivers/pic/pic.h>

// The IDT entries
static struct idt_entry idt[IDT_ENTRIES];

// The IDT pointer
static struct idt_ptr idt_pointer;

// Backup of the IDT for recovery
static struct idt_entry idt_backup[IDT_ENTRIES];
static struct idt_ptr idt_pointer_backup;

// Array of handler pointers
static interrupt_handler_t interrupt_handlers[IDT_ENTRIES] = {0};

// External assembly function to load the IDT
extern void idt_load(struct idt_ptr* idt_ptr);

// External interrupt handlers from assembly
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

// External IRQ handlers from assembly
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

// Default exception names for logging
static const char *exception_names[] = {
    "Divide By Zero",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Set up an IDT gate
bool idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr) {
    if (num >= IDT_ENTRIES) {
        return false;
    }
    
    idt[num].offset_low = (base & 0xFFFF);
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = ist & 0x7; // Only use the first 3 bits for IST
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
    
    return true;
}

// Initialize IDT with default entries
static void idt_initialize_gates(void) {
    // Set up exception handlers (ISRs 0-31)
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0, 0x8E);   // 0x8E = Present, Ring0, Interrupt Gate
    idt_set_gate(1, (uint64_t)isr1, 0x08, 0, 0x8E);
    idt_set_gate(2, (uint64_t)isr2, 0x08, 0, 0x8E);
    idt_set_gate(3, (uint64_t)isr3, 0x08, 0, 0x8E);
    idt_set_gate(4, (uint64_t)isr4, 0x08, 0, 0x8E);
    idt_set_gate(5, (uint64_t)isr5, 0x08, 0, 0x8E);
    idt_set_gate(6, (uint64_t)isr6, 0x08, 0, 0x8E);
    idt_set_gate(7, (uint64_t)isr7, 0x08, 0, 0x8E);
    idt_set_gate(8, (uint64_t)isr8, 0x08, 0, 0x8E);
    idt_set_gate(9, (uint64_t)isr9, 0x08, 0, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0, 0x8E);
    
    // Set up IRQ handlers (ISRs 32-47)
    idt_set_gate(32, (uint64_t)irq0, 0x08, 0, 0x8E);
    idt_set_gate(33, (uint64_t)irq1, 0x08, 0, 0x8E);
    idt_set_gate(34, (uint64_t)irq2, 0x08, 0, 0x8E);
    idt_set_gate(35, (uint64_t)irq3, 0x08, 0, 0x8E);
    idt_set_gate(36, (uint64_t)irq4, 0x08, 0, 0x8E);
    idt_set_gate(37, (uint64_t)irq5, 0x08, 0, 0x8E);
    idt_set_gate(38, (uint64_t)irq6, 0x08, 0, 0x8E);
    idt_set_gate(39, (uint64_t)irq7, 0x08, 0, 0x8E);
    idt_set_gate(40, (uint64_t)irq8, 0x08, 0, 0x8E);
    idt_set_gate(41, (uint64_t)irq9, 0x08, 0, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0, 0x8E);
}

// Initialize the IDT
void idt_init(void) {
    LOG_INFO_MSG("Initializing IDT");
    
    // Clear IDT
    memset(idt, 0, sizeof(struct idt_entry) * IDT_ENTRIES);
    
    // Set up the IDT pointer
    idt_pointer.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    // Set up IDT gates
    idt_initialize_gates();
    
    // Create a backup of the IDT
    idt_save_backup();
    
    // Initialize the PIC
    pic_init();
    
    // Load the IDT
    idt_load(&idt_pointer);
    
    LOG_INFO_MSG("IDT initialized");
}

// Save the current IDT to backup storage
void idt_save_backup(void) {
    // Copy the IDT contents
    memcpy(idt_backup, idt, sizeof(idt));
    
    // Copy the IDT pointer
    idt_pointer_backup.limit = idt_pointer.limit;
    idt_pointer_backup.base = idt_pointer.base;
}

// Check if the IDT is still valid
bool idt_check_integrity(void) {
    // Check if the IDT pointer is still pointing to our IDT
    if (idt_pointer.base != (uint64_t)&idt) {
        return false;
    }
    
    // Check if the limit is correct
    if (idt_pointer.limit != (sizeof(struct idt_entry) * IDT_ENTRIES) - 1) {
        return false;
    }
    
    // Verify all entries in the IDT against their backups
    if (memcmp(idt, idt_backup, sizeof(idt)) != 0) {
        return false;
    }
    
    return true;
}

// Reload the IDT
void idt_reload(void) {
    idt_load(&idt_pointer);
}

// Recover the IDT from backup
bool idt_recover(void) {
    // Restore the IDT contents from backup
    memcpy(idt, idt_backup, sizeof(idt));
    
    // Restore the IDT pointer
    idt_pointer.limit = idt_pointer_backup.limit;
    idt_pointer.base = idt_pointer_backup.base;
    
    // Reload the IDT
    idt_reload();
    
    // Check if the recovery worked
    return idt_check_integrity();
}

// Register an interrupt handler function
void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    if (vector < IDT_ENTRIES) {
        interrupt_handlers[vector] = handler;
    }
}

// The main C interrupt handler
void interrupt_handler(struct interrupt_frame *frame) {
    // If we have a custom handler registered, call it
    if (interrupt_handlers[frame->int_no] != NULL) {
        interrupt_handlers[frame->int_no](frame);
    } 
    // Handle CPU exceptions (0-31)
    else if (frame->int_no < 32) {
        LOG_ERROR("Exception: %s (code %d) at RIP=0x%X", 
                  exception_names[frame->int_no], 
                  frame->error_code, 
                  frame->rip);
        
        LOG_ERROR("RAX=0x%X RBX=0x%X RCX=0x%X RDX=0x%X", 
                  frame->rax, frame->rbx, frame->rcx, frame->rdx);
        LOG_ERROR("RSI=0x%X RDI=0x%X RBP=0x%X RSP=0x%X", 
                  frame->rsi, frame->rdi, frame->rbp, frame->rsp);
        LOG_ERROR("R8=0x%X R9=0x%X R10=0x%X R11=0x%X", 
                  frame->r8, frame->r9, frame->r10, frame->r11);
        LOG_ERROR("R12=0x%X R13=0x%X R14=0x%X R15=0x%X", 
                  frame->r12, frame->r13, frame->r14, frame->r15);
        LOG_ERROR("RFLAGS=0x%X CS=0x%X SS=0x%X", 
                  frame->rflags, frame->cs, frame->ss);
        
        // Halt the system for critical exceptions
        LOG_CRITICAL_MSG("System halted due to unhandled exception");
        hcf();
    }
    
    // Send EOI to the PIC for hardware interrupts (32-47)
    if (frame->int_no >= 32 && frame->int_no < 48) {
        pic_send_eoi(frame->int_no - 32);
    }
}

// Control interrupt state
void interrupt_enable(void) {
    asm volatile("sti");
}

void interrupt_disable(void) {
    asm volatile("cli");
}

bool interrupt_state(void) {
    uint64_t flags;
    asm volatile("pushfq; pop %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0; // Check IF (Interrupt Flag) bit
}