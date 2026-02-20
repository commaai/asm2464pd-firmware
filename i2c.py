#!/usr/bin/env python3
"""
I2C bit-bang via USB E4/E5 vendor commands on ASM2464PD.

Uses GPIO9 (SCL, pin E1) and GPIO10 (SDA, pin F1) to bit-bang I2C.
Zero added delays -- USB round-trip latency only (avoids INA231 SMBus timeout).

INA231 (U5) at address 0x45 monitors the +12V ATX rail.
  Shunt: 2x 1mOhm parallel = 0.5mOhm (R15, R16)

Usage:
  python3 i2c.py              # Show +12V voltage and current
  python3 i2c.py scan         # Scan I2C bus
  python3 i2c.py dump         # Dump all INA231 registers
"""
import sys, struct
from tinygrad.runtime.support.usb import USB3

GPIO_SCL, GPIO_SDA, GPIO_INPUT = 0xC629, 0xC62A, 0xC651
SCL_BIT, SDA_BIT = 1, 2
PIN_HI, PIN_LO = 0x00, 0x02  # open-drain: release (pullup) / drive low
INA231_ADDR = 0x45
R_SHUNT = 0.0005  # 0.5mOhm

def _asm(a): return (a & 0x1FFFF) | 0x500000
def xw(dev, a, v): dev.send_batch(cdbs=[struct.pack('>BBBHB9x', 0xE5, v, _asm(a)>>16, _asm(a)&0xFFFF, 0)])
def xr(dev, a):
  r = dev.send_batch(cdbs=[struct.pack('>BBBHB9x', 0xE4, 1, _asm(a)>>16, _asm(a)&0xFFFF, 0)], idata=[1])
  return r[0][0] if r and r[0] else 0

class I2C:
  def __init__(self, dev): self.dev = dev
  def _rd(self): return (xr(self.dev, GPIO_INPUT) >> SDA_BIT) & 1
  def _sh(self): xw(self.dev, GPIO_SCL, PIN_HI)
  def _sl(self): xw(self.dev, GPIO_SCL, PIN_LO)
  def _dh(self): xw(self.dev, GPIO_SDA, PIN_HI)
  def _dl(self): xw(self.dev, GPIO_SDA, PIN_LO)
  def start(self): self._dh(); self._sh(); self._dl(); self._sl()
  def stop(self): self._dl(); self._sh(); self._dh()
  def _wb(self, b):
    for i in range(8):
      (self._dh if (b>>(7-i))&1 else self._dl)(); self._sh(); self._sl()
    self._dh(); self._sh(); ack = self._rd(); self._sl(); return ack == 0
  def _rb(self, ack=True):
    v = 0
    for _ in range(8): self._dh(); self._sh(); v = (v<<1)|self._rd(); self._sl()
    (self._dl if ack else self._dh)(); self._sh(); self._sl(); self._dh(); return v
  def read16(self, addr, reg):
    self.start()
    if not self._wb((addr<<1)|0): self.stop(); return None
    self._wb(reg); self.start()
    if not self._wb((addr<<1)|1): self.stop(); return None
    hi = self._rb(True); lo = self._rb(False); self.stop(); return (hi<<8)|lo
  def ping(self, addr):
    self.start(); ack = self._wb((addr<<1)|0); self.stop(); return ack

def open_dev():
  for vid, pid in [(0xADD1, 0x0001), (0x174C, 0x2464), (0x174C, 0x2463)]:
    try: return USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, max_streams=1)
    except RuntimeError: pass
  raise RuntimeError("No ASM2464PD found")

def read_power(i2c):
  bus = i2c.read16(INA231_ADDR, 0x02)
  shunt = i2c.read16(INA231_ADDR, 0x01)
  if bus is None: print("INA231 not responding"); return
  bus_v = bus * 1.25 / 1000
  shunt_uv = (shunt if shunt < 0x8000 else shunt - 0x10000) * 2.5
  current_a = shunt_uv / 1e6 / R_SHUNT
  print(f"+12V rail:  {bus_v:.3f} V")
  print(f"Current:    {current_a:.3f} A  ({shunt_uv:.1f} uV shunt)")
  print(f"Power:      {bus_v * current_a:.2f} W")
  if bus_v < 1: print("** NO ATX POWER **")
  elif bus_v < 10: print("** LOW VOLTAGE **")

def scan_bus(i2c):
  print("Scanning I2C bus...")
  found = [a for a in range(0x03, 0x78) if i2c.ping(a)]
  print(f"Found {len(found)} device(s): {['0x%02X'%a for a in found]}" if found else "No devices found")
  for a in found:
    mfr = i2c.read16(a, 0xFE)
    print(f"  0x{a:02X}: MfrID=0x{mfr:04X}" if mfr else f"  0x{a:02X}: read failed")

def dump_regs(i2c):
  regs = [(0x00,"Config"), (0x01,"Shunt V"), (0x02,"Bus V"), (0x03,"Power"),
          (0x04,"Current"), (0x05,"Calibration"), (0x06,"Mask/En"), (0x07,"Alert"),
          (0xFE,"Mfr ID"), (0xFF,"Die ID")]
  for reg, name in regs:
    val = i2c.read16(INA231_ADDR, reg)
    extra = ""
    if val is not None:
      if reg == 0x02: extra = f"  ({val*1.25/1000:.3f} V)"
      elif reg == 0x01:
        s = (val if val < 0x8000 else val - 0x10000) * 2.5
        extra = f"  ({s:.1f} uV = {s/1e6/R_SHUNT:.3f} A)"
    print(f"  0x{reg:02X} {name:>12}: 0x{val:04X}{extra}" if val else f"  0x{reg:02X} {name:>12}: NACK")

if __name__ == "__main__":
  dev = open_dev()
  i2c = I2C(dev)
  i2c._dh(); i2c._sh()
  cmd = sys.argv[1] if len(sys.argv) > 1 else "power"
  if cmd == "scan": scan_bus(i2c)
  elif cmd == "dump": dump_regs(i2c)
  else: read_power(i2c)
