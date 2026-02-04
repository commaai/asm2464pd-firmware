/*
 * MMIO Proxy Firmware for ASM2464PD
 *
 * Protocol (binary):
 *   CMD_ECHO (0x00):  Send 1 byte, receive same byte back (loopback test)
 *   CMD_READ (0x01):  Send addr_hi, addr_lo -> receive value
 *   CMD_WRITE (0x02): Send addr_hi, addr_lo, value -> receive 0x00 (ACK)
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

void xdata_write(uint16_t addr, uint8_t val)
{
    if (is_uart_addr(addr)) return;
    *(__xdata volatile uint8_t *)addr = val;
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
            break;

        case CMD_READ:
            addr_hi = uart_getc();
            addr_lo = uart_getc();
            addr = ((uint16_t)addr_hi << 8) | addr_lo;
            val = xdata_read(addr);
            uart_putc(val);
            break;

        case CMD_WRITE:
            addr_hi = uart_getc();
            addr_lo = uart_getc();
            val = uart_getc();
            addr = ((uint16_t)addr_hi << 8) | addr_lo;
            xdata_write(addr, val);
            uart_putc(0x00);
            break;

        default:
            uart_putc('?');
            break;
        }
    }
}
