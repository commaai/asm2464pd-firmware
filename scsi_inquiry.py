#!/usr/bin/env python3
"""Send SCSI INQUIRY via BOT to stock firmware to trigger PCIe training."""
import usb.core, usb.util, struct, time, sys

dev = usb.core.find(idVendor=0xADD1, idProduct=0x0001)
if dev is None:
    print("Device not found!")
    sys.exit(1)

try:
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
except:
    pass
dev.set_configuration()
usb.util.claim_interface(dev, 0)

tag = 0

def bot_cmd(cdb_bytes, data_len=0, direction_in=True, timeout=5000):
    """Send a BOT command. Returns data for IN, None for OUT."""
    global tag
    tag += 1
    flags = 0x80 if direction_in and data_len > 0 else 0x00
    cbw = struct.pack('<IIIBBB', 0x43425355, tag, data_len, flags, 0, len(cdb_bytes))
    cbw += cdb_bytes + b'\x00' * (16 - len(cdb_bytes))
    dev.write(0x02, cbw, timeout=timeout)
    
    data = None
    if data_len > 0 and direction_in:
        try:
            data = bytes(dev.read(0x81, data_len, timeout=timeout))
        except Exception as e:
            print(f"  Data read error: {e}")
    
    try:
        csw = bytes(dev.read(0x81, 13, timeout=timeout))
        sig, rtag, residue, status = struct.unpack('<IIIB', csw)
        print(f"  CSW: sig={sig:#010x} tag={rtag} residue={residue} status={status}")
        return data
    except Exception as e:
        print(f"  CSW read error: {e}")
        return data

def xdata_read(addr):
    a = (addr & 0x1FFFF) | 0x500000
    cdb = struct.pack('>BBBHB9x', 0xE4, 1, a >> 16, a & 0xFFFF, 0)
    global tag; tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, tag, 1, 0x80, 0, len(cdb))
    cbw += cdb + b'\x00' * (16 - len(cdb))
    dev.write(0x02, cbw, timeout=2000)
    data = dev.read(0x81, 1, timeout=2000)
    csw = dev.read(0x81, 13, timeout=2000)
    return bytes(data)[0]

# Check initial LTSSM
ltssm = xdata_read(0xB450)
print(f"Initial LTSSM: 0x{ltssm:02x}")

if ltssm >= 0x40:
    print("Already trained!")
    sys.exit(0)

# Send SCSI TEST UNIT READY (opcode 0x00)
print("\n=== Sending TEST_UNIT_READY (0x00) ===")
cdb = b'\x00' + b'\x00' * 5  # 6-byte CDB
result = bot_cmd(cdb, data_len=0, direction_in=False)
time.sleep(0.5)
ltssm = xdata_read(0xB450)
print(f"After TUR: LTSSM=0x{ltssm:02x}")

if ltssm >= 0x40:
    print("TRAINED after TUR!")
    sys.exit(0)

# Send SCSI INQUIRY (opcode 0x12)
print("\n=== Sending INQUIRY (0x12) ===")
cdb = struct.pack('>BBHB', 0x12, 0x00, 0x0000, 36) + b'\x00'  # Standard INQUIRY, 36 bytes
result = bot_cmd(cdb, data_len=36, direction_in=True)
if result:
    print(f"  INQUIRY response ({len(result)} bytes): {result.hex()}")
time.sleep(0.5)
ltssm = xdata_read(0xB450)
print(f"After INQUIRY: LTSSM=0x{ltssm:02x}")

if ltssm >= 0x40:
    print("TRAINED after INQUIRY!")
else:
    # Monitor for a bit
    print("\n=== Monitoring LTSSM (5s) ===")
    prev = 0xFF
    for i in range(50):
        ltssm = xdata_read(0xB450)
        if ltssm != prev:
            print(f"  t={i*100}ms: LTSSM=0x{ltssm:02x}")
            prev = ltssm
        if ltssm >= 0x40:
            print("  *** TRAINED! ***")
            break
        time.sleep(0.1)
    else:
        print(f"  Still not trained: LTSSM=0x{ltssm:02x}")

usb.util.release_interface(dev, 0)
