#include <drivers/ata/ata.h>
#include <drivers/pci/pci.h>
#include <lib/io.h>
#include <lib/string.h>
#include <utils/log.h>
#include <memory/vmm.h>

// ATA controller I/O ports
#define ATA_PRIMARY_DATA            0x1F0
#define ATA_PRIMARY_ERROR           0x1F1
#define ATA_PRIMARY_SECTOR_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LOW         0x1F3
#define ATA_PRIMARY_LBA_MID         0x1F4
#define ATA_PRIMARY_LBA_HIGH        0x1F5
#define ATA_PRIMARY_DRIVE_HEAD      0x1F6
#define ATA_PRIMARY_STATUS          0x1F7
#define ATA_PRIMARY_COMMAND         0x1F7

#define ATA_SECONDARY_DATA          0x170
#define ATA_SECONDARY_ERROR         0x171
#define ATA_SECONDARY_SECTOR_COUNT  0x172
#define ATA_SECONDARY_LBA_LOW       0x173
#define ATA_SECONDARY_LBA_MID       0x174
#define ATA_SECONDARY_LBA_HIGH      0x175
#define ATA_SECONDARY_DRIVE_HEAD    0x176
#define ATA_SECONDARY_STATUS        0x177
#define ATA_SECONDARY_COMMAND       0x177

// Control ports
#define ATA_PRIMARY_CONTROL         0x3F6
#define ATA_SECONDARY_CONTROL       0x376

// ATA commands
#define ATA_CMD_READ_PIO            0x20
#define ATA_CMD_READ_PIO_EXT        0x24
#define ATA_CMD_WRITE_PIO           0x30
#define ATA_CMD_WRITE_PIO_EXT       0x34
#define ATA_CMD_IDENTIFY            0xEC
#define ATA_CMD_CACHE_FLUSH         0xE7
#define ATA_CMD_CACHE_FLUSH_EXT     0xEA

// Status register bits
#define ATA_STATUS_ERR              0x01  // Error
#define ATA_STATUS_DRQ              0x08  // Data Request Ready
#define ATA_STATUS_SRV              0x10  // Overlapped Mode Service Request
#define ATA_STATUS_DF               0x20  // Drive Fault Error
#define ATA_STATUS_RDY              0x40  // Ready
#define ATA_STATUS_BSY              0x80  // Busy

// Control register bits
#define ATA_CONTROL_NIEN            0x02  // Disable interrupts
#define ATA_CONTROL_SRST            0x04  // Software reset
#define ATA_CONTROL_HOB             0x80  // High Order Byte (for 48-bit LBA)

// Device register bits
#define ATA_DEVICE_MASTER           0x00
#define ATA_DEVICE_SLAVE            0x10
#define ATA_DEVICE_LBA              0x40

// Polling timeout in milliseconds
#define ATA_TIMEOUT                 1000

// Drive types for detection
#define DRIVE_TYPE_UNKNOWN          0x00
#define DRIVE_TYPE_PATA             0x01
#define DRIVE_TYPE_SATA             0x02
#define DRIVE_TYPE_PATAPI           0x03
#define DRIVE_TYPE_SATAPI           0x04

// Structures for ATA controller
typedef struct {
    uint16_t base;       // I/O base port
    uint16_t control;    // Control port
    uint16_t bmide;      // Bus Master IDE port
    bool master;         // true for master, false for slave
    bool present;        // Is drive present?
    uint8_t type;        // Drive type
    char model[41];      // Model string
    uint32_t size;       // Size in sectors
} ata_drive_info_t;

// Static array to store detected drives
static ata_drive_t detected_drives[ATA_MAX_DRIVES];
static int drive_count = 0;

// Internal helper functions
static uint16_t ata_get_data_port(int drive_idx);
static uint16_t ata_get_control_port(int drive_idx);
static bool ata_is_master(int drive_idx);
static bool ata_wait_not_busy(uint16_t port, uint32_t timeout_ms);
static bool ata_wait_drq(uint16_t port, uint32_t timeout_ms);
static void ata_400ns_delay(uint16_t port);
static void ata_software_reset(uint16_t control_port);
static void ata_detect_drive(uint16_t base, uint16_t control, bool master);
static void ata_identify_drive(int drive_idx);
static void ata_extract_string(char* dest, uint16_t* src, int length);

