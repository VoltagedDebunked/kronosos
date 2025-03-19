#ifndef PIC_H
#define PIC_H

#include <stdint.h>
#include <stdbool.h>

// PIC constants
#define PIC1            0x20    // IO base address for master PIC
#define PIC2            0xA0    // IO base address for slave PIC
#define PIC1_COMMAND    PIC1
#define PIC1_DATA       (PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA       (PIC2+1)
#define PIC_EOI         0x20    // End-of-interrupt command code

// PIC initialization and control functions
void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_disable(void);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
uint16_t pic_get_irq_mask(void);
void pic_set_irq_mask(uint16_t mask);

#endif // PIC_H