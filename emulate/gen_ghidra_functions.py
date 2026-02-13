#!/usr/bin/env python3
"""
Generate ghidra_functions.py from the Ghidra MCP server.

Connects to the Ghidra MCP HTTP endpoint, fetches all function names/addresses,
and writes them out as Python dicts keyed by bank (CODE=bank0, B1=bank1).

Usage:
    python emulate/gen_ghidra_functions.py [--mcp-url URL]

Requires the Ghidra MCP server to be running with a program open.
Default URL: http://localhost:8080/mcp
"""

import argparse
import json
import urllib.request
import os
import sys

MCP_DEFAULT_URL = "http://localhost:8080/mcp"
OUTPUT_FILE = os.path.join(os.path.dirname(__file__), "ghidra_functions.py")

# Address prefix -> bank number
BANK_MAP = {
    "CODE": 0,
    "B1": 1,
}


class McpClient:
    """Minimal MCP JSON-RPC client over Streamable HTTP."""

    def __init__(self, url):
        self.url = url
        self.session_id = None

    def _post(self, body_dict):
        """POST JSON to the MCP endpoint and return (headers, raw_body)."""
        data = json.dumps(body_dict).encode()
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        req = urllib.request.Request(self.url, data=data, headers=headers)
        with urllib.request.urlopen(req) as resp:
            sid = resp.headers.get("Mcp-Session-Id")
            if sid:
                self.session_id = sid
            return resp.read().decode()

    @staticmethod
    def _parse_sse(raw):
        """Extract JSON-RPC message from SSE or plain JSON response."""
        stripped = raw.lstrip()
        if stripped.startswith("{"):
            return json.loads(raw)
        # SSE format: lines of "id: ...", "event: ...", "data: ..."
        for line in raw.splitlines():
            if line.startswith("data:"):
                return json.loads(line[len("data:"):].strip())
        raise ValueError(f"Could not parse MCP response: {raw[:200]}")

    def request(self, method, params=None, req_id=1):
        """Send a JSON-RPC request and return the result."""
        body = {"jsonrpc": "2.0", "id": req_id, "method": method}
        if params:
            body["params"] = params
        raw = self._post(body)
        msg = self._parse_sse(raw)
        if "error" in msg:
            raise RuntimeError(f"MCP error: {msg['error']}")
        return msg.get("result")

    def initialize(self):
        """Perform MCP handshake."""
        return self.request("initialize", {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "gen_ghidra_functions", "version": "1.0"},
        }, req_id=0)

    def call_tool(self, name, arguments=None, req_id=2):
        """Call an MCP tool and return the text content."""
        result = self.request("tools/call", {
            "name": name,
            "arguments": arguments or {},
        }, req_id=req_id)
        text = ""
        for item in result.get("content", []):
            if item.get("type") == "text":
                text += item["text"]
        return text


def fetch_functions(mcp):
    """Fetch all functions from Ghidra via MCP."""
    all_functions = []
    offset = 0
    batch = 200
    while True:
        text = mcp.call_tool("list_functions", {"limit": batch, "offset": offset},
                             req_id=offset + 2)
        if not text:
            break

        # Parse lines like "- func_name @ CODE:addr (N params)"
        count = 0
        for line in text.splitlines():
            line = line.strip()
            if not line.startswith("- "):
                continue
            parts = line[2:].split(" @ ", 1)
            if len(parts) != 2:
                continue
            name = parts[0].strip()
            rest = parts[1].strip()
            # "CODE:0000 (0 params)" or "B1::ef4e (0 params)"
            addr_part = rest.split("(")[0].strip()
            all_functions.append((name, addr_part))
            count += 1

        if count == 0:
            break
        offset += batch
        print(f"  fetched {offset} functions...")

    return all_functions


