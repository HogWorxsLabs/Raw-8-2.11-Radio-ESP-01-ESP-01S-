<p align="center">
  <img src="HWL.png" alt="Hog Worxs Labs" width="320">
</p>

<h1 align="center">ESP-Radio</h1>

<p align="center">
  Raw 802.11 radio firmware for ESP-01 / ESP-01S<br>
  <a href="https://hogworxslabs.com">hogworxslabs.com</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-ESP8266-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/sdk-Non--OS_SDK-orange?style=flat-square" alt="SDK">
  <img src="https://img.shields.io/badge/latency-~8.5ms-brightgreen?style=flat-square" alt="Latency">
  <img src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square" alt="License">
</p>

---

## What Is This?

ESP-Radio turns a $2 ESP-01S into a transparent UART-to-WiFi radio link for RC planes and drones. The ESP acts as a **dumb radio** — it passes encrypted bytes between UART and raw 802.11 without interpreting them. All intelligence (framing, encryption, validation) lives on the flight controller.

```
 Flight Controller                ESP-01S                     ESP-01S                Flight Controller
┌──────────────┐           ┌─────────────────┐         ┌─────────────────┐           ┌──────────────┐
│   RP2040     │   UART    │  Raw 802.11 TX  │ ~~air~~ │  Promiscuous RX │   UART    │   RP2040     │
│  ChaCha20    ├──────────►│  Probe Request  ├────────►│  BSSID Filter   ├──────────►│  ChaCha20    │
│  Framing     │  460800   │  Injection      │  2.4GHz │  Extraction     │  460800   │  Validation  │
└──────────────┘   8N1     └─────────────────┘         └─────────────────┘   8N1     └──────────────┘
```

## Features

- **Raw 802.11 injection** via `wifi_send_pkt_freedom()` — no AP association needed
- **Promiscuous RX** with BSSID-based filtering — zero payload inspection
- **Symmetric protocol** — same length-prefixed format in both directions
- **GUI flash tool** — configure channel, BSSID, TX rate, baud rate, then build + flash in one click
- **~8.5 ms one-way latency** (UART + WiFi + UART)
- **88 bytes** tested payload per packet (92 byte theoretical max)

---

## Compatible Hardware

| Module | Flash | Pullups | Wiring |
|--------|:-----:|:-------:|--------|
| **ESP-01S** (recommended) | 1 MB | Built-in | 4 wires to flight controller |
| **ESP-01** | 1 MB | None | 7 wires — must tie RST, CH_PD, GPIO0 HIGH |

Both use the same ESP8266EX chip. Firmware is identical.

**Programmer:** CH340G USB adapter with PROG / UART mode switch.

---

## Quick Start

### 1. Install Dependencies

```bash
sudo apt install gcc-xtensa-lx106 esptool python3-tk
```

Download the [ESP8266 Non-OS SDK](https://github.com/espressif/ESP8266_NONOS_SDK) and set `SDK_BASE` in the Makefile to your SDK path.

### 2. Build & Flash

**GUI tool (recommended):**

```bash
python3 flash_tool.py
```

The GUI lets you configure all settings (channel, BSSID, TX rate, baud rate, packet size) and build + flash with one click.

**Command line:**

```bash
make              # build firmware
make flash        # flash to ESP (adapter must be in PROG mode)
```

### 3. Monitor Serial Output

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 460800
```

Exit with `Ctrl+]`. Switch the CH340G adapter to **UART mode** before monitoring.

---

## Wiring

Pre-program the ESP on the CH340G adapter, then move it to the aircraft.

### ESP-01S (4 wires)

```
ESP-01S              Flight Controller
───────              ─────────────────
VCC  ──────────────  3.3V (300 mA capable)
GND  ──────────────  GND
TX   ──────────────  UART RX
RX   ──────────────  UART TX
```

### ESP-01 (7 wires)

Same four wires as above, plus tie three pins to 3.3V for reliable boot:

```
RST   ─────── 3.3V
CH_PD ─────── 3.3V
GPIO0 ─────── 3.3V
```

> GPIO2 is optional on both modules — it drives the onboard status LED.

---

## Configuration

All settings live in [`src/user_config.h`](src/user_config.h) and are also configurable via the GUI flash tool.

| Setting | Default | Description |
|---------|---------|-------------|
| `WIFI_DEFAULT_CHANNEL` | `11` | WiFi channel 1–14 (must match both ends) |
| `CUSTOM_BSSID` | `AA:BB:CC:DD:EE:00` | Link ID — unique per aircraft |
| `WIFI_TX_RATE` | `PHY_RATE_1M_L` | 1 Mbps for maximum range |
| `UART_BAUD_RATE` | `460800` | Must match flight controller |
| `MAX_PACKET_SIZE` | `256` | Maximum payload in bytes |

> **Tip:** Each aircraft gets a unique BSSID so multiple planes on the same channel don't interfere with each other.

---

## UART Protocol

Both directions use the same simple format:

```
┌──────────┬──────────┬─────────────────────┐
│ LEN_HI   │ LEN_LO   │ payload ...         │
│ (1 byte) │ (1 byte) │ (LEN bytes)         │
└──────────┴──────────┴─────────────────────┘
```

- 2-byte big-endian length prefix (payload length only)
- 460800 baud, 8N1
- ESP passes bytes through as-is — the flight controller handles encryption and validation

---

## Project Structure

```
esp-radio/
├── src/
│   ├── main.c            # Entry point, init, timer loop
│   ├── wifi_raw.c/.h     # 802.11 TX injection & RX promiscuous
│   ├── uart.c/.h         # UART driver with ring buffers
│   └── user_config.h     # All configuration constants
├── ld/
│   └── eagle.app.v6.ld   # Linker script (Non-OTA, 1 MB flash)
├── flash_tool.py         # GUI build & flash tool
├── Makefile              # Build system
└── HWL.png               # Hog Worxs Labs logo
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `xtensa-lx106-elf-gcc: not found` | `sudo apt install gcc-xtensa-lx106` |
| Flash fails with port error | Check `/dev/ttyUSB*` exists. Run `sudo usermod -a -G dialout $USER` and re-login. Ensure adapter is in **PROG** mode. |
| No output after flash | Switch adapter to **UART** mode. Verify baud rate is 460800. |
| ESP keeps resetting | Use **3.3V** (not 5V). Ensure 300 mA supply. Add 100 uF cap near VCC. |
| ESP-01 won't boot | Tie RST, CH_PD, and GPIO0 to 3.3V. |

---

## License

MIT License — see [LICENSE](LICENSE).

---

<p align="center">
  <a href="https://hogworxslabs.com"><strong>Hog Worxs Labs</strong></a><br>
  <sub>Electronics</sub>
</p>