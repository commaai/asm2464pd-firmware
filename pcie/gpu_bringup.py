#!/usr/bin/env python3
"""
Minimal AMD GPU bringup for DMA via ASM2464PD USB-PCIe bridge.

Only boots enough for SDMA to work:
  1. IP discovery (from VRAM)
  2. PSP SOS boot
  3. PSP ring -> TOC -> SMU fw -> TMR -> SDMA fw
  4. MMHUB system aperture (so SDMA can reach PCIe addrs)
  5. SDMA unhalt + ring setup
  6. SDMA copy: VRAM -> PCIe 0x200000, read at XDATA 0xF000

Does NOT init: SMU features, RLC, MEC, GFX, compute queues, clockgating.

Prerequisites:
    python3 pcie/pcie_bringup.py   # link up
    python3 pcie/pcie_probe.py     # bridges + BARs

DMA memory map (ASM2464PD internal SRAM):
  PCIe 0x200000  (512KB SRAM, readable via MRd TLP)
  Note: XDATA 0xF000 is a separate SRAM bank, NOT aliased to PCIe 0x200000.
"""

import ctypes, struct, sys, time, functools, collections

from pcie.pcie_probe import (
  usb_open, usb_close, xdata_read, xdata_write,
  pcie_mem_read, pcie_mem_write,
)

from tinygrad.helpers import lo32, hi32, fetch
from tinygrad.runtime.autogen.am import am

BAR5 = 0x10000000
BAR0 = 0x800000000
FW_BASE = "https://gitlab.com/kernel-firmware/linux-firmware/-/raw/1e2c15348485939baf1b6d1f5a7a3b799d80703d/amdgpu"

# -- PCIe TLP via E5 writes to B2xx (same path tinygrad uses) --
def _e5_pcie_write(h, address, value):
  """PCIe MEM WR via E5 to B2xx registers (matches tinygrad pcie_prep_request exactly)."""
  v = value & 0xFFFFFFFF
  masked = address & 0xFFFFFFFC
  # Exactly matches tinygrad pcie_prep_request WriteOp sequence:
  # 1. B220: data (big-endian)
  for i, b in enumerate(struct.pack('>I', v)):
    xdata_write(h, 0xB220 + i, b)
  # 2. B218: address low (big-endian)
  for i, b in enumerate(struct.pack('>I', masked & 0xFFFFFFFF)):
    xdata_write(h, 0xB218 + i, b)
  # 3. B21C: address high
  for i, b in enumerate(struct.pack('>I', (address >> 32) & 0xFFFFFFFF)):
    xdata_write(h, 0xB21C + i, b)
  # 4. B217: byte enable
  xdata_write(h, 0xB217, 0x0F)
  # 5. B210: format type
  fmt = 0x60 if address > 0xFFFFFFFF else 0x40
  xdata_write(h, 0xB210, fmt)
  # 6. B254: trigger
  xdata_write(h, 0xB254, 0x0F)
  # 7. B296: clear status (posted write, no completion to wait for)
  xdata_write(h, 0xB296, 0x04)

def _e5_pcie_read(h, address, size=4):
  """PCIe MEM RD via E5 to B2xx registers (matches tinygrad pcie_request read path)."""
  masked = address & 0xFFFFFFFC
  offset = address & 0x3
  # Clear B210-B21B
  for i in range(12): xdata_write(h, 0xB210 + i, 0)
  # Format type
  fmt = 0x20 if address > 0xFFFFFFFF else 0x00
  xdata_write(h, 0xB210, fmt)
  xdata_write(h, 0xB213, 0x01)
  xdata_write(h, 0xB217, ((1 << size) - 1) << offset)
  # Address
  for i, b in enumerate(struct.pack('>I', masked & 0xFFFFFFFF)):
    xdata_write(h, 0xB218 + i, b)
  if address > 0xFFFFFFFF:
    for i, b in enumerate(struct.pack('>I', (address >> 32) & 0xFFFFFFFF)):
      xdata_write(h, 0xB21C + i, b)
  xdata_write(h, 0xB216, 0x20)
  # Clear stale status, trigger
  xdata_write(h, 0xB296, 0x01)
  xdata_write(h, 0xB296, 0x02)
  xdata_write(h, 0xB296, 0x04)
  xdata_write(h, 0xB254, 0x0F)
  # Poll completion
  import time
  for _ in range(200):
    b296 = xdata_read(h, 0xB296, 1)[0]
    if b296 & 0x02:
      xdata_write(h, 0xB296, 0x02)
      raw = struct.unpack('>I', xdata_read(h, 0xB220, 4))[0]
      return (raw >> (8 * offset)) & ((1 << (8 * size)) - 1)
    if b296 & 0x01:
      xdata_write(h, 0xB296, 0x01)
      return None
    time.sleep(0.001)
  return None

# -- PCIe access via E4/E5 to B2xx (same path as tinygrad/pcie_bringup) --
# This avoids the F0 TLP path which has issues with clock-gated GC registers.
_bringup_dev = None

def _get_bringup_dev(h):
  global _bringup_dev
  if _bringup_dev is None:
    from pcie.pcie_bringup import ASM2464PD
    _bringup_dev = ASM2464PD()
    _bringup_dev.dev = type('FakeUSB3', (), {'handle': h})()
  return _bringup_dev

