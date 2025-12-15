; crt0.s - Custom startup code for ASM2464PD firmware
;
; This startup code matches the original firmware's entry at 0x436b
; Original firmware entry sequence:
;   436b: mov r0, #0xff       ; Start at top of IDATA
;   436d: clr a               ; Clear accumulator
;   436e: mov @r0, a          ; Clear IDATA[r0]
;   436f: djnz r0, 0x436e     ; Loop until r0=0
;   4371: mov SP, #0x72       ; Set stack pointer
;   4374: lcall 0x030a        ; Init call (sets IDATA[8]=0x0A, DPX=0)
;   4377: ljmp 0x43a3         ; Jump to process_init_table

    .module crt0
    .globl  _main
    .globl  _ext0_isr
    .globl  _ext1_isr
    .globl  _timer1_isr

; Interrupt vectors in absolute area
    .area   VECTOR  (ABS,CODE)

; Reset vector (address 0x0000)
    .org    0x0000
__reset:
    ljmp    __sdcc_program_startup

; External interrupt 0 vector (address 0x0003)
    .org    0x0003
__ext0_vector:
    ljmp    _ext0_isr

; Timer 0 overflow vector (address 0x000B)
    .org    0x000B
__timer0_vector:
    reti                    ; Not used, return immediately

; External interrupt 1 vector (address 0x0013)
    .org    0x0013
__ext1_vector:
    ljmp    _ext1_isr

; Timer 1 overflow vector (address 0x001B)
    .org    0x001B
__timer1_vector:
    ljmp    _timer1_isr

; Serial interrupt vector (address 0x0023)
    .org    0x0023
__serial_vector:
    reti                    ; Not used in main flow

; Startup code in relocatable area
    .area   HOME    (CODE)
__sdcc_program_startup:
    ; Clear all internal RAM (IDATA 0x00-0xFF)
    ; This matches original firmware at 0x436b-0x4370
    mov     r0, #0xff
    clr     a
clear_ram_loop:
    mov     @r0, a
    djnz    r0, clear_ram_loop

    ; Initialize stack pointer to 0x72
    ; This matches original firmware at 0x4371
    mov     sp, #0x72

    ; Initialize IDATA[0x08] = 0x0A and DPX = 0
    ; This matches original firmware's call to 0x030a
    mov     0x08, #0x0a     ; R0 in bank 1 = 0x0A
    mov     0x96, #0x00     ; DPX = 0 (bank 0)

    ; Call main() - which will process init table and enter main loop
    ljmp    _main

    ; main() should never return (infinite loop)

    .area   GSINIT  (CODE)
    .area   GSFINAL (CODE)
    .area   HOME    (CODE)
