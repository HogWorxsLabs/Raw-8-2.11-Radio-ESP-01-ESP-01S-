/* ==================================================
 * UART Driver for ESP-01/ESP-01S
 * Interrupt-driven UART0 with ring buffers
 * Designed for reliable 460800 baud operation
 * ================================================== */

#ifndef UART_H
#define UART_H

#include "c_types.h"

/* ==================================================
 * PUBLIC API
 * ================================================== */

/**
 * Initialize UART0 at specified baud rate
 * Configures 8N1 format (8 data, no parity, 1 stop)
 * Enables RX interrupt for non-blocking operation
 *
 * @param baud_rate: Target baud rate (e.g., 460800)
 */
void uart_init(uint32_t baud_rate);

/**
 * Check number of bytes available in RX buffer
 *
 * @return: Number of bytes ready to read
 */
uint16_t uart_rx_available(void);

/**
 * Read bytes from RX buffer (non-blocking)
 *
 * @param buffer: Destination buffer
 * @param max_len: Maximum bytes to read
 * @return: Actual bytes read (may be less than max_len)
 */
uint16_t uart_read_bytes(uint8_t *buffer, uint16_t max_len);

/**
 * Read single byte from RX buffer
 *
 * @param byte: Pointer to store byte
 * @return: true if byte read, false if buffer empty
 */
bool uart_read_byte(uint8_t *byte);

/**
 * Write bytes to TX buffer (non-blocking)
 * Queues data for transmission via interrupt
 *
 * @param data: Source data buffer
 * @param len: Number of bytes to write
 * @return: Actual bytes written (may be less if buffer full)
 */
uint16_t uart_write_bytes(const uint8_t *data, uint16_t len);

/**
 * Write single byte to TX buffer
 *
 * @param byte: Byte to write
 * @return: true if queued, false if buffer full
 */
bool uart_write_byte(uint8_t byte);

/**
 * Get RX buffer overflow count (diagnostic)
 * Increments when RX data arrives but buffer is full
 *
 * @return: Number of overflow events since init
 */
uint32_t uart_get_rx_overflow_count(void);

/**
 * Get TX buffer overflow count (diagnostic)
 * Increments when attempting to write but buffer is full
 *
 * @return: Number of overflow events since init
 */
uint32_t uart_get_tx_overflow_count(void);

/**
 * Reset statistics counters
 */
void uart_reset_stats(void);

#endif /* UART_H */
