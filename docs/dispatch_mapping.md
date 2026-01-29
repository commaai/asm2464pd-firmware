# Dispatch Function Mapping

This document maps each dispatch function to its target address and known implementation.

## Legend
- **Dispatch Addr**: Address of the dispatch stub in the dispatch table
- **Target Addr**: Address the dispatch jumps to
- **Bank**: 0 = Bank 0 (file 0x0000-0xFFFF), 1 = Bank 1 (file 0xFF6B+)
- **Status**: `implemented` = has real code, `stub` = just sets DPX and returns, `partial` = some logic

---

## Bank 0 Dispatch Functions (0x0322-0x03A7)

| Dispatch Addr | Function Name | Target Addr | Status | Implementation |
|---------------|---------------|-------------|--------|----------------|
| 0x0322 | dispatch_0322 | 0xCA51 | **done** | `handler_ca51()` - system state handler, checks 0x09FA and 0x0AE1 |
| 0x0327 | dispatch_0327 | 0xB1CB | stub | `usb_power_init()` in power.c |
| 0x032C | phy_power_config_handler | 0x92C5 | stub | REG_PHY_POWER config |
| 0x0331 | dispatch_0331 | 0xC4B3 | stub | error_log_handler |
| 0x0336 | dispatch_0336 | 0xBF0F | stub | reg_restore_handler |
| 0x033B | dispatch_033b | 0xCDE7 | implemented | `handler_cde7()` - USB control transfer |
| 0x0340 | buffer_dispatch_bf8e | 0xBF8E | stub | buffer dispatch |
| 0x0345 | dispatch_0345 | 0x9B95 | **done** | `nvme_queue_handler()` - NVMe queue status handler with timeout loop |
| 0x034A | dispatch_034a | 0xC465 | **partial** | `handler_c465()` - PHY handler with computed dispatch (0x0DC7), main paths implemented |
| 0x034F | dispatch_034f | 0xE6AA | **done** | `handler_e6aa()` - clears 0x0A7D, calls state handler |
| 0x0354 | dispatch_0354 | 0xE682 | **done** | `handler_e682()` - calls helper_cc56 and helper_cc79 |
| 0x0359 | dispatch_0359 | 0xE423 | **done** | `handler_e423()` - calls helper_cc60, if bit6 of 0x92C2 clear calls init_bda4 (state clear + helper chain) |
| 0x035E | dispatch_035e | 0xE6BD | stub | handler_e6bd |
| 0x0363 | dispatch_0363 | 0xE969 | stub | handler_e969 |
| 0x0368 | dispatch_0368 | 0xDF15 | stub | handler_df15 |
| 0x036D | dispatch_036d | 0xE96F | stub | handler_e96f |
| 0x0372 | dispatch_0372 | 0xE970 | stub | handler_e970 |
| 0x0377 | dispatch_0377 | 0xE952 | stub | handler_e952 |
| 0x037C | dispatch_037c | 0xE941 | stub | handler_e941 |
| 0x0381 | dispatch_0381 | 0xE947 | stub | handler_e947 |
| 0x0386 | dispatch_0386 | 0xE92C | stub | handler_e92c |
| 0x038B | dispatch_038b | 0xD2BD | stub | handler_d2bd |
| 0x0390 | dispatch_0390 | 0xCD10 | stub | handler_cd10 |
| 0x0395 | dispatch_0395 | 0xD5FB | **done** | `handler_d5fb()` - USB poll wait |
| 0x039A | dispatch_039a | 0xD92E | **done** | `handler_d92e()` - USB buffer init, PHY config |
| 0x039F | pcie_dispatch_d916 | 0xD916 | stub | pcie dispatch |
| 0x03A4 | dispatch_03a4 | 0xCB37 | stub | power_ctrl_cb37 |

---

## Bank 1 Dispatch Functions (0x03A9-0x040D)

