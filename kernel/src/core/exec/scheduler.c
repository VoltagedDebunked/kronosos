#include <core/exec/scheduler.h>
#include <core/exec/elf.h>
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <drivers/timer/timer.h>
#include <utils/log.h>
#include <lib/string.h>

// Default time quantum in timer ticks
#define DEFAULT_TIME_QUANTUM 20

// Task table
static task_t task_table[TASK_MAX_COUNT];
static uint32_t next_tid = 1;
static task_t* current_task = NULL;
static task_t* idle_task = NULL;

// Task queues
static task_t* ready_queue_head = NULL;
static task_t* ready_queue_tail = NULL;
static task_t* blocked_queue_head = NULL;

// Task table lock
static spinlock_t task_lock;

// Scheduler statistics
static scheduler_stats_t scheduler_stats = {0};

void schedule_next();
void add_to_ready_queue(task_t* task);

// Scheduler configuration
static scheduler_config_t scheduler_config = {
    .max_tasks = TASK_MAX_COUNT,
    .default_time_quantum = DEFAULT_TIME_QUANTUM,
    .tick_rate = 1000,
    .preemption_enabled = true,
    .kernel_stack_size = 16384,
    .user_stack_size = 65536
};

// Timer callback for preemptive scheduling
static void timer_callback(uint64_t tick_count) {
    (void)tick_count; // Unused

    // Update scheduler statistics
    scheduler_stats.ticks_since_boot++;

    // Check if we need to schedule another task
    if (current_task && current_task->state == TASK_STATE_RUNNING) {
        current_task->cpu_time++;

        // If the task has used its time quantum, reschedule
        if (current_task->cpu_time - current_task->last_schedule >= current_task->quantum) {
            // Move current task back to ready queue
            spinlock_acquire(&task_lock);

            if (current_task != idle_task) {
                current_task->state = TASK_STATE_READY;
                add_to_ready_queue(current_task);
            }

            spinlock_release(&task_lock);

            // Schedule next task
            schedule_next();
        }
    } else {
        // No current task or task is not running, schedule next
        schedule_next();
    }
}

// Initialize the scheduler
bool scheduler_init(void) {
    LOG_DEBUG("Initializing scheduler");

    // Initialize task table
    memset(task_table, 0, sizeof(task_table));

    // Initialize spinlock
    spinlock_init(&task_lock);

    // Register timer callback for preemptive scheduling
    timer_register_callback(timer_callback);

    // Initialize timer with the configured tick rate
    timer_init(scheduler_config.tick_rate);

    // Create idle task
    idle_task = &task_table[0]; // Reserve slot 0 for idle
    memset(idle_task, 0, sizeof(task_t));

    idle_task->tid = 0;
    idle_task->state = TASK_STATE_READY;
    strncpy(idle_task->name, "idle_task", sizeof(idle_task->name) - 1);

    LOG_DEBUG("Idle kernel task created with TID 0 and task state READY");

    // The idle task already has a context (current kernel execution context)
    idle_task->context.cr3 = vmm_get_current_address_space();

    // Set up other fields
    idle_task->quantum = UINT64_MAX; // Idle task runs until another task is ready
    idle_task->base_priority = TASK_PRIORITY_IDLE;
    idle_task->dynamic_priority = TASK_PRIORITY_IDLE;

    // Set as idle task
    current_task = idle_task;

    LOG_INFO("Scheduler initialized successfully");
    return true;
}

// Allocate a new task ID
static uint32_t allocate_tid(void) {
    // Simple TID allocation: increment and wrap around if needed
    uint32_t tid = next_tid++;

    // Skip 0 (reserved for idle)
    if (next_tid == 0) {
        next_tid = 1;
    }

    return tid;
}

// Create a new page table for a task
static uintptr_t create_task_address_space(void) {
    // Create a new page table (returns physical address)
    uintptr_t page_table = vmm_create_address_space();

    if (page_table == 0) {
        LOG_ERROR("Failed to create task address space");
        return 0;
    }

    return page_table;
}

