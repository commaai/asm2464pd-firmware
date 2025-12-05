#ifndef __SFR_H__
#define __SFR_H__

/*
 * ASM2464PD Firmware - 8051 Special Function Registers
 *
 * Standard 8051 SFR definitions plus ASM2464PD-specific registers
 * The ASM2464PD uses an 8051-compatible core with extended features
 */

#include "types.h"

/*===========================================================================
 * Standard 8051 SFRs
 *===========================================================================*/

/* Accumulator and B register */
__sfr __at(0x80) P0;        /* Port 0 */
__sfr __at(0x81) SP;        /* Stack Pointer */
__sfr __at(0x82) DPL;       /* Data Pointer Low */
__sfr __at(0x83) DPH;       /* Data Pointer High */
__sfr __at(0x87) PCON;      /* Power Control */
__sfr __at(0x88) TCON;      /* Timer Control */
__sfr __at(0x89) TMOD;      /* Timer Mode */
__sfr __at(0x8A) TL0;       /* Timer 0 Low */
__sfr __at(0x8B) TL1;       /* Timer 1 Low */
__sfr __at(0x8C) TH0;       /* Timer 0 High */
__sfr __at(0x8D) TH1;       /* Timer 1 High */
__sfr __at(0x90) P1;        /* Port 1 */
__sfr __at(0x98) SCON;      /* Serial Control */
__sfr __at(0x99) SBUF;      /* Serial Buffer */
__sfr __at(0xA0) P2;        /* Port 2 */
__sfr __at(0xA8) IE;        /* Interrupt Enable */
__sfr __at(0xB0) P3;        /* Port 3 */
__sfr __at(0xB8) IP;        /* Interrupt Priority */
__sfr __at(0xD0) PSW;       /* Program Status Word */
__sfr __at(0xE0) ACC;       /* Accumulator */
__sfr __at(0xF0) B;         /* B Register */

/*===========================================================================
 * PSW Bits
 *===========================================================================*/
__sbit __at(0xD0) P;        /* Parity flag */
__sbit __at(0xD1) F1;       /* User flag 1 */
__sbit __at(0xD2) OV;       /* Overflow flag */
__sbit __at(0xD3) RS0;      /* Register bank select bit 0 */
__sbit __at(0xD4) RS1;      /* Register bank select bit 1 */
__sbit __at(0xD5) F0;       /* User flag 0 */
__sbit __at(0xD6) AC;       /* Auxiliary carry */
__sbit __at(0xD7) CY;       /* Carry flag */

/*===========================================================================
 * IE Bits (Interrupt Enable)
 *===========================================================================*/
__sbit __at(0xA8) EX0;      /* External interrupt 0 enable */
__sbit __at(0xA9) ET0;      /* Timer 0 interrupt enable */
__sbit __at(0xAA) EX1;      /* External interrupt 1 enable */
__sbit __at(0xAB) ET1;      /* Timer 1 interrupt enable */
__sbit __at(0xAC) ES;       /* Serial interrupt enable */
__sbit __at(0xAF) EA;       /* Global interrupt enable */

/*===========================================================================
 * TCON Bits
 *===========================================================================*/
__sbit __at(0x88) IT0;      /* Interrupt 0 type */
__sbit __at(0x89) IE0;      /* External interrupt 0 flag */
__sbit __at(0x8A) IT1;      /* Interrupt 1 type */
__sbit __at(0x8B) IE1;      /* External interrupt 1 flag */
__sbit __at(0x8C) TR0;      /* Timer 0 run */
__sbit __at(0x8D) TF0;      /* Timer 0 overflow flag */
__sbit __at(0x8E) TR1;      /* Timer 1 run */
__sbit __at(0x8F) TF1;      /* Timer 1 overflow flag */

/*===========================================================================
 * ASM2464PD Extended SFRs
 * These are specific to the ASM2464PD chip
 *===========================================================================*/

/*
 * DPX - Data Pointer Extended / Code Bank Select Register
 *
 * The ASM2464PD has ~98KB of firmware but the 8051 can only address 64KB.
 * The DPX register implements code banking to access all firmware.
 *
 * Memory Map:
 *   0x0000-0x7FFF: Always visible (32KB shared - vectors, dispatch routines)
 *   0x8000-0xFFFF with DPX=0: Bank 0 upper (file offset 0x08000-0x0FFFF)
 *   0x8000-0xFFFF with DPX=1: Bank 1 upper (file offset 0x10000-0x17F0C)
 *
 * The dispatch functions at 0x0300 and 0x0311 use this for banking:
 *   - jump_bank_0 (0x0300): Sets DPX=0 (bank 0)
 *   - jump_bank_1 (0x0311): Sets DPX=1 (bank 1)
 *
 * To calculate file offset for bank 1 addresses:
 *   file_offset = 0x10000 + (addr - 0x8000) = addr + 0x8000
 *
 * Example: Address 0xE911 in bank 1 -> file offset 0x16911
 */
__sfr __at(0x96) DPX;       /* Data pointer extended / Code bank select */

/*===========================================================================
 * Interrupt Vector Numbers
 *===========================================================================*/
#define INT_EXT0            0   /* External Interrupt 0 */
#define INT_TIMER0          1   /* Timer 0 Overflow */
#define INT_EXT1            2   /* External Interrupt 1 */
#define INT_TIMER1          3   /* Timer 1 Overflow */
#define INT_SERIAL          4   /* Serial Port */

/* Extended interrupts (ASM2464PD specific) */
#define INT_VEC_USB         5   /* USB interrupt vector */
#define INT_VEC_NVME        6   /* NVMe interrupt vector */
#define INT_VEC_DMA         7   /* DMA interrupt vector */

/*===========================================================================
 * Helper Macros
 *===========================================================================*/

/* Enable/disable all interrupts */
#define ENABLE_INTERRUPTS()     (EA = 1)
#define DISABLE_INTERRUPTS()    (EA = 0)

/* Set register bank (0-3) */
#define SET_REGBANK(n)          (PSW = (PSW & 0xE7) | ((n) << 3))

#endif /* __SFR_H__ */
