#include <drivers/pci/pci.h>
#include <lib/io.h>
#include <lib/string.h>
#include <utils/log.h>

// Maximum number of PCI devices we'll track
#define MAX_PCI_DEVICES 256

// Global storage for detected PCI devices
static pci_device_t pci_devices[MAX_PCI_DEVICES];
static uint16_t detected_device_count = 0;

// Create PCI configuration address
static uint32_t pci_get_config_addr(uint8_t bus, uint8_t device, 
                                    uint8_t function, uint8_t offset) {
    return (1U << 31) |  // Enable bit
           ((bus & 0xFF) << 16) |       // Bus number
           ((device & 0x1F) << 11) |    // Device number
           ((function & 0x07) << 8) |   // Function number
           (offset & 0xFC);             // Register number (align to 4 bytes)
}

// Read 32-bit value from PCI configuration space
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, 
                               uint8_t function, uint8_t offset) {
    // Ensure 4-byte alignment
    offset &= 0xFC;
    
    // Write configuration address
    outl(PCI_CONFIG_ADDR, pci_get_config_addr(bus, device, function, offset));
    
    // Read configuration data
    return inl(PCI_CONFIG_DATA);
}

// Write 32-bit value to PCI configuration space
void pci_write_config_dword(uint8_t bus, uint8_t device, 
                            uint8_t function, uint8_t offset, uint32_t value) {
    // Ensure 4-byte alignment
    offset &= 0xFC;
    
    // Write configuration address
    outl(PCI_CONFIG_ADDR, pci_get_config_addr(bus, device, function, offset));
    
    // Write configuration data
    outl(PCI_CONFIG_DATA, value);
}

// Scan for PCI devices
static void pci_scan_bus(uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        // Check if device exists
        uint32_t vendor_id = pci_read_config_dword(bus, device, 0, PCI_VENDOR_ID);
        
        // Skip if no device
        if (vendor_id == 0xFFFFFFFF) continue;
        
        // Check for multifunction device
        uint32_t header_type = pci_read_config_dword(bus, device, 0, PCI_CACHE_LINE_SIZE) >> 16;
        uint8_t max_functions = (header_type & 0x80) ? 8 : 1;
        
        for (uint8_t function = 0; function < max_functions; function++) {
            // Read configuration
            uint32_t class_rev = pci_read_config_dword(bus, device, function, PCI_REVISION_ID);
            
            // If no device, skip
            if (pci_read_config_dword(bus, device, function, PCI_VENDOR_ID) == 0xFFFFFFFF) 
                continue;
            
            // Store device information
            if (detected_device_count < MAX_PCI_DEVICES) {
                pci_device_t* pci_dev = &pci_devices[detected_device_count];
                
                pci_dev->bus = bus;
                pci_dev->device = device;
                pci_dev->function = function;
                pci_dev->vendor_id = vendor_id & 0xFFFF;
                pci_dev->device_id = (vendor_id >> 16) & 0xFFFF;
                pci_dev->class_code = (class_rev >> 24) & 0xFF;
                pci_dev->subclass = (class_rev >> 16) & 0xFF;
                pci_dev->prog_if = (class_rev >> 8) & 0xFF;
                
                detected_device_count++;
                
                LOG_DEBUG("PCI Device: %02x:%02x.%d Vendor:0x%04x Device:0x%04x "
                    "Class:0x%02x Subclass:0x%02x",
                    bus, device, function, 
                    pci_dev->vendor_id, pci_dev->device_id,
                    pci_dev->class_code, pci_dev->subclass);
            }
        }
    }
}

// Initialize PCI subsystem
void pci_init(void) {
    LOG_INFO_MSG("Initializing PCI Bus");
    
    // Reset device count
    detected_device_count = 0;
    
    // Scan all possible buses
    for (uint16_t bus = 0; bus < 256; bus++) {
        pci_scan_bus(bus);
    }
    
    LOG_INFO("Total PCI devices detected: %d", detected_device_count);
}

// Find a PCI device by class and subclass
bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, 
                               pci_device_t* out_device) {
    for (uint16_t i = 0; i < detected_device_count; i++) {
        if (pci_devices[i].class_code == class_code && 
            pci_devices[i].subclass == subclass) {
            // Copy device information
            if (out_device) {
                memcpy(out_device, &pci_devices[i], sizeof(pci_device_t));
            }
            return true;
        }
    }
    return false;
}

// Get Base Address Register (BAR) value
uint64_t pci_get_bar(const pci_device_t* device, uint8_t bar_index) {
    if (!device || bar_index > 5) {
        return 0;
    }
    
    // Calculate BAR offset
    uint8_t bar_offset = PCI_BASE_ADDRESS_0 + (bar_index * 4);
    
    // Read 32-bit BAR value
    uint32_t bar_low = pci_read_config_dword(
        device->bus, device->device, device->function, bar_offset
    );
    
    // Check if 64-bit BAR
    if (bar_index < 5 && (bar_low & 0x04)) {
        // 64-bit BAR, read next 32 bits
        uint32_t bar_high = pci_read_config_dword(
            device->bus, device->device, device->function, bar_offset + 4
        );
        
        // Combine 64-bit address
        return ((uint64_t)bar_high << 32) | (bar_low & 0xFFFFFFF0);
    }
    
    // 32-bit BAR
    return bar_low & 0xFFFFFFF0;
}