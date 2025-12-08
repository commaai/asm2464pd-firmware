/*
 * ASM2464PD Firmware - Dispatch Table Functions
 *
 * This file contains all the dispatch stub functions that route
 * calls to the appropriate handlers via bank switching.
 *
 * The dispatch functions follow a simple pattern:
 *   1. Load target address into DPTR (mov dptr, #ADDR)
 *   2. Jump to bank switch handler:
 *      - ajmp 0x0300 (jump_bank_0) for Bank 0 targets
 *      - ajmp 0x0311 (jump_bank_1) for Bank 1 targets
 *
 * Each dispatch stub is exactly 5 bytes:
 *   90 HH LL  - mov dptr, #ADDR
 *   61 00/11  - ajmp 0x0300 or 0x0311
 *
 * ============================================================================
 * DISPATCH TABLE LAYOUT (0x0322-0x0650)
 * ============================================================================
 *
 * 0x0322-0x03A7: Bank 0 dispatch stubs (ajmp 0x0300)
 * 0x03A9-0x0411: Bank 1 dispatch stubs (ajmp 0x0311)
 * 0x0412-0x04DE: Mixed bank dispatch stubs
 * 0x04DF-0x0650: Event/interrupt dispatch stubs
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"

/* External bank switch functions from main.c */
extern void jump_bank_0(uint16_t addr);
extern void jump_bank_1(uint16_t addr);

/*===========================================================================
 * Bank 0 Dispatch Functions (0x0322-0x03A7)
 * These all jump to 0x0300 (jump_bank_0)
 *===========================================================================*/

// 0x0322-0x0326: Dispatch to 0xCA0D (system state handler)
void dispatch_0322(void) { jump_bank_0(0xCA0D); }

// 0x0327-0x032B: Dispatch to 0xB1CB (usb_power_init)
void dispatch_0327(void) { jump_bank_0(0xB1CB); }

// 0x032C-0x0330: Dispatch to 0x92C5
void dispatch_032c(void) { jump_bank_0(0x92C5); }

// 0x0331-0x0335: Dispatch to 0xC4B3
void dispatch_0331(void) { jump_bank_0(0xC4B3); }

// 0x0336-0x033A: Dispatch to 0xBF0F
void dispatch_0336(void) { jump_bank_0(0xBF0F); }

// 0x033B-0x033F: Dispatch to 0xCF7F
void dispatch_033b(void) { jump_bank_0(0xCF7F); }

// 0x0340-0x0344: Dispatch to 0xBF8E (handler_bf8e)
void dispatch_0340(void) { jump_bank_0(0xBF8E); }

// 0x0345-0x0349: Dispatch to 0x9C2B
void dispatch_0345(void) { jump_bank_0(0x9C2B); }

// 0x034A-0x034E: Dispatch to 0xC66A
void dispatch_034a(void) { jump_bank_0(0xC66A); }

// 0x034F-0x0353: Dispatch to 0xE94D
void dispatch_034f(void) { jump_bank_0(0xE94D); }

// 0x0354-0x0358: Dispatch to 0xE925
void dispatch_0354(void) { jump_bank_0(0xE925); }

// 0x0359-0x035D: Dispatch to 0xDEE3
void dispatch_0359(void) { jump_bank_0(0xDEE3); }

// 0x035E-0x0362: Dispatch to 0xE6BD
void dispatch_035e(void) { jump_bank_0(0xE6BD); }

// 0x0363-0x0367: Dispatch to 0xE969
void dispatch_0363(void) { jump_bank_0(0xE969); }

// 0x0368-0x036C: Dispatch to 0xDF15
void dispatch_0368(void) { jump_bank_0(0xDF15); }

// 0x036D-0x0371: Dispatch to 0xE96F
void dispatch_036d(void) { jump_bank_0(0xE96F); }

// 0x0372-0x0376: Dispatch to 0xE970
void dispatch_0372(void) { jump_bank_0(0xE970); }

// 0x0377-0x037B: Dispatch to 0xE952
void dispatch_0377(void) { jump_bank_0(0xE952); }

// 0x037C-0x0380: Dispatch to 0xE941
void dispatch_037c(void) { jump_bank_0(0xE941); }

// 0x0381-0x0385: Dispatch to 0xE947
void dispatch_0381(void) { jump_bank_0(0xE947); }

// 0x0386-0x038A: Dispatch to 0xE92C
void dispatch_0386(void) { jump_bank_0(0xE92C); }

