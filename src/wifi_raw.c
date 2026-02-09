/* ==================================================
 * WiFi Raw 802.11 Layer Implementation
 * TX/RX using wifi_send_pkt_freedom() and promiscuous mode
 * ================================================== */

#include "wifi_raw.h"
#include "user_config.h"
#include "uart.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"

/* ==================================================
 * STATIC BUFFERS
 * ================================================== */

/* TX frame assembly buffer (802.11 header + payload) */
static uint8_t tx_frame_buffer[TX_FRAME_BUFFER_SIZE];

/* MAC addresses */
static const uint8_t broadcast_mac[6] = BROADCAST_MAC;
static const uint8_t custom_bssid[6] = CUSTOM_BSSID;

/* Sequence number for TX frames */
static uint16_t tx_sequence = 0;

/* TX ready flag: cleared when TX in progress, set by callback */
static volatile uint8_t tx_ready = 1;

/* Statistics */
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static uint32_t tx_error_count = 0;
static uint32_t rx_drop_count = 0;

/* ==================================================
 * FORWARD DECLARATIONS
 * ================================================== */

static void wifi_promiscuous_rx_cb(uint8_t *buf, uint16_t len);
static void wifi_freedom_tx_cb(uint8_t status);

/* ==================================================
 * 802.11 FRAME CONSTRUCTION
 * ================================================== */

/**
 * Build 802.11 MAC header
 * Type: Probe Request management frame (0x0040)
 * Addr1: Broadcast
 * Addr2: ESP8266 MAC address
 * Addr3: Custom BSSID (used for RX filtering)
 */
static void build_80211_header(struct ieee80211_hdr *hdr)
{
    /* Frame Control: Probe Request management frame (type 0, subtype 4)
     * Using management frames because ESP8266 promiscuous mode only
     * captures full payload for management frames (data frames truncated).
     */
    hdr->frame_control = IEEE80211_FC_PROBE_REQ;

    /* Duration: 0 (not using ACKs) */
    hdr->duration_id = 0;

    /* Addr1: Broadcast (destination) */
    os_memcpy(hdr->addr1, broadcast_mac, 6);

    /* Addr2: Our MAC address (source) */
    wifi_get_macaddr(STATION_IF, hdr->addr2);

    /* Addr3: Custom BSSID (RX filtering key) */
    os_memcpy(hdr->addr3, custom_bssid, 6);

    /* Sequence Control: [15:4] = sequence, [3:0] = fragment (0) */
    hdr->seq_ctrl = (tx_sequence << 4) & 0xFFF0;
    tx_sequence++;
}

/* ==================================================
 * TX CALLBACK
 * ================================================== */

/**
 * Called by SDK when a freedom packet has been sent.
 * Must be registered before wifi_send_pkt_freedom() will work.
 */
static void wifi_freedom_tx_cb(uint8_t status)
{
    tx_ready = 1;
}

/* ==================================================
 * TX IMPLEMENTATION
 * ================================================== */

int wifi_raw_send(const uint8_t *raw_data, uint16_t len)
{
    /* Validate input */
    if (raw_data == NULL || len == 0 || len > MAX_PACKET_SIZE) {
        DEBUG_PRINTF("wifi_raw_send: Invalid input (len=%u)\n", len);
        tx_error_count++;
        return -1;
    }

    /* Check if previous TX is still in progress */
    if (!tx_ready) {
        DEBUG_PRINTF("TX BUSY\n");
        tx_error_count++;
        return -1;
    }

    /* Build 802.11 header */
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_frame_buffer;
    build_80211_header(hdr);

    /* Append raw payload (encrypted by RP2040) */
    os_memcpy(tx_frame_buffer + IEEE80211_HEADER_SIZE, raw_data, len);

    /* Total frame size */
    uint16_t frame_len = IEEE80211_HEADER_SIZE + len;

    /* Mark TX as in-progress before sending */
    tx_ready = 0;

    /* Send using SDK raw packet injection
     * Arguments:
     *   buf: Complete 802.11 frame (header + payload)
     *   len: Total frame length
     *   sys_seq: 0 = use our sequence number from header
     *
     * Returns: 0 on success, -1 on error (queue full, etc.)
     */
    int result = wifi_send_pkt_freedom(tx_frame_buffer, frame_len, 0);

    if (result == 0) {
        tx_count++;
        DEBUG_PRINTF("TX: len=%u, seq=%u\n", len, tx_sequence - 1);
    } else {
        tx_ready = 1;  /* Reset on failure so we can retry */
        tx_error_count++;
        DEBUG_PRINTF("TX FAILED: len=%u\n", len);
    }

    return result;
}

/* ==================================================
 * RX IMPLEMENTATION
 * ================================================== */

/**
 * Promiscuous mode RX callback
 * CRITICAL: Runs in interrupt context - keep SHORT!
 *
 * Filtering strategy:
 * 1. Check sig_mode and minimum length (early reject)
 * 2. Parse frame control (data frames only)
 * 3. Check BSSID (custom MAC address filter)
 * 4. Extract payload and send to UART
 */
