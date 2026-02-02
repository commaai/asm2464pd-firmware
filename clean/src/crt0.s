; crt0.s - Startup with interrupt handlers
    .module crt0
    .globl  _main
    .globl  _timer0_isr_handler
    .globl  _usb_isr_handler
    .globl  _int1_isr_handler

; Interrupt vectors in absolute area
    .area   VECTOR  (ABS,CODE)

; Reset vector (address 0x0000)
    .org    0x0000
__reset:
    ljmp    __sdcc_program_startup

; External interrupt 0 vector (address 0x0003) - USB
    .org    0x0003
    ljmp    __usb_isr

; Timer 0 overflow vector (address 0x000B)
    .org    0x000B
    ljmp    __timer0_isr

; External interrupt 1 vector (address 0x0013) - System
    .org    0x0013
    ljmp    __int1_isr

; Timer 1 (address 0x001B)
    .org    0x001B
    reti

; Serial (address 0x0023)
    .org    0x0023
    reti

; Startup code
    .area   HOME    (CODE)
__sdcc_program_startup:
    mov     sp, #0x72
    mov     0x96, #0x00
    ljmp    _main

; Timer 0 ISR wrapper
__timer0_isr:
    push    0xe0        ; ACC
    push    0xf0        ; B
    push    0x83        ; DPH
    push    0x82        ; DPL
    push    0xd0        ; PSW
    mov     0xd0, #0x00
    push    0x00
    push    0x01
    push    0x02
    push    0x03
    push    0x04
    push    0x05
    push    0x06
    push    0x07
    lcall   _timer0_isr_handler
    pop     0x07
    pop     0x06
    pop     0x05
    pop     0x04
    pop     0x03
    pop     0x02
    pop     0x01
    pop     0x00
    pop     0xd0
    pop     0x82
    pop     0x83
    pop     0xf0
    pop     0xe0
    reti

; USB ISR wrapper (INT0)
__usb_isr:
    push    0xe0
    push    0xf0
    push    0x83
    push    0x82
    push    0xd0
    mov     0xd0, #0x00
    push    0x00
    push    0x01
    push    0x02
    push    0x03
    push    0x04
    push    0x05
    push    0x06
    push    0x07
    lcall   _usb_isr_handler
    pop     0x07
    pop     0x06
    pop     0x05
    pop     0x04
    pop     0x03
    pop     0x02
    pop     0x01
    pop     0x00
    pop     0xd0
    pop     0x82
    pop     0x83
    pop     0xf0
    pop     0xe0
    reti

; INT1 ISR wrapper
__int1_isr:
    push    0xe0
    push    0xf0
    push    0x83
    push    0x82
    push    0xd0
    mov     0xd0, #0x00
    push    0x00
    push    0x01
    push    0x02
    push    0x03
    push    0x04
    push    0x05
    push    0x06
    push    0x07
    lcall   _int1_isr_handler
    pop     0x07
    pop     0x06
    pop     0x05
    pop     0x04
    pop     0x03
    pop     0x02
    pop     0x01
    pop     0x00
    pop     0xd0
    pop     0x82
    pop     0x83
    pop     0xf0
    pop     0xe0
    reti

    .area   GSINIT  (CODE)
    .area   GSFINAL (CODE)
    .area   HOME    (CODE)
