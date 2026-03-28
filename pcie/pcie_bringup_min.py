#!/usr/bin/env python3
"""Minimal PCIe bringup for ASM2464PD — 42 writes."""

import ctypes, sys, time
from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb

SUPPORTED_DEVICES = [(0xADD1, 0x0001), (0x174C, 0x2463), (0x174C, 0x2464)]

class ASM2464PD:
    def __init__(self):
        self.dev = None
        self._wc = 0
        self._rc = 0
    def open(self):
        for vid, pid in SUPPORTED_DEVICES:
            try:
                self.dev = USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, use_bot=True)
                print(f"Opened {vid:04X}:{pid:04X}")
                return
            except RuntimeError: pass
        raise RuntimeError("No device found")
    def close(self):
        if self.dev:
            libusb.libusb_release_interface(self.dev.handle, 0)
            libusb.libusb_close(self.dev.handle)
    def read8(self, addr):
        self._rc += 1
        buf = (ctypes.c_ubyte * 1)()
        ret = libusb.libusb_control_transfer(self.dev.handle, 0xC0, 0xE4, addr, 0, buf, 1, 1000)
        assert ret >= 0, f"read 0x{addr:04X} failed: {ret}"
        return buf[0]
    def write(self, addr, val):
        self._wc += 1
        ret = libusb.libusb_control_transfer(self.dev.handle, 0x40, 0xE5, addr, val, None, 0, 1000)
        assert ret >= 0, f"write 0x{addr:04X}=0x{val:02X} failed: {ret}"
    def set_bits(self, addr, mask):
        self.write(addr, self.read8(addr) | mask)
    def clear_bits(self, addr, mask):
        self.write(addr, self.read8(addr) & ~mask & 0xFF)
    def bank1_or_bits(self, addr, mask):
        self._rc += 1
        buf = (ctypes.c_ubyte * 1)()
        libusb.libusb_control_transfer(self.dev.handle, 0xC0, 0xE4, addr, 1 << 8, buf, 1, 1000)
        self._wc += 1
        libusb.libusb_control_transfer(self.dev.handle, 0x40, 0xE5, addr, (buf[0] | mask) | (1 << 8), None, 0, 1000)


