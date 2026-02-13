#!/usr/bin/env python3
"""Extract high-level firmware flow from enumerate_robbe_calls trace.
Produces a compact, human-readable markdown document."""

import re
import sys
from collections import OrderedDict

TRACE_FILE = '/home/batman/asm2464pd-firmware/trace/enumerate_robbe_calls'
OUT_FILE = '/home/batman/asm2464pd-firmware/docs/enumerate_fw_flow.md'


def parse_trace(filename):
    """Parse trace into structured events."""
    events = []
    with open(filename) as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip()
            if not line:
                continue

            # Interrupt trigger
            m = re.match(r'^\[PROXY\] >>> INTERRUPT mask=0x(\w+) \((\w+)\)', line)
            if m:
                events.append({'type': 'INTERRUPT', 'line': lineno, 'name': m.group(2)})
                continue

            # ISR entry
            m = re.search(r'>>> (INT\d): \S+ \(0x(\w+)', line)
            if m:
                events.append({'type': 'ISR_ENTRY', 'line': lineno, 'name': m.group(1), 'addr': m.group(2)})
                continue

            # ISR return
            m = re.search(r'(?:RETI|RET)\s+\(ISR:(\w+)\)', line)
            if m:
                events.append({'type': 'ISR_RET', 'line': lineno, 'name': m.group(1)})
                continue

            # Cycle count
            cycle_m = re.match(r'\[\s*(\d+)\]', line)
            cycle = int(cycle_m.group(1)) if cycle_m else None

            # CALL with depth
            m = re.match(r'\[\s*\d+\]\s*((?:\s{2})*)CALL\s+(\S+)\s+\(0x(\w+)\s+bank=(\d+)\)', line)
            if m:
                depth = len(m.group(1)) // 2
                events.append({'type': 'CALL', 'line': lineno, 'name': m.group(2),
                              'addr': m.group(3), 'bank': int(m.group(4)), 'cycle': cycle, 'depth': depth})
                continue

            # RET with depth
            m = re.match(r'\[\s*\d+\]\s*((?:\s{2})*)RET\s+\((\S+)\)', line)
            if m:
                depth = len(m.group(1)) // 2
                events.append({'type': 'RET', 'line': lineno, 'name': m.group(2), 'cycle': cycle, 'depth': depth})
                continue

            # Register Read
            m = re.match(r'\[\s*\d+\] PC=0x(\w+) Read\s+0x(\w+) = 0x(\w+)\s*(.*)', line)
            if m:
                events.append({'type': 'READ', 'line': lineno, 'pc': m.group(1),
                              'addr': m.group(2), 'val': m.group(3), 'name': m.group(4).strip(), 'cycle': cycle})
                continue

            # Register Write
            m = re.match(r'\[\s*\d+\] PC=0x(\w+) Write\s+0x(\w+) = 0x(\w+)\s*(.*)', line)
            if m:
                events.append({'type': 'WRITE', 'line': lineno, 'pc': m.group(1),
                              'addr': m.group(2), 'val': m.group(3), 'name': m.group(4).strip(), 'cycle': cycle})
                continue

            # UART
            m = re.search(r'\[UART\]\s+(.*)', line)
            if m:
                events.append({'type': 'UART', 'line': lineno, 'text': m.group(1), 'cycle': cycle})
                continue

    return events


def split_phases(events):
    """Split into boot, ISR, and main-loop phases."""
    phases = []
    current = {'name': 'Boot/Init', 'start': 1, 'events': []}

    int_counts = {}
    in_isr = False

    for ev in events:
        if ev['type'] == 'INTERRUPT':
            iname = ev['name']
            int_counts[iname] = int_counts.get(iname, 0) + 1
            if not in_isr:
                if current['events']:
                    phases.append(current)
                current = {'name': f"{iname} #{int_counts[iname]}", 'start': ev['line'], 'events': [ev]}
                in_isr = True
            else:
                # Nested interrupt
                current['events'].append(ev)
        elif ev['type'] == 'ISR_RET':
            current['events'].append(ev)
            phases.append(current)
            current = {'name': 'Main Loop', 'start': ev['line'], 'events': []}
            in_isr = False
        else:
            current['events'].append(ev)

    if current['events']:
        phases.append(current)
    return phases


