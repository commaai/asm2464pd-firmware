## Usage

Run with full proxy:
```bash
python3 emulate/emu.py --proxy fw.bin --max-cycles 2000000
```

Run with selective masking to reduce proxy calls:
```bash
python3 emulate/emu.py --proxy \
  --proxy-mask 0x6000-0x9000 \
  --proxy-mask 0x9400-0x9E00 \
  --proxy-mask 0x9F00-0xC800 \
  --proxy-mask 0xC900-0xCC00 \
  --proxy-mask 0xCD00-0xE700 \
  --proxy-mask 0xE800-0xFFFF \
  fw.bin
```
