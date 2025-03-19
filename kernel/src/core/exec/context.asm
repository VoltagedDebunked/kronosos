section .text
global task_switch_context
global task_restore_context

task_switch_context:
    ; Save the current task's context
    mov [rdi + 0], rax    ; Save RAX
    mov [rdi + 8], rbx    ; Save RBX
    mov [rdi + 16], rcx   ; Save RCX
    mov [rdi + 24], rdx   ; Save RDX
    mov [rdi + 32], rsi   ; Save RSI
    mov [rdi + 40], rdi   ; Save RDI
    mov [rdi + 48], rbp   ; Save RBP
    mov [rdi + 56], rsp   ; Save RSP
    mov [rdi + 64], r8    ; Save R8
    mov [rdi + 72], r9    ; Save R9
    mov [rdi + 80], r10   ; Save R10
    mov [rdi + 88], r11   ; Save R11
    mov [rdi + 96], r12   ; Save R12
    mov [rdi + 104], r13  ; Save R13
    mov [rdi + 112], r14  ; Save R14
    mov [rdi + 120], r15  ; Save R15

    ; Save the flags register (RFLAGS)
    pushfq
    pop qword [rdi + 128]

    ; Restore the next task's context
    mov rax, [rsi + 0]    ; Restore RAX
    mov rbx, [rsi + 8]    ; Restore RBX
    mov rcx, [rsi + 16]   ; Restore RCX
    mov rdx, [rsi + 24]   ; Restore RDX
    mov rsi, [rsi + 32]   ; Restore RSI
    mov rdi, [rsi + 40]   ; Restore RDI
    mov rbp, [rsi + 48]   ; Restore RBP
    mov rsp, [rsi + 56]   ; Restore RSP
    mov r8, [rsi + 64]    ; Restore R8
    mov r9, [rsi + 72]    ; Restore R9
    mov r10, [rsi + 80]   ; Restore R10
    mov r11, [rsi + 88]   ; Restore R11
    mov r12, [rsi + 96]   ; Restore R12
    mov r13, [rsi + 104]  ; Restore R13
    mov r14, [rsi + 112]  ; Restore R14
    mov r15, [rsi + 120]  ; Restore R15

    ; Restore the flags register (RFLAGS)
    push qword [rsi + 128]
    popfq

    ; Jump to the next task's instruction pointer (RIP)
    ret

task_restore_context:
    ; Restore the task's context
    mov rax, [rdi + 0]    ; Restore RAX
    mov rbx, [rdi + 8]    ; Restore RBX
    mov rcx, [rdi + 16]   ; Restore RCX
    mov rdx, [rdi + 24]   ; Restore RDX
    mov rsi, [rdi + 32]   ; Restore RSI
    mov rdi, [rdi + 40]   ; Restore RDI
    mov rbp, [rdi + 48]   ; Restore RBP
    mov rsp, [rdi + 56]   ; Restore RSP
    mov r8, [rdi + 64]    ; Restore R8
    mov r9, [rdi + 72]    ; Restore R9
    mov r10, [rdi + 80]   ; Restore R10
    mov r11, [rdi + 88]   ; Restore R11
    mov r12, [rdi + 96]   ; Restore R12
    mov r13, [rdi + 104]  ; Restore R13
    mov r14, [rdi + 112]  ; Restore R14
    mov r15, [rdi + 120]  ; Restore R15

    ; Restore the flags register (RFLAGS)
    push qword [rdi + 128]
    popfq

    ; Jump to the task's instruction pointer (RIP)
    ret