// Initialize the ATA driver
void ata_init() {
    LOG_INFO_MSG("Initializing ATA driver");
    
    // Reset drive count
    drive_count = 0;
    memset(detected_drives, 0, sizeof(detected_drives));
    
    // Initialize PCI subsystem if needed
    pci_init();
    
    // Try to detect SATA/IDE controllers
    pci_device_t controller;
    
    // First try for SATA controllers (class 0x01, subclass 0x06)
    bool found_controller = pci_find_device_by_class(0x01, 0x06, &controller);
    
    if (!found_controller) {
        // Fall back to IDE controllers (class 0x01, subclass 0x01)
        found_controller = pci_find_device_by_class(0x01, 0x01, &controller);
    }
    
    if (found_controller) {
        LOG_INFO("Found Storage controller: Vendor 0x%X, Device 0x%X (Class 0x%X, Subclass 0x%X)", 
                 controller.vendor_id, controller.device_id,
                 controller.class_code, controller.subclass);
        
        // Configure PCI device (enable Bus Mastering, I/O space, etc.)
        uint32_t command = pci_read_config_dword(controller.bus, 
                                                controller.device, 
                                                controller.function, 
                                                PCI_COMMAND);
        command |= 0x5; // Enable I/O Space and Bus Mastering
        pci_write_config_dword(controller.bus, 
                             controller.device, 
                             controller.function, 
                             PCI_COMMAND, 
                             command);
        
        // Get the Bus Master IDE (BMIDE) base address
        uint32_t bmide_base = pci_get_bar(&controller, 4) & 0xFFFFFFF0;
        if (bmide_base) {
            LOG_INFO("Storage controller BMIDE base: 0x%X", bmide_base);
        }
        
        if (controller.subclass == 0x06) {
            // This is a SATA controller
            LOG_INFO("SATA controller detected - still using legacy port compatibility");
            
            // Check if controller is in AHCI mode
            uint8_t prog_if = controller.prog_if;
            if (prog_if == 0x01) {
                LOG_INFO("Controller in AHCI mode - Stay tuned for AHCI support soon.");
            }
        }
    } else {
        LOG_INFO_MSG("No PCI storage controller found, using legacy ports");
    }
    
    // Detect all drives using legacy ports
    LOG_INFO_MSG("Detecting ATA drives");
    
    // Try primary channel (master and slave)
    ata_detect_drive(ATA_PRIMARY_DATA, ATA_PRIMARY_CONTROL, true);
    ata_detect_drive(ATA_PRIMARY_DATA, ATA_PRIMARY_CONTROL, false);
    
    // Try secondary channel (master and slave)
    ata_detect_drive(ATA_SECONDARY_DATA, ATA_SECONDARY_CONTROL, true);
    ata_detect_drive(ATA_SECONDARY_DATA, ATA_SECONDARY_CONTROL, false);
    
    LOG_INFO("ATA driver initialized with %d drives", drive_count);
    
    // Print info about detected drives
    ata_print_info();
}

