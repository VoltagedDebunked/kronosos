#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of drives supported
#define ATA_MAX_DRIVES 8

// Drive type definitions
#define ATA_DRIVE_TYPE_NONE    0
#define ATA_DRIVE_TYPE_PATA    1
#define ATA_DRIVE_TYPE_SATA    2
#define ATA_DRIVE_TYPE_PATAPI  3
#define ATA_DRIVE_TYPE_SATAPI  4

// AHCI Port Command Register (PxCMD) Bit Definitions
#define AHCI_PORT_CMD_ST       (1 << 0)   // Start
#define AHCI_PORT_CMD_SUD      (1 << 1)   // Spin-Up Device
#define AHCI_PORT_CMD_POD      (1 << 2)   // Power On Device
#define AHCI_PORT_CMD_CLO      (1 << 3)   // Command List Override
#define AHCI_PORT_CMD_FRE      (1 << 4)   // FIS Receive Enable
#define AHCI_PORT_CMD_CCS      (1 << 8)   // Current Command Slot
#define AHCI_PORT_CMD_MPSS     (1 << 13)  // MSI Message Sent
#define AHCI_PORT_CMD_FR        (1 << 14)  // FIS Receive Running
#define AHCI_PORT_CMD_CR        (1 << 15)  // Command List Running
#define AHCI_PORT_CMD_CPS       (1 << 16)  // Cold Presence State
#define AHCI_PORT_CMD_PMA       (1 << 17)  // Port Multiplier Attached
#define AHCI_PORT_CMD_HPCP      (1 << 18)  // Hot Plug Capable Port
#define AHCI_PORT_CMD_MPAB      (1 << 19)  // Mechanical Presence Activate
#define AHCI_PORT_CMD_CQE       (1 << 23)  // Command List Quality Event
#define AHCI_PORT_CMD_FBSCP     (1 << 24)  // FIS-Based Switching Capable Port
#define AHCI_PORT_CMD_APSTE     (1 << 25)  // Automatic Partial to Slumber Transitions Enabled
#define AHCI_PORT_CMD_ATAPI     (1 << 24)  // Device is ATAPI
#define AHCI_PORT_CMD_DLAE      (1 << 30)  // Device LED Active on External
#define AHCI_PORT_CMD_ASP       (1 << 31)  // Aggressive Slumber/Partial

// Drive information structure
typedef struct {
    uint16_t type;           // Drive type (PATA, SATA, etc.)
    uint16_t signature;      // Drive signature
    uint16_t capabilities;   // Features supported
    uint32_t command_sets;   // Command sets supported
    uint32_t size;           // Size in sectors
    char model[41];          // Model string (null-terminated)
    char serial[21];         // Serial number (null-terminated)
    uint16_t cylinders;      // For CHS addressing (legacy)
    uint16_t heads;          // For CHS addressing (legacy)
    uint16_t sectors;        // For CHS addressing (legacy)
} ata_drive_t;

// ATA driver initialization
void ata_init(void);

// Check if a drive is present
bool ata_drive_present(uint8_t drive_index);

// Get drive information
const ata_drive_t* ata_get_drive_info(uint8_t drive_index);

// Print information about detected drives
void ata_print_info(void);

// Read sectors from a drive
bool ata_read_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, void* buffer);

// Write sectors to a drive
bool ata_write_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, const void* buffer);

// Flush drive cache to ensure writes are completed
bool ata_flush_cache(uint8_t drive_index);

#endif // ATA_H