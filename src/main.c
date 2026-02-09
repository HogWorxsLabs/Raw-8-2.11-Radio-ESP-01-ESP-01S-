/* ==================================================
 * ESP-01/ESP-01S Raw 802.11 Radio Transceiver Firmware
 *
 * Purpose: "Dumb radio" that passes encrypted bytes
 *          between UART (RP2040) and 802.11 (WiFi)
 *
 * Architecture:
 *   UART RX → Length-prefixed packets → WiFi TX
 *   WiFi RX → BSSID filter → UART TX
 *
 * The ESP does NOT:
 *   - Interpret encrypted data
 *   - Validate CRC or sequence numbers
 *   - Add framing (just 802.11 header)
 *
 * All intelligence lives on RP2040 (framing, encryption, validation)
 * ================================================== */

#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "user_config.h"
#include "uart.h"
#include "wifi_raw.h"
#include "gpio.h"

/* ==================================================
 * PARTITION TABLE (Required by SDK for 1MB Non-OTA)
 * Flash layout: SPI_SIZE_MAP=2 (1MB, 8Mbit)
 * ================================================== */
#define SYSTEM_PARTITION_RF_CAL_ADDR            0xFB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR          0xFC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR  0xFD000

static const partition_item_t partition_table[] = {
    { SYSTEM_PARTITION_RF_CAL,             SYSTEM_PARTITION_RF_CAL_ADDR,            0x1000},
    { SYSTEM_PARTITION_PHY_DATA,           SYSTEM_PARTITION_PHY_DATA_ADDR,          0x1000},
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,   SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR,  0x3000},
};

/* ==================================================
 * STATIC BUFFERS
 * ================================================== */

/* Packet assembly buffer for length-prefixed reads */
static uint8_t packet_buffer[MAX_PACKET_SIZE];

/* ==================================================
 * TIMER FOR PERIODIC PROCESSING
 * ================================================== */

static os_timer_t main_timer;

/**
 * Main processing callback (runs at MAIN_TIMER_PERIOD_MS rate)
 *
 * Tasks:
 * 1. Check for UART data (length-prefixed packets)
 * 2. Read complete packets and transmit over WiFi
 * 3. Feed watchdog if needed
 */
static void ICACHE_FLASH_ATTR main_timer_callback(void *arg)
{
    static uint32_t led_counter = 0;
    static uint32_t heartbeat_counter = 0;

    /* Brief LED flash every 5 seconds (aligned with heartbeat) */
    led_counter++;
    if (led_counter == 500) {  /* 500 * 10ms = 5s — LED on */
        GPIO_OUTPUT_SET(LED_GPIO, LED_ON);
    } else if (led_counter == 505) {  /* 50ms flash — LED off, reset */
        GPIO_OUTPUT_SET(LED_GPIO, LED_OFF);
        led_counter = 0;
    }

    /* Heartbeat message every 5 seconds */
    heartbeat_counter++;
    if (heartbeat_counter >= 500) {  /* 500 * 10ms = 5 seconds */
        os_printf("[HEARTBEAT] heap=%u tx=%u txerr=%u rx=%u rxdrop=%u\n",
                 system_get_free_heap_size(),
                 wifi_get_tx_count(), wifi_get_tx_error_count(),
                 wifi_get_rx_count(), wifi_get_rx_drop_count());
        heartbeat_counter = 0;
    }

    /* UART → WiFi bridge: read length-prefixed packets, send over 802.11
     * Protocol: [LEN_HI][LEN_LO][payload...]
     * State machine persists across timer ticks via static vars
     */
    {
        static uint16_t pkt_expected = 0;  /* Payload length from prefix (0 = waiting for header) */
        static uint16_t pkt_received = 0;  /* Bytes accumulated so far */

        if (pkt_expected == 0) {
            /* Waiting for 2-byte length prefix */
            if (uart_rx_available() >= 2) {
                uint8_t len_bytes[2];
                uart_read_bytes(len_bytes, 2);
                pkt_expected = (len_bytes[0] << 8) | len_bytes[1];
                pkt_received = 0;

                if (pkt_expected == 0 || pkt_expected > MAX_PACKET_SIZE) {
                    if (pkt_expected > MAX_PACKET_SIZE) {
                        DEBUG_PRINTF("UART: bad length %u\n", pkt_expected);
                    }
                    pkt_expected = 0;
                }
            }
        }

        if (pkt_expected > 0) {
            /* Accumulate payload bytes */
            uint16_t remaining = pkt_expected - pkt_received;
            uint16_t avail = uart_rx_available();
            uint16_t to_read = (avail < remaining) ? avail : remaining;

            if (to_read > 0) {
                pkt_received += uart_read_bytes(packet_buffer + pkt_received, to_read);
            }

            /* Complete packet — send over WiFi */
            if (pkt_received >= pkt_expected) {
                wifi_raw_send(packet_buffer, pkt_expected);
                DEBUG_PRINTF("UART->WiFi: %u bytes\n", pkt_expected);
                pkt_expected = 0;
                pkt_received = 0;
            }
        }
    }
}

