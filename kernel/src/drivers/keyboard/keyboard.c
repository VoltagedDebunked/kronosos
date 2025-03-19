#include <drivers/keyboard/keyboard.h>
#include <drivers/pic/pic.h>
#include <core/idt.h>
#include <lib/io.h>
#include <utils/log.h>
#include <stddef.h>

// US QWERTY layout scancode to ASCII mapping (non-shifted)
static const char scancode_to_ascii[] = {
    0,    // 0x00: Error or NULL
    0,    // 0x01: Escape
    '1',  // 0x02
    '2',  // 0x03
    '3',  // 0x04
    '4',  // 0x05
    '5',  // 0x06
    '6',  // 0x07
    '7',  // 0x08
    '8',  // 0x09
    '9',  // 0x0A
    '0',  // 0x0B
    '-',  // 0x0C
    '=',  // 0x0D
    0,    // 0x0E: Backspace
    0,    // 0x0F: Tab
    'q',  // 0x10
    'w',  // 0x11
    'e',  // 0x12
    'r',  // 0x13
    't',  // 0x14
    'y',  // 0x15
    'u',  // 0x16
    'i',  // 0x17
    'o',  // 0x18
    'p',  // 0x19
    '[',  // 0x1A
    ']',  // 0x1B
    0,    // 0x1C: Enter
    0,    // 0x1D: Left Control
    'a',  // 0x1E
    's',  // 0x1F
    'd',  // 0x20
    'f',  // 0x21
    'g',  // 0x22
    'h',  // 0x23
    'j',  // 0x24
    'k',  // 0x25
    'l',  // 0x26
    ';',  // 0x27
    '\'', // 0x28
    '`',  // 0x29
    0,    // 0x2A: Left Shift
    '\\', // 0x2B
    'z',  // 0x2C
    'x',  // 0x2D
    'c',  // 0x2E
    'v',  // 0x2F
    'b',  // 0x30
    'n',  // 0x31
    'm',  // 0x32
    ',',  // 0x33
    '.',  // 0x34
    '/',  // 0x35
    0,    // 0x36: Right Shift
    '*',  // 0x37: Keypad *
    0,    // 0x38: Left Alt
    ' ',  // 0x39: Space
    0,    // 0x3A: Caps Lock
    0,    // 0x3B: F1
    0,    // 0x3C: F2
    0,    // 0x3D: F3
    0,    // 0x3E: F4
    0,    // 0x3F: F5
    0,    // 0x40: F6
    0,    // 0x41: F7
    0,    // 0x42: F8
    0,    // 0x43: F9
    0,    // 0x44: F10
    0,    // 0x45: Num Lock
    0,    // 0x46: Scroll Lock
    '7',  // 0x47: Keypad 7
    '8',  // 0x48: Keypad 8
    '9',  // 0x49: Keypad 9
    '-',  // 0x4A: Keypad -
    '4',  // 0x4B: Keypad 4
    '5',  // 0x4C: Keypad 5
    '6',  // 0x4D: Keypad 6
    '+',  // 0x4E: Keypad +
    '1',  // 0x4F: Keypad 1
    '2',  // 0x50: Keypad 2
    '3',  // 0x51: Keypad 3
    '0',  // 0x52: Keypad 0
    '.',  // 0x53: Keypad .
    0,    // 0x54: Alt-SysRq
    0,    // 0x55: F11/F12?
    0,    // 0x56: International
    0,    // 0x57: F11
    0,    // 0x58: F12
    0     // 0x59: All other keys are undefined
};

// US QWERTY layout scancode to ASCII mapping (shifted)
static const char scancode_to_ascii_shifted[] = {
    0,    // 0x00: Error or NULL
    0,    // 0x01: Escape
    '!',  // 0x02
    '@',  // 0x03
    '#',  // 0x04
    '$',  // 0x05
    '%',  // 0x06
    '^',  // 0x07
    '&',  // 0x08
    '*',  // 0x09
    '(',  // 0x0A
    ')',  // 0x0B
    '_',  // 0x0C
    '+',  // 0x0D
    0,    // 0x0E: Backspace
    0,    // 0x0F: Tab
    'Q',  // 0x10
    'W',  // 0x11
    'E',  // 0x12
    'R',  // 0x13
    'T',  // 0x14
    'Y',  // 0x15
    'U',  // 0x16
    'I',  // 0x17
    'O',  // 0x18
    'P',  // 0x19
    '{',  // 0x1A
    '}',  // 0x1B
    0,    // 0x1C: Enter
    0,    // 0x1D: Left Control
    'A',  // 0x1E
    'S',  // 0x1F
    'D',  // 0x20
    'F',  // 0x21
    'G',  // 0x22
    'H',  // 0x23
    'J',  // 0x24
    'K',  // 0x25
    'L',  // 0x26
    ':',  // 0x27
    '"',  // 0x28
    '~',  // 0x29
    0,    // 0x2A: Left Shift
    '|',  // 0x2B
    'Z',  // 0x2C
    'X',  // 0x2D
    'C',  // 0x2E
    'V',  // 0x2F
    'B',  // 0x30
    'N',  // 0x31
    'M',  // 0x32
    '<',  // 0x33
    '>',  // 0x34
    '?',  // 0x35
    0,    // 0x36: Right Shift
    '*',  // 0x37: Keypad *
    0,    // 0x38: Left Alt
    ' ',  // 0x39: Space
    0,    // 0x3A: Caps Lock
    0,    // 0x3B: F1
    0,    // 0x3C: F2
    0,    // 0x3D: F3
    0,    // 0x3E: F4
    0,    // 0x3F: F5
    0,    // 0x40: F6
    0,    // 0x41: F7
    0,    // 0x42: F8
    0,    // 0x43: F9
    0,    // 0x44: F10
    0,    // 0x45: Num Lock
    0,    // 0x46: Scroll Lock
    '7',  // 0x47: Keypad 7
    '8',  // 0x48: Keypad 8
    '9',  // 0x49: Keypad 9
    '-',  // 0x4A: Keypad -
    '4',  // 0x4B: Keypad 4
    '5',  // 0x4C: Keypad 5
    '6',  // 0x4D: Keypad 6
    '+',  // 0x4E: Keypad +
    '1',  // 0x4F: Keypad 1
    '2',  // 0x50: Keypad 2
    '3',  // 0x51: Keypad 3
    '0',  // 0x52: Keypad 0
    '.',  // 0x53: Keypad .
    0,    // 0x54: Alt-SysRq
    0,    // 0x55: F11/F12?
    0,    // 0x56: International
    0,    // 0x57: F11
    0,    // 0x58: F12
    0     // 0x59: All other keys are undefined
};

