/*
 * MMIO Proxy Firmware for ASM2464PD
 *
 * Protocol (binary):
 *   CMD_ECHO (0x00):  Send 1 byte -> receive 2 bytes: <value> <~value>
 *   CMD_READ (0x01):  Send addr_hi, addr_lo -> receive 2 bytes: <value> <~value>
 *   CMD_WRITE (0x02): Send addr_hi, addr_lo, value -> receive 2 bytes: 0x00 0xFF
 *
 * Interrupt signaling:
 *   When an interrupt fires, proxy sends: 0xFE 0xFE <int_num>
 *   int_num: 0=INT0, 1=Timer0, 2=INT1, 3=Timer1, 4=Serial, etc.
 *
 * All responses are 2 bytes. If first two bytes are 0xFE 0xFE, it's an interrupt.
 * Otherwise byte[0] is value and byte[1] is ~value (complement for verification).
 */

typedef unsigned char uint8_t;
typedef unsigned int uint16_t;

/* UART registers */
#define REG_UART_RBR    (*(__xdata volatile uint8_t *)0xC000)
#define REG_UART_THR    (*(__xdata volatile uint8_t *)0xC001)
#define REG_UART_RFBR   (*(__xdata volatile uint8_t *)0xC005)
#define REG_UART_LCR    (*(__xdata volatile uint8_t *)0xC007)

/* Protocol commands */
#define CMD_ECHO        0x00
#define CMD_READ        0x01
#define CMD_WRITE       0x02
#define CMD_SFR_READ    0x03
#define CMD_SFR_WRITE   0x04

/* Interrupt signal - double 0xFE prefix to avoid confusion with data */
#define INT_SIGNAL      0xFE

void uart_putc(uint8_t c)
{
    REG_UART_THR = c;
}

uint8_t uart_getc(void)
{
    /* Wait for data in RX FIFO */
    while (REG_UART_RFBR == 0)
        ;
    return REG_UART_RBR;
}

uint8_t is_uart_addr(uint16_t addr)
{
    return (addr >= 0xC000 && addr <= 0xC00F);
}

uint8_t xdata_read(uint16_t addr)
{
    if (is_uart_addr(addr)) {
        if (addr == 0xC009) return 0x60;
        return 0x00;
    }
    return *(__xdata volatile uint8_t *)addr;
}

/*
 * SFR access - SFRs are at 0x80-0xFF and require direct addressing.
 * We use inline assembly since C can't do arbitrary SFR access.
 */
__sfr __at(0xA8) SFR_IE;    /* Interrupt Enable */
__sfr __at(0xB8) SFR_IP;    /* Interrupt Priority */
__sfr __at(0x88) SFR_TCON;  /* Timer Control */
__sfr __at(0x89) SFR_TMOD;  /* Timer Mode */
__sfr __at(0x8A) SFR_TL0;   /* Timer 0 Low */
__sfr __at(0x8B) SFR_TL1;   /* Timer 1 Low */
__sfr __at(0x8C) SFR_TH0;   /* Timer 0 High */
__sfr __at(0x8D) SFR_TH1;   /* Timer 1 High */
__sfr __at(0xD0) SFR_PSW;   /* Program Status Word */
__sfr __at(0xE0) SFR_ACC;   /* Accumulator */
__sfr __at(0xF0) SFR_B;     /* B Register */
__sfr __at(0x81) SFR_SP;    /* Stack Pointer */
__sfr __at(0x82) SFR_DPL;   /* Data Pointer Low */
__sfr __at(0x83) SFR_DPH;   /* Data Pointer High */
__sfr __at(0x87) SFR_PCON;  /* Power Control */

uint8_t sfr_read(uint8_t addr)
{
    switch (addr) {
        case 0xA8: return SFR_IE;
        case 0xB8: return SFR_IP;
        case 0x88: return SFR_TCON;
        case 0x89: return SFR_TMOD;
        case 0x8A: return SFR_TL0;
        case 0x8B: return SFR_TL1;
        case 0x8C: return SFR_TH0;
        case 0x8D: return SFR_TH1;
        case 0xD0: return SFR_PSW;
        case 0xE0: return SFR_ACC;
        case 0xF0: return SFR_B;
        case 0x81: return SFR_SP;
        case 0x82: return SFR_DPL;
        case 0x83: return SFR_DPH;
        case 0x87: return SFR_PCON;
        default: return 0x00;
    }
}

