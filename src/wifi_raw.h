/* ==================================================
 * WiFi Raw 802.11 Layer
 * Handles packet TX/RX using ESP8266 SDK raw functions
 * ================================================== */

#ifndef WIFI_RAW_H
#define WIFI_RAW_H

#include "c_types.h"

/* ==================================================
 * 802.11 MAC HEADER STRUCTURES
 * ================================================== */

/**
 * IEEE 802.11 MAC header (24 bytes minimum)
 * Used for Probe Request management frames
 */
struct ieee80211_hdr {
    uint16_t frame_control;    /* Frame control field */
    uint16_t duration_id;      /* Duration/ID */
    uint8_t  addr1[6];         /* Receiver address (broadcast) */
    uint8_t  addr2[6];         /* Transmitter address (our MAC) */
    uint8_t  addr3[6];         /* BSSID (filtering key) */
    uint16_t seq_ctrl;         /* Sequence control */
} __attribute__((packed));

/**
 * RX Control structure (SDK-specific metadata)
 * Prepended to received frames by promiscuous callback
 */
struct RxControl {
    signed rssi:8;             /* Signal strength */
    unsigned rate:4;           /* PHY rate */
    unsigned is_group:1;       /* Group addressed frame */
    unsigned:1;
    unsigned sig_mode:2;       /* 0=11b/g, non-zero=11n */
    unsigned legacy_length:12; /* Frame length */
    unsigned damatch0:1;
    unsigned damatch1:1;
    unsigned bssidmatch0:1;
    unsigned bssidmatch1:1;
    unsigned MCS:7;            /* Modulation coding scheme */
    unsigned CWB:1;            /* Channel bandwidth */
    unsigned HT_length:16;     /* HT frame length */
    unsigned Smoothing:1;
    unsigned Not_Sounding:1;
    unsigned:1;
    unsigned Aggregation:1;
    unsigned STBC:2;
    unsigned FEC_CODING:1;
    unsigned SGI:1;
    unsigned rxend_state:8;
    unsigned ampdu_cnt:8;
    unsigned channel:4;        /* RX channel */
    unsigned:12;
};

/* ==================================================
 * PUBLIC API
 * ================================================== */

/**
 * Initialize WiFi in raw mode
 * Sets up promiscuous mode and configures channel
 *
 * @param channel: WiFi channel 1-14
 */
void wifi_raw_init(uint8_t channel);

/**
 * Send raw data wrapped in 802.11 frame
 * Builds minimal 802.11 header and injects packet
 *
 * @param raw_data: Payload bytes (encrypted by RP2040)
 * @param len: Payload length
 * @return: 0 on success, -1 on error
 */
int wifi_raw_send(const uint8_t *raw_data, uint16_t len);

/**
 * Set WiFi channel (runtime configuration)
 *
 * @param channel: Target channel 1-14
 */
void wifi_raw_set_channel(uint8_t channel);

/**
 * Get current WiFi channel
 *
 * @return: Current channel number
 */
uint8_t wifi_raw_get_channel(void);

/**
 * Get TX packet count
 *
 * @return: Number of packets transmitted
 */
uint32_t wifi_get_tx_count(void);

/**
 * Get RX packet count
 *
 * @return: Number of packets received (after filtering)
 */
uint32_t wifi_get_rx_count(void);

/**
 * Get TX error count
 *
 * @return: Number of TX failures
 */
uint32_t wifi_get_tx_error_count(void);

/**
 * Get RX drop count
 *
 * @return: Number of packets dropped (failed filters)
 */
uint32_t wifi_get_rx_drop_count(void);

/**
 * Reset statistics counters
 */
void wifi_reset_stats(void);

#endif /* WIFI_RAW_H */
