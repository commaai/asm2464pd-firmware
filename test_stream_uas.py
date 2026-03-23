#!/usr/bin/env python3
"""
Targeted stream/UAS bring-up diagnostics for ASM2464 firmware.

This focuses on the exact path needed for tinygrad default mode:
  1) BOT smoke still works
  2) Stream (alt=1) open works
  3) Raw EP 0x04 bulk OUT works
  4) ASM24Controller stream init works

Run:
  sudo PYTHONPATH=/home/geohot/tinygrad python3 test_stream_uas.py
"""

import ctypes
import struct
import sys
import traceback

sys.path.insert(0, "/home/geohot/tinygrad")

from tinygrad.runtime.support.usb import USB3, ASM24Controller
from tinygrad.runtime.autogen import libusb


def _bot_send(dev: USB3, cdb: bytes, rlen: int = 0, send_data: bytes | None = None, timeout: int = 2000):
    dev._tag += 1
    dir_in = rlen > 0
    data_len = rlen if dir_in else (len(send_data) if send_data is not None else 0)
    flags = 0x80 if dir_in else 0x00
    cbw = struct.pack("<IIIBBB", 0x43425355, dev._tag, data_len, flags, 0, len(cdb)) + cdb + b"\x00" * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)

    data_in = None
    if dir_in:
        data_in = dev._bulk_in(dev.ep_data_in, rlen, timeout=timeout)
    elif send_data is not None:
        dev._bulk_out(dev.ep_data_out, send_data, timeout=timeout)

    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=timeout)
    sig, rtag, residue, status = struct.unpack("<IIIB", csw)
    return sig, rtag, residue, status, data_in


def _ctrl_e4_read(handle, addr: int) -> tuple[int, int | None]:
    buf = (ctypes.c_ubyte * 1)()
    rc = libusb.libusb_control_transfer(handle, 0xC0, 0xE4, addr, 0, buf, 1, 1000)
    return rc, (buf[0] if rc >= 0 else None)


def _close_usb3(dev: USB3) -> None:
    try:
        libusb.libusb_release_interface(dev.handle, 0)
    except Exception:
        pass
    try:
        libusb.libusb_close(dev.handle)
    except Exception:
        pass
    try:
        libusb.libusb_exit(dev.ctx)
    except Exception:
        pass


def test_bot_smoke() -> None:
    dev = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
    try:
        sig, _tag, _residue, status, _ = _bot_send(dev, b"\x00" * 6)
        assert sig == 0x53425355, f"Bad CSW sig 0x{sig:08X}"
        assert status in (0, 1), f"Unexpected TUR status {status}"
    finally:
        _close_usb3(dev)


def test_stream_open() -> USB3:
    return USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=False)


def test_raw_ep04_bulk_out(stream_dev: USB3) -> None:
    payload = bytes([0] * 31)
    transferred = ctypes.c_int(0)
    rc = libusb.libusb_bulk_transfer(
        stream_dev.handle,
        0x04,
        (ctypes.c_ubyte * len(payload))(*payload),
        len(payload),
        ctypes.byref(transferred),
        1000,
    )
    assert rc == 0, f"EP 0x04 bulk OUT rc={rc}, xfer={transferred.value}"
    assert transferred.value == len(payload), f"EP 0x04 short write {transferred.value}/{len(payload)}"


def test_asm24_stream_init() -> None:
    ctrl = ASM24Controller()
    _close_usb3(ctrl.usb)


def dump_key_state(stream_dev: USB3) -> None:
    addrs = [
        0x9007, 0x9008, 0x900C, 0x900E,
        0x901A, 0x905A,
        0x9093, 0x9094,
        0x90E2, 0x90E3,
        0x9101, 0x91D0,
        0x9300, 0x9301, 0x9302,
        0xC8D4, 0xC4ED, 0xC4EE, 0xC4EF,
        0x0108, 0x0052, 0x0003,
    ]
    print("  state dump:")
    for addr in addrs:
        rc, val = _ctrl_e4_read(stream_dev.handle, addr)
        if rc < 0:
            print(f"    0x{addr:04X}: rc={rc}")
        else:
            print(f"    0x{addr:04X}: 0x{val:02X}")


def main() -> int:
    failures = 0
    stream_dev = None

    print("--- 1) BOT smoke ---")
    try:
        test_bot_smoke()
        print("  PASS")
    except Exception as e:
        failures += 1
        print(f"  FAIL: {e}")
        traceback.print_exc()

    print("--- 2) Stream open (alt=1) ---")
    try:
        stream_dev = test_stream_open()
        print("  PASS")
    except Exception as e:
        failures += 1
        print(f"  FAIL: {e}")
        traceback.print_exc()

    print("--- 3) Raw EP 0x04 bulk OUT ---")
    if stream_dev is None:
        print("  SKIP (stream open failed)")
        failures += 1
    else:
        try:
            test_raw_ep04_bulk_out(stream_dev)
            print("  PASS")
        except Exception as e:
            failures += 1
            print(f"  FAIL: {e}")
            traceback.print_exc()
            dump_key_state(stream_dev)

    if stream_dev is not None:
        _close_usb3(stream_dev)

    print("--- 4) ASM24Controller stream init ---")
    try:
        test_asm24_stream_init()
        print("  PASS")
    except Exception as e:
        failures += 1
        print(f"  FAIL: {e}")
        traceback.print_exc()

    print("=" * 50)
    print(f"Failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
