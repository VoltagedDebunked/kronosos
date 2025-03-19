#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

// PCI Configuration Space Registers
#define PCI_CONFIG_ADDR   0xCF8
#define PCI_CONFIG_DATA   0xCFC

// PCI Configuration Space Offsets
#define PCI_VENDOR_ID        0x00
#define PCI_DEVICE_ID        0x02
#define PCI_COMMAND          0x04
#define PCI_STATUS           0x06
#define PCI_REVISION_ID      0x08
#define PCI_PROG_IF          0x09
#define PCI_SUBCLASS         0x0A
#define PCI_CLASS_CODE       0x0B
#define PCI_CACHE_LINE_SIZE  0x0C
#define PCI_BASE_ADDRESS_0   0x10
#define PCI_BASE_ADDRESS_1   0x14
#define PCI_BASE_ADDRESS_2   0x18
#define PCI_BASE_ADDRESS_3   0x1C
#define PCI_BASE_ADDRESS_4   0x20
#define PCI_BASE_ADDRESS_5   0x24

// PCI Device Structure
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
} pci_device_t;

// Initialize PCI subsystem
void pci_init(void);

// Read 32-bit value from PCI configuration space
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, 
                               uint8_t function, uint8_t offset);

// Write 32-bit value to PCI configuration space
void pci_write_config_dword(uint8_t bus, uint8_t device, 
                            uint8_t function, uint8_t offset, uint32_t value);

// Find a PCI device by class and subclass
bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, 
                               pci_device_t* device);

// Get Base Address Register (BAR) value
uint64_t pci_get_bar(const pci_device_t* device, uint8_t bar_index);

#endif // PCI_H