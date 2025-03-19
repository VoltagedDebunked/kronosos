#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <memory/vmm.h>
#include <memory/pmm.h>

#define TASK_MAX_COUNT 256

// Task states
typedef enum {
    TASK_STATE_NEW,         // Task is newly created
    TASK_STATE_READY,        // Task is ready to run
    TASK_STATE_RUNNING,      // Task is currently running
    TASK_STATE_BLOCKED,      // Task is blocked (e.g., waiting for I/O)
    TASK_STATE_TERMINATED    // Task has terminated
} task_state_t;

// Task priority levels
typedef enum {
    TASK_PRIORITY_IDLE = 0,  // Lowest priority, idle task
    TASK_PRIORITY_LOW = 1,    // Low priority background tasks
    TASK_PRIORITY_NORMAL = 2, // Normal user tasks
    TASK_PRIORITY_HIGH = 3,   // High priority tasks
    TASK_PRIORITY_REALTIME = 4 // Highest priority, real-time tasks
} task_priority_t;

// CPU context structure (x86_64)
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs, ss, ds, es, fs, gs;
    uint64_t cr3; // Page table base
} cpu_context_t;

// Task control block structure
typedef struct task {
    uint32_t tid;                      // Task ID
    char name[32];                     // Task name
    task_state_t state;                // Current state
    task_priority_t base_priority;     // Base priority
    task_priority_t dynamic_priority;  // Current (dynamic) priority
    uint64_t quantum;                  // Time quantum in ticks
    uint64_t cpu_time;                 // Total CPU time used
    uint64_t last_schedule;            // Last time the task was scheduled
    uint64_t start_time;               // Time when the task was created
    int exit_code;                     // Exit code (if terminated)
    
    cpu_context_t context;             // CPU context
    uintptr_t page_table;              // Page table (CR3 value)
    void* stack_top;                   // Top of the task's stack
    size_t stack_size;                 // Size of the task's stack
    
    struct task* next;                 // Next task in queue
    struct task* prev;                 // Previous task in queue
} task_t;

// Scheduler configuration
typedef struct {
    uint32_t max_tasks;                // Maximum number of tasks
    uint64_t default_time_quantum;     // Default time quantum in ticks
    uint64_t tick_rate;                // Ticks per second
    bool preemption_enabled;           // Whether preemption is enabled
    size_t kernel_stack_size;          // Default kernel stack size
    size_t user_stack_size;            // Default user stack size
} scheduler_config_t;

// Scheduler statistics
typedef struct {
    uint64_t total_tasks_created;      // Total number of tasks ever created
    uint64_t context_switches;         // Total number of context switches
    uint64_t ticks_since_boot;         // Total ticks since system boot
    uint32_t current_task_count;       // Current number of tasks
    uint32_t ready_tasks;              // Number of tasks in READY state
    uint32_t blocked_tasks;            // Number of tasks in BLOCKED state
    uint64_t idle_ticks;               // Time spent in idle task
    uint64_t kernel_ticks;             // Time spent in kernel tasks
    uint64_t user_ticks;               // Time spent in user tasks
} scheduler_stats_t;

// Spinlock structure
typedef struct {
    volatile int locked;               // Lock state (0 = unlocked, 1 = locked)
} spinlock_t;

// Spinlock functions
static inline void spinlock_init(spinlock_t* lock) {
    lock->locked = 0;
}

static inline void spinlock_acquire(spinlock_t* lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        // Busy-wait until the lock is acquired
        __asm__ volatile("pause");
    }
}

static inline void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(&lock->locked);
}

// Scheduler functions
bool scheduler_init(void);
bool scheduler_scheduler_init(void);
bool scheduler_register_kernel_idle(void);
uint32_t scheduler_create_task(const void* elf_data, size_t elf_size, const char* name, task_priority_t priority);
bool scheduler_execute_task(uint32_t tid, int argc, char* argv[], char* envp[]);
bool scheduler_terminate_task(uint32_t tid, int exit_code);
task_t* scheduler_get_current_task(void);
task_t* scheduler_get_task_by_id(uint32_t tid);
void scheduler_yield(void);
void scheduler_block_task(task_state_t state);
bool scheduler_unblock_task(uint32_t tid);
bool scheduler_set_task_priority(uint32_t tid, task_priority_t priority);
bool scheduler_get_task_stats(uint32_t tid, uint64_t* cpu_time, task_state_t* state);
int scheduler_get_task_list(uint32_t* tids, int max_count);
extern void task_switch_context(uint64_t* old_ctx, uint64_t* new_ctx);
extern void task_restore_context(uint64_t* ctx);

#endif // SCHEDULER_H