// Create a stack for a task
static void* create_task_stack(size_t stack_size, uintptr_t page_table) {
    // Align stack size to page boundary
    stack_size = (stack_size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);

    // Allocate physical memory for the stack
    size_t page_count = stack_size / PAGE_SIZE_4K;
    void* stack_phys = pmm_alloc_pages(page_count);
    if (!stack_phys) {
        LOG_ERROR("Failed to allocate physical memory for task stack");
        return NULL;
    }

    // Define a virtual address for the stack (just below 2GB marker for user space)
    uint64_t stack_virt = 0x00000000EFFFF000ULL - stack_size + PAGE_SIZE_4K;

    // Save current address space
    uintptr_t old_cr3 = vmm_get_current_address_space();

    // Switch to task address space
    vmm_switch_address_space(page_table);

    // Map stack pages into task address space
    for (size_t i = 0; i < stack_size; i += PAGE_SIZE_4K) {
        if (!vmm_map_page(stack_virt + i, (uint64_t)stack_phys + i, 
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
            LOG_ERROR("Failed to map task stack page at 0x%lx", stack_virt + i);

            // Clean up already mapped pages
            for (size_t j = 0; j < i; j += PAGE_SIZE_4K) {
                vmm_unmap_page(stack_virt + j);
            }

            // Switch back to original address space
            vmm_switch_address_space(old_cr3);

            // Free physical memory
            pmm_free_pages(stack_phys, page_count);

            return NULL;
        }
    }

    // Add a guard page below the stack
    uint64_t guard_virt = stack_virt - PAGE_SIZE_4K;
    if (!vmm_map_page(guard_virt, 0, VMM_FLAG_PRESENT | VMM_FLAG_NO_EXECUTE)) {
        LOG_WARN("Failed to create stack guard page");
        // Continue anyway, this is just a protection feature
    }

    // Switch back to original address space
    vmm_switch_address_space(old_cr3);

    // Return the stack top (stacks grow downward)
    return (void*)(stack_virt + stack_size);
}

// Initialize a task's CPU context
static void init_task_context(task_t* task, uint64_t entry, uint64_t stack) {
    // Clear the context
    memset(&task->context, 0, sizeof(task->context));

    // Set up the initial CPU context for the task
    task->context.rip = entry;
    task->context.rsp = stack;
    task->context.rflags = 0x202; // IF flag set (interrupts enabled)

    // Set up segment registers for user mode
    task->context.cs = 0x1B | 3; // User code segment with RPL=3
    task->context.ss = 0x23 | 3; // User data segment with RPL=3
    task->context.ds = 0x23 | 3;
    task->context.es = 0x23 | 3;
    task->context.fs = 0x23 | 3;
    task->context.gs = 0x23 | 3;

    // Set page table
    task->context.cr3 = task->page_table;
}

// Add a task to the ready queue
void add_to_ready_queue(task_t* task) {
    if (!task) return;

    // Initialize links
    task->next = NULL;

    if (!ready_queue_head) {
        // Queue is empty
        ready_queue_head = task;
        ready_queue_tail = task;
        task->prev = NULL;
    } else {
        // Add to end of queue
        ready_queue_tail->next = task;
        task->prev = ready_queue_tail;
        ready_queue_tail = task;
    }

    task->state = TASK_STATE_READY;
}

// Remove a task from the ready queue
static void remove_from_ready_queue(task_t* task) {
    if (!task) return;

    if (task == ready_queue_head) {
        // Task is at head of queue
        ready_queue_head = task->next;
        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            // Queue is now empty
            ready_queue_tail = NULL;
        }
    } else if (task == ready_queue_tail) {
        // Task is at tail of queue
        ready_queue_tail = task->prev;
        if (ready_queue_tail) {
            ready_queue_tail->next = NULL;
        } else {
            // Queue is now empty
            ready_queue_head = NULL;
        }
    } else {
        // Task is in middle of queue
        if (task->prev) {
            task->prev->next = task->next;
        }
        if (task->next) {
            task->next->prev = task->prev;
        }
    }

    task->next = NULL;
    task->prev = NULL;
}

// Add a task to the blocked queue
static void add_to_blocked_queue(task_t* task) {
    if (!task) return;

    // Simple linked list implementation (no need for doubly-linked here)
    task->next = blocked_queue_head;
    blocked_queue_head = task;

    task->state = TASK_STATE_BLOCKED;
}

// Remove a task from the blocked queue
static void remove_from_blocked_queue(task_t* task) {
    if (!task) return;

    task_t* current = blocked_queue_head;
    task_t* prev = NULL;

    while (current) {
        if (current == task) {
            if (prev) {
                prev->next = current->next;
            } else {
                blocked_queue_head = current->next;
            }
            task->next = NULL;
            break;
        }

        prev = current;
        current = current->next;
    }
}

// Free task resources
static void free_task_resources(task_t* task) {
    if (!task) return;

    // Free page table (if any)
    if (task->page_table) {
        vmm_delete_address_space(task->page_table);
        task->page_table = 0;
    }

    // Free stack memory (if any)
    if (task->stack_top) {
        pmm_free_pages(task->stack_top, task->stack_size / PAGE_SIZE_4K);
        task->stack_top = NULL;
    }
}

// Context switch to another task
static void context_switch(task_t* next) {
    if (!next || next == current_task) {
        return;
    }

    task_t* prev = current_task;
    current_task = next;

    // Update task states
    if (prev) {
        if (prev->state == TASK_STATE_RUNNING) {
            prev->state = TASK_STATE_READY;
        }
    }

    next->state = TASK_STATE_RUNNING;
    next->last_schedule = next->cpu_time;

    // Perform the actual context switch
    if (prev) {
        // Save current context and switch to new one
        vmm_switch_address_space(next->page_table);
        task_switch_context((uint64_t*)&prev->context, (uint64_t*)&next->context);
    } else {
        // No previous context, just restore new one
        vmm_switch_address_space(next->page_table);
        task_restore_context((uint64_t*)&next->context);
    }
}

// Schedule the next task to run
void schedule_next(void) {
    // Disable interrupts while scheduling
    __asm__ volatile("cli");

    // Try to get next task from ready queue
    task_t* next = NULL;

    // Simple round-robin scheduler
    if (ready_queue_head) {
        next = ready_queue_head;
        ready_queue_head = ready_queue_head->next;

        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            ready_queue_tail = NULL;
        }

        next->next = NULL;
        next->prev = NULL;
    } else {
        // No ready tasks, use idle task
        next = idle_task;
    }

    // Switch to the next task
    if (next) {
        context_switch(next);
    }

    // Interrupts will be re-enabled by the context switch
}