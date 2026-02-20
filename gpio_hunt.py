#!/usr/bin/env python3
"""Systematically toggle each GPIO and check if PCIe link comes up."""

import sys, time, struct, subprocess
from tinygrad.runtime.support.usb import USB3

def reset_device():
  subprocess.run(["./ftdi_debug.py", "-rn"], check=True, capture_output=True)
  time.sleep(5)

def open_device():
  for vid, pid in [(0xADD1, 0x0001), (0x174C, 0x2464), (0x174C, 0x2463)]:
    try: return USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, max_streams=1)
    except RuntimeError: pass
  raise RuntimeError("No device found")

def _asm_addr(addr): return (addr & 0x1FFFF) | 0x500000

def xw(dev, addr, val):
  a = _asm_addr(addr)
  dev.send_batch(cdbs=[struct.pack('>BBBHB9x', 0xE5, val, a >> 16, a & 0xFFFF, 0)])

def xr(dev, addr):
  a = _asm_addr(addr)
  r = dev.send_batch(cdbs=[struct.pack('>BBBHB9x', 0xE4, 1, a >> 16, a & 0xFFFF, 0)], idata=[1])
  return r[0][0] if r and r[0] else 0

def check_link(dev):
  return xr(dev, 0xC520)

def get_baseline(dev):
  """Read all GPIO control regs."""
  return [xr(dev, 0xC620 + i) for i in range(28)]

if __name__ == "__main__":
  gpio_to_test = list(range(28)) if len(sys.argv) < 2 else [int(x) for x in sys.argv[1:]]

  # First, get baseline after a clean reset
  print("=== Getting baseline after clean reset ===")
  reset_device()
  dev = open_device()
  baseline = get_baseline(dev)
  link0 = check_link(dev)
  print(f"Baseline NVMe link: 0x{link0:02X}")
  print(f"GPIO baseline: {' '.join(f'{i}=0x{v:02x}' for i,v in enumerate(baseline))}")

  if link0 != 0:
    print(f"PCIe link is already up (0x{link0:02X})! No GPIO hunting needed.")
    sys.exit(0)

  for gpio_num in gpio_to_test:
    orig = baseline[gpio_num]
    # Skip GPIOs that are INPUT (0x00) - toggling them as output could break things
    # Actually, test everything
    print(f"\n--- Testing GPIO {gpio_num} (baseline=0x{orig:02x}) ---")

    # Reset to clean state
    reset_device()
    dev = open_device()

    # Verify link is still down
    if check_link(dev) != 0:
      print(f"  Link came up after reset! (before toggling)")
      break

    # Strategy 1: If currently HIGH, try cycling LOW->HIGH
    # Strategy 2: If currently LOW, try cycling HIGH->LOW->HIGH  
    # Strategy 3: If other mux, try setting to HIGH then back
    
    # Try: set LOW, wait, set HIGH, check link
    print(f"  Setting GPIO {gpio_num} -> LOW (0x02)")
    xw(dev, 0xC620 + gpio_num, 0x02)
    time.sleep(1)
    
    print(f"  Setting GPIO {gpio_num} -> HIGH (0x03)")
    xw(dev, 0xC620 + gpio_num, 0x03)
    time.sleep(2)
    
    link = check_link(dev)
    print(f"  NVMe link after LOW->HIGH: 0x{link:02X}")
    
    if link != 0:
      print(f"\n*** GPIO {gpio_num} controls PCIe link! ***")
      break

    # Restore original value
    print(f"  Restoring GPIO {gpio_num} -> 0x{orig:02x}")
    xw(dev, 0xC620 + gpio_num, orig)

  else:
    # Try the reverse: HIGH->LOW for each
    print("\n\n=== Phase 2: Try HIGH->LOW (maybe active-low power) ===")
    for gpio_num in gpio_to_test:
      orig = baseline[gpio_num]
      print(f"\n--- Testing GPIO {gpio_num} (baseline=0x{orig:02x}) HIGH->LOW ---")
      
      reset_device()
      dev = open_device()
      
      if check_link(dev) != 0:
        print(f"  Link came up after reset!")
        break

      print(f"  Setting GPIO {gpio_num} -> HIGH (0x03)")
      xw(dev, 0xC620 + gpio_num, 0x03)
      time.sleep(1)
      
      print(f"  Setting GPIO {gpio_num} -> LOW (0x02)")
      xw(dev, 0xC620 + gpio_num, 0x02)
      time.sleep(2)
      
      link = check_link(dev)
      print(f"  NVMe link after HIGH->LOW: 0x{link:02X}")
      
      if link != 0:
        print(f"\n*** GPIO {gpio_num} (active-low) controls PCIe link! ***")
        break

      # Restore
      xw(dev, 0xC620 + gpio_num, orig)
