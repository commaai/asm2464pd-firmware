/*
 * dispatch.h - Bank Switching and Code Dispatch
 *
 * The dispatch subsystem handles code bank switching for the 8051's
 * limited 64KB address space. The ASM2464PD firmware exceeds 64KB,
 * requiring runtime bank switching to access code in different banks.
 *
 * 8051 ADDRESS SPACE LAYOUT:
 *   0x0000-0x7FFF: Common code (always visible)
 *   0x8000-0xFFFF: Banked region (switched between Bank 0/1)
 *
 * BANK MAPPING:
 *   Bank 0: Physical 0x08000-0x0FFFF → Logical 0x8000-0xFFFF
 *   Bank 1: Physical 0x10000-0x17FFF → Logical 0x8000-0xFFFF
 *
 * DISPATCH MECHANISM:
 *   Functions in the banked region cannot be called directly from
 *   code in a different bank. Instead, dispatch stubs in the common
 *   region (0x0000-0x7FFF) handle bank switching:
 *
 *   1. Caller invokes dispatch_XXXX() in common code
 *   2. Dispatch stub switches to target bank
 *   3. Stub jumps to actual function in banked region
 *   4. Function returns through stub, restoring original bank
 *
 * DISPATCH STUB FORMAT:
 *   Each stub is 5 bytes at fixed addresses (0x0206, 0x0322, etc.)
 *   containing bank switch + jump instructions.
 *
 * BANK JUMP FUNCTIONS:
 *   jump_bank_0(addr): Switch to Bank 0, jump to addr
 *   jump_bank_1(addr): Switch to Bank 1, jump to addr
 *
 * USAGE:
 *   To call function at 0x10XXX (Bank 1, logical 0x8XXX):
 *   - Use corresponding dispatch_XXXX() stub
 *   - Or use jump_bank_1(0x8XXX) directly
 *
 * NOTE: The numbered dispatch_XXXX functions are stubs whose
 * target functions have not yet been fully reverse-engineered.
 */
#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "../types.h"

/* Bank jump functions */
void jump_bank_0(uint16_t reg_addr);
void jump_bank_1(uint16_t reg_addr);

/* Named dispatch handlers */
void phy_power_config_handler(void);
void handler_bf8e(void);
void handler_d916(void);
void handler_e6fc(void);
void handler_e91d(void);
void handler_e96c(void);
void handler_0327_usb_power_init(void);
void handler_039a_buffer_dispatch(void);
void handler_0395(void);

/* Dispatch stubs (numbered) */
void dispatch_0206(void);
void dispatch_0322(void);
void dispatch_0327(void);
void dispatch_0331(void);
void dispatch_0336(void);
void dispatch_033b(void);
void dispatch_0345(void);
void dispatch_034a(void);
void dispatch_034f(void);
void dispatch_0354(void);
void dispatch_0359(void);
void dispatch_035e(void);
void dispatch_0363(void);
void dispatch_0368(void);
void dispatch_036d(void);
void dispatch_0372(void);
void dispatch_0377(void);
void dispatch_037c(void);
void dispatch_0381(void);
void dispatch_0386(void);
void dispatch_038b(void);
void dispatch_0390(void);
void dispatch_0395(void);
void dispatch_039a(void);
void dispatch_03a4(void);
void dispatch_03a9(void);
void dispatch_03ae(void);
void dispatch_03b3(void);
void dispatch_03b8(void);
void dispatch_03bd(void);
void dispatch_03c2(void);
void dispatch_03c7(void);
void dispatch_03cc(void);
void dispatch_03d1(void);
void dispatch_03d6(void);
void dispatch_03db(void);
void dispatch_03e0(void);
void dispatch_03e5(void);
void dispatch_03ea(void);
void dispatch_03ef(void);
void dispatch_03f4(void);
void dispatch_03f9(void);
void dispatch_03fe(void);
void dispatch_0403(void);
void dispatch_0408(void);
void dispatch_040d(void);
void dispatch_0412(uint8_t param);
void dispatch_0417(void);
void dispatch_041c(uint8_t param);
void dispatch_0421(uint8_t param);
void dispatch_0426(void);
void dispatch_042b(void);
void dispatch_0430(void);
void dispatch_0435(void);
void dispatch_043a(void);
void dispatch_043f(void);
void dispatch_0444(void);
void dispatch_0449(void);
void dispatch_044e(void);
void dispatch_0453(void);
void dispatch_0458(void);
void dispatch_045d(void);
void dispatch_0462(void);
void dispatch_0467(void);
void dispatch_046c(void);
void dispatch_0471(void);
void dispatch_0476(void);
void dispatch_047b(void);
void dispatch_0480(void);
void dispatch_0485(void);
void dispatch_048a(void);
void dispatch_048f(void);
void dispatch_0494(void);
void dispatch_0499(void);
void dispatch_049e(void);
void dispatch_04a3(void);
void dispatch_04a8(void);
void dispatch_04ad(void);
void dispatch_04b2(void);
void dispatch_04b7(void);
void dispatch_04bc(void);
void dispatch_04c1(void);
void dispatch_04c6(void);
void dispatch_04cb(void);
void dispatch_04d0(void);
void dispatch_04d5(void);
void dispatch_04da(void);
void dispatch_04df(void);
void dispatch_04e4(void);
void dispatch_04e9(void);
void dispatch_04ee(void);
void dispatch_04f3(void);
void dispatch_04f8(void);
void dispatch_04fd(void);
void dispatch_0502(void);
void dispatch_0507(void);
void dispatch_050c(void);
void dispatch_0511(void);
void dispatch_0516(void);
void dispatch_051b(void);
void dispatch_0520(void);
void dispatch_0525(void);
void dispatch_052a(void);
void dispatch_052f(void);
void dispatch_0534(void);
void dispatch_0539(void);
void dispatch_053e(void);
void dispatch_0543(void);
void dispatch_0548(void);
void dispatch_054d(void);
void dispatch_0552(void);
void dispatch_0557(void);
void dispatch_055c(void);
void dispatch_0561(void);
void dispatch_0566(void);
void dispatch_056b(void);
void dispatch_0570(void);
void dispatch_0575(void);
void dispatch_057a(void);
void dispatch_057f(void);
void dispatch_0584(void);
void dispatch_0589(void);
void dispatch_058e(void);
void dispatch_0593(void);
void dispatch_0598(void);
void dispatch_059d(void);
void dispatch_05a2(void);
void dispatch_05a7(void);
void dispatch_05ac(void);
void dispatch_05b1(void);
void dispatch_05b6(void);
void dispatch_05bb(void);
void dispatch_05c0(void);
void dispatch_05c5(void);
void dispatch_05ca(void);
void dispatch_05cf(void);
void dispatch_05d4(void);
void dispatch_05d9(void);
void dispatch_05de(void);
void dispatch_05e3(void);
void dispatch_05e8(void);
void dispatch_05ed(void);
void dispatch_05f2(void);
void dispatch_05f7(void);
void dispatch_05fc(void);
void dispatch_0601(void);
void dispatch_0606(void);
void dispatch_060b(void);
void dispatch_0610(void);
void dispatch_0615(void);
void dispatch_061a(void);
void dispatch_061f(void);
void dispatch_0624(void);

#endif /* _DISPATCH_H_ */