def group_init_calls(events):
    """Group init phase top-level calls into logical blocks.
    Returns list of (group_name, [(func_name, addr, bank, cycle, line), ...])"""

    # Extract top-level calls with their cycle times
    calls = []
    for ev in events:
        if ev['type'] == 'CALL' and ev['depth'] == 0:
            calls.append((ev['name'], ev['addr'], ev['bank'], ev['cycle'], ev['line']))

    if not calls:
        return []

    # Group consecutive calls to the same function
    groups = []
    current_group = [calls[0]]
    for c in calls[1:]:
        # Check if same function or part of a tight sequence
        if c[0] == current_group[-1][0]:
            current_group.append(c)
        elif current_group[-1][3] and c[3] and (c[3] - current_group[-1][3]) > 3000:
            # Big gap = new group
            groups.append(current_group)
            current_group = [c]
        else:
            current_group.append(c)
    groups.append(current_group)

    # Now collapse each group
    result = []
    for group in groups:
        # Count function occurrences
        func_counts = OrderedDict()
        for name, addr, bank, cycle, line in group:
            key = name
            if key not in func_counts:
                func_counts[key] = {'count': 0, 'addr': addr, 'bank': bank,
                                    'first_cycle': cycle, 'last_cycle': cycle, 'first_line': line}
            func_counts[key]['count'] += 1
            func_counts[key]['last_cycle'] = cycle

        # Determine group time range
        first_cycle = group[0][3] or 0
        last_cycle = group[-1][3] or 0

        # Create compact representation
        entries = []
        for fname, info in func_counts.items():
            if info['count'] > 1:
                entries.append(f"{fname}() x{info['count']}")
            else:
                entries.append(f"{fname}()")

        result.append({
            'cycle_start': first_cycle,
            'cycle_end': last_cycle,
            'line_start': group[0][4],
            'line_end': group[-1][4],
            'total_calls': len(group),
            'unique_funcs': len(func_counts),
            'entries': entries,
        })

    return result


def decode_setup_packet(fields):
    """Decode setup packet bytes to human-readable."""
    bmReq = int(fields.get('bmReq', '0'), 16)
    bReq = int(fields.get('bReq', '0'), 16)
    wVal = int(fields.get('wValH', '0'), 16) << 8 | int(fields.get('wValL', '0'), 16)
    wIdx = int(fields.get('wIdxH', '0'), 16) << 8 | int(fields.get('wIdxL', '0'), 16)
    wLen = int(fields.get('wLenH', '0'), 16) << 8 | int(fields.get('wLenL', '0'), 16)

    req_names = {
        0x00: 'GET_STATUS', 0x01: 'CLEAR_FEATURE', 0x03: 'SET_FEATURE',
        0x05: 'SET_ADDRESS', 0x06: 'GET_DESCRIPTOR', 0x08: 'GET_CONFIGURATION',
        0x09: 'SET_CONFIGURATION', 0x0B: 'SET_INTERFACE',
        0x30: 'SET_SEL', 0x31: 'SET_ISOCH_DELAY',
    }
    desc_types = {1: 'DEVICE', 2: 'CONFIGURATION', 3: 'STRING', 15: 'BOS'}

    rname = req_names.get(bReq, f'0x{bReq:02X}')
    s = f"{rname}"

    if bReq == 0x05:
        s += f"(addr={wVal})"
    elif bReq == 0x06:
        dt = (wVal >> 8) & 0xFF
        di = wVal & 0xFF
        tname = desc_types.get(dt, f'type_{dt}')
        s += f"({tname}"
        if di: s += f", idx={di}"
        s += f", len={wLen})"
    elif bReq == 0x09:
        s += f"(config={wVal})"
    elif bReq == 0x0B:
        s += f"(alt={wVal}, iface={wIdx})"
    elif bReq == 0x31:
        s += f"(delay={wVal})"
    elif bReq == 0x30:
        s += f"(len={wLen})"

    direction = "IN" if bmReq & 0x80 else "OUT"
    return s, direction, bmReq, bReq, wVal, wIdx, wLen


