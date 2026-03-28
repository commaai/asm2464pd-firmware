#!/usr/bin/env python3
"""Test removing each write from bringup one at a time. Flash firmware first, then run this."""
import ctypes, sys, time, subprocess
from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb

SUPPORTED_DEVICES = [(0xADD1, 0x0001), (0x174C, 0x2463), (0x174C, 0x2464)]

class ASM2464PD:
    def __init__(self):
        self.dev = None
    def open(self):
        for vid, pid in SUPPORTED_DEVICES:
            try:
                self.dev = USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, use_bot=True)
                return
            except RuntimeError: pass
        raise RuntimeError("No device found")
    def close(self):
        if self.dev:
            libusb.libusb_release_interface(self.dev.handle, 0)
            libusb.libusb_close(self.dev.handle)
    def read8(self, addr):
        buf = (ctypes.c_ubyte * 1)()
        ret = libusb.libusb_control_transfer(self.dev.handle, 0xC0, 0xE4, addr, 0, buf, 1, 1000)
        if ret < 0: return None
        return buf[0]
    def write(self, addr, val):
        return libusb.libusb_control_transfer(self.dev.handle, 0x40, 0xE5, addr, val, None, 0, 1000)
    def set_bits(self, addr, mask):
        v = self.read8(addr)
        if v is not None: self.write(addr, v | mask)
    def clear_bits(self, addr, mask):
        v = self.read8(addr)
        if v is not None: self.write(addr, v & ~mask & 0xFF)
    def bank1_or_bits(self, addr, mask):
        buf = (ctypes.c_ubyte * 1)()
        libusb.libusb_control_transfer(self.dev.handle, 0xC0, 0xE4, addr, 1 << 8, buf, 1, 1000)
        libusb.libusb_control_transfer(self.dev.handle, 0x40, 0xE5, addr, (buf[0] | mask) | (1 << 8), None, 0, 1000)

def reset_device():
    subprocess.run(["python3", "ftdi_debug.py", "-rn"], capture_output=True, timeout=10)
    time.sleep(3)  # wait for USB re-enum

def run_bringup(dev, skip=None):
    """Run all steps, skipping one by index."""
    steps = []

    # === Power ===
    steps.append(("C656 3.3V", lambda: dev.set_bits(0xC656, 0x20)))
    steps.append(("C65B PHY mode", lambda: dev.set_bits(0xC65B, 0x20)))
    steps.append(("C659 12V", lambda: dev.set_bits(0xC659, 0x01)))

    # === PERST CfgWr0 ===
    steps.append(("B455=0x02 clear", lambda: dev.write(0xB455, 0x02)))
    steps.append(("B455=0x04 arm", lambda: dev.write(0xB455, 0x04)))
    steps.append(("B2D5=0x01 cfg route", lambda: dev.write(0xB2D5, 0x01)))
    steps.append(("B296=0x08 reset TLP", lambda: dev.write(0xB296, 0x08)))
    steps.append(("B210-B21B clear", lambda: [dev.write(0xB210+i, 0x00) for i in range(12)]))
    steps.append(("B210=0x40 CfgWr0", lambda: dev.write(0xB210, 0x40)))
    steps.append(("B213=0x01 TLP ctrl", lambda: dev.write(0xB213, 0x01)))
    steps.append(("B217=0x0F byte en", lambda: dev.write(0xB217, 0x0F)))
    steps.append(("B216=0x20 TLP len", lambda: dev.write(0xB216, 0x20)))
    steps.append(("B218=0x00 addr0", lambda: dev.write(0xB218, 0x00)))
    steps.append(("B219=0xD0 addr1", lambda: dev.write(0xB219, 0xD0)))
    steps.append(("B21A=0x00 addr2", lambda: dev.write(0xB21A, 0x00)))
    steps.append(("B21B=0x14 addr3", lambda: dev.write(0xB21B, 0x14)))
    steps.append(("B220=0x00 data0", lambda: dev.write(0xB220, 0x00)))
    steps.append(("B221=0x46 data1", lambda: dev.write(0xB221, 0x46)))
    steps.append(("B222=0x40 data2", lambda: dev.write(0xB222, 0x40)))
    steps.append(("B223=0x01 data3", lambda: dev.write(0xB223, 0x01)))
    steps.append(("B296=0x01 clr err", lambda: dev.write(0xB296, 0x01)))
    steps.append(("B296=0x02 clr cpl", lambda: dev.write(0xB296, 0x02)))
    steps.append(("B296=0x04 arm bsy", lambda: dev.write(0xB296, 0x04)))
    steps.append(("B254=0x0F trigger", lambda: dev.write(0xB254, 0x0F)))
    def _poll_busy():
        for _ in range(100):
            v = dev.read8(0xB296)
            if v is not None and v & 0x04: break
            time.sleep(0.001)
        dev.write(0xB296, 0x04)
    steps.append(("B296 poll+clear", lambda: _poll_busy()))
    steps.append(("B480 PERST deassert", lambda: dev.clear_bits(0xB480, 0x01)))

    # === Gen3 retraining ===
    steps.append(("CA06 Gen3 mode", lambda: dev.write(0xCA06, (dev.read8(0xCA06) & 0x1F) | 0x20)))
    steps.append(("B403 tunnel enable", lambda: dev.set_bits(0xB403, 0x01)))
    # lane_config inlined
    steps.append(("B402 clear bit1", lambda: dev.clear_bits(0xB402, 0x02)))
    steps.append(("B402 clear bit1 dup", lambda: dev.clear_bits(0xB402, 0x02)))
    def _lane_steps():
        current = dev.read8(0xB434) & 0x0F
        counter = 0x01
        for _ in range(4):
            if current == 0x0C: break
            new = current & (0x0C | (counter ^ 0x0F))
            current = new
            b434 = dev.read8(0xB434)
            dev.write(0xB434, new | (b434 & 0xF0))
            time.sleep(0.01)
            counter = (counter << 1) & 0xFF
    steps.append(("B434 lane reconfig", lambda: _lane_steps()))
    steps.append(("B401 pulse set", lambda: dev.set_bits(0xB401, 0x01)))
    steps.append(("B401 pulse clear", lambda: dev.write(0xB401, dev.read8(0xB401) & 0xFE)))
    def _b436_lo():
        b436 = dev.read8(0xB436)
        dev.write(0xB436, (b436 & 0xF0) | (0x0C & 0x0E))
    steps.append(("B436 low nibble", lambda: _b436_lo()))
    def _b436_hi():
        b404 = dev.read8(0xB404)
        upper = ((b404 & 0x0F) ^ 0x0F) << 4
        b436 = dev.read8(0xB436)
        dev.write(0xB436, (b436 & 0x0F) | (upper & 0xF0))
    steps.append(("B436 high nibble", lambda: _b436_hi()))
    steps.append(("E710 lanes for EQ", lambda: dev.write(0xE710, (dev.read8(0xE710) & 0xE0) | 0x1F)))
    steps.append(("E751=0x01 EQ mode", lambda: dev.write(0xE751, 0x01)))
    steps.append(("E764 set bit3", lambda: dev.write(0xE764, (dev.read8(0xE764) & 0xF7) | 0x08)))
    steps.append(("E764 clear bit2", lambda: dev.write(0xE764, dev.read8(0xE764) & 0xFB)))
    steps.append(("E764 clear bit0", lambda: dev.write(0xE764, dev.read8(0xE764) & 0xFE)))
    steps.append(("E764 set bit1", lambda: dev.write(0xE764, (dev.read8(0xE764) & 0xFD) | 0x02)))
    def _poll_rxpll():
        for _ in range(200):
            v = dev.read8(0xE762)
            if v is not None and v & 0x10: break
            time.sleep(0.01)
    steps.append(("RXPLL poll", lambda: _poll_rxpll()))

    # === Post-train ===
    steps.append(("B430 clear tunnel", lambda: dev.clear_bits(0xB430, 0x01)))
    steps.append(("bank1 6025 route", lambda: dev.bank1_or_bits(0x6025, 0x80)))
    steps.append(("B455=0x02 clear2", lambda: dev.write(0xB455, 0x02)))
    steps.append(("B455=0x04 arm2", lambda: dev.write(0xB455, 0x04)))
    steps.append(("B2D5=0x01 route2", lambda: dev.write(0xB2D5, 0x01)))
    steps.append(("B296=0x08 reset2", lambda: dev.write(0xB296, 0x08)))
    def _poll_detect():
        for _ in range(200):
            v = dev.read8(0xB455)
            if v is not None and v & 0x02:
                dev.write(0xB455, 0x02); break
            time.sleep(0.005)
    steps.append(("B455 poll detect", lambda: _poll_detect()))

    for i, (label, fn) in enumerate(steps):
        if skip is not None and i == skip:
            continue
        try:
            fn()
        except Exception:
            pass

    return steps

