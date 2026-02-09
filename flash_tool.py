#!/usr/bin/env python3
"""
ESP-01S Radio Flash Tool — Hog Worxs Labs
Configure and flash ESP-01S firmware for RC plane radio link.
"""

import os
import sys
import re
import glob
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext

# ── Paths ─────────────────────────────────────────────
# PyInstaller sets sys.executable to the bundled exe path;
# normal Python sets __file__ to the script path.
if getattr(sys, 'frozen', False):
    SCRIPT_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "src", "user_config.h")

# ── Theme (Hog Worxs Labs) ───────────────────────────
BG        = "#09090b"
BG_FIELD  = "#18181b"
BG_HOVER  = "#27272a"
FG        = "#fafafa"
FG_DIM    = "#a1a1aa"
FG_ACCENT = "#e4e4e7"
BORDER    = "#3f3f46"
GREEN     = "#4ade80"
RED       = "#f87171"
YELLOW    = "#facc15"

FONT      = ("DejaVu Sans Mono", 11)
FONT_SM   = ("DejaVu Sans Mono", 9)
FONT_LG   = ("DejaVu Sans Mono", 13, "bold")
FONT_TITLE = ("DejaVu Sans Mono", 20, "bold")

# ── Config helpers ────────────────────────────────────

def read_config():
    with open(CONFIG_FILE, "r") as f:
        return f.read()

def write_config(content):
    with open(CONFIG_FILE, "w") as f:
        f.write(content)

def get_define(content, name):
    if name == "CUSTOM_BSSID":
        m = re.search(r'#define\s+CUSTOM_BSSID\s+\{([^}]+)\}', content)
        if m:
            hexb = [b.strip().replace("0x", "").replace("0X", "") for b in m.group(1).split(",")]
            return ":".join(hexb)
        return "AA:BB:CC:DD:EE:FF"
    m = re.search(r'#define\s+' + name + r'\s+(\S+)', content)
    return m.group(1) if m else ""

def set_define(content, name, value):
    if name == "CUSTOM_BSSID":
        parts = value.split(":")
        bssid = "{" + ", ".join(f"0x{p}" for p in parts) + "}"
        return re.sub(r'(#define\s+CUSTOM_BSSID\s+)\{[^}]+\}', r'\g<1>' + bssid, content)
    return re.sub(r'(#define\s+' + name + r'\s+)\S+(.*)', r'\g<1>' + value + r'\2', content)

def find_ports():
    return sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))

# ── TX Rate mapping ───────────────────────────────────

TX_RATES = {
    "PHY_RATE_1M_L":  "1M  (max range)",
    "PHY_RATE_2M_L":  "2M  (good range)",
    "PHY_RATE_5M_S":  "5.5M (balanced)",
    "PHY_RATE_11M_S": "11M (short range)",
}
TX_RATE_KEYS = list(TX_RATES.keys())

# ── GUI ───────────────────────────────────────────────