/* ==================================================
 * SYSTEM CALLBACKS (REQUIRED BY SDK)
 * ================================================== */

/**
 * Called when system initialization is fully complete.
 * WiFi hardware is ready - safe to enable promiscuous mode
 * and use wifi_send_pkt_freedom().
 */
static void ICACHE_FLASH_ATTR system_init_done(void)
{
    /* Initialize WiFi in raw mode (must be after system init) */
    wifi_raw_init(WIFI_DEFAULT_CHANNEL);
    os_printf("WiFi: Channel %u (raw mode active)\n", WIFI_DEFAULT_CHANNEL);

    /* Get and print MAC address */
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    os_printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Start processing timer now that WiFi is ready */
    os_timer_disarm(&main_timer);
    os_timer_setfn(&main_timer, (os_timer_func_t *)main_timer_callback, NULL);
    os_timer_arm(&main_timer, MAIN_TIMER_PERIOD_MS, 1);

    os_printf("Timer: %u ms (%u Hz)\n", MAIN_TIMER_PERIOD_MS, 1000/MAIN_TIMER_PERIOD_MS);
    os_printf("========================================\n");
    os_printf("Ready! UART<->WiFi bridge active.\n");
    os_printf("========================================\n\n");
    os_printf("Free heap: %u bytes\n", system_get_free_heap_size());
}

/**
 * User initialization - called by SDK on boot
 *
 * UART and LED are set up here. WiFi initialization is
 * deferred to system_init_done_cb because wifi_send_pkt_freedom()
 * requires the system to be fully initialized.
 */
void ICACHE_FLASH_ATTR user_init(void)
{
    /* Configure SDK's UART for os_printf() at 460800 baud */
    uart_div_modify(0, UART_CLK_FREQ / UART_BAUD_RATE);

    /* Delay to let SDK stabilize */
    os_delay_us(100000);  /* 100ms */

    /* Print banner */
    os_printf("\n\n");
    os_printf("========================================\n");
    os_printf("ESP-01/ESP-01S Raw 802.11 Radio\n");
    os_printf("Firmware v1.0\n");
    os_printf("========================================\n");

    /* Initialize UART (460800 baud, 8N1, interrupt-driven) */
    uart_init(UART_BAUD_RATE);
    os_printf("UART: %u baud\n", UART_BAUD_RATE);

    /* Initialize LED on GPIO2 for status indication */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    GPIO_OUTPUT_SET(LED_GPIO, LED_OFF);  /* Start with LED off */
    os_printf("LED: GPIO%u initialized\n", LED_GPIO);

    /* Defer WiFi init until system is fully ready */
    system_init_done_cb(system_init_done);
    os_printf("Waiting for system init...\n");
}

/**
 * User pre-initialization
 * Called before system initialization
 */
void ICACHE_FLASH_ATTR user_pre_init(void)
{
    if (!system_partition_table_regist(partition_table,
            sizeof(partition_table) / sizeof(partition_table[0]),
            SPI_SIZE_MAP)) {
        os_printf("partition table regist fail\r\n");
        while(1);
    }
}

/**
 * RF pre-initialization
 * Required by SDK but can be empty for our use case
 */
void ICACHE_FLASH_ATTR user_rf_pre_init(void)
{
    /* Empty - use default RF calibration */
}

/**
 * User RF calibration sector
 * Required by SDK - returns sector for RF calibration data
 */
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;
        case FLASH_SIZE_8M_MAP_512_512:  // 1MB flash (our ESP-01S)
            rf_cal_sec = 256 - 5;  // Sector 251 = 0xFB000
            break;
        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;
        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}