// 0x038B-0x038F: Dispatch to 0xD2BD
void dispatch_038b(void) { jump_bank_0(0xD2BD); }

// 0x0390-0x0394: Dispatch to 0xCD10
void dispatch_0390(void) { jump_bank_0(0xCD10); }

// 0x0395-0x0399: Dispatch to 0xDA8F
void dispatch_0395(void) { jump_bank_0(0xDA8F); }

// 0x039A-0x039E: Dispatch to 0xD810 (protocol handler)
void dispatch_039a(void) { jump_bank_0(0xD810); }

// 0x039F-0x03A3: Dispatch to 0xD916 (d916 handler)
void dispatch_039f(void) { jump_bank_0(0xD916); }

// 0x03A4-0x03A8: Dispatch to 0xCB37
void dispatch_03a4(void) { jump_bank_0(0xCB37); }

/*===========================================================================
 * Bank 1 Dispatch Functions (0x03A9-0x0411)
 * These all jump to 0x0311 (jump_bank_1)
 *===========================================================================*/

// 0x03A9-0x03AD: Dispatch to Bank1 0x89DB
void dispatch_03a9(void) { jump_bank_1(0x89DB); }

// 0x03AE-0x03B2: Dispatch to Bank1 0xEF3E
void dispatch_03ae(void) { jump_bank_1(0xEF3E); }

// 0x03B3-0x03B7: Dispatch to Bank1 0xA327
void dispatch_03b3(void) { jump_bank_1(0xA327); }

// 0x03B8-0x03BC: Dispatch to Bank1 0xBD76
void dispatch_03b8(void) { jump_bank_1(0xBD76); }

// 0x03BD-0x03C1: Dispatch to Bank1 0xDDE0
void dispatch_03bd(void) { jump_bank_1(0xDDE0); }

// 0x03C2-0x03C6: Dispatch to Bank1 0xE12B
void dispatch_03c2(void) { jump_bank_1(0xE12B); }

// 0x03C7-0x03CB: Dispatch to Bank1 0xEF42
void dispatch_03c7(void) { jump_bank_1(0xEF42); }

// 0x03CC-0x03D0: Dispatch to Bank1 0xE632
void dispatch_03cc(void) { jump_bank_1(0xE632); }

// 0x03D1-0x03D5: Dispatch to Bank1 0xD440
void dispatch_03d1(void) { jump_bank_1(0xD440); }

// 0x03D6-0x03DA: Dispatch to Bank1 0xC65F
void dispatch_03d6(void) { jump_bank_1(0xC65F); }

// 0x03DB-0x03DF: Dispatch to Bank1 0xEF46
void dispatch_03db(void) { jump_bank_1(0xEF46); }

// 0x03E0-0x03E4: Dispatch to Bank1 0xE01F
void dispatch_03e0(void) { jump_bank_1(0xE01F); }

// 0x03E5-0x03E9: Dispatch to Bank1 0xCA52
void dispatch_03e5(void) { jump_bank_1(0xCA52); }

// 0x03EA-0x03EE: Dispatch to Bank1 0xEC9B
void dispatch_03ea(void) { jump_bank_1(0xEC9B); }

// 0x03EF-0x03F3: Dispatch to Bank1 0xC98D
void dispatch_03ef(void) { jump_bank_1(0xC98D); }

// 0x03F4-0x03F8: Dispatch to Bank1 0xDD1A
void dispatch_03f4(void) { jump_bank_1(0xDD1A); }

// 0x03F9-0x03FD: Dispatch to Bank1 0xDD7E
void dispatch_03f9(void) { jump_bank_1(0xDD7E); }

// 0x03FE-0x0402: Dispatch to Bank1 0xDA30
void dispatch_03fe(void) { jump_bank_1(0xDA30); }

// 0x0403-0x0407: Dispatch to Bank1 0xBC5E
void dispatch_0403(void) { jump_bank_1(0xBC5E); }

// 0x0408-0x040C: Dispatch to Bank1 0xE89B
void dispatch_0408(void) { jump_bank_1(0xE89B); }

// 0x040D-0x0411: Dispatch to Bank1 0xDBE7
void dispatch_040d(void) { jump_bank_1(0xDBE7); }

/*===========================================================================
 * Mixed Bank Dispatch Functions (0x0412-0x04DE)
 *===========================================================================*/

// 0x0412-0x0416: Dispatch to 0xE617 (Bank 0)
void dispatch_0412(void) { jump_bank_0(0xE617); }