class FlashTool(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ESP-01S Radio — Hog Worxs Labs")
        self.configure(bg=BG)
        self.geometry("660x720")
        self.resizable(False, False)

        self.config_content = read_config()
        self.flashing = False
        self.entries = {}

        self._build_ui()
        self._load_values()

    def _build_ui(self):
        # ── Title ──
        tk.Label(self, text="HOG WORXS LABS", font=FONT_TITLE,
                 fg=FG, bg=BG, anchor="w").pack(fill="x", padx=24, pady=(20, 0))
        tk.Label(self, text="ESP-01S Radio Flash Tool", font=FONT,
                 fg=FG_DIM, bg=BG, anchor="w").pack(fill="x", padx=24, pady=(0, 8))
        tk.Frame(self, bg=BORDER, height=1).pack(fill="x", padx=24)

        # ── Port row ──
        pf = tk.Frame(self, bg=BG)
        pf.pack(fill="x", padx=24, pady=(14, 0))
        tk.Label(pf, text="PORT", font=FONT, fg=FG_DIM, bg=BG).pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(pf, textvariable=self.port_var,
                                    font=FONT, state="readonly", width=16)
        self.port_cb.pack(side="left", padx=(12, 0))
        self._refresh_ports()
        tk.Button(pf, text="REFRESH", font=FONT_SM, fg=FG_DIM, bg=BG_FIELD,
                  activeforeground=FG, activebackground=BG_HOVER,
                  relief="flat", bd=0, padx=10, pady=3,
                  command=self._refresh_ports).pack(side="left", padx=(10, 0))

        # ── Config section ──
        tk.Label(self, text="CONFIGURATION", font=FONT_SM,
                 fg=FG_DIM, bg=BG, anchor="w").pack(fill="x", padx=24, pady=(16, 6))

        cfg = tk.Frame(self, bg=BG_FIELD, highlightbackground=BORDER, highlightthickness=1)
        cfg.pack(fill="x", padx=24)
        cfg.columnconfigure(1, weight=1)

        rows = [
            ("WIFI_DEFAULT_CHANNEL", "WiFi Channel",    "entry",      "1-14, match both ends"),
            ("CUSTOM_BSSID",         "Link ID (BSSID)", "entry",      "unique per plane"),
            ("WIFI_TX_RATE",         "TX Rate",         "combo_tx",   "lower = more range"),
            ("UART_BAUD_RATE",       "Baud Rate",       "combo_baud", "match RP2040"),
            ("MAX_PACKET_SIZE",      "Max Packet Size", "entry",      "16-256 bytes"),
        ]

        for i, (define, label, wtype, hint) in enumerate(rows):
            pad_top = 12 if i == 0 else 6
            pad_bot = 12 if i == len(rows) - 1 else 0

            # Label + hint
            tk.Label(cfg, text=label, font=FONT, fg=FG_ACCENT, bg=BG_FIELD,
                     anchor="w").grid(row=i*2, column=0, sticky="w", padx=(14, 8), pady=(pad_top, 0))
            tk.Label(cfg, text=hint, font=FONT_SM, fg=FG_DIM, bg=BG_FIELD,
                     anchor="w").grid(row=i*2+1, column=0, sticky="w", padx=(14, 8), pady=(0, pad_bot))

            # Input widget
            if wtype == "entry":
                var = tk.StringVar()
                e = tk.Entry(cfg, textvariable=var, font=FONT, fg=FG, bg=BG,
                             insertbackground=FG, relief="flat",
                             highlightbackground=BORDER, highlightthickness=1, width=24)
                e.grid(row=i*2, column=1, rowspan=2, sticky="e", padx=(0, 14), pady=(pad_top, pad_bot))
                self.entries[define] = var

            elif wtype == "combo_tx":
                var = tk.StringVar()
                c = ttk.Combobox(cfg, textvariable=var, font=FONT, state="readonly",
                                 width=22, values=[TX_RATES[k] for k in TX_RATE_KEYS])
                c.grid(row=i*2, column=1, rowspan=2, sticky="e", padx=(0, 14), pady=(pad_top, pad_bot))
                self.entries[define] = var

            elif wtype == "combo_baud":
                var = tk.StringVar()
                c = ttk.Combobox(cfg, textvariable=var, font=FONT, state="readonly",
                                 width=22, values=["115200", "230400", "460800", "921600"])
                c.grid(row=i*2, column=1, rowspan=2, sticky="e", padx=(0, 14), pady=(pad_top, pad_bot))
                self.entries[define] = var

        # ── Flash button ──
        self.flash_btn = tk.Button(self, text="[ FLASH FIRMWARE ]", font=FONT_LG,
                                   fg=BG, bg=FG, activeforeground=BG,
                                   activebackground=FG_DIM, relief="flat",
                                   bd=0, padx=20, pady=10, command=self._flash)
        self.flash_btn.pack(fill="x", padx=24, pady=(16, 0))

        # ── Output log ──
        tk.Label(self, text="OUTPUT", font=FONT_SM, fg=FG_DIM, bg=BG,
                 anchor="w").pack(fill="x", padx=24, pady=(12, 4))
        self.log = scrolledtext.ScrolledText(self, font=FONT_SM, fg=FG_DIM, bg=BG_FIELD,
                                              insertbackground=FG, relief="flat", height=8,
                                              highlightbackground=BORDER, highlightthickness=1)
        self.log.pack(fill="both", expand=True, padx=24, pady=(0, 12))
        self.log.configure(state="disabled")

        # ── Status bar ──
        self.status_var = tk.StringVar(value="Ready")
        tk.Label(self, textvariable=self.status_var, font=FONT_SM,
                 fg=FG_DIM, bg=BG, anchor="w").pack(fill="x", padx=24, pady=(0, 8))

        # ── Style comboboxes ──
        s = ttk.Style()
        s.theme_use("clam")
        s.configure("TCombobox", fieldbackground=BG, background=BG_FIELD,
                    foreground=FG, arrowcolor=FG_DIM, bordercolor=BORDER,
                    lightcolor=BG, darkcolor=BG)
        s.map("TCombobox", fieldbackground=[("readonly", BG)],
              foreground=[("readonly", FG)])

    # ── Helpers ───────────────────────────────────────

    def _refresh_ports(self):
        ports = find_ports()
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _load_values(self):
        c = self.config_content
        self.entries["WIFI_DEFAULT_CHANNEL"].set(get_define(c, "WIFI_DEFAULT_CHANNEL"))
        self.entries["CUSTOM_BSSID"].set(get_define(c, "CUSTOM_BSSID"))
        self.entries["MAX_PACKET_SIZE"].set(get_define(c, "MAX_PACKET_SIZE"))
        self.entries["UART_BAUD_RATE"].set(get_define(c, "UART_BAUD_RATE"))
        tx = get_define(c, "WIFI_TX_RATE")
        self.entries["WIFI_TX_RATE"].set(TX_RATES.get(tx, TX_RATES[TX_RATE_KEYS[0]]))

    def _log(self, text, color=None):
        self.log.configure(state="normal")
        if color:
            tag = f"c_{color}"
            self.log.tag_configure(tag, foreground=color)
            self.log.insert("end", text + "\n", tag)
        else:
            self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _validate(self):
        try:
            ch = int(self.entries["WIFI_DEFAULT_CHANNEL"].get())
            if not 1 <= ch <= 14:
                self._log("ERROR: Channel must be 1-14", RED)
                return False
        except ValueError:
            self._log("ERROR: Channel must be a number", RED)
            return False

        bssid = self.entries["CUSTOM_BSSID"].get().strip()
        parts = bssid.split(":")
        if len(parts) != 6 or not all(len(p) == 2 for p in parts):
            self._log("ERROR: BSSID format: AA:BB:CC:DD:EE:FF", RED)
            return False
        try:
            [int(p, 16) for p in parts]
        except ValueError:
            self._log("ERROR: BSSID has invalid hex", RED)
            return False

        try:
            pkt = int(self.entries["MAX_PACKET_SIZE"].get())
            if not 16 <= pkt <= 256:
                self._log("ERROR: Packet size must be 16-256", RED)
                return False
        except ValueError:
            self._log("ERROR: Packet size must be a number", RED)
            return False

        return True

    def _apply_config(self):
        c = self.config_content
        c = set_define(c, "WIFI_DEFAULT_CHANNEL", self.entries["WIFI_DEFAULT_CHANNEL"].get().strip())
        c = set_define(c, "CUSTOM_BSSID", self.entries["CUSTOM_BSSID"].get().strip().upper())
        c = set_define(c, "MAX_PACKET_SIZE", self.entries["MAX_PACKET_SIZE"].get().strip())
        c = set_define(c, "UART_BAUD_RATE", self.entries["UART_BAUD_RATE"].get().strip())
        tx_disp = self.entries["WIFI_TX_RATE"].get()
        for key, desc in TX_RATES.items():
            if desc == tx_disp:
                c = set_define(c, "WIFI_TX_RATE", key)
                break
        self.config_content = c

    # ── Flash ─────────────────────────────────────────

    def _flash(self):
        if self.flashing:
            return
        port = self.port_var.get()
        if not port:
            self._log("ERROR: No port selected", RED)
            return
        if not self._validate():
            return
        self._apply_config()
        self.flashing = True
        self.flash_btn.configure(state="disabled", text="[ FLASHING... ]", bg=FG_DIM)
        threading.Thread(target=self._flash_worker, args=(port,), daemon=True).start()

    def _flash_worker(self, port):
        try:
            self.after(0, self._log, "Writing config...")
            write_config(self.config_content)

            self.after(0, self._log, "Building firmware...", YELLOW)
            subprocess.run(["make", "clean"], cwd=SCRIPT_DIR, capture_output=True, text=True)
            r = subprocess.run(["make"], cwd=SCRIPT_DIR, capture_output=True, text=True)
            if r.returncode != 0:
                self.after(0, self._log, "BUILD FAILED:", RED)
                for line in (r.stdout + r.stderr).splitlines()[-10:]:
                    self.after(0, self._log, f"  {line}", RED)
                self.after(0, self._done, False)
                return
            self.after(0, self._log, "Build OK", GREEN)

            self.after(0, self._log, f"Flashing {port}...", YELLOW)
            r = subprocess.run(["make", "flash", f"SERIAL_PORT={port}"],
                               cwd=SCRIPT_DIR, capture_output=True, text=True)
            if r.returncode != 0:
                self.after(0, self._log, "FLASH FAILED:", RED)
                for line in (r.stdout + r.stderr).splitlines()[-10:]:
                    self.after(0, self._log, f"  {line}", RED)
                self.after(0, self._done, False)
                return
            self.after(0, self._log, "Flash complete!", GREEN)
            self.after(0, self._log, "Switch to UART mode to run.", FG_ACCENT)
            self.after(0, self._done, True)
        except Exception as e:
            self.after(0, self._log, f"ERROR: {e}", RED)
            self.after(0, self._done, False)

    def _done(self, ok):
        self.flashing = False
        self.flash_btn.configure(state="normal", text="[ FLASH FIRMWARE ]", bg=FG)
        self.status_var.set("Flash complete" if ok else "Flash failed")


if __name__ == "__main__":
    app = FlashTool()
    app.mainloop()
