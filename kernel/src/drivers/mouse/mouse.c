#include <drivers/mouse/mouse.h>
#include <lib/io.h>
#include <core/idt.h>
#include <drivers/pic/pic.h>
#include <utils/log.h>
#include <stddef.h>

// PS/2 mouse ports
#define MOUSE_DATA_PORT     0x60
#define MOUSE_STATUS_PORT   0x64
#define MOUSE_COMMAND_PORT  0x64

// Mouse status register bits
#define MOUSE_STATUS_OUTPUT_FULL  0x01
#define MOUSE_STATUS_INPUT_FULL   0x02

// Mouse command bytes
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_DEFAULTS      0xF6
#define MOUSE_CMD_SAMPLE_RATE   0xF3

// Mouse initialization sequence command bytes
#define MOUSE_INIT_DEFAULTS     0xF6
#define MOUSE_INIT_SAMPLE_RATE  0xF3

// Packet states and buffers
#define MOUSE_PACKET_SIZE       3
#define MOUSE_PACKET_OVERFLOW   0x80
#define MOUSE_PACKET_X_SIGN     0x10
#define MOUSE_PACKET_Y_SIGN     0x20

static uint8_t mouse_packet[MOUSE_PACKET_SIZE];
static int mouse_cycle = 0;
static int mouse_x = 0;
static int mouse_y = 0;

// Key state tracking
static uint8_t mouse_buttons = 0;

// Registered callback
static mouse_callback_t mouse_callback = NULL;

// Read a byte from the mouse data port
static uint8_t mouse_read(void) {
    // Wait for data to be available
    while (!(inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_OUTPUT_FULL)) {
        // Busy wait
    }
    return inb(MOUSE_DATA_PORT);
}

// Send a command to the mouse
static void mouse_write(uint8_t cmd) {
    // Wait for input buffer to be empty
    while (inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_INPUT_FULL) {
        // Busy wait
    }
    
    // Send command
    outb(MOUSE_COMMAND_PORT, 0xD4);  // Next byte will go to mouse
    
    // Wait for input buffer to be empty again
    while (inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_INPUT_FULL) {
        // Busy wait
    }
    
    // Send the actual command
    outb(MOUSE_DATA_PORT, cmd);
}

// Wait and acknowledge a response from the mouse
static bool mouse_wait_ack(void) {
    uint8_t response = mouse_read();
    return (response == 0xFA);  // 0xFA is the ACK response
}

// Mouse interrupt handler
static void mouse_interrupt_handler(struct interrupt_frame *frame) {
    (void)frame; // Unused parameter
    
    // Read the mouse packet byte
    uint8_t mouse_data = inb(MOUSE_DATA_PORT);
    
    switch (mouse_cycle) {
        case 0: // Status byte
            mouse_packet[0] = mouse_data;
            mouse_cycle++;
            break;
        
        case 1: // X movement
            mouse_packet[1] = mouse_data;
            mouse_cycle++;
            break;
        
        case 2: // Y movement
            mouse_packet[2] = mouse_data;
            
            // Process the complete packet
            mouse_event_t event = {0};
            
            // Buttons
            event.buttons = mouse_packet[0] & 0x07;
            
            // X movement (signed)
            event.delta_x = mouse_packet[1];
            if (mouse_packet[0] & MOUSE_PACKET_X_SIGN) {
                event.delta_x |= 0xFFFFFF00; // Sign extend
            }
            
            // Y movement (signed)
            event.delta_y = mouse_packet[2];
            if (mouse_packet[0] & MOUSE_PACKET_Y_SIGN) {
                event.delta_y |= 0xFFFFFF00; // Sign extend
            }
            event.delta_y = -event.delta_y; // Invert Y (screen coordinates)
            
            // Call the callback if registered
            if (mouse_callback) {
                mouse_callback(&event);
            }
            
            // Update button tracking
            mouse_buttons = event.buttons;
            
            // Reset the cycle
            mouse_cycle = 0;
            break;
    }
}

// Initialize the mouse
void mouse_init(void) {
    LOG_INFO_MSG("Initializing PS/2 Mouse");
    
    // First, disable the mouse
    mouse_write(MOUSE_CMD_DISABLE);
    
    // Reset the mouse
    mouse_write(MOUSE_CMD_RESET);
    if (!mouse_wait_ack()) {
        LOG_WARN_MSG("Mouse reset failed");
        return;
    }
    
    // Discard the reset response
    uint8_t reset_response1 = mouse_read();
    uint8_t reset_response2 = mouse_read();
    LOG_DEBUG("Mouse reset response: 0x%X 0x%X", reset_response1, reset_response2);
    
    // Set default settings
    mouse_write(MOUSE_CMD_DEFAULTS);
    if (!mouse_wait_ack()) {
        LOG_WARN_MSG("Mouse default settings failed");
        return;
    }
    
    // Set sample rate (100 samples per second)
    mouse_write(MOUSE_CMD_SAMPLE_RATE);
    if (!mouse_wait_ack()) {
        LOG_WARN_MSG("Mouse sample rate command failed");
        return;
    }
    mouse_write(100);  // 100 samples per second
    if (!mouse_wait_ack()) {
        LOG_WARN_MSG("Mouse sample rate set failed");
        return;
    }
    
    // Enable the mouse
    mouse_write(MOUSE_CMD_ENABLE);
    if (!mouse_wait_ack()) {
        LOG_WARN_MSG("Mouse enable failed");
        return;
    }
    
    // Register the mouse interrupt handler
    idt_register_handler(IRQ_MOUSE, mouse_interrupt_handler);
    
    // Unmask the mouse IRQ
    pic_unmask_irq(12);
    
    LOG_INFO_MSG("PS/2 Mouse initialized");
}

// Register a callback function to handle mouse events
void mouse_register_callback(mouse_callback_t callback) {
    mouse_callback = callback;
}

// Get the button state for a specific button
bool mouse_get_button_state(uint8_t button) {
    if (button > 2) {
        return false;
    }
    return (mouse_buttons & (1 << button)) != 0;
}