// 0x0417-0x041B: Dispatch to 0xE62F (Bank 0)
void dispatch_0417(void) { jump_bank_0(0xE62F); }

// 0x041C-0x0420: Dispatch to 0xE647 (Bank 0)
void dispatch_041c(void) { jump_bank_0(0xE647); }

// 0x0421-0x0425: Dispatch to 0xE65F (Bank 0)
void dispatch_0421(void) { jump_bank_0(0xE65F); }

// 0x0426-0x042A: Dispatch to 0xE762 (Bank 0)
void dispatch_0426(void) { jump_bank_0(0xE762); }

// 0x042B-0x042F: Dispatch to 0xE4F0 (Bank 0)
void dispatch_042b(void) { jump_bank_0(0xE4F0); }

// 0x0430-0x0434: Dispatch to 0x9037 (Bank 0)
void dispatch_0430(void) { jump_bank_0(0x9037); }

// 0x0435-0x0439: Dispatch to 0xD127 (Bank 0)
void dispatch_0435(void) { jump_bank_0(0xD127); }

// 0x043A-0x043E: Dispatch to 0xE677 (Bank 0)
void dispatch_043a(void) { jump_bank_0(0xE677); }

// 0x043F-0x0443: Dispatch to 0xE2A6 (Bank 0)
void dispatch_043f(void) { jump_bank_0(0xE2A6); }

// 0x0444-0x0448: Dispatch to 0xA840 (Bank 0)
void dispatch_0444(void) { jump_bank_0(0xA840); }

// 0x0449-0x044D: Dispatch to 0xDD78 (Bank 0)
void dispatch_0449(void) { jump_bank_0(0xDD78); }

// 0x044E-0x0452: Dispatch to 0xE91D (Bank 0)
void dispatch_044e(void) { jump_bank_0(0xE91D); }

// 0x0453-0x0457: Dispatch to 0xE902 (Bank 0)
void dispatch_0453(void) { jump_bank_0(0xE902); }

// 0x0458-0x045C: Dispatch to 0xE77A (Bank 0)
void dispatch_0458(void) { jump_bank_0(0xE77A); }

// 0x045D-0x0461: Dispatch to 0xC00D (pcie_error_handler - Bank 0)
void dispatch_045d(void) { jump_bank_0(0xC00D); }

// 0x0462-0x0466: Dispatch to 0xCD6C (Bank 0)
void dispatch_0462(void) { jump_bank_0(0xCD6C); }

// 0x0467-0x046B: Dispatch to 0xE57D (Bank 0)
void dispatch_0467(void) { jump_bank_0(0xE57D); }

// 0x046C-0x0470: Dispatch to 0xCDC6 (Bank 0)
void dispatch_046c(void) { jump_bank_0(0xCDC6); }

// 0x0471-0x0475: Dispatch to 0xE8A9 (Bank 0)
void dispatch_0471(void) { jump_bank_0(0xE8A9); }

// 0x0476-0x047A: Dispatch to 0xE8D9 (Bank 0)
void dispatch_0476(void) { jump_bank_0(0xE8D9); }

// 0x047B-0x047F: Dispatch to 0xD436 (Bank 0)
void dispatch_047b(void) { jump_bank_0(0xD436); }

// 0x0480-0x0484: Dispatch to 0xE84D (Bank 0)
void dispatch_0480(void) { jump_bank_0(0xE84D); }

// 0x0485-0x0489: Dispatch to 0xE85C (Bank 0)
void dispatch_0485(void) { jump_bank_0(0xE85C); }

// 0x048A-0x048E: Dispatch to Bank1 0xECE1
void dispatch_048a(void) { jump_bank_1(0xECE1); }

// 0x048F-0x0493: Dispatch to Bank1 0xEF1E
void dispatch_048f(void) { jump_bank_1(0xEF1E); }

// 0x0494-0x0498: Dispatch to Bank1 0xE56F
void dispatch_0494(void) { jump_bank_1(0xE56F); }

// 0x0499-0x049D: Dispatch to Bank1 0xC0A5 (timer0_poll_handler)
void dispatch_0499(void) { jump_bank_1(0xC0A5); }

// 0x049E-0x04A2: Dispatch to 0xE957 (Bank 0)
void dispatch_049e(void) { jump_bank_0(0xE957); }

// 0x04A3-0x04A7: Dispatch to 0xE95B (Bank 0)
void dispatch_04a3(void) { jump_bank_0(0xE95B); }

