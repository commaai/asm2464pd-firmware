; crt0.s - Minimal startup code for clean firmware
;
; Simple startup: clear RAM, set stack, jump to main
; USB interrupt handler on INT0 (External Interrupt 0)

    .module crt0
    .globl  _main
    .globl  _usb_isr_handler    ; C function for USB interrupt handling
    .globl  _int1_isr_handler   ; C function for INT1 (system) interrupt handling

; Interrupt vectors in absolute area
    .area   VECTOR  (ABS,CODE)

; Reset vector (address 0x0000)
    .org    0x0000
__reset:
    ljmp    __sdcc_program_startup

; External interrupt 0 vector (address 0x0003) - USB Interrupt
    .org    0x0003
__ext0_vector:
    ljmp    __usb_isr

; Timer 0 overflow vector (address 0x000B)
    .org    0x000B
__timer0_vector:
    reti

; External interrupt 1 vector (address 0x0013) - System Interrupt
    .org    0x0013
__ext1_vector:
    ljmp    __int1_isr

; Timer 1 overflow vector (address 0x001B)
    .org    0x001B
__timer1_vector:
    reti

; Serial interrupt vector (address 0x0023)
    .org    0x0023
__serial_vector:
    reti

; Startup code in relocatable area
    .area   HOME    (CODE)
__sdcc_program_startup:
    ; DON'T clear IDATA - preserve bootloader state!
    ; Original firmware also clears IDATA but something else is different
    
    ; Initialize stack pointer (same as original at 0x4371)
    mov     sp, #0x72

    ; Initialize DPX = 0 (bank 0)
    mov     0x96, #0x00

    ; Jump to main
    ljmp    _main

; USB Interrupt Service Routine
; Matches original firmware ISR structure at 0x0E33
__usb_isr:
    ; Save context (same order as original firmware)
    push    0xe0        ; ACC
    push    0xf0        ; B
    push    0x83        ; DPH
    push    0x82        ; DPL
    push    0xd0        ; PSW
    mov     0xd0, #0x00 ; Select register bank 0
    push    0x00        ; r0
    push    0x01        ; r1
    push    0x02        ; r2
    push    0x03        ; r3
    push    0x04        ; r4
    push    0x05        ; r5
    push    0x06        ; r6
    push    0x07        ; r7

    ; Call C handler
    lcall   _usb_isr_handler

    ; Restore context
    pop     0x07        ; r7
    pop     0x06        ; r6
    pop     0x05        ; r5
    pop     0x04        ; r4
    pop     0x03        ; r3
    pop     0x02        ; r2
    pop     0x01        ; r1
    pop     0x00        ; r0
    pop     0xd0        ; PSW
    pop     0x82        ; DPL
    pop     0x83        ; DPH
    pop     0xf0        ; B
    pop     0xe0        ; ACC

    reti

; INT1 (System) Interrupt Service Routine
; Matches original firmware ISR structure at 0x44D7
__int1_isr:
    ; Save context (same order as original firmware)
    push    0xe0        ; ACC
    push    0xf0        ; B
    push    0x83        ; DPH
    push    0x82        ; DPL
    push    0xd0        ; PSW
    mov     0xd0, #0x00 ; Select register bank 0
    push    0x00        ; r0
    push    0x01        ; r1
    push    0x02        ; r2
    push    0x03        ; r3
    push    0x04        ; r4
    push    0x05        ; r5
    push    0x06        ; r6
    push    0x07        ; r7

    ; Call C handler
    lcall   _int1_isr_handler

    ; Restore context
    pop     0x07        ; r7
    pop     0x06        ; r6
    pop     0x05        ; r5
    pop     0x04        ; r4
    pop     0x03        ; r3
    pop     0x02        ; r2
    pop     0x01        ; r1
    pop     0x00        ; r0
    pop     0xd0        ; PSW
    pop     0x82        ; DPL
    pop     0x83        ; DPH
    pop     0xf0        ; B
    pop     0xe0        ; ACC

    reti

    .area   GSINIT  (CODE)
    .area   GSFINAL (CODE)
    .area   HOME    (CODE)
