# USB Development Notes for Clean Firmware

## Current Status (2026-02-03 Session 17): Investigating Phase Handling

### Key Discovery from Original Firmware Disassembly

The control transfer phases are managed via register 0x9091:
- **Bit 0**: SETUP phase pending (new control request)  
- **Bit 1**: DATA IN phase (send data to host)
- **Bit 2**: STATUS phase (transfer complete)
- **Bit 3**: Phase 0x08 (unknown)
- **Bit 4**: Phase 0x10 (unknown)

**Critical**: After handling each phase, the firmware writes back the bit value to 0x9091 to acknowledge!

The phase handler at 0xcde7-0xce3c shows:
1. Check 0x9091 bit 0 - if set AND bit 2 NOT set, handle SETUP
2. Check 0x9002 bit 1 - some USB config check
3. Check 0x9091 bit 1 - if set, call DATA handler (0xd088), write 0x02
4. Check 0x9091 bit 2 - if set, call STATUS handler (0xdcd5), write 0x04
5. etc.

### Current Firmware v6 Observations
- Receiving SETUP packets correctly
- Lots of USB interrupts (U's)  
- Not seeing DATA IN phase trigger (bit 1)
- Device not enumerating

### Hypothesis
The DATA IN phase (bit 1) may not be set automatically - the firmware might need to explicitly transition phases after writing descriptor data.

---

## Previous Status (Session 16): EP0 IN TX Still Not Working

### What Works
1. **Device visible on USB bus** - Host sees device after our init sequence
2. **USB ISR fires** - C802=0x01 triggers INT0, we print 'U'
3. **Setup packet detection** - Using 0x9091 bit 0 edge detection
4. **Setup packet reading** - Reading 8 bytes from 0x9104-0x910B (NOT 0x9E00!)
5. **GET_DESCRIPTOR parsing** - We correctly parse: `80 06 00 01 00 00 40 00`
6. **Descriptor data copied** - Written to 0x9E00 buffer
7. **Original firmware verified working** - Enumerates as ADD1:0001 "USB 3.2 PCIe TinyEnclosure"

### What Doesn't Work
- **EP0 IN data never reaches host** - Device times out, host gives up
- **Device shows as bootloader** - VID/PID 174c:2463 "AS2462" (not our ADD1:0001)

### Latest Debug Output
```
=== EP0 v4 ===
90E0:00 9100:03 9000:00 9002:3F
Set 9000, readback: 01
OK
....U.....
SETUP:80060001 00004000
D...
```

### Key Observations
1. **90E0 (USB speed) = 0x00** - Shows Full Speed, but link is SuperSpeed (0x9100=0x03)
2. **9000 (USB status) = 0x00** - USB not active at start, even after we write 0x81
3. **Register 9000 write takes effect** - Readback shows 0x01 immediately after write
4. **Something clears 9000** - By the time we check status registers, it's 0x00 again

### Root Cause Analysis

The bootloader (in ROM) handles the initial USB enumeration at SuperSpeed:
1. Device connects, bootloader enumerates as 174c:2463
2. Host resets device
3. Our firmware starts, sees GET_DESCRIPTOR setup packet
4. We try to respond, but USB hardware isn't in correct state

The hardware may require specific init sequence that the bootloader uses but we don't replicate.

---

## Key Register Mappings

### Setup Packet Location (CRITICAL!)
```
Setup packets are at 0x9104-0x910B, NOT 0x9E00!

0x9104 = bmRequestType
0x9105 = bRequest
0x9106 = wValue low
0x9107 = wValue high  
0x9108 = wIndex low
0x9109 = wIndex high
0x910A = wLength low
0x910B = wLength high
```

### EP0 Data Buffer
```
0x9E00+ = EP0 IN data buffer (firmware writes descriptor here)
```

### Key Control Registers
| Register | Purpose |
|----------|---------|
| 0x9000   | USB status - bit 0 = active, bit 7 = connected |
| 0x9002   | USB config (0x3F after init) |
| 0x9091   | USB control phase - bit 0 indicates new setup packet |
| 0x9092   | DMA trigger - 0x01=send data, 0x02=STALL |
| 0x9003   | EP0 status (clear before send) |
| 0x9004   | EP0 transfer length |
| 0x905B/905C | DMA source address high/low |
| 0x905F   | EP control - clear bit 0 before send |
| 0x905D   | EP control - clear bit 0 before send |
| 0x90E0   | USB speed mode (0=Full, 1=High, 2=Super, 3=Super+) |
| 0x90E3   | EP status trigger (write 0x01) |
| 0x90A0   | Control trigger (write 0x01) |
| 0x9100   | USB link status (speed) |
| 0x924C   | USB PHY - set bit 0 to activate |

---

## 0xbd25 Function Analysis

The original firmware calls `0xbd25` before sending descriptors. Full disassembly:

```
0xbd25: mov r5, @0x07           ; Save parameter
        lcall 0xc16c            ; Clear USB state variables
        mov r7, #0x01
        lcall 0xcf3d            ; USB endpoint mask configuration
        
        ; C428: clear bit 2
        mov dptr, #0xc428
        movx a, @dptr
        anl a, #0xfb
        movx @dptr, a
        
        mov dptr, #0xc473
        lcall 0xbbc6            ; Clear bit 2, set bit 2
        lcall 0xbc00            ; Clear bit 1, set bit 1
        
        ; C428: clear bit 5, set bit 5
        mov dptr, #0xc428
        movx a, @dptr
        anl a, #0xdf
        orl a, #0x20
        movx @dptr, a
        
        mov dptr, #0xc473
        lcall 0xbbd6            ; Clear bit 3, C472 &= 0xFD
        lcall 0xbb9f            ; Write 0xFF to dptr+0..3
        
        mov dptr, #0xc473
        lcall 0xbb8f            ; Clear bit 4, C472 &= 0xFB, C438-C43B = 0xFF
        
        mov dptr, #0xc42a
        lcall 0xbbc0            ; Clear/set bit 1,2
        lcall 0xbc11            ; Clear bit 3, set bit 3
        
        ; C42A: clear bits 1,2,3
        movx a, @dptr
        anl a, #0xfd
        movx @dptr, a
        movx a, @dptr
        anl a, #0xfb
        movx @dptr, a
        movx a, @dptr
        anl a, #0xf7
        movx @dptr, a
        
        ; C471 = 0x01
        mov dptr, #0xc471
        mov a, #0x01
        movx @dptr, a
        
        ; C472: clear bit 0
        inc dptr
        movx a, @dptr
        anl a, #0xfe
        movx @dptr, a
        
        ; C42A: clear bit 4, set bit 4, clear bit 4
        mov dptr, #0xc42a
        movx a, @dptr
        anl a, #0xef
        orl a, #0x10
        movx @dptr, a
        movx a, @dptr
        anl a, #0xef
        movx @dptr, a
        
        ; If parameter == 0: clear 900B bits, clear 9000 bit 0, clear 924C bit 0
        mov a, r5
        jnz 0xbda3
        
        mov dptr, #0x900b
        lcall 0xbbc0
        ...clear bits...
        
        mov dptr, #0x9000
        movx a, @dptr
        anl a, #0xfe
        movx @dptr, a
        
        mov dptr, #0x924c
        movx a, @dptr
        anl a, #0xfe
        movx @dptr, a
        
        ret
```

### Helper Functions

**0xbbc6** - Read @dptr, clear bit 2, set bit 2
**0xbc00** - Read @dptr, clear bit 1, set bit 1
**0xbc11** - Read @dptr, clear bit 3, set bit 3
**0xbbc0** - Read @dptr, clear bit 1, set bit 1; clear bit 2, set bit 2
**0xbbd6** - Clear bit 3 @dptr; C472 &= 0xFD
**0xbb8f** - Clear bit 4 @dptr; C472 &= 0xFB; C438-C43B = 0xFF
**0xbb9f** - Write A to dptr+0,+1,+2,+3

---

## 0x9092 DMA Trigger Values

| Value | Purpose | Source |
|-------|---------|--------|
| 0x01  | Normal EP0 IN data send | 0xa57a, 0xb33c |
| 0x02  | STALL response | 0xa4ed (sets 0x9002 bit 1 first) |
| 0x04  | Used when state==0x03 | 0xb2e4, 0xb96e |
| 0x08  | Another mode | 0xb6a9 |

---

## USB PHY Initialization

### Critical: 91C0 Sequence
Original firmware uses: `0x03 -> 0x02 -> 0x00`
```c
XDATA8(0x91C0) = 0x03;  // Set bits 0,1
XDATA8(0x91C0) = 0x02;  // Clear bit 0, keep bit 1  
XDATA8(0x91C0) = 0x00;  // Clear all - triggers USB visibility
```

### Current Init Sequence
```c
// Interrupt controller
XDATA8(0xC800) = 0x05;
XDATA8(0xC801) = 0x10;
XDATA8(0xC807) = 0x04;
XDATA8(0xC809) = 0x20;
XDATA8(0xC8A1) = 0x80;
XDATA8(0xC8A4) = 0x80;

// USB core
XDATA8(0x92C0) = 0x81;  // Power enable
XDATA8(0x9301) = 0xC0;  // EP0 arm
XDATA8(0x9091) = 0x1F;
XDATA8(0x91C1) = 0xF0;
XDATA8(0x9002) = 0xE0;
XDATA8(0x9241) = 0xD0;  // PHY config
XDATA8(0x9000) = 0x81;  // USB active (but gets cleared!)

// Interrupt masks
XDATA8(0x9096) = 0xFF;  // through 0x909D
XDATA8(0x909E) = 0x03;

// PHY enable sequence
XDATA8(0x91C0) = 0x03;
XDATA8(0x91C0) = 0x02;
XDATA8(0x91C0) = 0x00;

// 8051 interrupts
TCON = 0x05;  // Edge triggered
IE = 0x85;    // EA + EX0 + EX1
```

---

## Useful Commands

```bash
# Build and flash
cd clean && make clean && make && make flash

# Check USB status
sudo dmesg | tail -20 | grep -iE "usb|error"
lsusb | grep -iE "174c|add1"

# Flash original firmware to verify hardware works  
USBDEV="174C:2463" python3 flash.py fw.bin && python3 ftdi_debug.py -rt 3

# Disassembly
r2 -a 8051 -q -c 'pd 50 @ 0xb343' fw.bin   # GET_DESCRIPTOR handler
r2 -a 8051 -q -c 'pd 100 @ 0xbd25' fw.bin  # EP0 IN prep function
r2 -a 8051 -q -c 'pd 30 @ 0xa513' fw.bin   # Trigger sequence
r2 -a 8051 -q -c 'pd 50 @ 0x0e33' fw.bin   # ISR entry
```

---

## Next Steps

1. **Try using the emulator** - Trace original firmware handling GET_DESCRIPTOR
2. **Compare register sequences** - Watch what original firmware writes to MMIO
3. **Check if USB 3.0 needs different handling** - Speed register shows 0 (Full) but link is 3 (Super+)
4. **Investigate why 9000 gets cleared** - Hardware might require different init order

---

## Session History

### Session 16 (Current)
- Implemented 0xbd25 helper functions
- Added detailed debug output
- Confirmed original firmware works (ADD1:0001 enumerates)
- Found 90E0=0x00 (Full Speed) but 9100=0x03 (SuperSpeed+) - mismatch
- 9000 gets cleared by hardware before we can use it

### Session 15
- Confirmed setup packet at 0x9104-0x910B
- Confirmed data buffer at 0x9E00
- Traced 91C0 sequence: 0x03 -> 0x02 -> 0x00
- Device visible, ISR fires, setup detected, but TX fails

### Session 14
- Found 0x9092 values: 0x01=send, 0x02=STALL
- Traced 0xa513 trigger sequence
- Found 0xbd25 is complex prep function

### Earlier Sessions
- Established USB init sequence
- Found interrupt controller config (C8Ax)
- Confirmed 8051 external interrupts work