// 0x04A8-0x04AC: Dispatch to 0xE79B (Bank 0)
void dispatch_04a8(void) { jump_bank_0(0xE79B); }

// 0x04AD-0x04B1: Dispatch to 0xE7AE (Bank 0)
void dispatch_04ad(void) { jump_bank_0(0xE7AE); }

// 0x04B2-0x04B6: Dispatch to 0xE971 (reserved stub - Bank 0)
void dispatch_04b2(void) { jump_bank_0(0xE971); }

// 0x04B7-0x04BB: Dispatch to 0xE597 (Bank 0)
void dispatch_04b7(void) { jump_bank_0(0xE597); }

// 0x04BC-0x04C0: Dispatch to 0xE14B (Bank 0)
void dispatch_04bc(void) { jump_bank_0(0xE14B); }

// 0x04C1-0x04C5: Dispatch to 0xBE02 (Bank 0)
void dispatch_04c1(void) { jump_bank_0(0xBE02); }

// 0x04C6-0x04CA: Dispatch to 0xDBF5 (Bank 0)
void dispatch_04c6(void) { jump_bank_0(0xDBF5); }

// 0x04CB-0x04CF: Dispatch to 0xE7C1 (Bank 0)
void dispatch_04cb(void) { jump_bank_0(0xE7C1); }

// 0x04D0-0x04D4: Dispatch to 0xCE79 (timer_link_handler - Bank 0)
void dispatch_04d0(void) { jump_bank_0(0xCE79); }

// 0x04D5-0x04D9: Dispatch to 0xD3A2 (Bank 0)
void dispatch_04d5(void) { jump_bank_0(0xD3A2); }

// 0x04DA-0x04DE: Dispatch to 0xE3B7 (nvme_event_dispatch - Bank 0)
void dispatch_04da(void) { jump_bank_0(0xE3B7); }

/*===========================================================================
 * Event/Interrupt Dispatch Functions (0x04DF-0x0650)
 *===========================================================================*/

// 0x04DF-0x04E3: Dispatch to 0xE95F (Bank 0)
void dispatch_04df(void) { jump_bank_0(0xE95F); }

// 0x04E4-0x04E8: Dispatch to 0xE2EC (Bank 0)
void dispatch_04e4(void) { jump_bank_0(0xE2EC); }

// 0x04E9-0x04ED: Dispatch to 0xE8E4 (Bank 0)
void dispatch_04e9(void) { jump_bank_0(0xE8E4); }

// 0x04EE-0x04F2: Dispatch to 0xE6FC (Bank 0)
void dispatch_04ee(void) { jump_bank_0(0xE6FC); }

// 0x04F3-0x04F7: Dispatch to 0x8A89 (Bank 0)
void dispatch_04f3(void) { jump_bank_0(0x8A89); }

// 0x04F8-0x04FC: Dispatch to 0xDE16 (Bank 0)
void dispatch_04f8(void) { jump_bank_0(0xDE16); }

// 0x04FD-0x0501: Dispatch to 0xE96C (Bank 0)
void dispatch_04fd(void) { jump_bank_0(0xE96C); }

// 0x0502-0x0506: Dispatch to 0xD7CD (Bank 0)
void dispatch_0502(void) { jump_bank_0(0xD7CD); }

// 0x0507-0x050B: Dispatch to 0xE50D (Bank 0)
void dispatch_0507(void) { jump_bank_0(0xE50D); }

// 0x050C-0x0510: Dispatch to 0xE965 (Bank 0)
void dispatch_050c(void) { jump_bank_0(0xE965); }

// 0x0511-0x0515: Dispatch to 0xE95D (Bank 0)
void dispatch_0511(void) { jump_bank_0(0xE95D); }

// 0x0516-0x051A: Dispatch to 0xE96E (Bank 0)
void dispatch_0516(void) { jump_bank_0(0xE96E); }

// 0x051B-0x051F: Dispatch to 0xE1C6 (Bank 0)
void dispatch_051b(void) { jump_bank_0(0xE1C6); }

// 0x0520-0x0524: Dispatch to 0xB4BA (handler_system_int - Bank 0)
void dispatch_0520(void) { jump_bank_0(0xB4BA); }

// 0x0525-0x0529: Dispatch to 0x8D77 (flash_cmd_handler - Bank 0)
void dispatch_0525(void) { jump_bank_0(0x8D77); }

// 0x052A-0x052E: Dispatch to 0xE961 (Bank 0)
void dispatch_052a(void) { jump_bank_0(0xE961); }

