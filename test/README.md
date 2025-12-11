# ASM2464PD Firmware Tests

This directory contains tests for the ASM2464PD firmware reverse engineering project.

## Round-Trip Disassembly Test

**File:** `test_roundtrip.py`

This test verifies that the 8051 disassembler (`asm/disasm8051.py`) produces valid assembly code that can be reassembled to byte-for-byte identical binaries.

### What it does:

1. **Disassembles** bank0.bin and bank1.bin using the `disasm8051` module
2. Generates complete SDCC-compatible assembly with proper labels
3. **Reassembles** the generated assembly using SDCC's `sdas8051` assembler
4. **Compares** the reassembled binaries with the originals byte-by-byte

### Requirements:

- Python 3
- SDCC (Small Device C Compiler) for the `sdas8051` assembler

Install SDCC:
```bash
# Ubuntu/Debian
sudo apt-get install sdcc

# macOS
brew install sdcc
```

### Running the tests:

```bash
# From project root - run all tests
pytest

# Or more explicitly
python3 -m pytest

# Run with verbose output
pytest -v

# Run with full output (show print statements)
pytest -v -s

# Run only round-trip tests
pytest test/test_roundtrip.py -v

# Run a specific test
pytest test/test_roundtrip.py::test_bank0_roundtrip -v
```

### Expected output:

```
============================= test session starts ==============================
platform linux -- Python 3.13.7, pytest-8.4.2, pluggy-1.6.0
rootdir: /home/light/fun/asm2464pd-firmware
configfile: pytest.ini
collected 2 items

test/test_roundtrip.py::test_bank0_roundtrip PASSED                      [ 50%]
test/test_roundtrip.py::test_bank1_roundtrip PASSED                      [100%]

============================== 2 passed in 0.43s ===============================
```

With `-s` flag for full output:
```
test/test_roundtrip.py::test_bank0_roundtrip
============================================================
Testing bank0
============================================================
Loaded: 65387 bytes from /home/light/fun/asm2464pd-firmware/bank0.bin
Disassembling...
Generated: 45837 lines of assembly
Reassembling with SDCC...
  Assembling bank0...
  Linking bank0...
  Converting to binary...

Comparing binaries...
✓ bank0: Byte-for-byte identical (65387 bytes)
PASSED
test/test_roundtrip.py::test_bank1_roundtrip
============================================================
Testing bank1
============================================================
Loaded: 32619 bytes from /home/light/fun/asm2464pd-firmware/bank1.bin
Disassembling...
Generated: 20123 lines of assembly
Reassembling with SDCC...
  Assembling bank1...
  Linking bank1...
  Converting to binary...

Comparing binaries...
✓ bank1: Byte-for-byte identical (32619 bytes)
PASSED

============================== 2 passed in 0.43s ===============================
```

## Why This Matters

This test ensures that:

1. **The disassembler is correct** - It properly decodes all 8051 instructions
2. **Label generation works** - All branch targets are correctly identified and labeled
3. **SDCC compatibility** - The generated assembly follows SDCC syntax conventions
4. **No information loss** - The disassembly preserves all information needed to recreate the exact binary

A passing test means the disassembler can be trusted for reverse engineering work, and the assembly it generates can be used as a starting point for reimplementation in C.

## Test Coverage

- **Bank 0**: 65,387 bytes (0x0000-0xFFEB) - ~45,837 instructions
- **Bank 1**: 32,619 bytes (0x8000-0xFF6A) - ~20,123 instructions
- **Total**: 97,906 bytes - ~65,960 instructions

Both banks are tested for complete round-trip accuracy.
