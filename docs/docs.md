# KronosOS Kernel Documentation

## Key Components

### 1. **Memory Management**
   - **Physical Memory Manager (PMM)**: Manages physical memory allocation and deallocation.
   - **Virtual Memory Manager (VMM)**: Handles virtual memory mapping, page tables, and address spaces.
   - **Heap Allocation**: Provides dynamic memory allocation for the kernel and user processes.

### 2. **Process Management**
   - **Scheduler**: Manages task scheduling, context switching, and process states.
   - **System Calls**: Provides an interface for user processes to interact with the kernel.
   - **ELF Loader**: Loads and executes ELF binaries.

### 3. **File System**
   - **EXT2 File System**: Implements the EXT2 file system for managing files and directories.
   - **ATA Driver**: Provides low-level access to ATA devices for file system operations.

### 4. **Device Drivers**
   - **Keyboard**: Handles keyboard input.
   - **Mouse**: Manages mouse input.
   - **Timer**: Provides timekeeping and scheduling.
   - **Serial Port**: Enables communication over serial ports.
   - **PCI**: Manages PCI devices and configuration.

### 5. **Interrupt Handling**
   - **Interrupt Descriptor Table (IDT)**: Manages interrupt handlers for hardware and software interrupts.
   - **Programmable Interrupt Controller (PIC)**: Handles hardware interrupts.

### 6. **Logging and Debugging**
   - **Logging**: Provides logging capabilities for debugging and monitoring.
   - **System Information**: Displays system information such as memory usage and task states.

### 7. **Bootstrapping**
   - **Limine Boot Protocol**: Uses the Limine bootloader for loading the kernel and initializing hardware.

## Detailed Documentation

### 1. **Memory Management**

#### Physical Memory Manager (PMM)
- **Functions**:
  - `pmm_init(struct limine_memmap_response *memmap)`: Initializes the PMM using the memory map provided by the bootloader.
  - `pmm_alloc_page()`: Allocates a single physical memory page.
  - `pmm_alloc_pages(size_t count)`: Allocates multiple contiguous physical memory pages.
  - `pmm_free_page(void *page_addr)`: Frees a previously allocated physical memory page.
  - `pmm_free_pages(void *page_addr, size_t count)`: Frees multiple contiguous physical memory pages.
  - `pmm_is_page_free(void *page_addr)`: Checks if a page is free.
  - `pmm_get_free_memory()`: Returns the amount of free physical memory in bytes.
  - `pmm_get_used_memory()`: Returns the amount of used physical memory in bytes.
  - `pmm_print_stats()`: Prints memory statistics.

#### Virtual Memory Manager (VMM)
- **Functions**:
  - `vmm_init(struct limine_memmap_response *memmap)`: Initializes the VMM using the memory map provided by the bootloader.
  - `vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)`: Maps a physical page to a virtual address with specified flags.
  - `vmm_map_pages(uint64_t virt_addr, uint64_t phys_addr, size_t count, uint64_t flags)`: Maps multiple pages at once.
  - `vmm_unmap_page(uint64_t virt_addr)`: Unmaps a page at the specified virtual address.
  - `vmm_unmap_pages(uint64_t virt_addr, size_t count)`: Unmaps multiple pages.
  - `vmm_get_physical_address(uint64_t virt_addr)`: Returns the physical address of a virtual address.
  - `vmm_is_mapped(uint64_t virt_addr)`: Checks if a virtual address is mapped.
  - `vmm_create_address_space()`: Creates a new address space.
  - `vmm_delete_address_space(uint64_t pml4_phys)`: Deletes an address space.
  - `vmm_switch_address_space(uint64_t pml4_phys)`: Switches to a different address space.
  - `vmm_get_current_address_space()`: Returns the current address space.
  - `vmm_allocate(size_t size, uint64_t flags)`: Allocates virtual memory.
  - `vmm_free(void* addr, size_t size)`: Frees allocated memory.
  - `vmm_map_physical(uint64_t phys_addr, size_t size, uint64_t flags)`: Maps physical memory to virtual address space.
  - `vmm_unmap_physical(void* virt_addr, size_t size)`: Unmaps previously mapped physical memory.
  - `vmm_handle_page_fault(uint64_t fault_addr, uint32_t error_code)`: Handles page faults.
  - `vmm_flush_tlb_page(uint64_t virt_addr)`: Flushes the TLB for a specific address.
  - `vmm_flush_tlb_full()`: Flushes the entire TLB.
  - `vmm_dump_page_tables(uint64_t virt_addr)`: Dumps page tables for debugging.

### 2. **Process Management**