// 0x052F-0x0533: Dispatch to 0xAF5E (handler_pcie_nvme - Bank 0)
void dispatch_052f(void) { jump_bank_0(0xAF5E); }

// 0x0534-0x0538: Dispatch to 0xE959 (Bank 0)
void dispatch_0534(void) { jump_bank_0(0xE959); }

// 0x0539-0x053D: Dispatch to 0xE963 (Bank 0)
void dispatch_0539(void) { jump_bank_0(0xE963); }

// 0x053E-0x0542: Dispatch to 0xE967 (Bank 0)
void dispatch_053e(void) { jump_bank_0(0xE967); }

// 0x0543-0x0547: Dispatch to 0xE953 (Bank 0)
void dispatch_0543(void) { jump_bank_0(0xE953); }

// 0x0548-0x054C: Dispatch to 0xE955 (Bank 0)
void dispatch_0548(void) { jump_bank_0(0xE955); }

// 0x054D-0x0551: Dispatch to 0xE96A (Bank 0)
void dispatch_054d(void) { jump_bank_0(0xE96A); }

// 0x0552-0x0556: Dispatch to 0xE96B (Bank 0)
void dispatch_0552(void) { jump_bank_0(0xE96B); }

// 0x0557-0x055B: Dispatch to 0xDA51 (Bank 0)
void dispatch_0557(void) { jump_bank_0(0xDA51); }

// 0x055C-0x0560: Dispatch to 0xE968 (Bank 0)
void dispatch_055c(void) { jump_bank_0(0xE968); }

// 0x0561-0x0565: Dispatch to 0xE966 (Bank 0)
void dispatch_0561(void) { jump_bank_0(0xE966); }

// 0x0566-0x056A: Dispatch to 0xE964 (Bank 0)
void dispatch_0566(void) { jump_bank_0(0xE964); }

// 0x056B-0x056F: Dispatch to 0xE962 (Bank 0)
void dispatch_056b(void) { jump_bank_0(0xE962); }

// 0x0570-0x0574: Dispatch to Bank1 0xE911 (error_handler)
void dispatch_0570(void) { jump_bank_1(0xE911); }

// 0x0575-0x0579: Dispatch to Bank1 0xEDBD
void dispatch_0575(void) { jump_bank_1(0xEDBD); }

// 0x057A-0x057E: Dispatch to Bank1 0xE0D9
void dispatch_057a(void) { jump_bank_1(0xE0D9); }

// 0x057F-0x0583: Dispatch to 0xB8DB (Bank 0)
void dispatch_057f(void) { jump_bank_0(0xB8DB); }

// 0x0584-0x0588: Dispatch to Bank1 0xEF24
void dispatch_0584(void) { jump_bank_1(0xEF24); }

// 0x0589-0x058D: Dispatch to 0xD894 (phy_config - Bank 0)
void dispatch_0589(void) { jump_bank_0(0xD894); }

// 0x058E-0x0592: Dispatch to 0xE0C7 (Bank 0)
void dispatch_058e(void) { jump_bank_0(0xE0C7); }

// 0x0593-0x0597: Dispatch to 0xC105 (handler_c105 - Bank 0)
void dispatch_0593(void) { jump_bank_0(0xC105); }

// 0x0598-0x059C: Dispatch to Bank1 0xE06B
void dispatch_0598(void) { jump_bank_1(0xE06B); }

// 0x059D-0x05A1: Dispatch to Bank1 0xE545
void dispatch_059d(void) { jump_bank_1(0xE545); }

// 0x05A2-0x05A6: Dispatch to 0xC523 (Bank 0)
void dispatch_05a2(void) { jump_bank_0(0xC523); }

// 0x05A7-0x05AB: Dispatch to 0xD1CC (Bank 0)
void dispatch_05a7(void) { jump_bank_0(0xD1CC); }

// 0x05AC-0x05B0: Dispatch to Bank1 0xE74E
void dispatch_05ac(void) { jump_bank_1(0xE74E); }

// 0x05B1-0x05B5: Dispatch to 0xD30B (Bank 0)
void dispatch_05b1(void) { jump_bank_0(0xD30B); }

// 0x05B6-0x05BA: Dispatch to Bank1 0xE561
void dispatch_05b6(void) { jump_bank_1(0xE561); }

// 0x05BB-0x05BF: Dispatch to 0xD5A1 (Bank 0)
void dispatch_05bb(void) { jump_bank_0(0xD5A1); }

