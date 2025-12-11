#!/usr/bin/env python3
"""
8051 Disassembler for SDCC Assembly Output

Disassembles 8051 binary code into SDCC-compatible assembly that
assembles back to identical bytes.
"""

# 8051 Instruction Set
# Format: opcode -> (mnemonic, size, operand_format)
# Operand formats:
#   None - no operands
#   'A' - accumulator
#   'AB' - A,B pair
#   'C' - carry flag
#   'DPTR' - data pointer
#   '@A+DPTR' - indexed addressing
#   '@A+PC' - PC-relative indexed
#   'Rn' - register (n encoded in opcode)
#   '@Ri' - indirect register (i encoded in opcode)
#   '#data' - immediate byte
#   '#data16' - immediate word
#   'direct' - direct address byte
#   'addr11' - 11-bit address (within 2K page)
#   'addr16' - 16-bit address
#   'rel' - relative offset (signed byte)
#   'bit' - bit address

INSTRUCTIONS = {
    # NOP
    0x00: ('nop', 1, None),

    # AJMP addr11 (pages 0-7)
    0x01: ('ajmp', 2, 'addr11'),
    0x21: ('ajmp', 2, 'addr11'),
    0x41: ('ajmp', 2, 'addr11'),
    0x61: ('ajmp', 2, 'addr11'),
    0x81: ('ajmp', 2, 'addr11'),
    0xa1: ('ajmp', 2, 'addr11'),
    0xc1: ('ajmp', 2, 'addr11'),
    0xe1: ('ajmp', 2, 'addr11'),

    # LJMP addr16
    0x02: ('ljmp', 3, 'addr16'),

    # RR A
    0x03: ('rr', 1, 'A'),

    # INC
    0x04: ('inc', 1, 'A'),
    0x05: ('inc', 2, 'direct'),
    0x06: ('inc', 1, '@R0'),
    0x07: ('inc', 1, '@R1'),
    0x08: ('inc', 1, 'R0'),
    0x09: ('inc', 1, 'R1'),
    0x0a: ('inc', 1, 'R2'),
    0x0b: ('inc', 1, 'R3'),
    0x0c: ('inc', 1, 'R4'),
    0x0d: ('inc', 1, 'R5'),
    0x0e: ('inc', 1, 'R6'),
    0x0f: ('inc', 1, 'R7'),

    # JBC bit, rel
    0x10: ('jbc', 3, 'bit,rel'),

    # ACALL addr11 (pages 0-7)
    0x11: ('acall', 2, 'addr11'),
    0x31: ('acall', 2, 'addr11'),
    0x51: ('acall', 2, 'addr11'),
    0x71: ('acall', 2, 'addr11'),
    0x91: ('acall', 2, 'addr11'),
    0xb1: ('acall', 2, 'addr11'),
    0xd1: ('acall', 2, 'addr11'),
    0xf1: ('acall', 2, 'addr11'),

    # LCALL addr16
    0x12: ('lcall', 3, 'addr16'),

    # RRC A
    0x13: ('rrc', 1, 'A'),

    # DEC
    0x14: ('dec', 1, 'A'),
    0x15: ('dec', 2, 'direct'),
    0x16: ('dec', 1, '@R0'),
    0x17: ('dec', 1, '@R1'),
    0x18: ('dec', 1, 'R0'),
    0x19: ('dec', 1, 'R1'),
    0x1a: ('dec', 1, 'R2'),
    0x1b: ('dec', 1, 'R3'),
    0x1c: ('dec', 1, 'R4'),
    0x1d: ('dec', 1, 'R5'),
    0x1e: ('dec', 1, 'R6'),
    0x1f: ('dec', 1, 'R7'),

    # JB bit, rel
    0x20: ('jb', 3, 'bit,rel'),

    # RET
    0x22: ('ret', 1, None),

    # RL A
    0x23: ('rl', 1, 'A'),

    # ADD A, #data
    0x24: ('add', 2, 'A,#data'),
    # ADD A, direct
    0x25: ('add', 2, 'A,direct'),
    # ADD A, @Ri
    0x26: ('add', 1, 'A,@R0'),
    0x27: ('add', 1, 'A,@R1'),
    # ADD A, Rn
    0x28: ('add', 1, 'A,R0'),
    0x29: ('add', 1, 'A,R1'),
    0x2a: ('add', 1, 'A,R2'),
    0x2b: ('add', 1, 'A,R3'),
    0x2c: ('add', 1, 'A,R4'),
    0x2d: ('add', 1, 'A,R5'),
    0x2e: ('add', 1, 'A,R6'),
    0x2f: ('add', 1, 'A,R7'),

    # JNB bit, rel
    0x30: ('jnb', 3, 'bit,rel'),

    # RETI
    0x32: ('reti', 1, None),

    # RLC A
    0x33: ('rlc', 1, 'A'),

    # ADDC A, #data
    0x34: ('addc', 2, 'A,#data'),
    # ADDC A, direct
    0x35: ('addc', 2, 'A,direct'),
    # ADDC A, @Ri
    0x36: ('addc', 1, 'A,@R0'),
    0x37: ('addc', 1, 'A,@R1'),
    # ADDC A, Rn
    0x38: ('addc', 1, 'A,R0'),
    0x39: ('addc', 1, 'A,R1'),
    0x3a: ('addc', 1, 'A,R2'),
    0x3b: ('addc', 1, 'A,R3'),
    0x3c: ('addc', 1, 'A,R4'),
    0x3d: ('addc', 1, 'A,R5'),
    0x3e: ('addc', 1, 'A,R6'),
    0x3f: ('addc', 1, 'A,R7'),

    # JC rel
    0x40: ('jc', 2, 'rel'),

    # ORL direct, A
    0x42: ('orl', 2, 'direct,A'),
    # ORL direct, #data
    0x43: ('orl', 3, 'direct,#data'),
    # ORL A, #data
    0x44: ('orl', 2, 'A,#data'),
    # ORL A, direct
    0x45: ('orl', 2, 'A,direct'),
    # ORL A, @Ri
    0x46: ('orl', 1, 'A,@R0'),
    0x47: ('orl', 1, 'A,@R1'),
    # ORL A, Rn
    0x48: ('orl', 1, 'A,R0'),
    0x49: ('orl', 1, 'A,R1'),
    0x4a: ('orl', 1, 'A,R2'),
    0x4b: ('orl', 1, 'A,R3'),
    0x4c: ('orl', 1, 'A,R4'),
    0x4d: ('orl', 1, 'A,R5'),
    0x4e: ('orl', 1, 'A,R6'),
    0x4f: ('orl', 1, 'A,R7'),

    # JNC rel
    0x50: ('jnc', 2, 'rel'),

    # ANL direct, A
    0x52: ('anl', 2, 'direct,A'),
    # ANL direct, #data
    0x53: ('anl', 3, 'direct,#data'),
    # ANL A, #data
    0x54: ('anl', 2, 'A,#data'),
    # ANL A, direct
    0x55: ('anl', 2, 'A,direct'),
    # ANL A, @Ri
    0x56: ('anl', 1, 'A,@R0'),
    0x57: ('anl', 1, 'A,@R1'),
    # ANL A, Rn
    0x58: ('anl', 1, 'A,R0'),
    0x59: ('anl', 1, 'A,R1'),
    0x5a: ('anl', 1, 'A,R2'),
    0x5b: ('anl', 1, 'A,R3'),
    0x5c: ('anl', 1, 'A,R4'),
    0x5d: ('anl', 1, 'A,R5'),
    0x5e: ('anl', 1, 'A,R6'),
    0x5f: ('anl', 1, 'A,R7'),

    # JZ rel
    0x60: ('jz', 2, 'rel'),

    # XRL direct, A
    0x62: ('xrl', 2, 'direct,A'),
    # XRL direct, #data
    0x63: ('xrl', 3, 'direct,#data'),
    # XRL A, #data
    0x64: ('xrl', 2, 'A,#data'),
    # XRL A, direct
    0x65: ('xrl', 2, 'A,direct'),
    # XRL A, @Ri
    0x66: ('xrl', 1, 'A,@R0'),
    0x67: ('xrl', 1, 'A,@R1'),
    # XRL A, Rn
    0x68: ('xrl', 1, 'A,R0'),
    0x69: ('xrl', 1, 'A,R1'),
    0x6a: ('xrl', 1, 'A,R2'),
    0x6b: ('xrl', 1, 'A,R3'),
    0x6c: ('xrl', 1, 'A,R4'),
    0x6d: ('xrl', 1, 'A,R5'),
    0x6e: ('xrl', 1, 'A,R6'),
    0x6f: ('xrl', 1, 'A,R7'),

    # JNZ rel
    0x70: ('jnz', 2, 'rel'),

    # ORL C, bit
    0x72: ('orl', 2, 'C,bit'),
    # JMP @A+DPTR
    0x73: ('jmp', 1, '@A+DPTR'),
    # MOV A, #data
    0x74: ('mov', 2, 'A,#data'),
    # MOV direct, #data
    0x75: ('mov', 3, 'direct,#data'),
    # MOV @Ri, #data
    0x76: ('mov', 2, '@R0,#data'),
    0x77: ('mov', 2, '@R1,#data'),
    # MOV Rn, #data
    0x78: ('mov', 2, 'R0,#data'),
    0x79: ('mov', 2, 'R1,#data'),
    0x7a: ('mov', 2, 'R2,#data'),
    0x7b: ('mov', 2, 'R3,#data'),
    0x7c: ('mov', 2, 'R4,#data'),
    0x7d: ('mov', 2, 'R5,#data'),
    0x7e: ('mov', 2, 'R6,#data'),
    0x7f: ('mov', 2, 'R7,#data'),

    # SJMP rel
    0x80: ('sjmp', 2, 'rel'),

    # ANL C, bit
    0x82: ('anl', 2, 'C,bit'),
    # MOVC A, @A+PC
    0x83: ('movc', 1, 'A,@A+PC'),
    # DIV AB
    0x84: ('div', 1, 'AB'),
    # MOV direct, direct
    0x85: ('mov', 3, 'direct,direct'),
    # MOV direct, @Ri
    0x86: ('mov', 2, 'direct,@R0'),
    0x87: ('mov', 2, 'direct,@R1'),
    # MOV direct, Rn
    0x88: ('mov', 2, 'direct,R0'),
    0x89: ('mov', 2, 'direct,R1'),
    0x8a: ('mov', 2, 'direct,R2'),
    0x8b: ('mov', 2, 'direct,R3'),
    0x8c: ('mov', 2, 'direct,R4'),
    0x8d: ('mov', 2, 'direct,R5'),
    0x8e: ('mov', 2, 'direct,R6'),
    0x8f: ('mov', 2, 'direct,R7'),

    # MOV DPTR, #data16
    0x90: ('mov', 3, 'DPTR,#data16'),

    # MOV bit, C
    0x92: ('mov', 2, 'bit,C'),
    # MOVC A, @A+DPTR
    0x93: ('movc', 1, 'A,@A+DPTR'),
    # SUBB A, #data
    0x94: ('subb', 2, 'A,#data'),
    # SUBB A, direct
    0x95: ('subb', 2, 'A,direct'),
    # SUBB A, @Ri
    0x96: ('subb', 1, 'A,@R0'),
    0x97: ('subb', 1, 'A,@R1'),
    # SUBB A, Rn
    0x98: ('subb', 1, 'A,R0'),
    0x99: ('subb', 1, 'A,R1'),
    0x9a: ('subb', 1, 'A,R2'),
    0x9b: ('subb', 1, 'A,R3'),
    0x9c: ('subb', 1, 'A,R4'),
    0x9d: ('subb', 1, 'A,R5'),
    0x9e: ('subb', 1, 'A,R6'),
    0x9f: ('subb', 1, 'A,R7'),

    # ORL C, /bit
    0xa0: ('orl', 2, 'C,/bit'),

    # MOV C, bit
    0xa2: ('mov', 2, 'C,bit'),
    # INC DPTR
    0xa3: ('inc', 1, 'DPTR'),
    # MUL AB
    0xa4: ('mul', 1, 'AB'),
    # reserved
    # MOV @Ri, direct
    0xa6: ('mov', 2, '@R0,direct'),
    0xa7: ('mov', 2, '@R1,direct'),
    # MOV Rn, direct
    0xa8: ('mov', 2, 'R0,direct'),
    0xa9: ('mov', 2, 'R1,direct'),
    0xaa: ('mov', 2, 'R2,direct'),
    0xab: ('mov', 2, 'R3,direct'),
    0xac: ('mov', 2, 'R4,direct'),
    0xad: ('mov', 2, 'R5,direct'),
    0xae: ('mov', 2, 'R6,direct'),
    0xaf: ('mov', 2, 'R7,direct'),

    # ANL C, /bit
    0xb0: ('anl', 2, 'C,/bit'),

    # CPL bit
    0xb2: ('cpl', 2, 'bit'),
    # CPL C
    0xb3: ('cpl', 1, 'C'),
    # CJNE A, #data, rel
    0xb4: ('cjne', 3, 'A,#data,rel'),
    # CJNE A, direct, rel
    0xb5: ('cjne', 3, 'A,direct,rel'),
    # CJNE @Ri, #data, rel
    0xb6: ('cjne', 3, '@R0,#data,rel'),
    0xb7: ('cjne', 3, '@R1,#data,rel'),
    # CJNE Rn, #data, rel
    0xb8: ('cjne', 3, 'R0,#data,rel'),
    0xb9: ('cjne', 3, 'R1,#data,rel'),
    0xba: ('cjne', 3, 'R2,#data,rel'),
    0xbb: ('cjne', 3, 'R3,#data,rel'),
    0xbc: ('cjne', 3, 'R4,#data,rel'),
    0xbd: ('cjne', 3, 'R5,#data,rel'),
    0xbe: ('cjne', 3, 'R6,#data,rel'),
    0xbf: ('cjne', 3, 'R7,#data,rel'),

    # PUSH direct
    0xc0: ('push', 2, 'direct'),

    # CLR bit
    0xc2: ('clr', 2, 'bit'),
    # CLR C
    0xc3: ('clr', 1, 'C'),
    # SWAP A
    0xc4: ('swap', 1, 'A'),
    # XCH A, direct
    0xc5: ('xch', 2, 'A,direct'),
    # XCH A, @Ri
    0xc6: ('xch', 1, 'A,@R0'),
    0xc7: ('xch', 1, 'A,@R1'),
    # XCH A, Rn
    0xc8: ('xch', 1, 'A,R0'),
    0xc9: ('xch', 1, 'A,R1'),
    0xca: ('xch', 1, 'A,R2'),
    0xcb: ('xch', 1, 'A,R3'),
    0xcc: ('xch', 1, 'A,R4'),
    0xcd: ('xch', 1, 'A,R5'),
    0xce: ('xch', 1, 'A,R6'),
    0xcf: ('xch', 1, 'A,R7'),

    # POP direct
    0xd0: ('pop', 2, 'direct'),

    # SETB bit
    0xd2: ('setb', 2, 'bit'),
    # SETB C
    0xd3: ('setb', 1, 'C'),
    # DA A
    0xd4: ('da', 1, 'A'),
    # DJNZ direct, rel
    0xd5: ('djnz', 3, 'direct,rel'),
    # XCHD A, @Ri
    0xd6: ('xchd', 1, 'A,@R0'),
    0xd7: ('xchd', 1, 'A,@R1'),
    # DJNZ Rn, rel
    0xd8: ('djnz', 2, 'R0,rel'),
    0xd9: ('djnz', 2, 'R1,rel'),
    0xda: ('djnz', 2, 'R2,rel'),
    0xdb: ('djnz', 2, 'R3,rel'),
    0xdc: ('djnz', 2, 'R4,rel'),
    0xdd: ('djnz', 2, 'R5,rel'),
    0xde: ('djnz', 2, 'R6,rel'),
    0xdf: ('djnz', 2, 'R7,rel'),

    # MOVX A, @DPTR
    0xe0: ('movx', 1, 'A,@DPTR'),

    # MOVX A, @Ri
    0xe2: ('movx', 1, 'A,@R0'),
    0xe3: ('movx', 1, 'A,@R1'),
    # CLR A
    0xe4: ('clr', 1, 'A'),
    # MOV A, direct
    0xe5: ('mov', 2, 'A,direct'),
    # MOV A, @Ri
    0xe6: ('mov', 1, 'A,@R0'),
    0xe7: ('mov', 1, 'A,@R1'),
    # MOV A, Rn
    0xe8: ('mov', 1, 'A,R0'),
    0xe9: ('mov', 1, 'A,R1'),
    0xea: ('mov', 1, 'A,R2'),
    0xeb: ('mov', 1, 'A,R3'),
    0xec: ('mov', 1, 'A,R4'),
    0xed: ('mov', 1, 'A,R5'),
    0xee: ('mov', 1, 'A,R6'),
    0xef: ('mov', 1, 'A,R7'),

    # MOVX @DPTR, A
    0xf0: ('movx', 1, '@DPTR,A'),

    # MOVX @Ri, A
    0xf2: ('movx', 1, '@R0,A'),
    0xf3: ('movx', 1, '@R1,A'),
    # CPL A
    0xf4: ('cpl', 1, 'A'),
    # MOV direct, A
    0xf5: ('mov', 2, 'direct,A'),
    # MOV @Ri, A
    0xf6: ('mov', 1, '@R0,A'),
    0xf7: ('mov', 1, '@R1,A'),
    # MOV Rn, A
    0xf8: ('mov', 1, 'R0,A'),
    0xf9: ('mov', 1, 'R1,A'),
    0xfa: ('mov', 1, 'R2,A'),
    0xfb: ('mov', 1, 'R3,A'),
    0xfc: ('mov', 1, 'R4,A'),
    0xfd: ('mov', 1, 'R5,A'),
    0xfe: ('mov', 1, 'R6,A'),
    0xff: ('mov', 1, 'R7,A'),
}