void sfr_write(uint8_t addr, uint8_t val)
{
    switch (addr) {
        case 0xA8: SFR_IE = val; break;
        case 0xB8: SFR_IP = val; break;
        case 0x88: SFR_TCON = val; break;
        case 0x89: SFR_TMOD = val; break;
        case 0x8A: SFR_TL0 = val; break;
        case 0x8B: SFR_TL1 = val; break;
        case 0x8C: SFR_TH0 = val; break;
        case 0x8D: SFR_TH1 = val; break;
        case 0xD0: SFR_PSW = val; break;
        case 0xE0: SFR_ACC = val; break;
        case 0xF0: SFR_B = val; break;
        case 0x81: SFR_SP = val; break;
        case 0x82: SFR_DPL = val; break;
        case 0x83: SFR_DPH = val; break;
        case 0x87: SFR_PCON = val; break;
        default: break;
    }
}

void xdata_write(uint16_t addr, uint8_t val)
{
    if (is_uart_addr(addr)) return;
    *(__xdata volatile uint8_t *)addr = val;
}

/* Send interrupt signal: 0xFE 0xFE <int_num> */
void signal_interrupt(uint8_t int_num)
{
    uart_putc(INT_SIGNAL);
    uart_putc(INT_SIGNAL);
    uart_putc(int_num);
}

/*
 * Interrupt handlers - signal to emulator and return
 * The emulator will run the actual ISR code
 */
void int0_isr(void) __interrupt(0)
{
    signal_interrupt(0x00);  /* INT0 */
}

void timer0_isr(void) __interrupt(1)
{
    signal_interrupt(0x01);  /* Timer0 */
}

void int1_isr(void) __interrupt(2)
{
    signal_interrupt(0x02);  /* INT1 */
}

void timer1_isr(void) __interrupt(3)
{
    signal_interrupt(0x03);  /* Timer1 */
}

void serial_isr(void) __interrupt(4)
{
    signal_interrupt(0x04);  /* Serial */
}

void timer2_isr(void) __interrupt(5)
{
    signal_interrupt(0x05);  /* Timer2 */
}

void main(void)
{
    uint8_t cmd, val;
    uint8_t addr_hi, addr_lo;
    uint16_t addr;

    /* Disable parity to get 8N1 */
    REG_UART_LCR &= 0xF7;
    
    /* Hello */
    uart_putc('P');
    uart_putc('K');
    uart_putc('\n');

    /* Proxy loop */
    while (1) {
        cmd = uart_getc();

        switch (cmd) {
        case CMD_ECHO:
            val = uart_getc();
            uart_putc(val);
            uart_putc(~val);  /* complement for verification */
            break;

        case CMD_READ:
            addr_hi = uart_getc();
            addr_lo = uart_getc();
            addr = ((uint16_t)addr_hi << 8) | addr_lo;
            val = xdata_read(addr);
            uart_putc(val);
            uart_putc(~val);  /* complement for verification */
            break;

        case CMD_WRITE:
            addr_hi = uart_getc();
            addr_lo = uart_getc();
            val = uart_getc();
            addr = ((uint16_t)addr_hi << 8) | addr_lo;
            xdata_write(addr, val);
            uart_putc(0x00);
            uart_putc(0xFF);  /* complement of 0x00 */
            break;

        case CMD_SFR_READ:
            addr_lo = uart_getc();  /* SFR address (0x80-0xFF) */
            val = sfr_read(addr_lo);
            uart_putc(val);
            uart_putc(~val);
            break;

        case CMD_SFR_WRITE:
            addr_lo = uart_getc();  /* SFR address (0x80-0xFF) */
            val = uart_getc();
            sfr_write(addr_lo, val);
            uart_putc(0x00);
            uart_putc(0xFF);
            break;

        default:
            uart_putc('?');
            uart_putc('?');  /* 2-byte response for unknown */
            break;
        }
    }
}
