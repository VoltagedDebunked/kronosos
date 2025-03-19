; IDT functions for KronosOS
global idt_load

; ISR handlers
%macro ISR_NO_ERR_CODE 1
global isr%1
isr%1:
    push 0                  ; Push dummy error code
    push %1                 ; Push the interrupt number
    jmp common_interrupt_handler
%endmacro

%macro ISR_ERR_CODE 1
global isr%1
isr%1:
    push %1                 ; Push the interrupt number
    jmp common_interrupt_handler
%endmacro

; IRQ handlers
%macro IRQ 2
global irq%1
irq%1:
    push 0                  ; Push dummy error code
    push %2                 ; Push the interrupt number
    jmp common_interrupt_handler
%endmacro

section .text
bits 64

; Function to load the IDT
; void idt_load(struct idt_ptr* idt_ptr);
idt_load:
    lidt [rdi]              ; Load IDT using the pointer in RDI
    ret

; Define ISRs for exceptions 0-31
; CPU exceptions 8, 10-14 push an error code, the rest don't
ISR_NO_ERR_CODE 0
ISR_NO_ERR_CODE 1
ISR_NO_ERR_CODE 2
ISR_NO_ERR_CODE 3
ISR_NO_ERR_CODE 4
ISR_NO_ERR_CODE 5
ISR_NO_ERR_CODE 6
ISR_NO_ERR_CODE 7
ISR_ERR_CODE    8
ISR_NO_ERR_CODE 9
ISR_ERR_CODE    10
ISR_ERR_CODE    11
ISR_ERR_CODE    12
ISR_ERR_CODE    13
ISR_ERR_CODE    14
ISR_NO_ERR_CODE 15
ISR_NO_ERR_CODE 16
ISR_ERR_CODE    17
ISR_NO_ERR_CODE 18
ISR_NO_ERR_CODE 19
ISR_NO_ERR_CODE 20
ISR_NO_ERR_CODE 21
ISR_NO_ERR_CODE 22
ISR_NO_ERR_CODE 23
ISR_NO_ERR_CODE 24
ISR_NO_ERR_CODE 25
ISR_NO_ERR_CODE 26
ISR_NO_ERR_CODE 27
ISR_NO_ERR_CODE 28
ISR_NO_ERR_CODE 29
ISR_NO_ERR_CODE 30
ISR_NO_ERR_CODE 31

; Define IRQs 0-15 (mapped to ISRs 32-47)
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; The common interrupt handler
common_interrupt_handler:
    ; Push all general purpose registers to create our interrupt frame
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass pointer to the stack frame to our C handler
    mov rdi, rsp
    
    ; Align the stack to 16 bytes before the function call
    ; (System V AMD64 ABI requirement)
    mov rbp, rsp
    and rsp, -16  ; Align to 16 bytes
    
    ; Call the C function
    extern interrupt_handler
    call interrupt_handler
    
    ; Restore stack pointer
    mov rsp, rbp
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up the error code and interrupt number
    add rsp, 16
    
    ; Return from interrupt
    iretq