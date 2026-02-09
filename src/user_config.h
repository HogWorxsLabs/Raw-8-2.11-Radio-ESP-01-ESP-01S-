/* ==================================================
 * ESP-01/ESP-01S Raw 802.11 Radio Configuration
 * Central configuration for all firmware constants
 * ================================================== */

#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Types provided by ESP8266 SDK (c_types.h) - don't include stdint.h */

/* ==================================================
 * COMPILER ATTRIBUTES
 * ================================================== */

/* Place function in IRAM (for interrupt handlers - fast access) */
#define ICACHE_RAM_ATTR __attribute__((section(".iram.text")))

/* Place function in flash (for regular functions - saves IRAM) */
/* Already defined in c_types.h as ICACHE_FLASH_ATTR */

/* ==================================================
 * UART CONFIGURATION
 * ================================================== */
#define UART_BAUD_RATE          460800      /* Required for encryption overhead */
#define UART_RX_BUFFER_SIZE     1024        /* Power of 2 for fast masking */
#define UART_TX_BUFFER_SIZE     1024        /* Power of 2 for fast masking */

/* ==================================================
 * PACKET CONFIGURATION
 * ================================================== */
#define MAX_PACKET_SIZE         256         /* Maximum payload size (bytes) */

/* Frame size calculation:
 * - RP2040 app frame: [0xAA][SEQ][LEN][payload][CRC8]
 * - After encryption: +28 bytes (12B nonce + 16B MAC)
 * - Length prefix: +2 bytes
 * - 802.11 header: +24 bytes (added by ESP)
 * Total overhead: 54 bytes on top of payload
 */

/* ==================================================
 * WIFI CONFIGURATION
 * ================================================== */
#define WIFI_DEFAULT_CHANNEL    11           /* 2.4GHz channel 1-14 */
#define WIFI_TX_RATE            PHY_RATE_1M_L  /* 1 Mbps for max range */

/* Custom BSSID for RX filtering (avoids payload inspection) */
#define CUSTOM_BSSID            {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00}

/* Broadcast MAC for TX (Addr1 in 802.11 header) */
#define BROADCAST_MAC           {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/* ==================================================
 * TIMING CONFIGURATION
 * ================================================== */
#define MAIN_TIMER_PERIOD_MS    10          /* 100Hz - check UART for TX data */

/* ==================================================
 * DEBUG CONFIGURATION
 * ================================================== */
#define DEBUG_ENABLED           1           /* 0=production, 1=debug output */

#if DEBUG_ENABLED
  #define DEBUG_PRINTF(fmt, ...) os_printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINTF(fmt, ...) do {} while(0)
#endif

/* ==================================================
 * LED CONFIGURATION (GPIO2 - ESP-01/ESP-01S)
 * ================================================== */
#define LED_GPIO                2           /* GPIO2 available on ESP-01S */
#define LED_ON                  0           /* Active LOW (LED cathode to GPIO) */
#define LED_OFF                 1           /* Active HIGH turns LED off */

/* ==================================================
 * 802.11 FRAME STRUCTURE
 * ================================================== */

/* 802.11 MAC header size (fixed) */
#define IEEE80211_HEADER_SIZE   24

/* Frame Control field values */
#define IEEE80211_FCTL_FTYPE    0x000C      /* Frame type mask */
#define IEEE80211_FCTL_MGMT     0x0000      /* Management frame type */

/* We use Probe Request management frames (type=0, subtype=4) instead of
 * data frames because ESP8266 promiscuous mode only captures full
 * payload for management frames. Data frames get truncated to ~4 bytes.
 * Beacons don't work either — SDK overwrites first 8 bytes with TSF timestamp.
 *
 * Frame Control byte layout (little-endian uint16_t = 0x0040):
 *   Byte 0: [Subtype:0100][Type:00][Proto:00] = 0x40
 *   Byte 1: 0x00
 */
#define IEEE80211_FC_PROBE_REQ  0x0040      /* Probe Request management frame */

/* ==================================================
 * MEMORY LAYOUT
 * ================================================== */

/* Static buffer allocations (avoid heap fragmentation) */
#define TX_FRAME_BUFFER_SIZE    (IEEE80211_HEADER_SIZE + MAX_PACKET_SIZE)

/* ==================================================
 * HARDWARE CONFIGURATION
 * ================================================== */

/* ESP-01/ESP-01S pinout:
 * Pin 1: GND
 * Pin 2: GPIO2 (UART1 TX - debug only)
 * Pin 3: GPIO0 (boot mode select)
 * Pin 4: RX (GPIO3, UART0 RX) ← Data from RP2040
 * Pin 5: TX (GPIO1, UART0 TX) → Data to RP2040
 * Pin 6: EN/CH_PD (chip enable, pull HIGH)
 * Pin 7: RST (reset, pull HIGH)
 * Pin 8: VCC (3.3V, up to 300mA peak)
 */

#endif /* USER_CONFIG_H */
