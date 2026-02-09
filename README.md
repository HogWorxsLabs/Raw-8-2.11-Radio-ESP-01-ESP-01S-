<p align="center">
  <strong>ESP-Radio</strong><br>
  <em>Raw 802.11 radio firmware for ESP-01 / ESP-01S</em><br><br>
  <a href="https://hogworxslabs.com">Hog Worxs Labs</a>
</p>

---

Transparent UART-to-WiFi bridge for RC plane and drone radio links. The ESP acts as a "dumb radio" — it passes encrypted bytes between UART and 802.11 without interpreting them. All intelligence (framing, encryption, validation) lives on the flight controller.

## Features

- **Raw 802.11 injection** via `wifi_send_pkt_freedom()`
- **Promiscuous RX** with BSSID-based filtering
- **Symmetric protocol** — same length-prefixed format in both directions
- **Configurable** — channel, BSSID, TX rate, baud rate via GUI or header file
- **Low latency** — ~8.5 ms one-way (UART + WiFi + UART)
- **88 bytes** tested payload per packet (92 byte theoretical max)

## Compatible Hardware

| Module | Flash | Pullups | Notes |
|--------|-------|:-------:|-------|
| **ESP-01S** (AITRIP) | 1 MB | Yes | Recommended — 4 wires to flight controller |
| **ESP-01** (HiLetgo) | 1 MB | No | 7 wires — tie RST, CH_PD, GPIO0 to 3.3 V |

Both use the same ESP8266EX chip. Firmware is identical on both.

**Programmer:** CH340G USB adapter with PROG / UART mode switch

---

## Quick Start

### Prerequisites

```bash
sudo apt install gcc-xtensa-lx106 esptool python3-tk
```

You also need the [ESP8266 Non-OS SDK](https://github.com/espressif/ESP8266_NONOS_SDK). Set `SDK_BASE` in the Makefile to your SDK path.

### Build & Flash

**GUI tool** (recommended):

```bash
python3 flash_tool.py
```

Configure channel, BSSID, TX rate, baud rate, and packet size — then build and flash in one click.

**Command line:**

```bash
make              # build
make flash        # flash (adapter in PROG mode)
```

---

## Wiring to Flight Controller

Pre-program the ESP on the CH340G adapter, then move it to the aircraft.

### ESP-01S — 4 wires

| ESP Pin | Connect to |
|---------|-----------|
| VCC | 3.3 V (300 mA) |
| GND | Ground |
| TX | Flight controller UART RX |
| RX | Flight controller UART TX |

### ESP-01 (non-S) — 7 wires

Same as above, plus tie three pins HIGH for reliable boot:

| Extra Pin | Connect to |
|-----------|-----------|
| RST | 3.3 V |
| CH_PD | 3.3 V |
| GPIO0 | 3.3 V |

> GPIO2 is optional on both — it drives the status LED.

---

## Configuration

All settings in `src/user_config.h` (also configurable via GUI):

| Setting | Default | Description |
|---------|---------|-------------|
| `WIFI_DEFAULT_CHANNEL` | `11` | WiFi channel 1–14 (must match both ends) |
| `CUSTOM_BSSID` | `AA:BB:CC:DD:EE:00` | Link ID (must match both ends) |
| `WIFI_TX_RATE` | `PHY_RATE_1M_L` | 1 Mbps for maximum range |
| `UART_BAUD_RATE` | `460800` | Must match flight controller |
| `MAX_PACKET_SIZE` | `256` | Max payload in bytes |

> **BSSID as link ID:** Each aircraft gets a unique BSSID so multiple planes on the same channel don't interfere.

---

## UART Protocol

Both directions use the same format:

```
[LEN_HI] [LEN_LO] [payload...]
```

- 2-byte big-endian length prefix (payload length only)
- 460800 baud, 8N1
- ESP passes bytes through as-is — flight controller handles encryption and validation

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `xtensa-lx106-elf-gcc: not found` | `sudo apt install gcc-xtensa-lx106` |
| Flash fails — port error | Check `/dev/ttyUSB*`, `sudo usermod -a -G dialout $USER`, ensure PROG mode |
| No output after flash | Switch adapter to UART mode, verify 460800 baud |
| ESP keeps resetting | 3.3 V supply (NOT 5 V), 300 mA peak, 100 uF cap near VCC |
| ESP-01 won't boot | Tie RST, CH_PD, GPIO0 to 3.3 V |

---

## License

MIT License — see [LICENSE](LICENSE)

---

<p align="center">
  <a href="https://hogworxslabs.com"><strong>Hog Worxs Labs</strong></a>
</p>