def check_probe(dev):
    """Quick probe: read VID at bus 0, then bus 4."""
    try:
        r = subprocess.run(["python3", "pcie/pcie_probe.py"], capture_output=True, timeout=15)
        return r.returncode == 0
    except:
        return False

def main():
    # First run: baseline
    print("=== BASELINE ===")
    reset_device()
    dev = ASM2464PD(); dev.open()
    run_bringup(dev, skip=None)
    ltssm = dev.read8(0xB450)
    dev.close()
    probe_ok = check_probe(None) if ltssm in (0x48, 0x78) else False
    print(f"  LTSSM=0x{ltssm:02X} probe={'PASS' if probe_ok else 'FAIL'}")
    if not probe_ok:
        print("BASELINE FAILED, aborting")
        sys.exit(1)

    # Get step count
    dev2 = ASM2464PD()
    reset_device()
    dev2.open()
    steps = run_bringup(dev2, skip=None)
    dev2.close()
    n = len(steps)
    step_names = [s[0] for s in steps]

    results = []
    for i in range(n):
        print(f"[{i:2d}/{n}] Skip '{step_names[i]}'...", end=" ", flush=True)
        reset_device()
        try:
            dev = ASM2464PD(); dev.open()
            run_bringup(dev, skip=i)
            ltssm = dev.read8(0xB450)
            dev.close()
            if ltssm not in (0x48, 0x78):
                print(f"LTSSM=0x{ltssm:02X} FAIL (link down)")
                results.append((i, step_names[i], "FAIL-LINK"))
                continue
            probe_ok = check_probe(None)
            status = "PASS" if probe_ok else "FAIL-PROBE"
            print(f"LTSSM=0x{ltssm:02X} probe={status}")
            results.append((i, step_names[i], status))
        except Exception as e:
            print(f"ERROR: {e}")
            results.append((i, step_names[i], f"ERROR"))

    print("\n========== SUMMARY ==========")
    for i, name, status in results:
        removable = "  <<< CAN REMOVE" if status == "PASS" else ""
        print(f"  [{i:2d}] {name:25s} {status}{removable}")

if __name__ == "__main__":
    main()