static void wifi_promiscuous_rx_cb(uint8_t *buf, uint16_t len)
{
    /* Parse RxControl structure (SDK metadata) */
    struct RxControl *rx_ctrl = (struct RxControl *)buf;

    /* Filter 1: Only accept legacy 802.11b/g frames
     * Skip 802.11n frames (sig_mode != 0)
     * Minimum length: RxControl + 802.11 header + payload + FCS
     */
    if ((rx_ctrl->sig_mode != 0) || (len < (sizeof(struct RxControl) + 28))) {
        rx_drop_count++;
        return;
    }

    /* Parse 802.11 MAC header (follows RxControl) */
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(buf + sizeof(struct RxControl));

    /* Filter 2: Only accept management frames (type 0x00)
     * We use Probe Request frames for full payload capture
     */
    uint16_t frame_type = hdr->frame_control & IEEE80211_FCTL_FTYPE;
    if (frame_type != IEEE80211_FCTL_MGMT) {
        rx_drop_count++;
        return;
    }

    /* Filter 3: Only accept frames with our custom BSSID
     * This is the KEY filter - rejects >99% of ambient WiFi
     */
    if (os_memcmp(hdr->addr3, custom_bssid, 6) != 0) {
        rx_drop_count++;
        return;
    }

    /* Frame passed all filters - extract payload */
    uint8_t *payload = (uint8_t *)hdr + IEEE80211_HEADER_SIZE;

    /* Calculate payload length from legacy_length (actual over-the-air frame size)
     * legacy_length = MAC header + payload + FCS(4)
     * The 'len' parameter is always 128 for management frames (fixed buffer).
     */
    uint16_t payload_len = rx_ctrl->legacy_length - IEEE80211_HEADER_SIZE - 4;

    /* Sanity check payload length */
    if (payload_len > MAX_PACKET_SIZE) {
        DEBUG_PRINTF("RX: Payload too large (%u bytes)\n", payload_len);
        rx_drop_count++;
        return;
    }

    rx_count++;

    /* WiFi → UART bridge: forward payload with length prefix
     * Protocol: [LEN_HI][LEN_LO][payload...]
     */
    uint8_t len_prefix[2];
    len_prefix[0] = (payload_len >> 8) & 0xFF;
    len_prefix[1] = payload_len & 0xFF;
    uart_write_bytes(len_prefix, 2);
    uart_write_bytes(payload, payload_len);

    DEBUG_PRINTF("WiFi->UART: %u bytes rssi=%d\n", payload_len, rx_ctrl->rssi);
}

/* ==================================================
 * INITIALIZATION
 * ================================================== */

void wifi_raw_init(uint8_t channel)
{
    /* Set WiFi mode to STATION (required even for raw packets) */
    wifi_set_opmode(STATION_MODE);

    /* Disable auto-connect (we don't want to associate with APs) */
    wifi_station_set_auto_connect(0);

    /* Set WiFi channel */
    wifi_set_channel(channel);

    /* Configure TX rate for maximum range */
    wifi_set_phy_mode(PHY_MODE_11G);  /* 802.11g mode */

    /* Register TX completion callback (REQUIRED for wifi_send_pkt_freedom) */
    wifi_register_send_pkt_freedom_cb(wifi_freedom_tx_cb);

    /* Register promiscuous callback */
    wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb);

    /* Enable promiscuous mode */
    wifi_promiscuous_enable(1);

    /* Reset statistics */
    tx_count = 0;
    rx_count = 0;
    tx_error_count = 0;
    rx_drop_count = 0;
    tx_sequence = 0;

    DEBUG_PRINTF("WiFi Raw initialized: channel %u\n", channel);
    DEBUG_PRINTF("BSSID filter: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 custom_bssid[0], custom_bssid[1], custom_bssid[2],
                 custom_bssid[3], custom_bssid[4], custom_bssid[5]);
    os_printf("Frame type: Probe Request (0x0040), legacy_length for RX sizing\n");
}

/* ==================================================
 * CHANNEL MANAGEMENT
 * ================================================== */

void wifi_raw_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 14) {
        DEBUG_PRINTF("Invalid channel: %u (must be 1-14)\n", channel);
        return;
    }

    wifi_set_channel(channel);
    DEBUG_PRINTF("Channel changed to: %u\n", channel);
}

uint8_t wifi_raw_get_channel(void)
{
    return wifi_get_channel();
}

/* ==================================================
 * STATISTICS
 * ================================================== */

uint32_t wifi_get_tx_count(void)
{
    return tx_count;
}

uint32_t wifi_get_rx_count(void)
{
    return rx_count;
}

uint32_t wifi_get_tx_error_count(void)
{
    return tx_error_count;
}

uint32_t wifi_get_rx_drop_count(void)
{
    return rx_drop_count;
}

void wifi_reset_stats(void)
{
    tx_count = 0;
    rx_count = 0;
    tx_error_count = 0;
    rx_drop_count = 0;
}
