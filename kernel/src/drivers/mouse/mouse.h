#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// Mouse packet structure
typedef struct {
    uint8_t buttons;
    int8_t delta_x;
    int8_t delta_y;
    bool scroll_event;
    int8_t scroll_delta;
} mouse_event_t;

// Mouse button flags
#define MOUSE_LEFT_BUTTON     0x01
#define MOUSE_RIGHT_BUTTON    0x02
#define MOUSE_MIDDLE_BUTTON   0x04

// Mouse callback type
typedef void (*mouse_callback_t)(mouse_event_t *event);

// Mouse functions
void mouse_init(void);
void mouse_register_callback(mouse_callback_t callback);
bool mouse_get_button_state(uint8_t button);

#endif // MOUSE_H