// Key state tracking
static uint8_t key_states[128] = {0};  // Tracks the state of each key (0=up, 1=down)
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;

// Registered callback
static keyboard_callback_t kb_callback = NULL;

// Forward declarations
static void keyboard_handle_keypress(uint8_t scancode);

// Keyboard interrupt handler
static void keyboard_interrupt_handler(struct interrupt_frame *frame) {
    (void)frame; // Unused parameter
    
    // Read the scancode from the keyboard
    uint8_t scancode = inb(KB_DATA_PORT);
    
    // Process the scancode
    keyboard_handle_keypress(scancode);
}

// Initialize the keyboard
void keyboard_init(void) {
    LOG_INFO_MSG("Initializing keyboard");
    
    // Register the keyboard interrupt handler
    idt_register_handler(IRQ_KEYBOARD, keyboard_interrupt_handler);
    
    // Unmask the keyboard IRQ
    pic_unmask_irq(1);
    
    LOG_INFO_MSG("Keyboard initialized");
}

// Register a callback function to handle keyboard events
void keyboard_register_callback(keyboard_callback_t callback) {
    kb_callback = callback;
}

// Get the key state for a given scancode
bool keyboard_get_key_state(uint8_t scancode) {
    if (scancode < 128) {
        return key_states[scancode] == 1;
    }
    return false;
}

// Get the name of a key from its scancode
const char *keyboard_get_key_name(uint8_t scancode) {
    static const char *key_names[] = {
        "Unknown", "Escape", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
        "Minus", "Equal", "Backspace", "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
        "LeftBracket", "RightBracket", "Enter", "LeftCtrl", "A", "S", "D", "F", "G", "H", "J", "K", "L",
        "Semicolon", "Apostrophe", "Backtick", "LeftShift", "Backslash", "Z", "X", "C", "V", "B", "N", "M",
        "Comma", "Period", "Slash", "RightShift", "KeypadMultiply", "LeftAlt", "Space", "CapsLock",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
        "NumLock", "ScrollLock", "Keypad7", "Keypad8", "Keypad9", "KeypadMinus",
        "Keypad4", "Keypad5", "Keypad6", "KeypadPlus", "Keypad1", "Keypad2", "Keypad3", "Keypad0", "KeypadDecimal"
    };
    
    if (scancode < sizeof(key_names) / sizeof(key_names[0])) {
        return key_names[scancode];
    }
    
    return "Unknown";
}

// Handle a keypress from the interrupt handler
static void keyboard_handle_keypress(uint8_t scancode) {
    bool is_release = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    
    // Update key state
    if (is_release) {
        key_states[key] = 0;
    } else {
        key_states[key] = 1;
    }
    
    // Update modifier key states
    if (key == 0x2A || key == 0x36) {  // Left or Right Shift
        shift_pressed = !is_release;
    } else if (key == 0x1D) {  // Left Control
        ctrl_pressed = !is_release;
    } else if (key == 0x38) {  // Left Alt
        alt_pressed = !is_release;
    } else if (key == 0x3A && !is_release) {  // Caps Lock (toggle on press)
        caps_lock = !caps_lock;
    }
    
    // Create a keyboard event
    keyboard_event_t event;
    event.scancode = key;
    event.state = is_release ? KEY_RELEASED : KEY_PRESSED;
    event.shift_pressed = shift_pressed;
    event.ctrl_pressed = ctrl_pressed;
    event.alt_pressed = alt_pressed;
    
    // Convert scancode to ASCII
    if (key < sizeof(scancode_to_ascii) && !is_release) {
        // Check if shift or caps lock should be applied
        bool uppercase = (shift_pressed != caps_lock); // XOR
        
        // For letters, apply caps lock
        if ((key >= 0x10 && key <= 0x19) || // Q-P
            (key >= 0x1E && key <= 0x26) || // A-L
            (key >= 0x2C && key <= 0x32)) { // Z-M
            
            if (uppercase) {
                event.ascii = scancode_to_ascii_shifted[key];
            } else {
                event.ascii = scancode_to_ascii[key];
            }
        } 
        // For non-letters, apply shift
        else {
            if (shift_pressed) {
                event.ascii = scancode_to_ascii_shifted[key];
            } else {
                event.ascii = scancode_to_ascii[key];
            }
        }
    } else {
        event.ascii = 0;
    }
    
    // Call the registered callback if available
    if (kb_callback != NULL) {
        kb_callback(&event);
    }
    
    // For debugging: print key presses (not releases)
    if (!is_release && key < 128) {
        const char *key_name = keyboard_get_key_name(key);
        
        if (event.ascii) {
            LOG_DEBUG("Key pressed: %s (ASCII: '%c')", key_name, event.ascii);
        } else {
            LOG_DEBUG("Key pressed: %s", key_name);
        }
    }
}