def extract_isr_summary(phase):
    """Extract compact summary of an ISR phase."""
    evts = phase['events']

    # Get 9101 reads (peripheral status)
    status_vals = [ev['val'] for ev in evts if ev['type'] == 'READ' and ev['addr'] == '9101']

    # Get setup packet
    pkt = {}
    addr_to_field = {
        '9104': 'bmReq', '9105': 'bReq', '9106': 'wValL', '9107': 'wValH',
        '9108': 'wIdxL', '9109': 'wIdxH', '910A': 'wLenL', '910B': 'wLenH'
    }
    for ev in evts:
        if ev['type'] == 'READ' and ev['addr'] in addr_to_field:
            pkt[addr_to_field[ev['addr']]] = ev['val']

    # Get 9091 operations
    phase_ops = []
    for ev in evts:
        if ev['addr'] == '9091' if 'addr' in ev else False:
            if ev['type'] == 'READ':
                phase_ops.append(('R', ev['val']))
            elif ev['type'] == 'WRITE':
                phase_ops.append(('W', ev['val']))

    # Get 9092 operations
    dma_ops = []
    for ev in evts:
        if 'addr' in ev and ev['addr'] == '9092':
            if ev['type'] == 'WRITE':
                dma_ops.append(('W', ev['val']))
            elif ev['type'] == 'READ':
                dma_ops.append(('R', ev['val']))

    # Get descriptor writes (0x9Exx)
    desc_writes = []
    for ev in evts:
        if ev['type'] == 'WRITE' and 'addr' in ev and ev['addr'].startswith('9E'):
            desc_writes.append((ev['addr'], ev['val']))

    # Get endpoint config writes (0x92xx)
    ep_writes = []
    for ev in evts:
        if ev['type'] == 'WRITE' and 'addr' in ev and ev['addr'].startswith('92'):
            ep_writes.append((ev['addr'], ev['val']))

    # Get 9003/9004 operations
    ep0_ops = []
    for ev in evts:
        if 'addr' in ev and ev['addr'] in ('9003', '9004'):
            ep0_ops.append((ev['type'][0], ev['addr'], ev['val']))

    # Key function calls (depth 0 and 1)
    func_calls = []
    for ev in evts:
        if ev['type'] == 'CALL' and ev['depth'] <= 1:
            prefix = "  " if ev['depth'] == 1 else ""
            func_calls.append(f"{prefix}{ev['name']}()")

    # Interrupt info
    c802_vals = [ev['val'] for ev in evts if ev['type'] == 'READ' and ev.get('addr') == 'C802']
    c806_vals = [ev['val'] for ev in evts if ev['type'] == 'READ' and ev.get('addr') == 'C806']

    return {
        'status_9101': status_vals,
        'setup_packet': pkt,
        'phase_9091': phase_ops,
        'dma_9092': dma_ops,
        'desc_writes': desc_writes,
        'ep_writes': ep_writes,
        'ep0_ops': ep0_ops,
        'func_calls': func_calls,
        'c802': c802_vals,
        'c806': c806_vals,
    }


def compact_phase_ops(ops):
    """Deduplicate consecutive identical ops."""
    if not ops:
        return []
    result = [(ops[0], 1)]
    for op in ops[1:]:
        if op == result[-1][0]:
            result[-1] = (result[-1][0], result[-1][1] + 1)
        else:
            result.append((op, 1))
    return result