| Dispatch Addr | Function Name | Target Addr | File Offset | Status | Implementation |
|---------------|---------------|-------------|-------------|--------|----------------|
| 0x03A9 | dispatch_03a9 | 0x89DB | 0x109DB | stub | handler_89db |
| 0x03AE | dispatch_03ae | 0xEF3E | 0x16F3E | stub | handler_ef3e |
| 0x03B3 | dispatch_03b3 | 0xA327 | 0x12327 | stub | handler_a327 |
| 0x03B8 | dispatch_03b8 | 0xBD76 | 0x13D76 | stub | handler_bd76 |
| 0x03BD | dispatch_03bd | 0xDDE0 | 0x15DE0 | stub | handler_dde0 |
| 0x03C2 | dispatch_03c2 | 0xE12B | 0x1612B | stub | handler_e12b |
| 0x03C7 | dispatch_03c7 | 0xEF42 | 0x16F42 | stub | handler_ef42 |
| 0x03CC | dispatch_03cc | 0xE632 | 0x16632 | stub | handler_e632 |
| 0x03D1 | dispatch_03d1 | 0xD440 | 0x15440 | stub | handler_d440 |
| 0x03D6 | dispatch_03d6 | 0xC65F | 0x1465F | stub | handler_c65f |
| 0x03DB | dispatch_03db | 0xEF46 | 0x16F46 | stub | handler_ef46 |
| 0x03E0 | dispatch_03e0 | 0xE01F | 0x1601F | stub | handler_e01f |
| 0x03E5 | dispatch_03e5 | 0xCA52 | 0x14A52 | stub | handler_ca52 |
| 0x03EA | dispatch_03ea | 0xEC9B | 0x16C9B | stub | handler_ec9b |
| 0x03EF | dispatch_03ef | 0xC98D | 0x1498D | stub | handler_c98d |
| 0x03F4 | dispatch_03f4 | 0xDD1A | 0x15D1A | stub | handler_dd1a |
| 0x03F9 | dispatch_03f9 | 0xDD7E | 0x15D7E | stub | handler_dd7e |
| 0x03FE | dispatch_03fe | 0xDA30 | 0x15A30 | stub | handler_da30 |
| 0x0403 | dispatch_0403 | 0xBC5E | 0x13C5E | stub | handler_bc5e |
| 0x0408 | dispatch_0408 | 0xE89B | 0x1689B | stub | handler_e89b |
| 0x040D | dispatch_040d | 0xDBE7 | 0x15BE7 | stub | handler_dbe7 |

---

## Mixed Bank Dispatch Functions (0x0412-0x04DE)

