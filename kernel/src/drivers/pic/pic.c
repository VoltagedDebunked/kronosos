#include <drivers/pic/pic.h>
#include <lib/io.h>
#include <utils/log.h>

// PIC initialization command words
#define ICW1_ICW4       0x01    // ICW4 needed
#define ICW1_SINGLE     0x02    // Single (cascade) mode
#define ICW1_INTERVAL4  0x04    // Call address interval 4 (8)
#define ICW1_LEVEL      0x08    // Level triggered (edge) mode
#define ICW1_INIT       0x10    // Initialization - required!

#define ICW4_8086       0x01    // 8086/88 (MCS-80/85) mode
#define ICW4_AUTO       0x02    // Auto (normal) EOI
#define ICW4_BUF_SLAVE  0x08    // Buffered mode/slave
#define ICW4_BUF_MASTER 0x0C    // Buffered mode/master
#define ICW4_SFNM       0x10    // Special fully nested (not)

// Current IRQ mask
static uint16_t irq_mask = 0xFFFF;

// Initialize the PIC
void pic_init(void) {
    LOG_INFO_MSG("Initializing PIC");
    
    // Save mask
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    
    // Start initialization sequence (cascade mode)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    
    // ICW2: Set vector offsets for each PIC (IRQ 0-7 -> INT 32-39, IRQ 8-15 -> INT 40-47)
    outb(PIC1_DATA, 32);   // Start of interrupts for PIC1 (0x20)
    outb(PIC2_DATA, 40);   // Start of interrupts for PIC2 (0x28)
    
    // ICW3: Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 4);
    // ICW3: Tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 2);
    
    // ICW4: Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    
    // Restore saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
    
    // Set the current mask in our variable
    irq_mask = (mask2 << 8) | mask1;
    
    LOG_INFO("PIC initialized with IRQ mask 0x%X", irq_mask);
    
    // Unmask only essential interrupts (timer, keyboard, cascade)
    // and mask the rest for now
    pic_mask_irq(0);     // Unmask timer (IRQ0)
    pic_mask_irq(1);     // Unmask keyboard (IRQ1)
    pic_unmask_irq(2);   // Unmask cascade for PIC2 (IRQ2)
}

// Send end of interrupt signal to PICs
void pic_send_eoi(uint8_t irq) {
    // If this is from the slave PIC, send EOI to both PICs
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    
    // Always send to master PIC
    outb(PIC1_COMMAND, PIC_EOI);
}

// Disable the PIC (useful when switching to APIC)
void pic_disable(void) {
    LOG_INFO_MSG("Disabling PIC");
    
    // Mask all interrupts
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    irq_mask = 0xFFFF;
}

// Mask (disable) a specific IRQ line
void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
    
    // Update our internal mask
    if (port == PIC1_DATA) {
        irq_mask = (irq_mask & 0xFF00) | value;
    } else {
        irq_mask = (irq_mask & 0x00FF) | (value << 8);
    }
}

// Unmask (enable) a specific IRQ line
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
    
    // Update our internal mask
    if (port == PIC1_DATA) {
        irq_mask = (irq_mask & 0xFF00) | value;
    } else {
        irq_mask = (irq_mask & 0x00FF) | (value << 8);
    }
}

// Get the current IRQ mask
uint16_t pic_get_irq_mask(void) {
    return irq_mask;
}

// Set the IRQ mask for both PICs
void pic_set_irq_mask(uint16_t mask) {
    irq_mask = mask;
    outb(PIC1_DATA, mask & 0xFF);
    outb(PIC2_DATA, (mask >> 8) & 0xFF);
}