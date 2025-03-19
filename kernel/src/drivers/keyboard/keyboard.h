#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

// Keyboard ports
#define KB_DATA_PORT       0x60
#define KB_STATUS_PORT     0x64
#define KB_COMMAND_PORT    0x64

// Keyboard status register bits
#define KB_STATUS_OUTPUT_FULL  0x01
#define KB_STATUS_INPUT_FULL   0x02

// Key states
typedef enum {
    KEY_RELEASED = 0,
    KEY_PRESSED = 1
} key_state_t;

// Keyboard event structure
typedef struct {
    uint8_t scancode;
    char ascii;
    key_state_t state;
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
} keyboard_event_t;

// Keyboard callback type
typedef void (*keyboard_callback_t)(keyboard_event_t *event);

// Keyboard functions
void keyboard_init(void);
void keyboard_register_callback(keyboard_callback_t callback);
bool keyboard_get_key_state(uint8_t scancode);
const char *keyboard_get_key_name(uint8_t scancode);

#endif // KEYBOARD_H