#### Scheduler
- **Functions**:
  - `scheduler_init()`: Initializes the scheduler.
  - `scheduler_register_kernel_idle()`: Registers the kernel idle task.
  - `scheduler_create_task(const void* elf_data, size_t elf_size, const char* name, task_priority_t priority, int argc, char* argv[], char* envp[])`: Creates a new task.
  - `scheduler_execute_task(uint32_t tid, int argc, char* argv[], char* envp[])`: Executes a task.
  - `scheduler_terminate_task(uint32_t tid, int exit_code)`: Terminates a task.
  - `scheduler_get_current_task()`: Returns the current task.
  - `scheduler_get_task_by_id(uint32_t tid)`: Returns a task by its ID.
  - `scheduler_yield()`: Yields the CPU to another task.
  - `scheduler_block_task(task_state_t state)`: Blocks a task.
  - `scheduler_unblock_task(uint32_t tid)`: Unblocks a task.
  - `scheduler_set_task_priority(uint32_t tid, task_priority_t priority)`: Sets the priority of a task.
  - `scheduler_get_task_stats(uint32_t tid, uint64_t* cpu_time, task_state_t* state)`: Returns the statistics of a task.
  - `scheduler_get_task_list(uint32_t* tids, int max_count)`: Returns a list of task IDs.

#### System Calls
- **Functions**:
  - `syscalls_init()`: Initializes the system call interface.
  - `sys_read(int fd, void *buf, size_t count)`: Reads from a file descriptor.
  - `sys_write(int fd, const void *buf, size_t count)`: Writes to a file descriptor.
  - `sys_open(const char *filename, int flags, int mode)`: Opens a file.
  - `sys_close(int fd)`: Closes a file descriptor.
  - `sys_brk(void *addr)`: Changes the data segment size.
  - `sys_exit(int status)`: Terminates the current process.
  - `sys_getpid()`: Returns the process ID of the current process.
  - `sys_fork()`: Creates a new process by duplicating the current process.
  - `sys_execve(const char *filename, char *const argv[], char *const envp[])`: Replaces the current process image with a new one.
  - `sys_waitpid(pid_t pid, int *status, int options)`: Waits for a child process to change state.
  - `sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)`: Maps files or devices into memory.
  - `sys_munmap(void *addr, size_t length)`: Unmaps files or devices from memory.
  - `sys_getdents(int fd, struct linux_dirent64 *dirp, unsigned int count)`: Reads directory entries.
  - `sys_getcwd(char *buf, size_t size)`: Gets the current working directory.
  - `sys_chdir(const char *path)`: Changes the current working directory.
  - `sys_fstat(int fd, struct stat *statbuf)`: Gets file status.
  - `sys_lseek(int fd, off_t offset, int whence)`: Repositions the file offset.
  - `sys_mkdir(const char *pathname, int mode)`: Creates a directory.
  - `sys_rmdir(const char *pathname)`: Removes a directory.
  - `sys_unlink(const char *pathname)`: Deletes a name and possibly the file it refers to.

### 3. **File System**

#### EXT2 File System
- **Functions**:
  - `ext2_init()`: Initializes the EXT2 file system.
  - `ext2_mount(uint8_t drive_index)`: Mounts an EXT2 file system from a drive.
  - `ext2_unmount()`: Unmounts the EXT2 file system.
  - `ext2_open(const char *path, uint32_t flags)`: Opens a file.
  - `ext2_close(int fd)`: Closes a file.
  - `ext2_read(int fd, void *buffer, size_t size)`: Reads from a file.
  - `ext2_write(int fd, const void *buffer, size_t size)`: Writes to a file.
  - `ext2_mkdir(const char *path, uint32_t mode)`: Creates a directory.
  - `ext2_rmdir(const char *path)`: Removes a directory.
  - `ext2_unlink(const char *path)`: Deletes a file.
  - `ext2_read_block(uint8_t drive_index, uint32_t block_no, void *buffer)`: Reads a block from the file system.
  - `ext2_write_block(uint8_t drive_index, uint32_t block_no, void *buffer)`: Writes a block to the file system.
  - `ext2_read_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode)`: Reads an inode from the file system.
  - `ext2_write_inode(uint8_t drive_index, uint32_t inode_no, ext2_inode_t *inode)`: Writes an inode to the file system.
  - `ext2_allocate_block(uint8_t drive_index)`: Allocates a block in the file system.
  - `ext2_allocate_inode(uint8_t drive_index)`: Allocates an inode in the file system.
  - `ext2_lookup_path(uint8_t drive_index, const char *path)`: Looks up a path in the file system.
  - `ext2_normalize_path(const char *path)`: Normalizes a path.

### 4. **Device Drivers**

#### Keyboard
- **Functions**:
  - `keyboard_init()`: Initializes the keyboard driver.
  - `keyboard_register_callback(keyboard_callback_t callback)`: Registers a callback for keyboard events.
  - `keyboard_get_key_state(uint8_t scancode)`: Returns the state of a key.
  - `keyboard_get_key_name(uint8_t scancode)`: Returns the name of a key.

