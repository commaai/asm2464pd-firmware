/*
 * UART Test Firmware for ASM2464PD
 *
 * Comprehensive test of UART RX using polling-based change detection.
 * Based on ASM1142 register documentation.
 */

typedef unsigned char uint8_t;
typedef unsigned int uint16_t;

/* UART registers (0xC000-0xC00F based on ASM2464PD, similar to ASM1142 at 0xF100-0xF10A) */
#define REG_UART_RBR    (*(__xdata volatile uint8_t *)0xC000)  /* Receive Buffer Register */
#define REG_UART_THR    (*(__xdata volatile uint8_t *)0xC001)  /* Transmit Holding Register */
#define REG_UART_IER    (*(__xdata volatile uint8_t *)0xC002)  /* Interrupt Enable Register */
#define REG_UART_IIR    (*(__xdata volatile uint8_t *)0xC004)  /* Interrupt Identification Register (RO) */
#define REG_UART_FCR    (*(__xdata volatile uint8_t *)0xC004)  /* FIFO Control Register (WO) */
#define REG_UART_LCR    (*(__xdata volatile uint8_t *)0xC007)  /* Line Control Register */
#define REG_UART_MCR    (*(__xdata volatile uint8_t *)0xC008)  /* Modem Control Register */
#define REG_UART_LSR    (*(__xdata volatile uint8_t *)0xC009)  /* Line Status Register */
#define REG_UART_RFBR   (*(__xdata volatile uint8_t *)0xC005)  /* RX FIFO Bytes Received (ASM1142: 0xF105) */
#define REG_UART_TFBF   (*(__xdata volatile uint8_t *)0xC006)  /* TX FIFO Bytes Free (ASM1142: 0xF106) */

/* LCR bits */
#define LCR_DATA_BITS_MASK  0x03  /* Bits 0-1: Data bits (0=5, 1=6, 2=7, 3=8) */
#define LCR_STOP_BITS       0x04  /* Bit 2: Stop bits (0=1, 1=2) */
#define LCR_PARITY_MASK     0x38  /* Bits 3-5: Parity (0xx=None, 001=Odd, 011=Even) */
#define LCR_PARITY_NONE     0x00
#define LCR_PARITY_ODD      0x08
#define LCR_PARITY_EVEN     0x18

/* LSR bits */
#define LSR_RX_FIFO_OVERFLOW 0x01  /* Bit 0: RX FIFO overflow (RW1C) */
#define LSR_TX_EMPTY        0x20  /* Bit 5: Transmitter empty */

/* IIR bits */
#define IIR_RFBR            0x01  /* Bit 0: RX FIFO bytes received interrupt */
#define IIR_TFBF            0x02  /* Bit 1: TX FIFO bytes free interrupt */
#define IIR_LSR_BIT1        0x04  /* Bit 2: LSR bit 1 */
#define IIR_CHAR_RECEIVED   0x08  /* Bit 3: Character received (pulses high) */

void uart_putc(uint8_t c)
{
    REG_UART_THR = c;
}

void uart_puts(__code const char *s)
{
    char c;
    while ((c = *s++) != '\0') {
        uart_putc(c);
    }
}

void uart_puthex(uint8_t val)
{
    uint8_t hi = (val >> 4) & 0x0F;
    uint8_t lo = val & 0x0F;
    uart_putc(hi < 10 ? '0' + hi : 'A' + hi - 10);
    uart_putc(lo < 10 ? '0' + lo : 'A' + lo - 10);
}

/*
 * uart_getc_poll - Read one byte using polling-based change detection
 * 
 * The ASM2464PD UART doesn't have a reliable RX ready flag in LSR.
 * Instead, we poll RBR for value changes. When a byte arrives, RBR
 * changes to the new value and holds steady.
 */
uint8_t uart_getc_poll(void)
{
    uint8_t data, last_data;
    uint16_t stable_count;
    
    /* Get initial value */
    last_data = REG_UART_RBR;
    
    /* Poll until value changes */
    while (1) {
        data = REG_UART_RBR;
        if (data != last_data) {
            /* Value changed - wait for stabilization */
            stable_count = 0;
            while (stable_count < 500) {
                if (REG_UART_RBR == data) {
                    stable_count++;
                } else {
                    /* Still changing, reset counter */
                    data = REG_UART_RBR;
                    stable_count = 0;
                }
            }
            return data;
        }
        last_data = data;
    }
}

void dump_uart_regs(void)
{
    uint8_t i;
    uart_puts("REGS:");
    for (i = 0; i < 16; i++) {
        uart_putc(' ');
        uart_puthex(*(__xdata volatile uint8_t *)(0xC000 + i));
    }
    uart_puts("\n");
}

void main(void)
{
    uint8_t i, rfbr, data;
    
    /* Disable parity to get 8N1 */
    REG_UART_LCR &= 0xF7;
    
    uart_puts("=== UART FIFO TEST ===\n");
    
    /* Dump all UART registers at startup */
    dump_uart_regs();
    
    uart_puts("READY\n");
    
    /* Main loop - use RFBR to read FIFO properly */
    while (1) {
        rfbr = REG_UART_RFBR;
        
        if (rfbr > 0) {
            /* Read all bytes from FIFO and echo them */
            for (i = 0; i < rfbr; i++) {
                data = REG_UART_RBR;
                uart_putc(data);  /* Echo byte back */
            }
        }
    }
}