def main():
    print("Parsing...", file=sys.stderr)
    events = parse_trace(TRACE_FILE)
    print(f"  {len(events)} events", file=sys.stderr)

    phases = split_phases(events)
    print(f"  {len(phases)} phases", file=sys.stderr)

    # Collect all setup packets for overview
    all_packets = []
    for phase in phases:
        if not phase['name'].startswith('INT0'):
            continue
        s = extract_isr_summary(phase)
        if s['setup_packet']:
            decoded, direction, *_ = decode_setup_packet(s['setup_packet'])
            all_packets.append((phase['name'], phase['start'], decoded, direction))

    doc = []
    w = doc.append

    w("# ASM2464PD Firmware Enumeration Flow")
    w("")
    w("Extracted from `trace/enumerate_robbe_calls` — 33K lines, 258K instructions, 400K cycles.")
    w("")
    w("---")
    w("")
    w("## USB Enumeration Summary")
    w("")
    w("Complete USB enumeration sequence observed in the trace:")
    w("")
    w("| # | ISR | Direction | Request |")
    w("|---|-----|-----------|---------|")
    for i, (pname, pline, decoded, direction) in enumerate(all_packets, 1):
        w(f"| {i} | {pname} (L{pline}) | {direction} | `{decoded}` |")
    w("")

    # ---- BOOT/INIT ----
    w("---")
    w("")
    w("## Phase 1: Boot & Hardware Init")
    w("")
    w("~183K cycles of hardware initialization before any interrupts fire.")
    w("")

    boot_phases = [p for p in phases if p['name'] == 'Boot/Init']
    if boot_phases:
        boot = boot_phases[0]
        groups = group_init_calls(boot['events'])

        w("### Init Call Sequence (grouped by timing)")
        w("")

        for i, g in enumerate(groups):
            cycle_range = f"{g['cycle_start']}-{g['cycle_end']}" if g['cycle_start'] != g['cycle_end'] else f"{g['cycle_start']}"
            w(f"**Block {i+1}** (cycles {cycle_range}, lines {g['line_start']}-{g['line_end']}, {g['total_calls']} calls):")

            # Show entries, collapsing if too many
            if len(g['entries']) <= 15:
                for e in g['entries']:
                    w(f"  - {e}")
            else:
                # Show first 5, last 5, count in middle
                for e in g['entries'][:5]:
                    w(f"  - {e}")
                w(f"  - ... ({len(g['entries']) - 10} more functions) ...")
                for e in g['entries'][-5:]:
                    w(f"  - {e}")
            w("")

        # Key register writes during init
        important = {'C800', 'C801', 'C802', 'C806', 'C809', '9091', '9092', '9090',
                    '9000', '9001', '9002', '92C2', '92E1', '92F7'}
        init_reg_writes = [(ev['addr'], ev['val'], ev['name'], ev['line'])
                          for ev in boot['events']
                          if ev['type'] == 'WRITE' and ev.get('addr', '') in important]

        if init_reg_writes:
            w("### Key Register Writes During Init")
            w("")
            for addr, val, name, line in init_reg_writes:
                w(f"- `0x{addr} = 0x{val}` {name} (L{line})")
            w("")

    # ---- INT1 (Power) ----
    w("---")
    w("")
    w("## Phase 2: INT1 — Power/Link Event")
    w("")

    for phase in phases:
        if not phase['name'].startswith('INT1'):
            continue

        w(f"### {phase['name']} (line {phase['start']})")
        w("")

        # Show key register ops
        key_reads = [(ev['addr'], ev['val'], ev['name'])
                    for ev in phase['events']
                    if ev['type'] == 'READ' and ev.get('addr', '') in
                    {'C806', '92E1', '92C2', '92F7', 'C800', 'C802'}]
        key_writes = [(ev['addr'], ev['val'], ev['name'])
                     for ev in phase['events']
                     if ev['type'] == 'WRITE' and ev.get('addr', '') in
                     {'C806', '92E1', '92C2', '92F7', 'C800', 'C802'}]

        if key_reads:
            w("Reads:")
            for a, v, n in key_reads:
                w(f"  - `0x{a} = 0x{v}` {n}")
        if key_writes:
            w("Writes:")
            for a, v, n in key_writes:
                w(f"  - `0x{a} = 0x{v}` {n}")

        # Functions
        funcs = [ev['name'] for ev in phase['events'] if ev['type'] == 'CALL' and ev['depth'] <= 1]
        if funcs:
            w("")
            w("Functions called:")
            seen = set()
            for f in funcs:
                if f not in seen:
                    w(f"  - `{f}()`")
                    seen.add(f)
        w("")

    # ---- INT0 (USB) ----
    w("---")
    w("")
    w("## Phase 3: INT0 — USB Enumeration")
    w("")
    w("Each INT0 handles one or more USB control transfer phases.")
    w("")

    for phase in phases:
        if not phase['name'].startswith('INT0'):
            continue

        s = extract_isr_summary(phase)
        w(f"### {phase['name']} (line {phase['start']})")
        w("")

        # Setup packet
        if s['setup_packet']:
            decoded, direction, bmReq, bReq, wVal, wIdx, wLen = decode_setup_packet(s['setup_packet'])
            w(f"**Setup Packet:** `{decoded}` ({direction})")
            w(f"  - Raw: bmReq=0x{bmReq:02X} bReq=0x{bReq:02X} wVal=0x{wVal:04X} wIdx=0x{wIdx:04X} wLen={wLen}")
        else:
            # No setup packet - probably a continuation
            w("*(No setup packet read — continuation of previous transfer)*")

        w("")

        # Peripheral status
        if s['status_9101']:
            vals = list(dict.fromkeys(s['status_9101']))  # unique, preserving order
            w(f"**Peripheral Status (0x9101):** {', '.join('0x' + v for v in vals)}")
            flags = []
            for v in vals:
                vi = int(v, 16)
                if vi & 0x02: flags.append("SETUP")
                if vi & 0x08: flags.append("BUFFER")
                if vi & 0x10: flags.append("LINK")
            if flags:
                w(f"  Flags: {', '.join(set(flags))}")
            w("")

        # Control phase ops
        if s['phase_9091']:
            compact = compact_phase_ops(s['phase_9091'])
            ops_str = []
            for (rw, val), count in compact:
                prefix = "Read" if rw == 'R' else "**Write**"
                suffix = f" x{count}" if count > 1 else ""
                ops_str.append(f"{prefix} 0x{val}{suffix}")
            w(f"**Control Phase (0x9091):** {' → '.join(ops_str)}")
            w("")

        # DMA ops
        if s['dma_9092']:
            ops_str = []
            for rw, val in s['dma_9092']:
                prefix = "Read" if rw == 'R' else "**Write**"
                vi = int(val, 16)
                meaning = ""
                if rw == 'W' and vi == 0x04: meaning = " (DMA send)"
                elif rw == 'W' and vi == 0x08: meaning = " (DMA complete)"
                elif rw == 'R' and vi == 0x00: meaning = " (done)"
                ops_str.append(f"{prefix} 0x{val}{meaning}")
            w(f"**DMA Trigger (0x9092):** {' → '.join(ops_str)}")
            w("")

        # EP0 ops
        if s['ep0_ops']:
            ops_str = []
            for rw, addr, val in s['ep0_ops']:
                prefix = "Read" if rw == 'R' else "Write"
                ops_str.append(f"{prefix} 0x{addr}=0x{val}")
            w(f"**EP0 (0x9003/9004):** {', '.join(ops_str)}")
            w("")

        # Descriptor writes
        if s['desc_writes']:
            data_bytes = [v for _, v in s['desc_writes']]
            addrs = [a for a, _ in s['desc_writes']]
            w(f"**Descriptor Buffer (0x{addrs[0]}-0x{addrs[-1]}):** `{' '.join(data_bytes)}`")
            # Try to decode
            if len(data_bytes) >= 2:
                b0 = int(data_bytes[0], 16)
                b1 = int(data_bytes[1], 16)
                desc_names = {0x01: 'DEVICE', 0x02: 'CONFIGURATION', 0x03: 'STRING',
                             0x05: 'ENDPOINT', 0x0F: 'BOS', 0x10: 'DEVICE_CAPABILITY'}
                if b1 in desc_names:
                    w(f"  → {desc_names[b1]} descriptor ({b0} bytes)")
            w("")

        # Endpoint config writes
        if s['ep_writes']:
            ep_strs = [f"0x{a}=0x{v}" for a, v in s['ep_writes']]
            w(f"**USB Config Writes:** {', '.join(ep_strs)}")
            w("")

        # Key function calls (compact)
        if s['func_calls']:
            # Deduplicate
            seen = []
            for f in s['func_calls']:
                if f not in seen:
                    seen.append(f)
            if len(seen) <= 10:
                w("**Functions:** " + ", ".join(f"`{f}`" for f in seen))
            else:
                w("**Functions:**")
                for f in seen[:8]:
                    w(f"  - `{f}`")
                w(f"  - ... and {len(seen) - 8} more")
            w("")

        w("")

    # ---- MAIN LOOP ----
    w("---")
    w("")
    w("## Phase 4: Main Loop")
    w("")
    w("Between interrupts, the firmware polls in a main loop:")
    w("")

    ml_funcs = set()
    for p in phases:
        if p['name'] != 'Main Loop':
            continue
        for ev in p['events']:
            if ev['type'] == 'CALL' and ev['depth'] == 0:
                ml_funcs.add((ev['name'], ev['addr']))

    for name, addr in sorted(ml_funcs, key=lambda x: x[1]):
        w(f"- `{name}()` @ 0x{addr}")
    w("")

    # ---- REGISTER SUMMARY ----
    w("---")
    w("")
    w("## Appendix: Register Access Summary")
    w("")

    reg_reads = {}
    reg_writes = {}
    reg_names = {}
    for ev in events:
        if ev['type'] == 'READ':
            a = ev['addr']
            reg_reads[a] = reg_reads.get(a, 0) + 1
            if ev['name']: reg_names[a] = ev['name']
        elif ev['type'] == 'WRITE':
            a = ev['addr']
            reg_writes[a] = reg_writes.get(a, 0) + 1
            if ev['name']: reg_names[a] = ev['name']

    all_regs = set(list(reg_reads.keys()) + list(reg_writes.keys()))
    sorted_regs = sorted(all_regs, key=lambda r: reg_reads.get(r, 0) + reg_writes.get(r, 0), reverse=True)

    w("### Top 30 Most Accessed Registers")
    w("")
    w("| Register | Reads | Writes | Total | Name |")
    w("|----------|-------|--------|-------|------|")
    for reg in sorted_regs[:30]:
        r = reg_reads.get(reg, 0)
        wr = reg_writes.get(reg, 0)
        name = reg_names.get(reg, '')
        w(f"| 0x{reg} | {r} | {wr} | {r+wr} | {name} |")
    w("")

    # ---- DETAILED USB REGISTER SEQUENCES ----
    w("---")
    w("")
    w("## Appendix: Detailed USB Register Sequences")
    w("")
    w("Exact register-level sequences for ISRs that handle setup packets or DMA.")
    w("")

    for phase in phases:
        if not phase['name'].startswith('INT0'):
            continue

        has_setup = any(ev['type'] == 'READ' and ev.get('addr') == '9104' for ev in phase['events'])
        has_dma = any(ev['type'] == 'WRITE' and ev.get('addr') == '9092' for ev in phase['events'])

        if not has_setup and not has_dma:
            continue

        w(f"### {phase['name']} — Register Sequence")
        w("")
        w("```")

        line_count = 0
        max_lines = 80  # Cap each sequence
        total_ops = 0

        for ev in phase['events']:
            shown = False
            if ev['type'] in ('READ', 'WRITE'):
                a = ev.get('addr', '')
                if a.startswith('91') or a.startswith('90') or a.startswith('92') or \
                   a.startswith('9E') or a in ('C800', 'C801', 'C802', 'C806'):
                    total_ops += 1
                    if line_count < max_lines:
                        rw = 'Read ' if ev['type'] == 'READ' else 'Write'
                        w(f"{rw} 0x{a} = 0x{ev['val']}  {ev.get('name', '')}")
                        line_count += 1
                    shown = True
            elif ev['type'] == 'CALL' and ev['depth'] <= 1:
                total_ops += 1
                if line_count < max_lines:
                    indent = "  " * ev['depth']
                    w(f"{indent}-> {ev['name']}()")
                    line_count += 1

        if total_ops > max_lines:
            w(f"... ({total_ops - max_lines} more register operations) ...")

        w("```")
        w("")

    # Write output
    output = "\n".join(doc)
    with open(OUT_FILE, 'w') as f:
        f.write(output)

    print(f"Written {len(output)} bytes, {output.count(chr(10))} lines to {OUT_FILE}", file=sys.stderr)


if __name__ == '__main__':
    main()