def main():
    dev = ASM2464PD()
    dev.open()
    try:
        # === Power: enable 3.3V and 12V ===
        dev.set_bits(0xC656, 0x20)                       # REG_HDDPC_CTRL: enable PCIE_3V3 (bit 5)
        dev.set_bits(0xC659, 0x01)                       # REG_PCIE_LANE_CTRL: enable 12V (bit 0)

        # === CfgWr0 TLP to 0x00D00014 (link speed config, required for TLP routing) ===
        dev.write(0xB296, 0x08)                          # REG_PCIE_STATUS: reset TLP engine
        for i in range(12): dev.write(0xB210+i, 0x00)   # clear TLP header window (12 writes)
        dev.write(0xB210, 0x40)                          # REG_PCIE_FMT_TYPE: CfgWr0
        dev.write(0xB213, 0x01)                          # REG_PCIE_TLP_CTRL: 1 DW
        dev.write(0xB217, 0x0F)                          # REG_PCIE_BYTE_EN: all 4 bytes enabled
        dev.write(0xB216, 0x20)                          # REG_PCIE_TLP_LENGTH
        dev.write(0xB218, 0x00)                          # REG_PCIE_ADDR_0: addr = 0x1400D000
        dev.write(0xB219, 0xD0)                          # REG_PCIE_ADDR_1
        dev.write(0xB21A, 0x00)                          # REG_PCIE_ADDR_2
        dev.write(0xB21B, 0x14)                          # REG_PCIE_ADDR_3
        dev.write(0xB220, 0x00)                          # REG_PCIE_DATA_0: data = 0x01404600
        dev.write(0xB221, 0x46)                          # REG_PCIE_DATA_1
        dev.write(0xB222, 0x40)                          # REG_PCIE_DATA_2
        dev.write(0xB223, 0x01)                          # REG_PCIE_DATA_3
        dev.write(0xB296, 0x01)                          # REG_PCIE_STATUS: clear error flag
        dev.write(0xB296, 0x02)                          # REG_PCIE_STATUS: clear completion flag
        dev.write(0xB296, 0x04)                          # REG_PCIE_STATUS: arm busy flag
        dev.write(0xB254, 0x0F)                          # REG_PCIE_TRIGGER: execute TLP
        for _ in range(100):                             # poll for TLP completion
            if dev.read8(0xB296) & 0x04: break
            time.sleep(0.001)
        dev.write(0xB296, 0x04)                          # clear busy flag

        # === Deassert PERST# ===
        dev.clear_bits(0xB480, 0x01)                     # REG_PCIE_PERST_CTRL: release device from reset

        # === Gen3 link training ===
        dev.set_bits(0xB403, 0x01)                       # REG_TUNNEL_CTRL_B403: enable tunnel
        dev.write(0xE764, (dev.read8(0xE764) & 0xF7) | 0x08)  # REG_PHY_TIMER_CTRL: set bit 3 (training prep)
        dev.write(0xE764, (dev.read8(0xE764) & 0xFD) | 0x02)  # REG_PHY_TIMER_CTRL: set bit 1 (start training)
        for _ in range(200):                             # poll RXPLL for link training completion
            if dev.read8(0xE762) & 0x10: break           # REG_PHY_RXPLL_STATUS bit 4 = trained
            time.sleep(0.01)

        # === Post-train: enable TLP forwarding ===
        dev.clear_bits(0xB430, 0x01)                     # REG_TUNNEL_LINK_STATE: clear link-up bit
        dev.bank1_or_bits(0x6025, 0x80)                  # bank1 0x6025 bit 7: TLP routing enable
        dev.write(0xB455, 0x02)                          # REG_PCIE_LTSSM_B455: clear link detect flag
        dev.write(0xB455, 0x04)                          # REG_PCIE_LTSSM_B455: arm link detect
        dev.write(0xB2D5, 0x01)                          # REG_PCIE_CTRL_B2D5: enable config routing
        dev.write(0xB296, 0x08)                          # REG_PCIE_STATUS: reset TLP engine
        for _ in range(200):                             # poll for downstream device detection
            if dev.read8(0xB455) & 0x02:
                dev.write(0xB455, 0x02); break
            time.sleep(0.005)

        # === Status (reads don't count) ===
        print(f"\nTotal: {dev._wc} writes, {dev._rc} reads")
        ltssm = dev.read8(0xB450)
        print(f"\n=== PCIe Status ===")
        print(f"  LTSSM state (B450): 0x{ltssm:02X}  {'(L0!)' if ltssm in (0x48,0x78) else ''}")
        print(f"  Link width  (B22B): 0x{dev.read8(0xB22B):02X}")
        print(f"  Lane enable (B434): 0x{dev.read8(0xB434):02X}")
        print(f"  Link detect (B455): 0x{dev.read8(0xB455):02X}")
        print(f"  Link width  (E710): 0x{dev.read8(0xE710):02X}")
        print(f"  CPU mode    (CC30): 0x{dev.read8(0xCC30):02X}")
        print(f"  CPU next    (CA06): 0x{dev.read8(0xCA06):02X}")
        print(f"  PERST ctrl  (B480): 0x{dev.read8(0xB480):02X}  {'(asserted)' if dev.read8(0xB480) & 0x01 else '(deasserted)'}")
        print(f"  12V enable  (C659): 0x{dev.read8(0xC659):02X}  {'(on)' if dev.read8(0xC659) & 0x01 else '(off)'}")
        print(f"  RXPLL       (E762): 0x{dev.read8(0xE762):02X}")
        print(f"  PHY timer   (E764): 0x{dev.read8(0xE764):02X}")
        print(f"  Tunnel link (B430): 0x{dev.read8(0xB430):02X}")
        print(f"  Tunnel ctrl (B403): 0x{dev.read8(0xB403):02X}")
        print(f"  Link status: {'UP' if ltssm in (0x48,0x78) else 'DOWN'}")
        sys.exit(0 if ltssm in (0x48, 0x78) else 1)
    finally:
        dev.close()

if __name__ == "__main__":
    main()
