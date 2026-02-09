/* ==================================================
 * UART Driver Implementation
 * Interrupt-driven UART0 with ring buffers
 * ================================================== */

#include "uart.h"
#include "user_config.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"
#include "eagle_soc.h"
#include "ets_sys.h"

/* ESP8266 UART register definitions (not SDK uart driver) */
#include "driver/uart_register.h"

/* Define UART ports */
#define UART0   0
#define UART1   1

/* ==================================================
 * RING BUFFER IMPLEMENTATION
 * ================================================== */

/* RX ring buffer (written by ISR, read by main code) */
static volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_head = 0;  /* Write index (ISR) */
static volatile uint16_t uart_rx_tail = 0;  /* Read index (main) */

/* TX ring buffer (written by main code, read by ISR) */
static volatile uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t uart_tx_head = 0;  /* Write index (main) */
static volatile uint16_t uart_tx_tail = 0;  /* Read index (ISR) */

/* Statistics */
static volatile uint32_t uart_rx_overflow_count = 0;
static volatile uint32_t uart_tx_overflow_count = 0;

/* ==================================================
 * RING BUFFER HELPER MACROS
 * ================================================== */

/* Fast modulo using mask (requires power-of-2 size) */
#define RX_BUFFER_MASK  (UART_RX_BUFFER_SIZE - 1)
#define TX_BUFFER_MASK  (UART_TX_BUFFER_SIZE - 1)

/* Increment with wrap */
#define RX_INCREMENT(idx)  (((idx) + 1) & RX_BUFFER_MASK)
#define TX_INCREMENT(idx)  (((idx) + 1) & TX_BUFFER_MASK)

/* ==================================================
 * UART INTERRUPT HANDLERS
 * CRITICAL: Must be in IRAM (ICACHE_RAM_ATTR)
 * ================================================== */

/**
 * UART0 RX interrupt handler
 * Runs when FIFO has data available
 */
static void uart0_rx_intr_handler(void) ICACHE_RAM_ATTR;
static void uart0_rx_intr_handler(void)
{
    uint8_t rx_fifo_len;
    uint8_t byte;
    uint16_t next_head;

    /* Check interrupt status */
    uint32_t uart_intr_status = READ_PERI_REG(UART_INT_ST(UART0));

    if (uart_intr_status & UART_RXFIFO_FULL_INT_ST) {
        /* RX FIFO full interrupt */
        rx_fifo_len = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

        /* Read all available bytes from hardware FIFO */
        while (rx_fifo_len > 0) {
            byte = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;

            /* Calculate next head position */
            next_head = RX_INCREMENT(uart_rx_head);

            /* Check if buffer would overflow */
            if (next_head != uart_rx_tail) {
                /* Space available - store byte */
                uart_rx_buffer[uart_rx_head] = byte;
                uart_rx_head = next_head;
            } else {
                /* Buffer full - drop byte and count overflow */
                uart_rx_overflow_count++;
            }

            rx_fifo_len--;
        }

        /* Clear interrupt */
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
    }

    if (uart_intr_status & UART_RXFIFO_TOUT_INT_ST) {
        /* RX timeout (data available but FIFO not full) */
        rx_fifo_len = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

        while (rx_fifo_len > 0) {
            byte = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
            next_head = RX_INCREMENT(uart_rx_head);

            if (next_head != uart_rx_tail) {
                uart_rx_buffer[uart_rx_head] = byte;
                uart_rx_head = next_head;
            } else {
                uart_rx_overflow_count++;
            }

            rx_fifo_len--;
        }

        /* Clear interrupt */
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
    }
}

/**
 * UART0 TX interrupt handler
 * Runs when TX FIFO has space available
 */
static void uart0_tx_intr_handler(void) ICACHE_RAM_ATTR;
static void uart0_tx_intr_handler(void)
{
    uint32_t uart_intr_status = READ_PERI_REG(UART_INT_ST(UART0));

    if (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST) {
        /* TX FIFO empty - send more data if available */
        uint8_t tx_fifo_space = 128; /* Max FIFO depth */

        while (tx_fifo_space > 0 && uart_tx_tail != uart_tx_head) {
            /* Data available in ring buffer - write to FIFO */
            WRITE_PERI_REG(UART_FIFO(UART0), uart_tx_buffer[uart_tx_tail]);
            uart_tx_tail = TX_INCREMENT(uart_tx_tail);
            tx_fifo_space--;
        }

        /* If ring buffer empty, disable TX interrupt */
        if (uart_tx_tail == uart_tx_head) {
            CLEAR_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
        }

        /* Clear interrupt */
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_TXFIFO_EMPTY_INT_CLR);
    }
}

