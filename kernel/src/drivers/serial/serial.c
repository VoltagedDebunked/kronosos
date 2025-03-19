#include <drivers/serial/serial.h>

bool serial_init(uint16_t port, uint16_t baud_divisor) {
    // Disable interrupts
    outb(port + SERIAL_INT_EN, 0x00);
    
    // Set DLAB to access baud rate divisor
    outb(port + SERIAL_LINE_CTRL, 0x80);
    
    // Set baud rate
    outb(port + SERIAL_DATA, baud_divisor & 0xFF);
    outb(port + SERIAL_INT_EN, (baud_divisor >> 8) & 0xFF);
    
    // 8N1 and reset DLAB
    outb(port + SERIAL_LINE_CTRL, 0x03);
    
    // Enable and clear FIFO with 14-byte threshold
    outb(port + SERIAL_FIFO_CTRL, 0xC7);
    
    // IRQs enabled, RTS/DSR set
    outb(port + SERIAL_MODEM_CTRL, 0x0B);
    
    // Test serial chip with loopback
    outb(port + SERIAL_MODEM_CTRL, 0x1E);
    outb(port + SERIAL_DATA, 0xAE);
    
    if (inb(port + SERIAL_DATA) != 0xAE) {
        return false;
    }
    
    // Set to normal operation mode
    outb(port + SERIAL_MODEM_CTRL, 0x0F);
    
    return true;
}

bool serial_is_transmit_ready(uint16_t port) {
    return (inb(port + SERIAL_LINE_STAT) & SERIAL_LINE_THR_EMPTY) != 0;
}

void serial_write_byte(uint16_t port, uint8_t data) {
    while (!serial_is_transmit_ready(port)) {
        // Wait
    }
    
    outb(port + SERIAL_DATA, data);
}

void serial_write_string(uint16_t port, const char *str) {
    if (!str) {
        return;
    }
    
    while (*str) {
        serial_write_byte(port, (uint8_t)*str++);
    }
}

void serial_write_hex(uint16_t port, uint64_t value, int num_digits) {
    if (num_digits <= 0 || num_digits > 16) {
        num_digits = 16;
    }
    
    serial_write_string(port, "0x");
    
    const char hex_digits[] = "0123456789ABCDEF";
    
    for (int i = num_digits - 1; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        serial_write_byte(port, hex_digits[nibble]);
    }
}

bool serial_is_data_ready(uint16_t port) {
    return (inb(port + SERIAL_LINE_STAT) & SERIAL_LINE_DATA_READY) != 0;
}

uint8_t serial_read_byte(uint16_t port) {
    while (!serial_is_data_ready(port)) {
        // Wait
    }
    
    return inb(port + SERIAL_DATA);
}