def parse_address(addr_str):
    """Parse 'CODE:0000' or 'B1::ef4e' into (bank, int_addr)."""
    # Normalize separators: CODE:0000 or B1::ef4e
    for prefix, bank in BANK_MAP.items():
        if addr_str.startswith(prefix):
            # Strip prefix and any colons
            hex_part = addr_str[len(prefix):].lstrip(":")
            return bank, int(hex_part, 16)
    raise ValueError(f"Unknown address space in: {addr_str}")


def generate(functions):
    """Generate the ghidra_functions.py content."""
    bank0 = {}
    bank1 = {}

    for name, addr_str in functions:
        bank, addr = parse_address(addr_str)
        if bank == 0:
            bank0[addr] = name
        elif bank == 1:
            bank1[addr] = name

    lines = []
    lines.append('"""')
    lines.append("Function name mappings from Ghidra analysis of ASM2464PD firmware.")
    lines.append("")
    lines.append("Auto-generated by gen_ghidra_functions.py -- do not edit by hand.")
    lines.append("")
    lines.append("BANK0_FUNCTIONS: Functions in CODE bank (bank 0), addresses 0x0000-0xFFFF")
    lines.append("BANK1_FUNCTIONS: Functions in B1 bank (bank 1), overlaid on 0x8000-0xFFFF when DPX bit 0 is set")
    lines.append('"""')
    lines.append("")

    # Bank 0
    lines.append("BANK0_FUNCTIONS = {")
    for addr in sorted(bank0.keys()):
        lines.append(f"    0x{addr:04x}: \"{bank0[addr]}\",")
    lines.append("}")
    lines.append("")

    # Bank 1
    lines.append("BANK1_FUNCTIONS = {")
    for addr in sorted(bank1.keys()):
        lines.append(f"    0x{addr:04x}: \"{bank1[addr]}\",")
    lines.append("}")
    lines.append("")

    # lookup_function
    lines.append("")
    lines.append("def lookup_function(addr, bank=0):")
    lines.append('    """Look up a function name by address and bank number.')
    lines.append("")
    lines.append("    For addresses below 0x8000, always uses BANK0_FUNCTIONS (bank 0 code space).")
    lines.append("    For addresses >= 0x8000, uses BANK1_FUNCTIONS if bank==1, otherwise BANK0_FUNCTIONS.")
    lines.append("")
    lines.append("    Args:")
    lines.append("        addr: Integer address to look up.")
    lines.append("        bank: Bank number (0 or 1). Default is 0.")
    lines.append("")
    lines.append("    Returns:")
    lines.append("        Function name string if found, or None if no function at that address.")
    lines.append('    """')
    lines.append("    if addr < 0x8000:")
    lines.append("        return BANK0_FUNCTIONS.get(addr)")
    lines.append("    else:")
    lines.append("        if bank == 1:")
    lines.append("            return BANK1_FUNCTIONS.get(addr)")
    lines.append("        else:")
    lines.append("            return BANK0_FUNCTIONS.get(addr)")
    lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate ghidra_functions.py from Ghidra MCP")
    parser.add_argument("--mcp-url", default=MCP_DEFAULT_URL, help=f"MCP endpoint (default: {MCP_DEFAULT_URL})")
    parser.add_argument("-o", "--output", default=OUTPUT_FILE, help=f"Output file (default: {OUTPUT_FILE})")
    args = parser.parse_args()

    print(f"Connecting to Ghidra MCP at {args.mcp_url}...")
    mcp = McpClient(args.mcp_url)
    init = mcp.initialize()
    print(f"MCP server: {init.get('serverInfo', {}).get('name', '?')}")
    functions = fetch_functions(mcp)
    print(f"Got {len(functions)} functions")

    bank0_count = sum(1 for _, a in functions if a.startswith("CODE"))
    bank1_count = sum(1 for _, a in functions if a.startswith("B1"))
    print(f"  bank 0 (CODE): {bank0_count}")
    print(f"  bank 1 (B1):   {bank1_count}")

    content = generate(functions)
    with open(args.output, "w") as f:
        f.write(content)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