| Dispatch Addr | Function Name | Target Addr | Bank | Status | Implementation |
|---------------|---------------|-------------|------|--------|----------------|
| 0x0412 | dispatch_0412 | 0xE617 | 0 | stub | handler_e617 |
| 0x0417 | dispatch_0417 | 0xE62F | 0 | stub | handler_e62f |
| 0x041C | dispatch_041c | 0xE647 | 0 | stub | handler_e647 |
| 0x0421 | dispatch_0421 | 0xE65F | 0 | stub | handler_e65f |
| 0x0426 | dispatch_0426 | 0xE762 | 0 | stub | handler_e762 |
| 0x042B | dispatch_042b | 0xE4F0 | 0 | stub | handler_e4f0 |
| 0x0430 | dispatch_0430 | 0x9037 | 0 | stub | nvme_config_handler |
| 0x0435 | dispatch_0435 | 0xD127 | 0 | stub | handler_d127 |
| 0x043A | dispatch_043a | 0xE677 | 0 | stub | handler_e677 |
| 0x043F | dispatch_043f | 0xE2A6 | 0 | stub | handler_e2a6 |
| 0x0444 | dispatch_0444 | 0xA840 | 0 | stub | handler_a840 |
| 0x0449 | dispatch_0449 | 0xDD78 | 0 | stub | handler_dd78 |
| 0x044E | pcie_dispatch_e91d | 0xE91D | 0 | stub | pcie dispatch |
| 0x0453 | dispatch_0453 | 0xE902 | 0 | stub | handler_e902 |
| 0x0458 | dispatch_0458 | 0xE77A | 0 | stub | handler_e77a |
| 0x045D | dispatch_045d | 0xC00D | 0 | stub | pcie_tunnel_enable |
| 0x0467 | dispatch_0467 | 0xE57D | 0 | stub | handler_e57d |
| 0x046C | dispatch_046c | 0xCDC6 | 0 | stub | handler_cdc6 |
| 0x0471 | dispatch_0471 | 0xE8A9 | 0 | stub | handler_e8a9 |
| 0x0476 | dispatch_0476 | 0xE8D9 | 0 | stub | handler_e8d9 |
| 0x047B | dispatch_047b | 0xD436 | 0 | stub | handler_d436 |
| 0x0480 | dispatch_0480 | 0xE84D | 0 | stub | handler_e84d |
| 0x0485 | dispatch_0485 | 0xE85C | 0 | stub | handler_e85c |
| 0x048A | dispatch_048a | 0xECE1 | 1 | stub | handler_ece1 |
| 0x048F | dispatch_048f | 0xEF1E | 1 | stub | handler_ef1e |
| 0x0494 | dispatch_0494 | 0xE56F | 1 | stub | event_handler_e56f |
| 0x0499 | dispatch_0499 | 0xC0A5 | 1 | stub | handler_c0a5 |
| 0x049E | dispatch_049e | 0xE957 | 0 | stub | sys_timer_handler |
| 0x04A3 | dispatch_04a3 | 0xE95B | 0 | stub | handler_e95b |
| 0x04A8 | dispatch_04a8 | 0xE79B | 0 | stub | handler_e79b |
| 0x04AD | dispatch_04ad | 0xE7AE | 0 | stub | handler_e7ae |
| 0x04B2 | dispatch_04b2 | 0xE971 | 0 | stub | reserved_stub |
| 0x04B7 | dispatch_04b7 | 0xE597 | 0 | stub | handler_e597 |
| 0x04BC | dispatch_04bc | 0xE14B | 0 | stub | handler_e14b |
| 0x04C1 | dispatch_04c1 | 0xBE02 | 0 | stub | dma_handler_be02 |
| 0x04C6 | dispatch_04c6 | 0xDBF5 | 0 | stub | handler_dbf5 |
| 0x04CB | dispatch_04cb | 0xDFAE | 0 | **done** | `handler_dfae()` - timer/link handler |
| 0x04D0 | dispatch_04d0 | 0xCE79 | 0 | stub | timer_link_handler |
| 0x04D5 | dispatch_04d5 | 0xD3A2 | 0 | stub | handler_d3a2 |
| 0x04DA | dispatch_04da | 0xE3B7 | 0 | stub | handler_e3b7 |

---

## Event/Interrupt Dispatch Functions (0x04DF-0x064C)