# SFR names for common addresses
SFR_NAMES = {
    0x80: 'P0',
    0x81: 'SP',
    0x82: 'DPL',
    0x83: 'DPH',
    0x87: 'PCON',
    0x88: 'TCON',
    0x89: 'TMOD',
    0x8a: 'TL0',
    0x8b: 'TL1',
    0x8c: 'TH0',
    0x8d: 'TH1',
    0x90: 'P1',
    0x98: 'SCON',
    0x99: 'SBUF',
    0xa0: 'P2',
    0xa8: 'IE',
    0xb0: 'P3',
    0xb8: 'IP',
    0xd0: 'PSW',
    0xe0: 'ACC',
    0xf0: 'B',
}


class Disassembler:
    def __init__(self, data, base_addr=0, labels=None, use_raw_branches=True,
                 valid_targets=None, bank_end=None):
        self.data = data
        self.base_addr = base_addr
        self.labels = labels or {}
        self.branch_targets = set()
        self.call_targets = set()
        self.use_raw_branches = use_raw_branches  # If True, emit .db for all branches
        self.valid_targets = valid_targets  # Set of valid instruction start addresses
        self.bank_end = bank_end  # End of current bank (for cross-bank detection)

    def read_byte(self, offset):
        if 0 <= offset < len(self.data):
            return self.data[offset]
        return 0

    def get_sfr_name(self, addr):
        """Get SFR name or hex address."""
        if addr in SFR_NAMES:
            return SFR_NAMES[addr]
        return f'0x{addr:02x}'

    def format_direct(self, addr):
        """Format direct address."""
        if addr in SFR_NAMES:
            return SFR_NAMES[addr]
        # Use _name for SDCC internal RAM references
        if addr < 0x80:
            return f'0x{addr:02x}'
        return f'0x{addr:02x}'

    def format_bit(self, bit_addr):
        """Format bit address."""
        # Bit addresses 0x00-0x7F are in byte addresses 0x20-0x2F
        # Bit addresses 0x80+ are SFR bits
        return f'0x{bit_addr:02x}'

    def is_valid_branch_target(self, target):
        """Check if target is a valid branch destination.

        Returns False if:
        - Target is outside the current bank (cross-bank jump)
        - Target is not at an instruction boundary (mid-instruction jump)
        - Target is in a different memory region
        """
        # Check if cross-bank
        if self.bank_end is not None and target >= self.bank_end:
            return False

        # Check if at valid instruction boundary
        if self.valid_targets is not None and target not in self.valid_targets:
            return False

        return True

    def should_use_raw_branch(self, target):
        """Determine if we should use raw bytes for a branch to this target."""
        if self.use_raw_branches:
            return True
        return not self.is_valid_branch_target(target)

    def disassemble_instruction(self, offset):
        """Disassemble a single instruction at offset."""
        if offset >= len(self.data):
            return None, 0, []

        opcode = self.data[offset]

        if opcode not in INSTRUCTIONS:
            # Unknown opcode - emit as .db
            return None, 1, [opcode]

        mnemonic, size, operand_fmt = INSTRUCTIONS[opcode]

        # Check we have enough bytes
        if offset + size > len(self.data):
            return None, 1, [opcode]

        # Get operand bytes
        operand_bytes = [self.data[offset + i] for i in range(1, size)]

        # Format the instruction
        addr = self.base_addr + offset

        if operand_fmt is None:
            return f'{mnemonic}', size, []

        # Handle different operand formats
        operands = operand_fmt

        if operand_fmt == 'addr16':
            # ljmp/lcall - 16-bit absolute address, always safe to use mnemonic
            target = (operand_bytes[0] << 8) | operand_bytes[1]
            self.branch_targets.add(target)
            if mnemonic == 'lcall':
                self.call_targets.add(target)
            if self.is_valid_branch_target(target):
                label = self.get_label(target)
                return f'{mnemonic}\t{label}', size, []
            else:
                # Use numeric address for cross-bank or invalid targets
                return f'{mnemonic}\t0x{target:04x}', size, []

        elif operand_fmt == 'addr11':
            # ajmp/acall - 11-bit address, page-relative (2KB boundary)
            high_bits = (opcode >> 5) & 0x07
            target = ((addr + 2) & 0xF800) | (high_bits << 8) | operand_bytes[0]
            self.branch_targets.add(target)
            if mnemonic == 'acall':
                self.call_targets.add(target)
            if self.is_valid_branch_target(target):
                label = self.get_label(target)
                return f'{mnemonic}\t{label}', size, []
            else:
                # Raw bytes - target on different 2KB page or invalid
                hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                return f'.db\t{hex_bytes:<16}; {mnemonic} 0x{target:04x}', size, []

        elif operand_fmt == 'rel':
            # Relative jump (sjmp, jz, jnz, etc.)
            rel = operand_bytes[0]
            if rel > 127:
                rel -= 256
            target = addr + size + rel
            self.branch_targets.add(target)
            if self.is_valid_branch_target(target):
                label = self.get_label(target)
                return f'{mnemonic}\t{label}', size, []
            else:
                # Raw bytes - target out of range or invalid
                hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                return f'.db\t{hex_bytes:<16}; {mnemonic} 0x{target:04x}', size, []

        elif operand_fmt == 'A,#data':
            return f'{mnemonic}\ta, #0x{operand_bytes[0]:02x}', size, []

        elif operand_fmt == 'A,direct':
            return f'{mnemonic}\ta, {self.format_direct(operand_bytes[0])}', size, []

        elif operand_fmt == 'direct,A':
            return f'{mnemonic}\t{self.format_direct(operand_bytes[0])}, a', size, []

        elif operand_fmt == 'direct,#data':
            return f'{mnemonic}\t{self.format_direct(operand_bytes[0])}, #0x{operand_bytes[1]:02x}', size, []

        elif operand_fmt == 'direct':
            return f'{mnemonic}\t{self.format_direct(operand_bytes[0])}', size, []

        elif operand_fmt == 'direct,direct':
            # Note: 8051 mov direct,direct has dest first in opcode, src second
            return f'{mnemonic}\t{self.format_direct(operand_bytes[1])}, {self.format_direct(operand_bytes[0])}', size, []

        elif operand_fmt == 'DPTR,#data16':
            val = (operand_bytes[0] << 8) | operand_bytes[1]
            return f'{mnemonic}\tdptr, #0x{val:04x}', size, []

        elif operand_fmt.endswith(',#data'):
            reg = operand_fmt.replace(',#data', '')
            return f'{mnemonic}\t{reg.lower()}, #0x{operand_bytes[0]:02x}', size, []

        elif operand_fmt.endswith(',direct'):
            reg = operand_fmt.replace(',direct', '')
            return f'{mnemonic}\t{reg.lower()}, {self.format_direct(operand_bytes[0])}', size, []

        elif operand_fmt == 'direct,rel':
            # DJNZ direct, rel - must come before generic 'direct,' handler
            rel = operand_bytes[1]
            if rel > 127:
                rel -= 256
            target = addr + size + rel
            self.branch_targets.add(target)
            if self.is_valid_branch_target(target):
                label = self.get_label(target)
                return f'{mnemonic}\t{self.format_direct(operand_bytes[0])}, {label}', size, []
            else:
                hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                return f'.db\t{hex_bytes:<16}; {mnemonic} {self.format_direct(operand_bytes[0])}, 0x{target:04x}', size, []

        elif operand_fmt.startswith('direct,'):
            reg = operand_fmt.replace('direct,', '')
            return f'{mnemonic}\t{self.format_direct(operand_bytes[0])}, {reg.lower()}', size, []

        elif operand_fmt == 'bit,rel':
            bit = operand_bytes[0]
            rel = operand_bytes[1]
            if rel > 127:
                rel -= 256
            target = addr + size + rel
            self.branch_targets.add(target)
            if self.is_valid_branch_target(target):
                label = self.get_label(target)
                return f'{mnemonic}\t{self.format_bit(bit)}, {label}', size, []
            else:
                hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                return f'.db\t{hex_bytes:<16}; {mnemonic} {self.format_bit(bit)}, 0x{target:04x}', size, []

        elif operand_fmt == 'bit,C':
            return f'{mnemonic}\t{self.format_bit(operand_bytes[0])}, c', size, []

        elif operand_fmt == 'C,bit':
            return f'{mnemonic}\tc, {self.format_bit(operand_bytes[0])}', size, []

        elif operand_fmt == 'C,/bit':
            return f'{mnemonic}\tc, /{self.format_bit(operand_bytes[0])}', size, []

        elif operand_fmt == 'bit':
            return f'{mnemonic}\t{self.format_bit(operand_bytes[0])}', size, []

        elif operand_fmt.endswith(',rel'):
            # CJNE and DJNZ variants
            parts = operand_fmt.split(',')
            if len(parts) == 3:
                # CJNE format: reg, #data, rel or reg, direct, rel
                reg = parts[0]
                if '#data' in operand_fmt:
                    imm = operand_bytes[0]
                    rel = operand_bytes[1]
                    if rel > 127:
                        rel -= 256
                    target = addr + size + rel
                    self.branch_targets.add(target)
                    if self.is_valid_branch_target(target):
                        label = self.get_label(target)
                        return f'{mnemonic}\t{reg.lower()}, #0x{imm:02x}, {label}', size, []
                    else:
                        hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                        return f'.db\t{hex_bytes:<16}; {mnemonic} {reg.lower()}, #0x{imm:02x}, 0x{target:04x}', size, []
                else:
                    direct = operand_bytes[0]
                    rel = operand_bytes[1]
                    if rel > 127:
                        rel -= 256
                    target = addr + size + rel
                    self.branch_targets.add(target)
                    if self.is_valid_branch_target(target):
                        label = self.get_label(target)
                        return f'{mnemonic}\t{reg.lower()}, {self.format_direct(direct)}, {label}', size, []
                    else:
                        hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                        return f'.db\t{hex_bytes:<16}; {mnemonic} {reg.lower()}, {self.format_direct(direct)}, 0x{target:04x}', size, []
            else:
                # DJNZ Rn, rel
                reg = parts[0]
                rel = operand_bytes[0]
                if rel > 127:
                    rel -= 256
                target = addr + size + rel
                self.branch_targets.add(target)
                if self.is_valid_branch_target(target):
                    label = self.get_label(target)
                    return f'{mnemonic}\t{reg.lower()}, {label}', size, []
                else:
                    hex_bytes = ', '.join(f'0x{self.data[offset+i]:02x}' for i in range(size))
                    return f'.db\t{hex_bytes:<16}; {mnemonic} {reg.lower()}, 0x{target:04x}', size, []

        # Simple operand formats - convert to lowercase and ensure comma spacing
        operands = operand_fmt.lower()
        # Ensure consistent comma spacing: ", " after each comma
        operands = operands.replace(',', ', ').replace(',  ', ', ')

        return f'{mnemonic}\t{operands}', size, []

    def get_label(self, addr):
        """Get label for address, generating one if needed."""
        if addr in self.labels:
            return self.labels[addr][0] if isinstance(self.labels[addr], tuple) else self.labels[addr]
        return f'L_{addr:04x}' if addr < 0x10000 else f'L_{addr:05x}'

    def first_pass(self):
        """First pass: identify all branch targets."""
        offset = 0
        while offset < len(self.data):
            _, size, _ = self.disassemble_instruction(offset)
            offset += size

    def disassemble(self):
        """Disassemble the entire data section."""
        # First pass to find all targets
        self.first_pass()

        # Generate labels for branch targets
        for target in sorted(self.branch_targets | self.call_targets):
            if target not in self.labels:
                if target in self.call_targets:
                    self.labels[target] = f'func_{target:04x}'
                else:
                    self.labels[target] = f'L_{target:04x}'

        # Second pass: generate assembly
        lines = []
        offset = 0

        while offset < len(self.data):
            addr = self.base_addr + offset

            # Add label if this address is a target
            if addr in self.labels:
                label = self.labels[addr]
                if isinstance(label, tuple):
                    label = label[0]
                lines.append(f'\n{label}:')

            instr, size, raw_bytes = self.disassemble_instruction(offset)

            if instr:
                # Format: instruction with hex comment
                hex_bytes = ' '.join(f'{self.data[offset+i]:02x}' for i in range(size))
                lines.append(f'\t{instr:<32}; {addr:04x}: {hex_bytes}')
            else:
                # Unknown instruction - emit as .db
                b = self.data[offset]
                lines.append(f'\t.db\t#0x{b:02x}\t\t\t\t; {addr:04x}: ???')

            offset += size

        return lines


def disassemble_region(data, start_addr, end_addr, base_addr, labels=None):
    """Disassemble a region of code."""
    region_data = data[start_addr:end_addr]
    disasm = Disassembler(region_data, base_addr, labels)
    return disasm.disassemble(), disasm.labels


if __name__ == '__main__':
    import sys

    if len(sys.argv) < 2:
        print("Usage: disasm8051.py <firmware.bin> [start] [end]")
        sys.exit(1)

    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    end = int(sys.argv[3], 0) if len(sys.argv) > 3 else len(data)

    lines, labels = disassemble_region(data, start, end, start)
    for line in lines:
        print(line)
