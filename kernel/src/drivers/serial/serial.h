#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include <lib/io.h>

// Serial ports
#define SERIAL_COM1       0x3F8
#define SERIAL_COM2       0x2F8
#define SERIAL_COM3       0x3E8
#define SERIAL_COM4       0x2E8

// Port offsets
#define SERIAL_DATA       0
#define SERIAL_INT_EN     1
#define SERIAL_FIFO_CTRL  2
#define SERIAL_LINE_CTRL  3
#define SERIAL_MODEM_CTRL 4
#define SERIAL_LINE_STAT  5
#define SERIAL_MODEM_STAT 6
#define SERIAL_SCRATCH    7

// Status flags
#define SERIAL_LINE_DATA_READY 0x01
#define SERIAL_LINE_THR_EMPTY  0x20

// Baud rate divisors
#define SERIAL_BAUD_115200 1
#define SERIAL_BAUD_57600  2
#define SERIAL_BAUD_38400  3
#define SERIAL_BAUD_19200  6
#define SERIAL_BAUD_9600   12

bool serial_init(uint16_t port, uint16_t baud_divisor);
bool serial_is_transmit_ready(uint16_t port);
void serial_write_byte(uint16_t port, uint8_t data);
void serial_write_string(uint16_t port, const char *str);
void serial_write_hex(uint16_t port, uint64_t value, int num_digits);
bool serial_is_data_ready(uint16_t port);
uint8_t serial_read_byte(uint16_t port);

#endif // SERIAL_H