| Dispatch Addr | Function Name | Target Addr | Bank | Status | Implementation |
|---------------|---------------|-------------|------|--------|----------------|
| 0x04DF | dispatch_04df | 0xE95F | 0 | stub | handler_e95f |
| 0x04E4 | dispatch_04e4 | 0xE2EC | 0 | stub | handler_e2ec |
| 0x04E9 | dispatch_04e9 | 0xE8E4 | 0 | stub | handler_e8e4 |
| 0x04EE | pcie_dispatch_e6fc | 0xE6FC | 0 | stub | pcie dispatch |
| 0x04F3 | dispatch_04f3 | 0x8A89 | 0 | stub | handler_8a89 |
| 0x04F8 | dispatch_04f8 | 0xDE16 | 0 | stub | handler_de16 |
| 0x04FD | pcie_dispatch_e96c | 0xE96C | 0 | stub | pcie dispatch |
| 0x0502 | dispatch_0502 | 0xD7CD | 0 | stub | handler_d7cd |
| 0x0507 | dispatch_0507 | 0xE50D | 0 | stub | handler_e50d |
| 0x050C | dispatch_050c | 0xE965 | 0 | stub | handler_e965 |
| 0x0511 | dispatch_0511 | 0xE95D | 0 | stub | handler_e95d |
| 0x0516 | dispatch_0516 | 0xE96E | 0 | stub | handler_e96e |
| 0x051B | dispatch_051b | 0xE1C6 | 0 | stub | handler_e1c6 |
| 0x0520 | dispatch_0520 | 0x8A81 | 0 | implemented | `system_init_from_flash()` |
| 0x0525 | dispatch_0525 | 0x8D77 | 0 | implemented | `system_init_from_flash()` |
| 0x052A | dispatch_052a | 0xE961 | 0 | stub | handler_e961 |
| 0x052F | dispatch_052f | 0xAF5E | 0 | stub | debug_output_handler |
| 0x0534 | scsi_dispatch_d6bc | 0xD6BC | 0 | stub | scsi dispatch |
| 0x0539 | dispatch_0539 | 0xE963 | 0 | stub | handler_e963 |
| 0x053E | dispatch_053e | 0xE967 | 0 | stub | handler_e967 |
| 0x0543 | dispatch_0543 | 0xE953 | 0 | stub | handler_e953 |
| 0x0548 | dispatch_0548 | 0xE955 | 0 | stub | handler_e955 |
| 0x054D | dispatch_054d | 0xE96A | 0 | stub | handler_e96a |
| 0x0552 | dispatch_0552 | 0xE96B | 0 | stub | handler_e96b |
| 0x0557 | dispatch_0557 | 0xDA51 | 0 | stub | handler_da51 |
| 0x055C | dispatch_055c | 0xE968 | 0 | stub | handler_e968 |
| 0x0561 | dispatch_0561 | 0xE966 | 0 | stub | handler_e966 |
| 0x0566 | dispatch_0566 | 0xE964 | 0 | stub | handler_e964 |
| 0x056B | dispatch_056b | 0xE962 | 0 | stub | handler_e962 |
| 0x0570 | dispatch_0570 | 0xE911 | 1 | stub | error_handler_e911 |
| 0x0575 | dispatch_0575 | 0xEDBD | 1 | stub | handler_edbd |
| 0x057A | dispatch_057a | 0xE0D9 | 1 | stub | handler_e0d9 |
| 0x057F | dispatch_057f | 0xB8DB | 0 | stub | handler_b8db |
| 0x0584 | dispatch_0584 | 0xEF24 | 1 | stub | handler_ef24 |
| 0x0589 | dispatch_0589 | 0xD894 | 0 | stub | phy_register_config |
| 0x058E | dispatch_058e | 0xE0C7 | 0 | stub | handler_e0c7 |
| 0x0593 | dispatch_0593 | 0xC105 | 0 | stub | handler_c105 |
| 0x0598 | dispatch_0598 | 0xE06B | 1 | stub | handler_e06b |
| 0x059D | dispatch_059d | 0xE545 | 1 | stub | handler_e545 |
| 0x05A2 | dispatch_05a2 | 0xC523 | 0 | stub | pcie_handler_c523 |
| 0x05A7 | dispatch_05a7 | 0xD1CC | 0 | stub | handler_d1cc |
| 0x05AC | dispatch_05ac | 0xE74E | 1 | stub | handler_e74e |
| 0x05B1 | dispatch_05b1 | 0xD30B | 0 | stub | handler_d30b |
| 0x05B6 | dispatch_05b6 | 0xE561 | 1 | stub | handler_e561 |
| 0x05BB | dispatch_05bb | 0xD5A1 | 0 | stub | handler_d5a1 |
| 0x05C0 | dispatch_05c0 | 0xC593 | 0 | stub | pcie_handler_c593 |
| 0x05C5 | dispatch_05c5 | 0xE7FB | 1 | stub | handler_e7fb |
| 0x05CA | dispatch_05ca | 0xE890 | 1 | stub | handler_e890 |
| 0x05CF | dispatch_05cf | 0xC17F | 0 | stub | pcie_handler_c17f |
| 0x05D4 | dispatch_05d4 | 0xB031 | 0 | stub | handler_b031 |
| 0x05D9 | dispatch_05d9 | 0xE175 | 1 | stub | handler_e175 |
| 0x05DE | dispatch_05de | 0xE282 | 1 | stub | handler_e282 |
| 0x05E3 | dispatch_05e3 | 0xB103 | 1 | implemented | `pd_debug_print_flp()` |
| 0x05E8 | dispatch_05e8 | 0x9D90 | 1 | stub | protocol_nop_handler |
| 0x05ED | dispatch_05ed | 0xD556 | 1 | stub | handler_d556 |
| 0x05F2 | dispatch_05f2 | 0xDBBB | 0 | stub | handler_dbbb |
| 0x05F7 | dispatch_05f7 | 0xD8D5 | 1 | stub | handler_d8d5 |
| 0x05FC | dispatch_05fc | 0xDAD9 | 1 | stub | handler_dad9 |
| 0x0601 | dispatch_0601 | 0xEA7C | 0 | stub | handler_ea7c |
| 0x0606 | dispatch_0606 | 0xC089 | 0 | stub | pcie_handler_c089 |
| 0x060B | dispatch_060b | 0xE1EE | 1 | stub | handler_e1ee |
| 0x0610 | dispatch_0610 | 0xED02 | 1 | stub | handler_ed02 |
| 0x0615 | dispatch_0615 | 0xEEF9 | 1 | stub | handler_eef9 (NOPs) |
| 0x061A | dispatch_061a | 0xA066 | 1 | stub | error_handler_a066 |
| 0x061F | dispatch_061f | 0xE25E | 1 | stub | handler_e25e |
| 0x0624 | dispatch_0624 | 0xE2C9 | 1 | stub | handler_e2c9 |
| 0x0629 | dispatch_0629 | 0xE352 | 1 | stub | handler_e352 |
| 0x062E | dispatch_062e | 0xE374 | 1 | stub | handler_e374 |
| 0x0633 | dispatch_0633 | 0xE396 | 1 | stub | handler_e396 |
| 0x0638 | pcie_transfer_handler | 0xE478 | 1 | stub | pcie transfer |
| 0x063D | dispatch_063d | 0xE496 | 1 | stub | handler_e496 |
| 0x0642 | dispatch_0642 | 0xEF4E | 1 | stub | error_handler_ef4e (NOPs) |
| 0x0647 | dispatch_0647 | 0xE4D2 | 1 | stub | handler_e4d2 |
| 0x064C | dispatch_064c | 0xE5CB | 1 | stub | handler_e5cb |

