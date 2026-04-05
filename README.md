# hms-fysetc

> **EXPERIMENTAL** -- This project is in early development. Contributions welcome.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3-blue.svg?logo=espressif)](https://docs.espressif.com/projects/esp-idf/)
[![Status: Experimental](https://img.shields.io/badge/Status-Experimental-orange.svg)]()

Turn a [FYSETC SD WiFi Pro](https://www.fysetc.com/products/fysetc-sd-wifi-pro-esp32-sdio-sd-tf-card) into a WiFi file server for any SD card. Serves files over HTTP on your home network -- no cloud, no subscription.

The FYSETC SD WiFi Pro is shaped like an SD card with an ESP32 inside. Insert your micro SD into the FYSETC board, plug it into any device's SD slot, and read files over WiFi. The host device and ESP32 share the bus safely -- the SD is only mounted read-only during HTTP requests.

## How It Works

```
Host device SD slot
    |
    v
+------------------------+
|  FYSETC SD WiFi Pro    |  <- Plugs into any SD card slot
|  (ESP32 + SD card)     |     Joins your home WiFi
|                        |     Serves files over HTTP
+------------------------+
    |
    v  (HTTP over WiFi)
+------------------------+
|  Any HTTP client       |  <- curl, Python, browser, etc.
+------------------------+
```

The firmware is a **read-only HTTP file server**. Between requests, the SD bus is released back to the host device. Tested with concurrent host writes and HTTP reads -- no conflicts.

## HTTP API

| Endpoint | Description |
|----------|-------------|
| `GET /dir?dir=A:path` | HTML directory listing |
| `GET /dir?dir=A:path%5Csubdir` | HTML listing for a subfolder |
| `GET /download?file=path%5Cfilename` | Raw file download (supports Range header) |
| `GET /api/status` | JSON device status |
| `GET /` | Web UI with live logs and PCNT bus monitor |

Path separators use `%5C` (backslash) in query parameters. The `A:` prefix is optional.

## Quick Start

### 1. Install ESP-IDF

Requires [ESP-IDF v5.3+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/).

```bash
. ~/esp/esp-idf/export.sh
```

### 2. Configure

```bash
idf.py set-target esp32
idf.py menuconfig
```

Navigate to **HMS-FYSETC Configuration**. You can optionally set:

- **WiFi** -- hardcode SSID and password (compile-time defaults)
- **Device Identity** -- name and version (optional)

If you skip WiFi configuration, the device starts a **captive portal** on first boot where you enter credentials from your phone.

### 3. Build and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On macOS the port is typically `/dev/cu.usbserial-*`.

### 4. Connect to WiFi

The device tries WiFi credentials in this order:

1. **NVS** -- credentials saved via the captive portal (persists across reboots)
2. **Kconfig** -- compile-time defaults from `idf.py menuconfig`
3. **Captive Portal** -- if neither exists, the device creates an open AP named `FYSETC-Setup-XXXX`

If using the captive portal:
- Connect your phone to the `FYSETC-Setup-XXXX` WiFi network
- A setup page opens automatically (or navigate to `http://192.168.4.1`)
- Select your WiFi network from the scan list and enter the password
- The device saves credentials to NVS and reboots

If NVS credentials fail to connect, they are automatically cleared and the device reboots into the captive portal.

### 5. Find the IP

The device prints its IP on the serial console at boot:

```
I (1234) wifi: Connected, IP: 192.168.x.x
```

Set a DHCP reservation in your router so the IP doesn't change.

### 6. Test

```bash
# List root directory
curl "http://<IP>/dir?dir=A:/"

# List a subdirectory
curl "http://<IP>/dir?dir=A:DATALOG"

# Download a file
curl "http://<IP>/download?file=DATALOG%5Cfile.dat" -o file.dat

# Check status
curl "http://<IP>/api/status"
```

## WiFi Credential Priority

| Priority | Source | How to set |
|----------|--------|------------|
| 1 | NVS (runtime) | Captive portal web form |
| 2 | Kconfig (compile-time) | `idf.py menuconfig` |
| 3 | Captive portal | Automatic if no credentials found |

NVS credentials override Kconfig. To force the captive portal, erase NVS with `idf.py erase-flash` or flash with default Kconfig (leave SSID as `your_wifi_ssid`).

## Web UI

Navigate to `http://<IP>/` in a browser to access the built-in dashboard:

- **Logs** -- Live ESP32 log output (color-coded by severity)
- **PCNT** -- Real-time pulse counter chart showing host SD bus activity

## Hardware

- **Board:** [FYSETC SD WiFi Pro](https://www.fysetc.com/products/fysetc-sd-wifi-pro-esp32-sdio-sd-tf-card)
- **SoC:** ESP32-PICO-D4, 4MB flash
- **SD interface:** SDMMC 4-bit (shared with host via analog MUX)
- **Bus switch:** GPIO 26 controls MUX (HIGH = host, LOW = ESP32)
- **Bus monitor:** GPIO 33 senses host CS line activity (PCNT hardware counter)

## Project Structure

```
main/
  main.c              # Entry point (NVS -> Kconfig -> captive portal flow)
  config.h            # Kconfig -> #define mappings
  pins_config.h       # GPIO assignments
  file_server.c/h     # HTTP file endpoints (/dir, /download, /api/status)
  sd_manager.c/h      # SD bus MUX control, mount/unmount
  wifi_manager.c/h    # WiFi station mode (compile-time + dynamic)
  web_server.c/h      # HTTP server, log capture, dashboard UI
  traffic_monitor.c/h # PCNT-based bus activity monitor (passive)
  nvs_store.c/h       # NVS storage for WiFi credentials
  captive_portal.c/h  # AP mode WiFi setup with DNS hijack
```

## Acknowledgments

- [amanuense](https://github.com/amanuense/CPAP_data_uploader) -- Original FYSETC SD WiFi Pro project that proved the hardware works.

## License

MIT