// Detect an ATA drive
static void ata_detect_drive(uint16_t base, uint16_t control, bool master) {
    // Check if we have room for another drive
    if (drive_count >= ATA_MAX_DRIVES) {
        return;
    }
    
    // Set up internal drive info
    int drive_idx = drive_count;
    
    // Reset the drive
    ata_software_reset(control);
    
    // Select drive
    uint8_t drive_select = master ? ATA_DEVICE_MASTER : ATA_DEVICE_SLAVE;
    outb(base + 6, drive_select | ATA_DEVICE_LBA); // Drive & LBA mode
    ata_400ns_delay(base);
    
    // Send IDENTIFY command
    outb(base + 7, ATA_CMD_IDENTIFY);
    ata_400ns_delay(base);
    
    // Check if drive exists
    uint8_t status = inb(base + 7);
    if (status == 0) {
        // No drive present
        return;
    }
    
    // Wait for operation to complete or timeout
    if (!ata_wait_not_busy(base + 7, ATA_TIMEOUT)) {
        // Timeout waiting for drive
        return;
    }
    
    // Determine drive type (ATA or ATAPI)
    uint8_t lba_mid = inb(base + 4);
    uint8_t lba_high = inb(base + 5);
    
    LOG_DEBUG("Drive detection - Status: 0x%X, Mid: 0x%X, High: 0x%X", 
              status, lba_mid, lba_high);
    
    uint8_t drive_type = DRIVE_TYPE_UNKNOWN;
    
    // Check for timeout or not present
    if (status == 0 || status == 0xFF) {
        LOG_DEBUG("No drive present on %s channel, %s drive (status = 0x%X)",
                 (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                 master ? "master" : "slave", status);
        return;
    }
    
    if (lba_mid == 0x14 && lba_high == 0xEB) {
        // PATAPI drive
        drive_type = DRIVE_TYPE_PATAPI;
        LOG_INFO("Found PATAPI drive on %s channel, %s drive",
                 (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                 master ? "master" : "slave");
                 
        // We don't support ATAPI yet
        return;
    } 
    else if (lba_mid == 0x3c && lba_high == 0xc3) {
        // SATA drive
        drive_type = DRIVE_TYPE_SATA;
        LOG_INFO("Found SATA drive on %s channel, %s drive",
                 (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                 master ? "master" : "slave");
    }
    else if (lba_mid == 0 && lba_high == 0) {
        // PATA drive
        drive_type = DRIVE_TYPE_PATA;
        LOG_INFO("Found PATA drive on %s channel, %s drive",
                 (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                 master ? "master" : "slave");
    }
    else {
        // In QEMU/KVM, drives can sometimes appear with non-standard signatures
        // We'll trust that the drive exists if status isn't 0 or 0xFF
        if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ) {
            // DRQ is set, BSY is clear - data is ready
            drive_type = DRIVE_TYPE_PATA; // Assume PATA for now
            LOG_INFO("Found drive with non-standard signature on %s channel, %s drive (assuming PATA)",
                     (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                     master ? "master" : "slave");
        } else {
            // Unknown drive type
            LOG_WARN("Unknown drive type (0x%X, 0x%X) on %s channel, %s drive",
                     lba_mid, lba_high,
                     (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                     master ? "master" : "slave");
            return;
        }
    }
    
    // Wait for data to be ready
    if (!ata_wait_drq(base + 7, ATA_TIMEOUT)) {
        LOG_WARN("Drive timeout waiting for DRQ on %s channel, %s drive",
                 (base == ATA_PRIMARY_DATA) ? "primary" : "secondary",
                 master ? "master" : "slave");
        return;
    }
    
    // Read identification data
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base);
    }
    
    // Extract drive information
    char model[41] = {0};
    ata_extract_string(model, &identify_data[27], 40);
    
    // Store drive information
    detected_drives[drive_count].type = drive_type;
    detected_drives[drive_count].signature = (lba_high << 8) | lba_mid;
    detected_drives[drive_count].capabilities = identify_data[49];
    detected_drives[drive_count].command_sets = identify_data[83];
    
    // LBA28 or LBA48 size
    if (identify_data[83] & (1 << 10)) {
        // LBA48 supported - get size from bytes 100-103
        detected_drives[drive_count].size = 
            ((uint32_t)identify_data[101] << 16) | identify_data[100];
    } else {
        // LBA28 - get size from bytes 60-61
        detected_drives[drive_count].size = 
            ((uint32_t)identify_data[61] << 16) | identify_data[60];
    }
    
    for (int i = 0; i < 40; i++) {
        detected_drives[drive_count].model[i] = model[i];
        if (model[i] == '\0') break;
    }
    detected_drives[drive_count].model[40] = '\0';
    
    // Extract serial number (words 10-19)
    char serial[21] = {0};
    ata_extract_string(serial, &identify_data[10], 20);
    // Manual string copy
    for (int i = 0; i < 20; i++) {
        detected_drives[drive_count].serial[i] = serial[i];
        if (serial[i] == '\0') break;
    }
    detected_drives[drive_count].serial[20] = '\0';
    
    // Extract CHS geometry
    detected_drives[drive_count].cylinders = identify_data[1];
    detected_drives[drive_count].heads = identify_data[3];
    detected_drives[drive_count].sectors = identify_data[6];
    
    LOG_INFO("Drive %d: %s", drive_count, detected_drives[drive_count].model);
    LOG_INFO("  Size: %u sectors (%u MB)", 
             detected_drives[drive_count].size,
             detected_drives[drive_count].size / 2048);
    
    // Register the drive
    drive_count++;
}

// Extract string from identify data (byte-swapped)
static void ata_extract_string(char* dest, uint16_t* src, int length) {
    // ATA strings are byte-swapped and space-padded
    for (int i = 0; i < length / 2; i++) {
        dest[i*2] = (src[i] >> 8) & 0xFF;
        dest[i*2+1] = src[i] & 0xFF;
    }
    
    // Remove trailing spaces
    for (int i = length - 1; i >= 0; i--) {
        if (dest[i] == ' ') {
            dest[i] = '\0';
        } else if (dest[i] != '\0') {
            break;
        }
    }
}

// Print information about all detected drives
void ata_print_info() {
    LOG_INFO("ATA Drive Information:");
    LOG_INFO("----------------------");
    
    if (drive_count == 0) {
        LOG_INFO("No ATA drives detected");
        return;
    }
    
    for (int i = 0; i < drive_count; i++) {
        char* type_str = "Unknown";
        
        switch (detected_drives[i].type) {
            case DRIVE_TYPE_PATA:
                type_str = "PATA";
                break;
            case DRIVE_TYPE_SATA:
                type_str = "SATA";
                break;
            case DRIVE_TYPE_PATAPI:
                type_str = "PATAPI";
                break;
            case DRIVE_TYPE_SATAPI:
                type_str = "SATAPI";
                break;
        }
        
        LOG_INFO("Drive %d:", i);
        LOG_INFO("  Model: %s", detected_drives[i].model);
        LOG_INFO("  Serial: %s", detected_drives[i].serial);
        LOG_INFO("  Type: %s", type_str);
        LOG_INFO("  Size: %u sectors (%u MB)", 
                 detected_drives[i].size, 
                 detected_drives[i].size / 2048);
        LOG_INFO("  CHS: %u/%u/%u", 
                 detected_drives[i].cylinders, 
                 detected_drives[i].heads, 
                 detected_drives[i].sectors);
        
        if (detected_drives[i].capabilities & (1 << 9)) {
            LOG_INFO("  LBA: Supported");
        } else {
            LOG_INFO("  LBA: Not supported");
        }
    }
}

// Check if a drive is present
bool ata_drive_present(uint8_t drive_index) {
    if (drive_index >= drive_count) {
        return false;
    }
    
    return detected_drives[drive_index].type != 0;
}

// Get drive information
const ata_drive_t* ata_get_drive_info(uint8_t drive_index) {
    if (drive_index >= drive_count) {
        return NULL;
    }
    
    return &detected_drives[drive_index];
}

// Get the data port for a drive
static uint16_t ata_get_data_port(int drive_idx) {
    if (drive_idx < 2) {
        return ATA_PRIMARY_DATA;
    } else {
        return ATA_SECONDARY_DATA;
    }
}

// Get the control port for a drive
static uint16_t ata_get_control_port(int drive_idx) {
    if (drive_idx < 2) {
        return ATA_PRIMARY_CONTROL;
    } else {
        return ATA_SECONDARY_CONTROL;
    }
}

// Check if a drive is master
static bool ata_is_master(int drive_idx) {
    return (drive_idx % 2) == 0;
}

// Wait for drive to not be busy
static bool ata_wait_not_busy(uint16_t port, uint32_t timeout_ms) {
    // Simple polling with timeout
    for (uint32_t i = 0; i < timeout_ms; i++) {
        uint8_t status = inb(port);
        
        if (!(status & ATA_STATUS_BSY)) {
            return true;
        }
        
        // Delay approximately 1ms
        for (volatile int j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }
    
    return false;
}

// Wait for DRQ (data request) flag
static bool ata_wait_drq(uint16_t port, uint32_t timeout_ms) {
    // Simple polling with timeout
    for (uint32_t i = 0; i < timeout_ms; i++) {
        uint8_t status = inb(port);
        
        // If ERR is set, check error register
        if (status & ATA_STATUS_ERR) {
            uint8_t error = inb(port - 7); // Read error register
            LOG_ERROR("ATA error waiting for DRQ: Status=%X, Error=%X", status, error);
            return false;
        }
        
        // Check for non-existent drive
        if (status == 0 || status == 0xFF) {
            LOG_ERROR("ATA device not present: Status=%X", status);
            return false;
        }
        
        // Check DF (Device Fault)
        if (status & ATA_STATUS_DF) {
            LOG_ERROR("ATA device fault detected: Status=%X", status);
            return false;
        }
        
        // If BSY is clear and DRQ is set, we're good to go
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return true;
        }
        
        // Delay approximately 1ms
        for (volatile int j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }
    
    LOG_WARN("ATA timeout waiting for DRQ (timeout=%dms)", timeout_ms);
    return false;
}

// 400ns delay (required by ATA specification for drive select)
static void ata_400ns_delay(uint16_t port) {
    // Read the status register 4 times for ~400ns delay
    for (int i = 0; i < 4; i++) {
        inb(port + 7);
    }
}

// Perform a software reset on an ATA channel
static void ata_software_reset(uint16_t control_port) {
    // Set SRST bit
    outb(control_port, ATA_CONTROL_SRST);
    
    // Wait at least 5us
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }
    
    // Clear SRST bit
    outb(control_port, 0);
    
    // Wait for BSY to clear on the status register
    uint16_t status_port = control_port == ATA_PRIMARY_CONTROL ? 
                          ATA_PRIMARY_STATUS : ATA_SECONDARY_STATUS;
    
    // Wait up to 100ms
    ata_wait_not_busy(status_port, 100);
}

// Read sectors from a drive
bool ata_read_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, void* buffer) {
    if (!ata_drive_present(drive_index) || buffer == NULL || count == 0) {
        return false;
    }
    
    // Get ports for this drive
    uint16_t data_port = ata_get_data_port(drive_index);
    uint16_t control_port = ata_get_control_port(drive_index);
    bool is_master = ata_is_master(drive_index);
    
    // Calculate port offsets
    uint16_t features = data_port + 1;
    uint16_t sector_count = data_port + 2;
    uint16_t lba_low = data_port + 3;
    uint16_t lba_mid = data_port + 4;
    uint16_t lba_high = data_port + 5;
    uint16_t drive_head = data_port + 6;
    uint16_t status_cmd = data_port + 7;
    
    // Wait for drive to be ready
    if (!ata_wait_not_busy(status_cmd, ATA_TIMEOUT)) {
        LOG_ERROR("Drive %d not ready for read operation", drive_index);
        return false;
    }
    
    // Select drive and set up LBA
    uint8_t drive_select = is_master ? ATA_DEVICE_MASTER : ATA_DEVICE_SLAVE;
    outb(drive_head, drive_select | ATA_DEVICE_LBA | ((lba >> 24) & 0x0F));
    
    // Set up other registers
    outb(features, 0);                  // No features
    outb(sector_count, count);          // Number of sectors
    outb(lba_low, lba & 0xFF);          // LBA low byte
    outb(lba_mid, (lba >> 8) & 0xFF);   // LBA middle byte
    outb(lba_high, (lba >> 16) & 0xFF); // LBA high byte
    
    // Send read command
    outb(status_cmd, ATA_CMD_READ_PIO);
    
    // Read the data
    uint16_t* buf = (uint16_t*)buffer;
    
    for (int sector = 0; sector < count; sector++) {
        // Wait for data to be ready
        if (!ata_wait_drq(status_cmd, ATA_TIMEOUT)) {
            LOG_ERROR("Drive %d timeout waiting for data", drive_index);
            return false;
        }
        
        // Read 256 words (512 bytes) per sector
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(data_port);
        }
    }
    
    return true;
}

// Write sectors to a drive
bool ata_write_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, const void* buffer) {
    if (!ata_drive_present(drive_index) || buffer == NULL || count == 0) {
        return false;
    }
    
    // Get ports for this drive
    uint16_t data_port = ata_get_data_port(drive_index);
    uint16_t control_port = ata_get_control_port(drive_index);
    bool is_master = ata_is_master(drive_index);
    
    // Calculate port offsets
    uint16_t features = data_port + 1;
    uint16_t sector_count = data_port + 2;
    uint16_t lba_low = data_port + 3;
    uint16_t lba_mid = data_port + 4;
    uint16_t lba_high = data_port + 5;
    uint16_t drive_head = data_port + 6;
    uint16_t status_cmd = data_port + 7;
    
    // Wait for drive to be ready
    if (!ata_wait_not_busy(status_cmd, ATA_TIMEOUT)) {
        LOG_ERROR("Drive %d not ready for write operation", drive_index);
        return false;
    }
    
    // Select drive and set up LBA
    uint8_t drive_select = is_master ? ATA_DEVICE_MASTER : ATA_DEVICE_SLAVE;
    outb(drive_head, drive_select | ATA_DEVICE_LBA | ((lba >> 24) & 0x0F));
    
    // Set up other registers
    outb(features, 0);                  // No features
    outb(sector_count, count);          // Number of sectors
    outb(lba_low, lba & 0xFF);          // LBA low byte
    outb(lba_mid, (lba >> 8) & 0xFF);   // LBA middle byte
    outb(lba_high, (lba >> 16) & 0xFF); // LBA high byte
    
    // Send write command
    outb(status_cmd, ATA_CMD_WRITE_PIO);
    
    // Write the data
    const uint16_t* buf = (const uint16_t*)buffer;
    
    for (int sector = 0; sector < count; sector++) {
        // Wait for drive to be ready to accept data
        if (!ata_wait_drq(status_cmd, ATA_TIMEOUT)) {
            LOG_ERROR("Drive %d timeout waiting for data ready flag", drive_index);
            return false;
        }
        
        // Write 256 words (512 bytes) per sector
        for (int i = 0; i < 256; i++) {
            outw(data_port, buf[sector * 256 + i]);
        }
        
        // Flush the write
        outb(status_cmd, ATA_CMD_CACHE_FLUSH);
        
        // Wait for write to complete
        if (!ata_wait_not_busy(status_cmd, ATA_TIMEOUT)) {
            LOG_ERROR("Drive %d timeout waiting for write to complete", drive_index);
            return false;
        }
    }
    
    return true;
}