def _e5_write(h, addr, val):
  """Single byte write via E5 control transfer."""
  xdata_write(h, addr, val)

def _e5_pcie_req(h, address, value=None, size=4):
  """PCIe request via E5 B2xx writes — matches tinygrad pcie_prep_request exactly."""
  masked = address & 0xFFFFFFFC
  offset = address & 0x3
  be = ((1 << size) - 1) << offset
  # 1. Data (B220, big-endian) — only for writes
  if value is not None:
    shifted = (value << (8 * offset)) & 0xFFFFFFFF
    for i, b in enumerate(struct.pack('>I', shifted)):
      _e5_write(h, 0xB220 + i, b)
  # 2. Address (B218-B21B, big-endian)
  for i, b in enumerate(struct.pack('>I', masked & 0xFFFFFFFF)):
    _e5_write(h, 0xB218 + i, b)
  # 3. Address high (B21C-B21F)
  for i, b in enumerate(struct.pack('>I', (address >> 32) & 0xFFFFFFFF)):
    _e5_write(h, 0xB21C + i, b)
  # 4. Byte enable (B217)
  _e5_write(h, 0xB217, be)
  # 5. Format type (B210)
  if value is not None:
    fmt = 0x60 if address > 0xFFFFFFFF else 0x40
  else:
    fmt = 0x20 if address > 0xFFFFFFFF else 0x00
  _e5_write(h, 0xB210, fmt)
  # 6. Clear stale status BEFORE trigger (matches tinygrad pcie_prep_request)
  _e5_write(h, 0xB296, 0x01)  # clear error
  _e5_write(h, 0xB296, 0x02)  # clear complete
  _e5_write(h, 0xB296, 0x04)  # clear kick
  # 7. Trigger (B254)
  _e5_write(h, 0xB254, 0x0F)
  # For writes: fire and forget
  if value is not None:
    return None
  # For reads: poll completion
  import time
  for _ in range(200):
    b296 = xdata_read(h, 0xB296, 1)[0]
    if b296 & 0x02:
      _e5_write(h, 0xB296, 0x02)
      raw = struct.unpack('>I', xdata_read(h, 0xB220, 4))[0]
      return (raw >> (8 * offset)) & ((1 << (8 * size)) - 1)
    if b296 & 0x01:
      _e5_write(h, 0xB296, 0x01)
      return None
    time.sleep(0.001)
  return None

def rreg(h, dw):    return _e5_pcie_req(h, BAR5 + dw * 4) or 0
def wreg(h, dw, v): _e5_pcie_req(h, BAR5 + dw * 4, value=v & 0xFFFFFFFF)
def smnrd(h, a):    wreg(h, 0, a); return rreg(h, 1)
def smnwr(h, a, v): wreg(h, 0, a); wreg(h, 1, v)

# -- VRAM: F0 for fast bulk access (reads work, writes need DMA wait fix) --
def vrd32(h, pa):      return pcie_mem_read(h, BAR0 + pa)
def vwr32(h, pa, v):   pcie_mem_write(h, BAR0 + pa, v & 0xFFFFFFFF)
def vread(h, pa, sz):  return b''.join(struct.pack('<I', vrd32(h, pa+o)) for o in range(0, sz, 4))[:sz]
def vwrite(h, pa, d):
  d = d + b'\x00' * ((4 - len(d) % 4) % 4)
  for i in range(0, len(d), 4): vwr32(h, pa + i, struct.unpack_from('<I', d, i)[0])

def hexdump(d, a=0):
  for i in range(0, len(d), 16):
    c = d[i:i+16]
    print(f"  {a+i:08x}: {' '.join(f'{b:02x}' for b in c):<48s}  {''.join(chr(b) if 32<=b<127 else '.' for b in c)}")

class Regs:
  def __init__(self, pfx, ver, bases):
    from tinygrad.runtime.support.amd import import_asic_regs, AMDReg
    self._r = import_asic_regs(pfx, ver, cls=functools.partial(AMDReg, bases=bases))
  def smn(self, name, inst=0): return self._r[name].addr[inst] * 4
  def dw(self, name, inst=0): return self._r[name].addr[inst]  # dword offset for direct MMIO

# ============================================================================
# 1. IP Discovery
# ============================================================================
def discover(h):
  vram_sz = rreg(h, 0xde3) << 20
  disc = bytearray(vread(h, vram_sz - (64 << 10), 8 << 10))
  bhdr = am.struct_binary_header.from_buffer(disc)
  assert bhdr.binary_signature == am.BINARY_SIGNATURE
  ihdr = am.struct_ip_discovery_header.from_buffer(disc, bhdr.table_list[am.IP_DISCOVERY].offset)
  assert ihdr.signature == am.DISCOVERY_TABLE_SIGNATURE
  ip_ver, bases = {}, collections.defaultdict(dict)
  for die in range(ihdr.num_dies):
    pos = ihdr.die_info[die].die_offset + ctypes.sizeof(am.struct_die_header)
    dhdr = am.struct_die_header.from_buffer(disc, ihdr.die_info[die].die_offset)
    for _ in range(dhdr.num_ips):
      ip = am.struct_ip_v4.from_buffer(disc, pos)
      bsz = 8 if ihdr.base_addr_64_bit else 4
      ba = struct.unpack_from('<' + ('Q' if ihdr.base_addr_64_bit else 'I') * ip.num_base_address, disc, pos + 8)
      for hw in range(1, am.MAX_HWIP):
        if hw in am.hw_id_map and am.hw_id_map[hw] == ip.hw_id:
          bases[hw][ip.instance_number] = tuple(ba)
          ip_ver[hw] = (ip.major, ip.minor, ip.revision)
      pos += 8 + bsz * ip.num_base_address
  for t, hw in [("GC", am.GC_HWIP), ("SDMA", am.SDMA0_HWIP), ("PSP", am.MP0_HWIP), ("SMU", am.MP1_HWIP), ("MMHUB", am.MMHUB_HWIP)]:
    print(f"  {t:6s} {ip_ver.get(hw, 'n/a')}")
  return vram_sz, ip_ver, bases