// 0x05C0-0x05C4: Dispatch to 0xC593 (Bank 0)
void dispatch_05c0(void) { jump_bank_0(0xC593); }

// 0x05C5-0x05C9: Dispatch to Bank1 0xE7FB
void dispatch_05c5(void) { jump_bank_1(0xE7FB); }

// 0x05CA-0x05CE: Dispatch to Bank1 0xE890
void dispatch_05ca(void) { jump_bank_1(0xE890); }

// 0x05CF-0x05D3: Dispatch to 0xC17F (Bank 0)
void dispatch_05cf(void) { jump_bank_0(0xC17F); }

// 0x05D4-0x05D8: Dispatch to 0xB031 (Bank 0)
void dispatch_05d4(void) { jump_bank_0(0xB031); }

// 0x05D9-0x05DD: Dispatch to Bank1 0xE175
void dispatch_05d9(void) { jump_bank_1(0xE175); }

// 0x05DE-0x05E2: Dispatch to Bank1 0xE282
void dispatch_05de(void) { jump_bank_1(0xE282); }

// 0x05E3-0x05E7: Dispatch to 0xDB80 (Bank 0)
void dispatch_05e3(void) { jump_bank_0(0xDB80); }

// 0x05E8-0x05EC: Dispatch to Bank1 0x9D90
void dispatch_05e8(void) { jump_bank_1(0x9D90); }

// 0x05ED-0x05F1: Dispatch to Bank1 0xD556
void dispatch_05ed(void) { jump_bank_1(0xD556); }

// 0x05F2-0x05F6: Dispatch to 0xDBBB (Bank 0)
void dispatch_05f2(void) { jump_bank_0(0xDBBB); }

// 0x05F7-0x05FB: Dispatch to Bank1 0xD8D5
void dispatch_05f7(void) { jump_bank_1(0xD8D5); }

// 0x05FC-0x0600: Dispatch to Bank1 0xDAD9
void dispatch_05fc(void) { jump_bank_1(0xDAD9); }

// 0x0601-0x0605: Dispatch to 0xEA7C (Bank 0)
void dispatch_0601(void) { jump_bank_0(0xEA7C); }

// 0x0606-0x060A: Dispatch to 0xC089 (error_state_handler - Bank 0)
void dispatch_0606(void) { jump_bank_0(0xC089); }

// 0x060B-0x060F: Dispatch to Bank1 0xE1EE
void dispatch_060b(void) { jump_bank_1(0xE1EE); }

// 0x0610-0x0614: Dispatch to Bank1 0xED02 (handler_ed02 - NOP)
void dispatch_0610(void) { jump_bank_1(0xED02); }

// 0x0615-0x0619: Dispatch to Bank1 0xEEF9 (handler_eef9 - NOP)
void dispatch_0615(void) { jump_bank_1(0xEEF9); }

// 0x061A-0x061E: Dispatch to Bank1 0xA066 (error_handler_a066)
void dispatch_061a(void) { jump_bank_1(0xA066); }

// 0x061F-0x0623: Dispatch to Bank1 0xE25E
void dispatch_061f(void) { jump_bank_1(0xE25E); }

// 0x0624-0x0628: Dispatch to Bank1 0xE2C9
void dispatch_0624(void) { jump_bank_1(0xE2C9); }

// 0x0629-0x062D: Dispatch to Bank1 0xE352
void dispatch_0629(void) { jump_bank_1(0xE352); }

// 0x062E-0x0632: Dispatch to Bank1 0xE374
void dispatch_062e(void) { jump_bank_1(0xE374); }

// 0x0633-0x0637: Dispatch to Bank1 0xE396
void dispatch_0633(void) { jump_bank_1(0xE396); }

// 0x0638-0x063C: Dispatch to Bank1 0xE478
void dispatch_0638(void) { jump_bank_1(0xE478); }

// 0x063D-0x0641: Dispatch to Bank1 0xE496
void dispatch_063d(void) { jump_bank_1(0xE496); }

// 0x0642-0x0646: Dispatch to Bank1 0xEF4E (error_handler_ef4e)
void dispatch_0642(void) { jump_bank_1(0xEF4E); }

// 0x0647-0x064B: Dispatch to Bank1 0xE4D2
void dispatch_0647(void) { jump_bank_1(0xE4D2); }

// 0x064C-0x0650: Dispatch to Bank1 0xE5CB
void dispatch_064c(void) { jump_bank_1(0xE5CB); }