// Flush drive cache to ensure writes are completed
bool ata_flush_cache(uint8_t drive_index) {
    if (!ata_drive_present(drive_index)) {
        return false;
    }
    
    // Get ports for this drive
    uint16_t data_port = ata_get_data_port(drive_index);
    uint16_t control_port = ata_get_control_port(drive_index);
    bool is_master = ata_is_master(drive_index);
    
    // Calculate port offsets
    uint16_t drive_head = data_port + 6;
    uint16_t status_cmd = data_port + 7;
    
    // Wait for drive to be ready
    if (!ata_wait_not_busy(status_cmd, ATA_TIMEOUT)) {
        LOG_ERROR("Drive %d not ready for flush operation", drive_index);
        return false;
    }
    
    // Select drive
    uint8_t drive_select = is_master ? ATA_DEVICE_MASTER : ATA_DEVICE_SLAVE;
    outb(drive_head, drive_select | ATA_DEVICE_LBA);
    
    // Send cache flush command
    outb(status_cmd, ATA_CMD_CACHE_FLUSH);
    
    // Wait for flush to complete
    if (!ata_wait_not_busy(status_cmd, ATA_TIMEOUT)) {
        LOG_ERROR("Drive %d timeout waiting for cache flush to complete", drive_index);
        return false;
    }
    
    return true;
}