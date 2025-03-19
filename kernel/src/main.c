#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <lib/asm.h>
#include <lib/string.h>
#include <utils/log.h>
#include <utils/sysinfo.h>
#include <core/gdt.h>
#include <core/idt.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <drivers/timer/timer.h>
#include <drivers/keyboard/keyboard.h>
#include <drivers/mouse/mouse.h>
#include <drivers/ata/ata.h>
#include <core/exec/scheduler.h>
#include <fs/ext2.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

void setup_fb() {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        LOG_CRITICAL_MSG("No framebuffer available");
        hcf();
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    LOG_INFO("Framebuffer: %dx%d, pitch: %d, bpp: %d",
             framebuffer->width, framebuffer->height, framebuffer->pitch, framebuffer->bpp);

    memset((void *)framebuffer->address, 0, framebuffer->pitch * framebuffer->height);
}

// Kernel initialization and setup
void kmain(void) {
    log_init(LOG_LEVEL_DEBUG);

    LOG_INFO_MSG("KronosOS booting");

    setup_fb();

    LOG_INFO_MSG("Initializing GDT");
    gdt_init();

    idt_init();

    // Initialize the physical memory manager
    if (memmap_request.response == NULL) {
        LOG_CRITICAL_MSG("Memory map information not available");
        hcf();
    }

    pmm_init(memmap_request.response);

    vmm_init(memmap_request.response);

    timer_init(100); // 100 Hz timer frequency

    LOG_INFO_MSG("Initializing I/O Drivers (KB, Mouse)");
    keyboard_init();
    mouse_init();

    LOG_INFO_MSG("Enabling interrupts");
    interrupt_enable();

    ata_init();

    ext2_init();

    scheduler_init();

    LOG_INFO_MSG("Kernel initialized");

    sysinfo_print();

    // Main kernel loop
    while (1) {}
}
