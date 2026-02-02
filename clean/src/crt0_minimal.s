; crt0_minimal.s - Absolute minimal startup
; ONLY reset vector, stack setup, jump to main
; No ISR handlers at all

    .module crt0_minimal
    .globl  _main

; Interrupt vectors in absolute area
    .area   VECTOR  (ABS,CODE)

; Reset vector (address 0x0000)
    .org    0x0000
__reset:
    ljmp    __sdcc_program_startup

; All interrupt vectors just reti
    .org    0x0003
    reti
    .org    0x000B
    reti
    .org    0x0013
    reti
    .org    0x001B
    reti
    .org    0x0023
    reti

; Startup code
    .area   HOME    (CODE)
__sdcc_program_startup:
    ; Minimal setup - just stack pointer
    mov     sp, #0x72
    ; DPX = 0 (bank 0)
    mov     0x96, #0x00
    ; Jump to main
    ljmp    _main

    .area   GSINIT  (CODE)
    .area   GSFINAL (CODE)
    .area   HOME    (CODE)
