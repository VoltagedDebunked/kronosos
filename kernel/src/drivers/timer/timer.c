#include <drivers/timer/timer.h>
#include <drivers/pic/pic.h>
#include <core/idt.h>
#include <lib/io.h>
#include <utils/log.h>
#include <stddef.h>

// Global timer tick counter
static volatile uint64_t timer_ticks = 0;

// Registered callback function
static timer_callback_t timer_callback = NULL;

// Timer interrupt handler
static void timer_interrupt_handler(struct interrupt_frame *frame) {
    (void)frame; // Unused parameter
    
    // Increment the tick counter
    timer_ticks++;
    
    // Call the registered callback if available
    if (timer_callback != NULL) {
        timer_callback(timer_ticks);
    }
}

// Initialize the timer with the specified frequency
void timer_init(uint32_t frequency) {
    LOG_INFO("Initializing timer with frequency %d Hz", frequency);
    
    // Set up the timer frequency
    timer_set_frequency(frequency);
    
    // Register our timer interrupt handler
    idt_register_handler(IRQ_TIMER, timer_interrupt_handler);
    
    // Unmask the timer IRQ
    pic_unmask_irq(0);
    
    LOG_INFO_MSG("Timer initialized");
}

// Set the timer frequency
void timer_set_frequency(uint32_t frequency) {
    // Calculate divisor
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    // Send command: Channel 0, Access mode: lobyte/hibyte, Mode 3 (square wave)
    outb(PIT_COMMAND, 0x36);
    
    // Send divisor (low byte first, then high byte)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

// Get the current tick count
uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

// Register a callback function for timer interrupts
void timer_register_callback(timer_callback_t callback) {
    timer_callback = callback;
}

// Get uptime in milliseconds
uint64_t timer_get_uptime_ms(void) {
    // This assumes a 100Hz timer.
    return timer_ticks * 10; // 10ms per tick at 100Hz
}

// Sleep for a specified number of milliseconds
void timer_sleep(uint32_t ms) {
    // Calculate how many ticks we need to wait
    // This assumes a 100Hz timer.
    uint64_t target_ticks = timer_ticks + (ms / 10);
    
    // Wait until we reach the target tick count
    while (timer_ticks < target_ticks) {
        // Yield CPU and wait for next timer interrupt
        asm volatile("hlt");
    }
}