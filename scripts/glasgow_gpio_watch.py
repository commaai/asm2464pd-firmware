#!/usr/bin/env python3
"""
Glasgow GPIO Watcher - Probe ASM2464PD GPIO pin states.

Determines whether each pin is floating, driven high, or driven low
by testing with pull-up and pull-down resistors.

Usage:
  # Continuous watch:
  glasgow script glasgow_gpio_watch.py control-gpio -V 3.3 --pins A0:7,B0:7

  # Single reading:
  glasgow script glasgow_gpio_watch.py control-gpio -V 3.3 --pins A0:7,B0:7 -- --single

Press Ctrl+C to stop.
"""

import sys

from glasgow.abstract import PullState

# Pin name mappings: index in each bank -> ASM2464PD GPIO name
GPIO_A_MAP = [
  "GPIO0",
  "GPIO1",
  "GPIO2",
  "GPIO3",
  "GPIO8",
  "GPIO14",
  "GPIO15",
  "GPIO16",
]

GPIO_B_MAP = [
  "GPIO17",
  "GPIO18",
  "GPIO19",
  "GPIO20",
  "GPIO21",
  "GPIO22",
  "GPIO23",
  "GPIO24",
]

PIN_NAMES = GPIO_A_MAP + GPIO_B_MAP
NUM_PINS = len(PIN_NAMES)

async def set_all_pulls(device, high=False):
    """Set all 16 pins to pull-up (high=True) or pull-down (high=False).

    Calls device.set_pulls directly to avoid configure_ports() which
    redundantly re-sets the voltage every time.
    """
    if high:
        await device.set_pulls("A", low=set(), high=set(range(8)))
        await device.set_pulls("B", low=set(), high=set(range(8)))
    else:
        await device.set_pulls("A", low=set(range(8)), high=set())
        await device.set_pulls("B", low=set(range(8)), high=set())

async def probe_once(iface, device):
    """Do one round of probing: pull-up read, pull-down read, classify.

    Glasgow's internal pulls are ~100kΩ. Can't distinguish weak pull
    from strong push-pull (both overcome ~100kΩ). Can't use output
    contest either — Glasgow's LVC1T45 (24mA) would overpower the
    ASM2464PD GPIOs (12mA max) and could damage them.
    """
    # Ensure all pins are inputs
    await iface._oe.set(0)

    # Pull-up read
    await set_all_pulls(device, high=True)
    await asyncio.sleep(0.005)
    val_up = await iface.get_all()

    # Pull-down read
    await set_all_pulls(device, high=False)
    await asyncio.sleep(0.005)
    val_down = await iface.get_all()

    results = []
    for i in range(NUM_PINS):
        bit = 1 << i
        up   = bool(val_up & bit)
        down = bool(val_down & bit)

        if up and not down:
            state = "FLOAT"
        elif up and down:
            state = "HIGH"
        elif not up and not down:
            state = "LOW"
        else:
            state = "NOISE"

        results.append((PIN_NAMES[i], state))

    return results

STATE_COLORS = {
    "FLOAT":  "\033[90m",   # dim gray
    "HIGH":   "\033[1;32m", # bold green
    "LOW":    "\033[1;31m", # bold red
    "NOISE":  "\033[1;33m", # bold yellow
}
RESET = "\033[0m"

def format_results(results, single=False):
    lines = []
    if not single:
        lines.append("\033[2J\033[H")  # clear screen, cursor home
        lines.append("  ASM2464PD GPIO Watch")
        lines.append("  " + "=" * 30)
        lines.append("")
    for name, state in results:
        color = STATE_COLORS.get(state, "")
        lines.append(f"  {name:<8s}  {color}{state:>5s}{RESET}")
    if not single:
        lines.append("")
        lines.append("  Ctrl+C to stop")
    return "\n".join(lines)

# -- main loop (runs in glasgow script context) --
# globals provided by glasgow script: gpio_iface, asyncio, self, args

single = "--single" in args.script_args

if single:
    results = await probe_once(gpio_iface, self.device)
    print(format_results(results, single=True), flush=True)
else:
    try:
        while True:
            results = await probe_once(gpio_iface, self.device)
            print(format_results(results), flush=True)
            await asyncio.sleep(0.2)
    except KeyboardInterrupt:
        print("\nStopped.")