#### Mouse
- **Functions**:
  - `mouse_init()`: Initializes the mouse driver.
  - `mouse_register_callback(mouse_callback_t callback)`: Registers a callback for mouse events.
  - `mouse_get_button_state(uint8_t button)`: Returns the state of a mouse button.

#### Timer
- **Functions**:
  - `timer_init(uint32_t frequency)`: Initializes the timer with a specified frequency.
  - `timer_set_frequency(uint32_t frequency)`: Sets the timer frequency.
  - `timer_get_ticks()`: Returns the number of timer ticks since boot.
  - `timer_sleep(uint32_t ms)`: Sleeps for a specified number of milliseconds.
  - `timer_get_uptime_ms()`: Returns the uptime in milliseconds.
  - `timer_register_callback(timer_callback_t callback)`: Registers a callback for timer events.

#### Serial Port
- **Functions**:
  - `serial_init(uint16_t port, uint16_t baud_divisor)`: Initializes a serial port.
  - `serial_is_transmit_ready(uint16_t port)`: Checks if the serial port is ready to transmit.
  - `serial_write_byte(uint16_t port, uint8_t data)`: Writes a byte to the serial port.
  - `serial_write_string(uint16_t port, const char *str)`: Writes a string to the serial port.
  - `serial_write_hex(uint16_t port, uint64_t value, int num_digits)`: Writes a hexadecimal value to the serial port.
  - `serial_is_data_ready(uint16_t port)`: Checks if data is ready to be read from the serial port.
  - `serial_read_byte(uint16_t port)`: Reads a byte from the serial port.

#### PCI
- **Functions**:
  - `pci_init()`: Initializes the PCI subsystem.
  - `pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)`: Reads a 32-bit value from PCI configuration space.
  - `pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)`: Writes a 32-bit value to PCI configuration space.
  - `pci_find_device_by_class(uint8_t class_code, uint8_t subclass, pci_device_t* device)`: Finds a PCI device by class and subclass.
  - `pci_get_bar(const pci_device_t* device, uint8_t bar_index)`: Gets the Base Address Register (BAR) value for a PCI device.

### 5. **Interrupt Handling**

#### Interrupt Descriptor Table (IDT)
- **Functions**:
  - `idt_init()`: Initializes the IDT.
  - `idt_check_integrity()`: Checks the integrity of the IDT.
  - `idt_reload()`: Reloads the IDT.
  - `idt_recover()`: Recovers the IDT from a backup.
  - `idt_save_backup()`: Saves a backup of the IDT.
  - `idt_register_handler(uint8_t vector, interrupt_handler_t handler)`: Registers an interrupt handler.
  - `idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr)`: Sets an IDT gate.
  - `interrupt_enable()`: Enables interrupts.
  - `interrupt_disable()`: Disables interrupts.
  - `interrupt_state()`: Returns the current interrupt state.

#### Programmable Interrupt Controller (PIC)
- **Functions**:
  - `pic_init()`: Initializes the PIC.
  - `pic_send_eoi(uint8_t irq)`: Sends an End-of-Interrupt (EOI) signal to the PIC.
  - `pic_disable()`: Disables the PIC.
  - `pic_mask_irq(uint8_t irq)`: Masks an IRQ.
  - `pic_unmask_irq(uint8_t irq)`: Unmasks an IRQ.
  - `pic_get_irq_mask()`: Returns the current IRQ mask.
  - `pic_set_irq_mask(uint16_t mask)`: Sets the IRQ mask.

### 6. **Logging and Debugging**

#### Logging
- **Functions**:
  - `log_init(log_level_t level)`: Initializes the logging system with a specified log level.
  - `log_printf(log_level_t level, const char *fmt, ...)`: Logs a formatted message.
  - `log_message(log_level_t level, const char *msg)`: Logs a message.
  - **Macros**:
    - `LOG_DEBUG(fmt, ...)`: Logs a debug message.
    - `LOG_INFO(fmt, ...)`: Logs an info message.
    - `LOG_WARN(fmt, ...)`: Logs a warning message.
    - `LOG_ERROR(fmt, ...)`: Logs an error message.
    - `LOG_CRITICAL(fmt, ...)`: Logs a critical message.

#### System Information
- **Functions**:
  - `sysinfo_init()`: Initializes the system information module.
  - `sysinfo_print()`: Prints system information.

### 7. **Bootstrapping**

#### Limine Boot Protocol
- **Functions**:
  - `setup_fb()`: Initializes the framebuffer using the Limine bootloader.
  - `kmain()`: The main kernel entry point, initializes all subsystems and starts the kernel.