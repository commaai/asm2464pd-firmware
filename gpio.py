#!/usr/bin/env python3
"""
GPIO control for ASM2464PD via USB E4/E5 SCSI commands.

GPIO registers: 0xC620 + gpio_num (control), 0xC650 + gpio_num//8 (input state)
  Control values: 0x00=input, 0x02=output low, 0x03=output high

Usage:
  ./gpio.py                      # Scan all GPIO states
  ./gpio.py set <n> high         # Set GPIO n high
  ./gpio.py set <n> low          # Set GPIO n low
  ./gpio.py set <n> input        # Set GPIO n as input
  ./gpio.py cycle <n> [delay]    # Toggle GPIO n low then high (power cycle), delay in seconds (default 1)
  ./gpio.py scan                 # Read control register for all 28 GPIOs
  ./gpio.py cycle-all [delay]    # Toggle ALL GPIOs low then high (find power GPIO)
"""

import sys, time, struct
from tinygrad.runtime.support.usb import USB3

SUPPORTED_CONTROLLERS = [(0xADD1, 0x0001), (0x174C, 0x2464), (0x174C, 0x2463)]

GPIO_INPUT = 0x00
GPIO_LOW   = 0x02
GPIO_HIGH  = 0x03
GPIO_MAX   = 27

def open_device():
  for vid, pid in SUPPORTED_CONTROLLERS:
    try: return USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, max_streams=1)
    except RuntimeError: pass
  raise RuntimeError("No ASM2464PD device found")

def _asm_addr(xdata_addr):
  """Convert raw XDATA address to ASM firmware's 0x50xxxx format."""
  return (xdata_addr & 0x1FFFF) | 0x500000

def xdata_write(dev, addr, val):
  """Write one byte to XDATA address via E5 SCSI command."""
  a = _asm_addr(addr)
  cdb = struct.pack('>BBBHB9x', 0xE5, val, a >> 16, a & 0xFFFF, 0)
  dev.send_batch(cdbs=[cdb])

def xdata_read(dev, addr, size=1):
  """Read bytes from XDATA address via E4 SCSI command."""
  a = _asm_addr(addr)
  cdb = struct.pack('>BBBHB9x', 0xE4, size, a >> 16, a & 0xFFFF, 0)
  results = dev.send_batch(cdbs=[cdb], idata=[size])
  return results[0] if results else b'\x00' * size

def gpio_set(dev, gpio_num, mode):
  assert 0 <= gpio_num <= GPIO_MAX, f"GPIO {gpio_num} out of range (0-{GPIO_MAX})"
  xdata_write(dev, 0xC620 + gpio_num, mode)

def gpio_read_ctrl(dev, gpio_num):
  """Read GPIO control register value."""
  return xdata_read(dev, 0xC620 + gpio_num, 1)[0]

def gpio_read_input(dev, gpio_num):
  """Read GPIO input pin state."""
  byte_val = xdata_read(dev, 0xC650 + (gpio_num >> 3), 1)[0]
  return (byte_val >> (gpio_num & 7)) & 1

def mode_str(val):
  if val == GPIO_INPUT: return "INPUT"
  if val == GPIO_LOW: return "OUT_LOW"
  if val == GPIO_HIGH: return "OUT_HIGH"
  return f"0x{val:02x}"

def cmd_scan(dev):
  print(f"{'GPIO':>5} {'CTRL':>6} {'MODE':>10} {'INPUT':>6}")
  print("-" * 32)
  for i in range(GPIO_MAX + 1):
    ctrl = gpio_read_ctrl(dev, i)
    inp = gpio_read_input(dev, i)
    print(f"{i:>5} 0x{ctrl:02x} {mode_str(ctrl):>10} {inp:>6}")

def cmd_set(dev, gpio_num, mode_name):
  modes = {"high": GPIO_HIGH, "low": GPIO_LOW, "input": GPIO_INPUT, "hi": GPIO_HIGH, "lo": GPIO_LOW, "in": GPIO_INPUT,
           "1": GPIO_HIGH, "0": GPIO_LOW}
  mode = modes.get(mode_name.lower())
  if mode is None:
    print(f"Unknown mode '{mode_name}'. Use: high, low, input")
    sys.exit(1)
  gpio_set(dev, gpio_num, mode)
  print(f"GPIO {gpio_num} -> {mode_str(mode)}")

def cmd_cycle(dev, gpio_num, delay=1.0):
  print(f"Power cycling GPIO {gpio_num}: LOW for {delay}s then HIGH")
  gpio_set(dev, gpio_num, GPIO_LOW)
  print(f"  GPIO {gpio_num} -> LOW")
  time.sleep(delay)
  gpio_set(dev, gpio_num, GPIO_HIGH)
  print(f"  GPIO {gpio_num} -> HIGH")
  print("Done.")

def cmd_cycle_all(dev, delay=1.0):
  print(f"Cycling ALL GPIOs low for {delay}s then restoring...")
  # Save current state
  saved = [gpio_read_ctrl(dev, i) for i in range(GPIO_MAX + 1)]
  print(f"  Saved {GPIO_MAX+1} GPIO states")

  # Set all to LOW
  for i in range(GPIO_MAX + 1):
    gpio_set(dev, i, GPIO_LOW)
  print(f"  All GPIOs -> LOW")

  time.sleep(delay)

  # Restore
  for i in range(GPIO_MAX + 1):
    gpio_set(dev, i, saved[i])
  print(f"  All GPIOs restored")
  print("Done.")

if __name__ == "__main__":
  args = sys.argv[1:]

  dev = open_device()
  print(f"Connected to ASM2464PD")

  if not args or args[0] == "scan":
    cmd_scan(dev)
  elif args[0] == "set" and len(args) >= 3:
    cmd_set(dev, int(args[1]), args[2])
  elif args[0] == "cycle" and len(args) >= 2:
    delay = float(args[2]) if len(args) >= 3 else 1.0
    cmd_cycle(dev, int(args[1]), delay)
  elif args[0] == "cycle-all":
    delay = float(args[1]) if len(args) >= 2 else 1.0
    cmd_cycle_all(dev, delay)
  elif args[0] == "read" and len(args) >= 2:
    gpio_num = int(args[1])
    ctrl = gpio_read_ctrl(dev, gpio_num)
    inp = gpio_read_input(dev, gpio_num)
    print(f"GPIO {gpio_num}: ctrl=0x{ctrl:02x} ({mode_str(ctrl)}), input={inp}")
  else:
    print(__doc__)
    sys.exit(1)