---

## Special Dispatch Functions

| Function Name | Target Addr | Status | Notes |
|---------------|-------------|--------|-------|
| dispatch_0206 | inline | implemented | USB/DMA status dispatch (not a jump stub) |
| jump_bank_0 | 0x0300 | no-op | Sets DPX=0, returns (should execute target) |
| jump_bank_1 | 0x0311 | no-op | Sets DPX=1, returns (should execute target) |

---

## Summary

- **Total dispatch functions**: ~110
- **Implemented**: 7 (dispatch_033b, dispatch_0345, dispatch_0359, dispatch_0520, dispatch_0525, dispatch_05e3, dispatch_0206)
- **Stubs (no-ops)**: ~103

## Known Implementations in Other Files

| Target Address | Function | File |
|----------------|----------|------|
| 0xB1CB | usb_power_init() | src/drivers/power.c |
| 0x8A81 | system_init_from_flash() | src/drivers/flash.c |
| 0xB103 | pd_debug_print_flp() | src/drivers/pd.c |

## Priority for Implementation

1. **0x0327 → 0xB1CB**: usb_power_init - already exists, just need to call it
2. **0x033B → 0xCDE7**: USB control transfer - partially implemented
3. ~~**0x0345 → 0x9B95**: NVMe queue handler~~ - DONE
4. **0x0534 → 0xD6BC**: SCSI dispatch - needed for USB storage
5. **0x032C → 0x92C5**: PHY power config - init sequence
6. **0x034A → 0xC66A**: PHY handler - called in interrupt.c
7. **0x0322 → 0xCA0D**: system_state_handler - complex, called in main.c