# ============================================================================
# 2. PSP SOS boot
# ============================================================================
def psp_sos(h, ip_ver, bases, vram_sz):
  mp0 = Regs('mp', ip_ver[am.MP0_HWIP], bases[am.MP0_HWIP])
  # PSP 14+ uses regMPASP_SMN_C2PMSG_*, older uses regMP0_SMN_C2PMSG_*
  pfx = "regMPASP_SMN_C2PMSG" if ip_ver[am.MP0_HWIP] >= (14,0,0) else "regMP0_SMN_C2PMSG"
  # PSP regs: use direct BAR5 MMIO (SMN INDEX/DATA doesn't reach PSP on this GPU)
  rd = lambda n: rreg(h, mp0.dw(f"{pfx}_{n}"))
  wr = lambda n, v: wreg(h, mp0.dw(f"{pfx}_{n}"), v)

  # Always parse the SOS blob (needed for TOC even if SOS is already alive)
  v = '_'.join(map(str, ip_ver[am.MP0_HWIP]))
  blob = bytearray(fetch(f"{FW_BASE}/psp_{v}_sos.bin", subdir="fw").read_bytes())
  buf = (ctypes.c_ubyte * len(blob)).from_buffer(blob)
  chdr = am.struct_common_firmware_header.from_address(ctypes.addressof(buf))
  hdr = getattr(am, f"struct_psp_firmware_header_v{chdr.header_version_major}_{chdr.header_version_minor}").from_address(ctypes.addressof(buf))

  sos_fw = {}
  for i in range(hdr.psp_fw_bin_count):
    d = am.struct_psp_fw_bin_desc.from_address(ctypes.addressof(hdr.psp_fw_bin) + i * ctypes.sizeof(am.struct_psp_fw_bin_desc))
    s = d.offset_bytes + hdr.header.ucode_array_offset_bytes
    sos_fw[d.fw_type] = blob[s:s + d.size_bytes]

  # Place msg1 lower in VRAM to avoid PSP boot-time TMR at the top
  msg1_pa = ((vram_sz - (128 << 20)) >> 20) << 20

  if rd("81"):
    print("  SOS already alive"); return rd, wr, sos_fw, msg1_pa
  # PSP bootloader needs MC address (fb_base + paddr), not physical address
  # Read fb_base from MMHUB
  mmhub = Regs('mmhub', ip_ver.get(am.MMHUB_HWIP, ip_ver[am.GC_HWIP]), bases.get(am.MMHUB_HWIP, bases[am.GC_HWIP]))
  fb_base = (rreg(h, mmhub.dw("regMMMC_VM_FB_LOCATION_BASE")) & 0xFFFFFF) << 24
  msg1_mc = fb_base + msg1_pa
  print(f"  fb_base=0x{fb_base:X}, msg1_pa=0x{msg1_pa:X}, msg1_mc=0x{msg1_mc:X}")

  spl = am.PSP_FW_TYPE_PSP_SPL if ip_ver[am.MP0_HWIP] >= (14,0,0) else am.PSP_FW_TYPE_PSP_KDB
  for fw_t, cid in [(am.PSP_FW_TYPE_PSP_KDB, am.PSP_BL__LOAD_KEY_DATABASE), (spl, am.PSP_BL__LOAD_TOS_SPL_TABLE),
      (am.PSP_FW_TYPE_PSP_SYS_DRV, am.PSP_BL__LOAD_SYSDRV), (am.PSP_FW_TYPE_PSP_SOC_DRV, am.PSP_BL__LOAD_SOCDRV),
      (am.PSP_FW_TYPE_PSP_INTF_DRV, am.PSP_BL__LOAD_INTFDRV), (am.PSP_FW_TYPE_PSP_DBG_DRV, am.PSP_BL__LOAD_DBGDRV),
      (am.PSP_FW_TYPE_PSP_RAS_DRV, am.PSP_BL__LOAD_RASDRV), (am.PSP_FW_TYPE_PSP_SOS, am.PSP_BL__LOAD_SOSDRV)]:
    if fw_t not in sos_fw: continue
    fw = bytes(sos_fw[fw_t])
    print(f"  {am.enum_psp_fw_type.get(fw_t, fw_t)} ({len(fw)}B)", end=' ', flush=True)
    for _ in range(10000):
      if rd("35") & 0x80000000: break
      time.sleep(0.001)
    else: raise TimeoutError("BL")
    vwrite(h, msg1_pa, fw + b'\x00' * ((16 - len(fw) % 16) % 16))
    wr("36", msg1_mc >> 20); wr("35", cid)
    if cid == am.PSP_BL__LOAD_SOSDRV: print("sent"); continue
    for _ in range(10000):
      if rd("35") & 0x80000000: break
      time.sleep(0.001)
    else: raise TimeoutError(f"BL {fw_t}")
    print("ok")

  for _ in range(30000):
    if rd("81"): break
    time.sleep(0.001)
  else: raise TimeoutError("SOS")
  print(f"  SOS alive (0x{rd('81'):08X})")
  return rd, wr, sos_fw, msg1_pa

