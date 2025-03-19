; GDT loading functions for KronosOS
global gdt_load
global tss_load

section .text
bits 64

; Function to load the GDT
; void gdt_load(struct gdt_ptr* gdt_ptr);
gdt_load:
    ; Load the GDT
    lgdt [rdi]
    
    ; Reload the segment registers
    ; We need to do this because changes to the GDT don't take effect immediately
    ; for most segment registers
    
    ; Reload CS register by doing a far jump
    ; 0x08 is the offset of the kernel code segment in the GDT
    push 0x08                ; Push the segment selector (kernel code segment)
    lea rax, [rel .reload_cs] ; Get address of the next instruction
    push rax                 ; Push the return address
    retfq                    ; Far return to reload CS
    
.reload_cs:
    ; Now reload the data segment registers
    ; 0x10 is the offset of the kernel data segment in the GDT
    mov ax, 0x10            ; Kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ret

; Function to load the TSS
; void tss_load(uint16_t tss_segment);
tss_load:
    ; Load the TSS into the TR register
    ; rdi contains the TSS segment selector
    ltr di
    ret