/* ==================================================
 * PUBLIC API IMPLEMENTATION
 * ================================================== */

void uart_init(uint32_t baud_rate)
{
    /* Calculate UART divider for baud rate */
    uint32_t uart_clkdiv = (80000000 / baud_rate);

    /* Disable UART interrupts during configuration */
    ETS_UART_INTR_DISABLE();

    /* Configure UART0 baud rate */
    WRITE_PERI_REG(UART_CLKDIV(UART0), uart_clkdiv & UART_CLKDIV_CNT);

    /* Configure 8N1 format */
    WRITE_PERI_REG(UART_CONF0(UART0),
        ((0x3 & UART_BIT_NUM) << UART_BIT_NUM_S) |  /* 8 bits */
        ((0x0 & UART_PARITY_EN) << UART_PARITY_EN_S) | /* No parity */
        ((0x1 & UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S) /* 1 stop bit */
    );

    /* Configure RX FIFO threshold (trigger interrupt when 8+ bytes) */
    WRITE_PERI_REG(UART_CONF1(UART0),
        (0x01 << UART_RXFIFO_FULL_THRHD_S) |  /* RX threshold = 8 bytes */
        (0x01 << UART_RX_TOUT_THRHD_S) |      /* RX timeout threshold */
        UART_RX_TOUT_EN                        /* Enable RX timeout */
    );

    /* Clear interrupt status */
    WRITE_PERI_REG(UART_INT_CLR(UART0), 0xFFFF);

    /* Enable RX interrupts (TX interrupt enabled on demand) */
    WRITE_PERI_REG(UART_INT_ENA(UART0),
        UART_RXFIFO_FULL_INT_ENA |
        UART_RXFIFO_TOUT_INT_ENA
    );

    /* Register interrupt handler */
    ETS_UART_INTR_ATTACH((void *)uart0_rx_intr_handler, NULL);

    /* Enable UART interrupts */
    ETS_UART_INTR_ENABLE();

    /* Reset ring buffer indices */
    uart_rx_head = 0;
    uart_rx_tail = 0;
    uart_tx_head = 0;
    uart_tx_tail = 0;

    /* Reset statistics */
    uart_rx_overflow_count = 0;
    uart_tx_overflow_count = 0;

    DEBUG_PRINTF("UART initialized: %u baud, 8N1\n", baud_rate);
}

uint16_t uart_rx_available(void)
{
    /* Calculate bytes available (handle wrap) */
    uint16_t head = uart_rx_head;
    uint16_t tail = uart_rx_tail;

    if (head >= tail) {
        return head - tail;
    } else {
        return UART_RX_BUFFER_SIZE - tail + head;
    }
}

uint16_t uart_read_bytes(uint8_t *buffer, uint16_t max_len)
{
    uint16_t count = 0;

    /* Read up to max_len bytes */
    while (count < max_len && uart_rx_tail != uart_rx_head) {
        buffer[count++] = uart_rx_buffer[uart_rx_tail];
        uart_rx_tail = RX_INCREMENT(uart_rx_tail);
    }

    return count;
}

bool uart_read_byte(uint8_t *byte)
{
    if (uart_rx_tail == uart_rx_head) {
        /* Buffer empty */
        return false;
    }

    *byte = uart_rx_buffer[uart_rx_tail];
    uart_rx_tail = RX_INCREMENT(uart_rx_tail);
    return true;
}

uint16_t uart_write_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t count = 0;
    uint16_t next_head;

    /* Disable TX interrupt while modifying buffer */
    ETS_UART_INTR_DISABLE();

    /* Write as many bytes as will fit in buffer */
    while (count < len) {
        next_head = TX_INCREMENT(uart_tx_head);

        if (next_head == uart_tx_tail) {
            /* Buffer full */
            uart_tx_overflow_count++;
            break;
        }

        uart_tx_buffer[uart_tx_head] = data[count++];
        uart_tx_head = next_head;
    }

    /* Enable TX FIFO empty interrupt to start transmission */
    if (uart_tx_tail != uart_tx_head) {
        SET_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);

        /* Trigger TX interrupt handler manually to start sending */
        uart0_tx_intr_handler();
    }

    ETS_UART_INTR_ENABLE();

    return count;
}

bool uart_write_byte(uint8_t byte)
{
    return uart_write_bytes(&byte, 1) == 1;
}

uint32_t uart_get_rx_overflow_count(void)
{
    return uart_rx_overflow_count;
}

uint32_t uart_get_tx_overflow_count(void)
{
    return uart_tx_overflow_count;
}

void uart_reset_stats(void)
{
    uart_rx_overflow_count = 0;
    uart_tx_overflow_count = 0;
}