# ============================================================================
# 3. PSP ring -> TOC -> SMU fw -> TMR -> SDMA fw
# ============================================================================
def psp_load_sdma(h, ip_ver, bases, vram_sz, mp0r, mp0w, sos_fw, msg1):
  alloc_p = [vram_sz - (80 << 20)]
  def palloc(sz, a=0x1000): alloc_p[0] = (alloc_p[0] - sz) & ~(a - 1); return alloc_p[0]

  ring_pa, cmd_pa, fence_pa = palloc(0x10000), palloc(am.PSP_CMD_BUFFER_SIZE), palloc(am.PSP_FENCE_BUFFER_SIZE)
  vwr32(h, fence_pa, 0)

  mmhub = Regs('mmhub', ip_ver.get(am.MMHUB_HWIP, ip_ver[am.GC_HWIP]), bases.get(am.MMHUB_HWIP, bases[am.GC_HWIP]))
  fb = (rreg(h, mmhub.dw("regMMMC_VM_FB_LOCATION_BASE")) & 0xFFFFFF) << 24
  mc = lambda pa: fb + pa
  print(f"  fb_base=0x{fb:X}")

  # destroy old ring if present
  if mp0r("71"): mp0w("64", am.GFX_CTRL_CMD_ID_DESTROY_RINGS); time.sleep(0.05)
  for _ in range(10000):
    if mp0r("64") & 0x80000000: break
    time.sleep(0.001)
  # create ring
  mp0w("69", lo32(mc(ring_pa))); mp0w("70", hi32(mc(ring_pa)))
  mp0w("71", 0x10000); mp0w("64", am.PSP_RING_TYPE__KM << 16)
  time.sleep(0.05)
  for _ in range(10000):
    if (mp0r("64") & 0x8000FFFF) == 0x80000000: break
    time.sleep(0.001)
  else: raise TimeoutError("ring")
  print("  PSP ring ok")

  def submit(cmd_b):
    wp = mp0r("67"); fv = wp + 1
    f = am.struct_psp_gfx_rb_frame()
    f.fence_value = fv
    f.cmd_buf_addr_lo, f.cmd_buf_addr_hi = lo32(mc(cmd_pa)), hi32(mc(cmd_pa))
    f.fence_addr_lo, f.fence_addr_hi = lo32(mc(fence_pa)), hi32(mc(fence_pa))
    vwrite(h, cmd_pa, cmd_b); vwrite(h, ring_pa + wp * 4, bytes(f))
    mp0w("67", wp + ctypes.sizeof(am.struct_psp_gfx_rb_frame) // 4)
    for _ in range(30000):
      if vrd32(h, fence_pa) == fv: break
      time.sleep(0.001)
    else: raise TimeoutError(f"psp fence {vrd32(h, fence_pa)} != {fv}")
    r = am.struct_psp_gfx_cmd_resp.from_buffer(bytearray(vread(h, cmd_pa, ctypes.sizeof(am.struct_psp_gfx_cmd_resp))))
    if r.resp.status: raise RuntimeError(f"PSP cmd {r.cmd_id} status={r.resp.status}")
    return r

  def flush_hdp():
    """Flush HDP write buffer so VRAM writes are visible to GPU engines."""
    # HDP_MEM_COHERENCY_FLUSH_CNTL is at a fixed NBIO address
    # For NBIO 7.11/GC 12.0: try well-known offsets
    wreg(h, 0xD8, 0)  # typical HDP flush register dword offset

  def load_ip(data, fw_type):
    vwrite(h, msg1, bytes(data))
    flush_hdp()
    c = am.struct_psp_gfx_cmd_resp(cmd_id=am.GFX_CMD_ID_LOAD_IP_FW)
    c.cmd.cmd_load_ip_fw.fw_phy_addr_lo, c.cmd.cmd_load_ip_fw.fw_phy_addr_hi = lo32(mc(msg1)), hi32(mc(msg1))
    c.cmd.cmd_load_ip_fw.fw_size = len(data); c.cmd.cmd_load_ip_fw.fw_type = fw_type
    submit(bytes(c))

  # TOC -> TMR size
  tmr_sz = 0
  if am.PSP_FW_TYPE_PSP_TOC in sos_fw:
    toc = bytes(sos_fw[am.PSP_FW_TYPE_PSP_TOC]); vwrite(h, msg1, toc)
    c = am.struct_psp_gfx_cmd_resp(cmd_id=am.GFX_CMD_ID_LOAD_TOC)
    c.cmd.cmd_load_toc.toc_phy_addr_lo, c.cmd.cmd_load_toc.toc_phy_addr_hi = lo32(mc(msg1)), hi32(mc(msg1))
    c.cmd.cmd_load_toc.toc_size = len(toc)
    tmr_sz = submit(bytes(c)).resp.tmr_size
    print(f"  TOC ok, TMR={tmr_sz}")

  # SMU fw (must be before TMR)
  sv = ip_ver[am.MP1_HWIP]
  try:
    sb = bytearray(fetch(f"{FW_BASE}/smu_{sv[0]}_{sv[1]}_{sv[2]}.bin", subdir="fw").read_bytes())
    sc = am.struct_common_firmware_header.from_address(ctypes.addressof((ctypes.c_ubyte * len(sb)).from_buffer(sb)))
    load_ip(sb[sc.ucode_array_offset_bytes:sc.ucode_array_offset_bytes + sc.ucode_size_bytes], am.GFX_FW_TYPE_SMU)
    print("  SMU fw ok")
  except Exception as e: print(f"  SMU fw skip: {e}")

  # TMR — always send setup even with boot_time_tmr (PSP needs acknowledgment)
  if True:
    tp = palloc(max(tmr_sz, 0x1300000), a=0x100000)
    c = am.struct_psp_gfx_cmd_resp(cmd_id=am.GFX_CMD_ID_SETUP_TMR)
    c.cmd.cmd_setup_tmr.buf_phy_addr_lo, c.cmd.cmd_setup_tmr.buf_phy_addr_hi = lo32(mc(tp)), hi32(mc(tp))
    c.cmd.cmd_setup_tmr.system_phy_addr_lo, c.cmd.cmd_setup_tmr.system_phy_addr_hi = lo32(tp), hi32(tp)
    c.cmd.cmd_setup_tmr.bitfield.virt_phy_addr = 1; c.cmd.cmd_setup_tmr.buf_size = max(tmr_sz, 0x1300000)
    try:
      submit(bytes(c)); print(f"  TMR ok @ 0x{tp:X}")
    except Exception as e: print(f"  TMR setup: {e} (boot_time_tmr, continuing)")

  # Load all IP firmware needed for SDMA (GC block must be unclocked via RLC autoload)
  # Use tinygrad's AMFirmware to get the correct firmware descriptors
  from tinygrad.runtime.support.am.amdev import AMFirmware
  class FakeAdev:
    def __init__(self, ip_ver): self.ip_ver = ip_ver; self.devfmt = "gpu"
  fw_obj = AMFirmware(FakeAdev(ip_ver))
  for fw_types, fw_bytes in fw_obj.descs:
    for fw_type in fw_types:
      name = am.enum_psp_gfx_fw_type.get(fw_type, f"{fw_type}")
      print(f"  {name} ({len(fw_bytes)}B)", end=' ', flush=True)
      load_ip(fw_bytes, fw_type)
      print("ok")

  # Load register list from SOS (needed for RLC autoload)
  if am.PSP_FW_TYPE_PSP_RL in sos_fw:
    rl = bytes(sos_fw[am.PSP_FW_TYPE_PSP_RL])
    print(f"  REG_LIST ({len(rl)}B)", end=' ', flush=True)
    load_ip(rl, am.GFX_FW_TYPE_REG_LIST)
    print("ok")

  # RLC autoload — unclocks GC block (including SDMA)
  print("  RLC autoload", end=' ', flush=True)
  r = submit(bytes(am.struct_psp_gfx_cmd_resp(cmd_id=am.GFX_CMD_ID_AUTOLOAD_RLC)))
  print(f"ok (status={r.resp.status}, cmd_id=0x{r.cmd_id:X})")
  # Check RLC boot status via SMN
  try:
    bst = rreg(h, gc.dw("regRLC_RLCS_BOOTLOAD_STATUS"))
    print(f"  RLC_RLCS_BOOTLOAD_STATUS = 0x{bst:08X}")
  except: pass
  try:
    cps = rreg(h, gc.dw("regCP_STAT"))
    print(f"  CP_STAT = 0x{cps:08X}")
  except: pass

  # Enable RLC via SMN (bypasses clock gating on direct MMIO path)
  gc = Regs('gc', ip_ver[am.GC_HWIP], bases[am.GC_HWIP])
  rlc_cntl_dw = gc.dw("regRLC_CNTL")
  print(f"  RLC_CNTL dw 0x{rlc_cntl_dw:X}")
  wreg(h, rlc_cntl_dw, 0x01)

  # Write RLC_CNTL = 1 (enable RLC — tinygrad AM_GFX.init_hw step)
  wreg(h, gc.dw("regRLC_CNTL"), 0x01)
  # Write RLC_SRM_CNTL (srm_enable=1, auto_incr_addr=1)
  try: wreg(h, gc.dw("regRLC_SRM_CNTL"), gc._r["regRLC_SRM_CNTL"].encode(srm_enable=1, auto_incr_addr=1))
  except: pass
  # Write RLC_SPM_MC_CNTL = 0xf
  try: wreg(h, gc.dw("regRLC_SPM_MC_CNTL"), 0x0F)
  except: pass

  # Wait for RLC to start and unclock GC block
  print("  Waiting for GC block...", end=' ', flush=True)
  for i in range(600):
    # Check SDMA status directly via BAR5 MMIO
    sdma_cntl = rreg(h, 0x1260 + 0x06D)  # approximate SDMA0_CNTL dword offset
    grbm = rreg(h, 0x2004)
    scratch = rreg(h, 0x2010)  # SCRATCH_REG0
    if grbm != 0 or scratch != 0:
      print(f"ready ({i*100}ms, GRBM=0x{grbm:08X} SCRATCH=0x{scratch:08X})")
      break
    if i % 50 == 49:
      imu_scratch = rreg(h, gc.dw("regGFX_IMU_SCRATCH_10"))
      imu_reset = rreg(h, gc.dw("regGFX_IMU_GFX_RESET_CTRL"))
      print(f"  ({i*100}ms: GRBM=0x{grbm:08X} IMU_S10=0x{imu_scratch:X} IMU_RST=0x{imu_reset:X})")
    time.sleep(0.1)
  else:
    print(f"timeout (GRBM=0x{rreg(h, 0x2004):08X})")

  return mc, palloc

# ============================================================================
# 4. MMHUB system aperture (minimal GMC)
# ============================================================================
def gmc_sys_aperture(h, ip_ver, bases, mc, palloc):
  mmhub = Regs('mmhub', ip_ver.get(am.MMHUB_HWIP, ip_ver[am.GC_HWIP]), bases.get(am.MMHUB_HWIP, bases[am.GC_HWIP]))
  fb = (rreg(h, mmhub.dw("regMMMC_VM_FB_LOCATION_BASE")) & 0xFFFFFF) << 24
  ft = (rreg(h, mmhub.dw("regMMMC_VM_FB_LOCATION_TOP")) & 0xFFFFFF) << 24
  print(f"  FB 0x{fb:X}..0x{ft:X}")

  wreg(h, mmhub.dw("regMMMC_VM_AGP_BASE"), 0)
  wreg(h, mmhub.dw("regMMMC_VM_AGP_BOT"), 0xFFFFFF)  # disable AGP
  wreg(h, mmhub.dw("regMMMC_VM_AGP_TOP"), 0)
  wreg(h, mmhub.dw("regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR"), fb >> 18)
  wreg(h, mmhub.dw("regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR"), ft >> 18)

  sp, dp = palloc(0x1000), palloc(0x1000)
  wreg(h, mmhub.dw("regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB"), lo32(sp >> 12))
  wreg(h, mmhub.dw("regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB"), hi32(sp >> 12))
  wreg(h, mmhub.dw("regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32"), lo32(dp >> 12))
  wreg(h, mmhub.dw("regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32"), hi32(dp >> 12))

  # L1 TLB: enable, system_access_mode=3, advanced_driver_model
  v = rreg(h, mmhub.dw("regMMMC_VM_MX_L1_TLB_CNTL"))
  v = (v | 1 | (1 << 3)) & ~(3 << 4) | (3 << 4)
  wreg(h, mmhub.dw("regMMMC_VM_MX_L1_TLB_CNTL"), v)
  print("  MMHUB sys aperture ok")

# ============================================================================
# 5. SDMA init + ring + DMA test
# ============================================================================
def sdma_dma_test(h, ip_ver, bases, mc, palloc):
  from tinygrad.runtime.support.amd import import_module
  sdma_mod = import_module('sdma', min(ip_ver[am.SDMA0_HWIP], (6, 0, 0)))
  # SDMA registers live in the GC block, not a separate 'sdma' prefix
  sdma = Regs('gc', ip_ver[am.GC_HWIP], bases[am.GC_HWIP])

  # Init SDMA0 (mirrors tinygrad AM_SDMA.init_hw for SDMA >= 6.0)
  # Use direct BAR5 MMIO (rreg/wreg) for GC block registers, not SMN
  BAR5_DW_LIMIT = 0x20000  # 512KB / 4 = 0x20000 dwords
  def sdma_rd(reg_name):
    return rreg(h, sdma.dw(reg_name))
  def sdma_wr(reg_name, v):
    wreg(h, sdma.dw(reg_name), v)
  def rmw(reg_name, **fields):
    v = sdma_rd(reg_name)
    r = sdma._r[reg_name]
    for fname, fval in fields.items():
      start, end = r.fields[fname]
      mask = ((1 << (end - start + 1)) - 1) << start
      v = (v & ~mask) | ((fval << start) & mask)
    sdma_wr(reg_name, v)

  if ip_ver[am.SDMA0_HWIP] >= (6,0,0):
    rmw("regSDMA0_WATCHDOG_CNTL", queue_hang_count=100)
    rmw("regSDMA0_UTCL1_CNTL", resp_mode=3, redo_delay=9)
    rmw("regSDMA0_UTCL1_PAGE", rd_l2_policy=2, wr_l2_policy=3,
        **({'llc_noalloc':1} if ip_ver[am.SDMA0_HWIP] < (7,0,0) else {}))
    # F32 engine for SDMA < 7.0, MCU engine for SDMA >= 7.0
    eng = "F32" if ip_ver[am.SDMA0_HWIP] < (7,0,0) else "MCU"
    reset_field = "th1_reset" if eng == "F32" else "reset"
    rmw(f"regSDMA0_{eng}_CNTL", halt=0, **{reset_field: 0})

  rmw("regSDMA0_CNTL", trap_enable=1)
  print("  SDMA0 init ok")

  # Ring in VRAM
  RSZ = 0x1000
  rpa, gpa = palloc(RSZ), palloc(0x1000)
  for o in range(0, RSZ, 4): vwr32(h, rpa + o, 0)
  for o in range(0, 256, 4): vwr32(h, gpa + o, 0)
  rmc, rpmc, wpmc = mc(rpa), mc(gpa), mc(gpa + 8)

  sdma_wr("regSDMA0_QUEUE0_MINOR_PTR_UPDATE", 1)
  for n in ["RB_RPTR", "RB_RPTR_HI", "RB_WPTR", "RB_WPTR_HI"]: sdma_wr(f"regSDMA0_QUEUE0_{n}", 0)
  sdma_wr("regSDMA0_QUEUE0_RB_BASE", lo32(rmc >> 8))
  sdma_wr("regSDMA0_QUEUE0_RB_BASE_HI", hi32(rmc >> 8))
  sdma_wr("regSDMA0_QUEUE0_RB_RPTR_ADDR_LO", lo32(rpmc))
  sdma_wr("regSDMA0_QUEUE0_RB_RPTR_ADDR_HI", hi32(rpmc))
  sdma_wr("regSDMA0_QUEUE0_RB_WPTR_POLL_ADDR_LO", lo32(wpmc))
  sdma_wr("regSDMA0_QUEUE0_RB_WPTR_POLL_ADDR_HI", hi32(wpmc))
  sdma_wr("regSDMA0_QUEUE0_MINOR_PTR_UPDATE", 0)
  rb_log = (RSZ // 4).bit_length() - 1
  # RB_CNTL fields: rb_enable[0], rb_size[1:5], f32_wptr_poll_enable[11],
  #   rptr_writeback_enable[12], rptr_writeback_timer[16:20], rb_priv[23], rb_vmid[24:27]
  wptr_poll_field = "f32_wptr_poll_enable" if ip_ver[am.SDMA0_HWIP] < (7,0,0) else "mcu_wptr_poll_enable"
  rb_cntl = sdma._r["regSDMA0_QUEUE0_RB_CNTL"].encode(
    rb_enable=1, rb_size=rb_log, rptr_writeback_enable=1, rptr_writeback_timer=4,
    rb_priv=1, rb_vmid=0, **{wptr_poll_field: 1})
  sdma_wr("regSDMA0_QUEUE0_RB_CNTL", rb_cntl)
  sdma_wr("regSDMA0_QUEUE0_IB_CNTL", sdma._r["regSDMA0_QUEUE0_IB_CNTL"].encode(ib_enable=1))
  # Verify ring config
  print(f"  RB_CNTL=0x{sdma_rd('regSDMA0_QUEUE0_RB_CNTL'):08X}")
  print(f"  RB_BASE=0x{sdma_rd('regSDMA0_QUEUE0_RB_BASE'):08X}:{sdma_rd('regSDMA0_QUEUE0_RB_BASE_HI'):08X}")
  print(f"  SDMA0_CNTL=0x{sdma_rd('regSDMA0_CNTL'):08X}")
  eng = "F32" if ip_ver[am.SDMA0_HWIP] < (7,0,0) else "MCU"
  print(f"  SDMA0_{eng}_CNTL=0x{sdma_rd(f'regSDMA0_{eng}_CNTL'):08X}")
  try: print(f"  STATUS=0x{sdma_rd('regSDMA0_STATUS_REG'):08X}")
  except: pass
  print("  SDMA ring ok")

  # Test pattern in VRAM
  tpa = palloc(0x100)
  pat = b'GPU DMA TEST OK!' * 4  # 64 bytes
  vwrite(h, tpa, pat)
  src, dst, sz = mc(tpa), 0x200000, 64

  # Clear destination
  for o in range(0, 64, 4): pcie_mem_write(h, 0x200000 + o, 0)

  # SDMA copy + fence packet
  mf = sdma_mod.SDMA_PKT_FENCE_HEADER_MTYPE(3) if ip_ver[am.GC_HWIP] >= (10,0,0) else 0
  fmc, fval = mc(gpa + 16), 0xCAFEBABE
  pkt = struct.pack('<IIIIIII',
    sdma_mod.SDMA_OP_COPY | sdma_mod.SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(sdma_mod.SDMA_SUBOP_COPY_LINEAR),
    sdma_mod.SDMA_PKT_COPY_LINEAR_COUNT_COUNT(sz - 1), 0,
    lo32(src), hi32(src), lo32(dst), hi32(dst))
  pkt += struct.pack('<IIII', sdma_mod.SDMA_OP_FENCE | mf, lo32(fmc), hi32(fmc), fval)

  vwrite(h, rpa, pkt)
  print(f"  copy {sz}B: MC 0x{src:X} -> PCIe 0x{dst:X}")

  # Kick SDMA via MMIO wptr
  sdma_wr("regSDMA0_QUEUE0_RB_WPTR", len(pkt))

  # Poll fence in VRAM
  for i in range(500):
    if vrd32(h, gpa + 16) == fval:
      print(f"  fence ok ({i+1} polls)")
      break
    time.sleep(0.01)
  else:
    print(f"  TIMEOUT fence=0x{vrd32(h, gpa+16):08X}")
    print(f"  RPTR=0x{sdma_rd('regSDMA0_QUEUE0_RB_RPTR'):X}")
    print(f"  WPTR=0x{sdma_rd('regSDMA0_QUEUE0_RB_WPTR'):X}")
    try: print(f"  STATUS=0x{sdma_rd('regSDMA0_STATUS_REG'):X}")
    except: pass
    return False

  # Read result from PCIe SRAM at 0x200000 via MRd TLP
  # (XDATA 0xF000 is a different SRAM bank; PCIe MRd is the correct readback path)
  # ASM2464PD SRAM byte-reverses each dword in the PCIe path.
  # GPU SDMA writes LE dwords. pcie_mem_read returns dwords through BE TLP path.
  # So raw MRd values need byte-reversal to recover the original LE bytes.
  raw_dwords = [pcie_mem_read(h, 0x200000 + o) for o in range(0, sz, 4)]
  # Each raw dword from pcie_mem_read is already interpreted as BE by pcie_probe.py.
  # The SRAM swaps bytes, so the actual SRAM content is the BE-interpreted value byte-reversed.
  # To get the original LE bytes the GPU wrote, we reverse each dword's bytes:
  got = b''.join(struct.pack('>I', dw) for dw in raw_dwords)
  print("  Expected:"); hexdump(pat[:sz], 0x200000)
  print("  Got:");      hexdump(got, 0x200000)
  ok = got == pat[:sz]
  if not ok:
    # Also try without byte swap in case SDMA data path doesn't swap
    got_noswap = b''.join(struct.pack('<I', dw) for dw in raw_dwords)
    ok2 = got_noswap == pat[:sz]
    if ok2:
      print("  (matched with LE interpretation)")
      got, ok = got_noswap, True
    else:
      print("  Raw dwords:", ' '.join(f'0x{dw:08X}' for dw in raw_dwords[:4]))
  print(f"\n  *** DMA {'PASS' if ok else 'FAIL'} ***")
  return ok

# ============================================================================
def main():
  h, ctx = usb_open()
  print("Opened device")

  usb_close(h, ctx)

  # Step 2: PCIe bringup via E4/E5 control transfers if link not up
  from pcie.pcie_bringup import ASM2464PD, full_bringup
  dev = ASM2464PD()
  dev.open()
  ltssm = dev.read8(0xB450)
  if ltssm not in (0x48, 0x78):
    print("PCIe link not up — running bringup...")
    full_bringup(dev)
  else:
    print(f"PCIe link already up (LTSSM=0x{ltssm:02X})")
  dev.close()

  # Step 3: Bridge + BAR setup via F0
  h, ctx = usb_open()
  from pcie.pcie_probe import setup_bridges, assign_bars, resize_bars
  gpu_bus = setup_bridges(h, 4)
  resize_bars(h, gpu_bus)
  assign_bars(h, gpu_bus)
  usb_close(h, ctx)

  # Step 4: Flash clean firmware for BOT support, then use tinygrad AMDev
  import os, subprocess
  print("\n--- Switching to clean firmware for tinygrad BOT ---")
  clean_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'clean')
  subprocess.check_call(['make', '-C', clean_dir, 'flash'], timeout=60)
  time.sleep(30)  # wait for clean firmware to boot + PCIe train

  # Step 5: Use tinygrad's AMDev for full GPU init
  os.environ.setdefault('GMMU', '0')
  os.environ.setdefault('USE_BOT', '1')
  os.environ.setdefault('AMD', '1')
  os.environ.setdefault('AMD_IFACE', 'USB')

  from tinygrad.runtime.support.system import USBPCIDevice
  from tinygrad.runtime.support.am.amdev import AMDev

  pci_dev = USBPCIDevice("AM", "usb:0")
  print(f"  USBPCIDevice ready, bars={list(pci_dev._bar_info.keys())}")

  adev = AMDev(pci_dev)
  print(f"  AMDev ready")
  print(f"  GC: {adev.ip_ver[am.GC_HWIP]}, SDMA: {adev.ip_ver[am.SDMA0_HWIP]}")
  print(f"  VRAM: {adev.vram_size >> 20} MB")

  # Verify GC is accessible
  grbm = adev.regGRBM_STATUS.read()
  adev.regSCRATCH_REG0.write(0xDEADBEEF)
  scratch = adev.regSCRATCH_REG0.read()
  gc_ok = scratch == 0xDEADBEEF
  print(f"  GRBM_STATUS: 0x{grbm:08X}")
  print(f"  SCRATCH test: {'PASS' if gc_ok else 'FAIL'} (0x{scratch:08X})")

  print(f"\n{'='*50}")
  print(f"  GPU: GC {adev.ip_ver.get(am.GC_HWIP)}, SDMA {adev.ip_ver.get(am.SDMA0_HWIP)}")
  print(f"  VRAM: {adev.vram_size >> 20} MB")
  print(f"  GC: {'PASS' if gc_ok else 'FAIL'}")
  return 0 if gc_ok else 1

if __name__ == "__main__":
  sys.exit(main())
