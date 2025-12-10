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
void jump_bank_0(uint16_t reg_addr);            /* 0x0300-0x0310 */
void jump_bank_1(uint16_t reg_addr);            /* 0x0311-0x0321 */

/* Named dispatch handlers */
void phy_power_config_handler(void);            /* 0x0206-0x024a */
void handler_bf8e(void);                        /* dispatch to 0xbf8e */
void handler_d916(uint8_t param);               /* dispatch to 0xd916 - param in R7 */
void handler_e6fc(void);                        /* dispatch to 0xe6fc */
void handler_e91d(void);                        /* dispatch to 0xe91d */
void handler_e96c(void);                        /* dispatch to 0xe96c */
void handler_0327_usb_power_init(void);         /* 0x0327-0x032a */
void handler_039a_buffer_dispatch(void);        /* 0x039a-0x039e -> 0xd810 */
void usb_poll_wait(void);                       /* 0x0395-0x0399 */

/* Dispatch stubs (numbered by address) */
/* Each stub is 5 bytes: bank switch + ljmp to target */
void dispatch_0206(void);                       /* 0x0206-0x020a */
void dispatch_0322(void);                       /* 0x0322-0x0326 */
void dispatch_0327(void);                       /* 0x0327-0x032b */
void dispatch_0331(void);                       /* 0x0331-0x0335 */
void dispatch_0336(void);                       /* 0x0336-0x033a */
void dispatch_033b(void);                       /* 0x033b-0x033f */
void dispatch_0345(void);                       /* 0x0345-0x0349 */
void dispatch_034a(void);                       /* 0x034a-0x034e */
void dispatch_034f(void);                       /* 0x034f-0x0353 */
void dispatch_0354(void);                       /* 0x0354-0x0358 */
void dispatch_0359(void);                       /* 0x0359-0x035d */
void dispatch_035e(void);                       /* 0x035e-0x0362 */
void dispatch_0363(void);                       /* 0x0363-0x0367 */
void dispatch_0368(void);                       /* 0x0368-0x036c */
void dispatch_036d(void);                       /* 0x036d-0x0371 */
void dispatch_0372(void);                       /* 0x0372-0x0376 */
void dispatch_0377(void);                       /* 0x0377-0x037b */
void dispatch_037c(void);                       /* 0x037c-0x0380 */
void dispatch_0381(void);                       /* 0x0381-0x0385 */
void dispatch_0386(void);                       /* 0x0386-0x038a */
void dispatch_038b(void);                       /* 0x038b-0x038f */
void dispatch_0390(void);                       /* 0x0390-0x0394 */
void dispatch_0395(void);                       /* 0x0395-0x0399 */
void dispatch_039a(void);                       /* 0x039a-0x039e */
void dispatch_03a4(void);                       /* 0x03a4-0x03a8 */
void dispatch_03a9(void);                       /* 0x03a9-0x03ad */
void dispatch_03ae(void);                       /* 0x03ae-0x03b2 */
void dispatch_03b3(void);                       /* 0x03b3-0x03b7 */
void dispatch_03b8(void);                       /* 0x03b8-0x03bc */
void dispatch_03bd(void);                       /* 0x03bd-0x03c1 */
void dispatch_03c2(void);                       /* 0x03c2-0x03c6 */
void dispatch_03c7(void);                       /* 0x03c7-0x03cb */
void dispatch_03cc(void);                       /* 0x03cc-0x03d0 */
void dispatch_03d1(void);                       /* 0x03d1-0x03d5 */
void dispatch_03d6(void);                       /* 0x03d6-0x03da */
void dispatch_03db(void);                       /* 0x03db-0x03df */
void dispatch_03e0(void);                       /* 0x03e0-0x03e4 */
void dispatch_03e5(void);                       /* 0x03e5-0x03e9 */
void dispatch_03ea(void);                       /* 0x03ea-0x03ee */
void dispatch_03ef(void);                       /* 0x03ef-0x03f3 */
void dispatch_03f4(void);                       /* 0x03f4-0x03f8 */
void dispatch_03f9(void);                       /* 0x03f9-0x03fd */
void dispatch_03fe(void);                       /* 0x03fe-0x0402 */
void dispatch_0403(void);                       /* 0x0403-0x0407 */
void dispatch_0408(void);                       /* 0x0408-0x040c */
void dispatch_040d(void);                       /* 0x040d-0x0411 */
void dispatch_0412(uint8_t param);              /* 0x0412-0x0416 */
void dispatch_0417(void);                       /* 0x0417-0x041b */
void dispatch_041c(uint8_t param);              /* 0x041c-0x0420 */
void dispatch_0421(uint8_t param);              /* 0x0421-0x0425 */
void dispatch_0426(void);                       /* 0x0426-0x042a */
void dispatch_042b(void);                       /* 0x042b-0x042f */
void dispatch_0430(void);                       /* 0x0430-0x0434 */
void dispatch_0435(void);                       /* 0x0435-0x0439 */
void dispatch_043a(void);                       /* 0x043a-0x043e */
void dispatch_043f(void);                       /* 0x043f-0x0443 */
void dispatch_0444(void);                       /* 0x0444-0x0448 */
void dispatch_0449(void);                       /* 0x0449-0x044d */
void dispatch_044e(void);                       /* 0x044e-0x0452 */
void dispatch_0453(void);                       /* 0x0453-0x0457 */
void dispatch_0458(void);                       /* 0x0458-0x045c */
void dispatch_045d(void);                       /* 0x045d-0x0461 */
void dispatch_0462(void);                       /* 0x0462-0x0466 */
void dispatch_0467(void);                       /* 0x0467-0x046b */
void dispatch_046c(void);                       /* 0x046c-0x0470 */
void dispatch_0471(void);                       /* 0x0471-0x0475 */
void dispatch_0476(void);                       /* 0x0476-0x047a */
void dispatch_047b(void);                       /* 0x047b-0x047f */
void dispatch_0480(void);                       /* 0x0480-0x0484 */
void dispatch_0485(void);                       /* 0x0485-0x0489 */
void dispatch_048a(void);                       /* 0x048a-0x048e */
void dispatch_048f(void);                       /* 0x048f-0x0493 */
void dispatch_0494(void);                       /* 0x0494-0x0498 */
void dispatch_0499(void);                       /* 0x0499-0x049d */
void dispatch_049e(void);                       /* 0x049e-0x04a2 */
void dispatch_04a3(void);                       /* 0x04a3-0x04a7 */
void dispatch_04a8(void);                       /* 0x04a8-0x04ac */
void dispatch_04ad(void);                       /* 0x04ad-0x04b1 */
void dispatch_04b2(void);                       /* 0x04b2-0x04b6 */
void dispatch_04b7(void);                       /* 0x04b7-0x04bb */
void dispatch_04bc(void);                       /* 0x04bc-0x04c0 */
void dispatch_04c1(void);                       /* 0x04c1-0x04c5 */
void dispatch_04c6(void);                       /* 0x04c6-0x04ca */
void dispatch_04cb(void);                       /* 0x04cb-0x04cf */
void dispatch_04d0(void);                       /* 0x04d0-0x04d4 -> 0xce79 */
void dispatch_04d5(void);                       /* 0x04d5-0x04d9 */
void dispatch_04da(void);                       /* 0x04da-0x04de */
void dispatch_04df(void);                       /* 0x04df-0x04e3 */
void dispatch_04e4(void);                       /* 0x04e4-0x04e8 */
void dispatch_04e9(void);                       /* 0x04e9-0x04ed */
void dispatch_04ee(void);                       /* 0x04ee-0x04f2 */
void dispatch_04f3(void);                       /* 0x04f3-0x04f7 */
void dispatch_04f8(void);                       /* 0x04f8-0x04fc */
void dispatch_04fd(void);                       /* 0x04fd-0x0501 */
void dispatch_0502(void);                       /* 0x0502-0x0506 */
void dispatch_0507(void);                       /* 0x0507-0x050b */
void dispatch_050c(void);                       /* 0x050c-0x0510 */
void dispatch_0511(void);                       /* 0x0511-0x0515 */
void dispatch_0516(void);                       /* 0x0516-0x051a */
void dispatch_051b(void);                       /* 0x051b-0x051f */
void dispatch_0520(void);                       /* 0x0520-0x0524 -> 0xb4ba */
void dispatch_0525(void);                       /* 0x0525-0x0529 */
void dispatch_052a(void);                       /* 0x052a-0x052e */
void dispatch_052f(void);                       /* 0x052f-0x0533 */
void dispatch_0534(void);                       /* 0x0534-0x0538 */
void dispatch_0539(void);                       /* 0x0539-0x053d */
void dispatch_053e(void);                       /* 0x053e-0x0542 */
void dispatch_0543(void);                       /* 0x0543-0x0547 */
void dispatch_0548(void);                       /* 0x0548-0x054c */
void dispatch_054d(void);                       /* 0x054d-0x0551 */
void dispatch_0552(void);                       /* 0x0552-0x0556 */
void dispatch_0557(void);                       /* 0x0557-0x055b */
void dispatch_055c(void);                       /* 0x055c-0x0560 */
void dispatch_0561(void);                       /* 0x0561-0x0565 */
void dispatch_0566(void);                       /* 0x0566-0x056a */
void dispatch_056b(void);                       /* 0x056b-0x056f */
void dispatch_0570(void);                       /* 0x0570-0x0574 */
void dispatch_0575(void);                       /* 0x0575-0x0579 */
void dispatch_057a(void);                       /* 0x057a-0x057e */
void dispatch_057f(void);                       /* 0x057f-0x0583 */
void dispatch_0584(void);                       /* 0x0584-0x0588 */
void dispatch_0589(void);                       /* 0x0589-0x058d -> 0xd894 */
void dispatch_058e(void);                       /* 0x058e-0x0592 */
void dispatch_0593(void);                       /* 0x0593-0x0597 */
void dispatch_0598(void);                       /* 0x0598-0x059c */
void dispatch_059d(void);                       /* 0x059d-0x05a1 */
void dispatch_05a2(void);                       /* 0x05a2-0x05a6 */
void dispatch_05a7(void);                       /* 0x05a7-0x05ab */
void dispatch_05ac(void);                       /* 0x05ac-0x05b0 */
void dispatch_05b1(void);                       /* 0x05b1-0x05b5 */
void dispatch_05b6(void);                       /* 0x05b6-0x05ba */
void dispatch_05bb(void);                       /* 0x05bb-0x05bf */
void dispatch_05c0(void);                       /* 0x05c0-0x05c4 */
void dispatch_05c5(void);                       /* 0x05c5-0x05c9 */
void dispatch_05ca(void);                       /* 0x05ca-0x05ce */
void dispatch_05cf(void);                       /* 0x05cf-0x05d3 */
void dispatch_05d4(void);                       /* 0x05d4-0x05d8 */
void dispatch_05d9(void);                       /* 0x05d9-0x05dd */
void dispatch_05de(void);                       /* 0x05de-0x05e2 */
void dispatch_05e3(void);                       /* 0x05e3-0x05e7 */
void dispatch_05e8(void);                       /* 0x05e8-0x05ec */
void dispatch_05ed(void);                       /* 0x05ed-0x05f1 */
void dispatch_05f2(void);                       /* 0x05f2-0x05f6 */
void dispatch_05f7(void);                       /* 0x05f7-0x05fb */
void dispatch_05fc(void);                       /* 0x05fc-0x0600 */
void dispatch_0601(void);                       /* 0x0601-0x0605 */
void dispatch_0606(void);                       /* 0x0606-0x060a */
void dispatch_060b(void);                       /* 0x060b-0x060f */
void dispatch_0610(void);                       /* 0x0610-0x0614 */
void dispatch_0615(void);                       /* 0x0615-0x0619 */
void dispatch_061a(void);                       /* 0x061a-0x061e */
void dispatch_061f(void);                       /* 0x061f-0x0623 */
void dispatch_0624(void);                       /* 0x0624-0x0628 */

#endif /* _DISPATCH_H_ */
