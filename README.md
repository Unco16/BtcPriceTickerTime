# Bitcoin Price Ticker & Clock (ESP32)

An **ESP32**-based desk gadget that shows the live **Bitcoin price** and the **local time**, alternating on an 8‑digit MAX7219 seven‑segment display. It is fully configurable from a built‑in web page, keeps an on‑device price history graph, and can trigger a wake‑up alarm on a Sonos speaker through Home Assistant (MQTT).

This is the **ESP32 port** of the original ESP8266 project. A matching 3D‑printed "Bitcoin character" enclosure is available on [Thingiverse](https://www.thingiverse.com/thing:4747477).

![large_display_Btc_Pic_Fusion](https://github.com/user-attachments/assets/ad0010b7-baf8-4db9-9a62-e5666d76c4d3)

  
## Features

- **Live Bitcoin price** in **USD or EUR**, fetched from Coinbase (no account / no API key required).
- **NTP clock** with **automatic daylight‑saving handling** via a timezone picker.
- **Alternating display** of price and time, with a configurable switch interval.
- **Web interface** (dark theme by default) to configure and monitor everything:
  - Live price, clock and alarm status.
  - **Persistent price chart** built from on‑device history (one sample every 15 minutes).
  - MQTT broker configuration.
  - Alarm time + enable toggle.
  - Brightness slider (0–15).
  - General settings: currency, timezone, toggle interval, MQTT topic.
  - WiFi reset button and firmware version display.
- **Wake‑up alarm**: at the configured time the device publishes an MQTT message that a Home Assistant automation uses to play a track on a Sonos speaker.
- **WiFiManager** captive portal — no WiFi credentials hard‑coded in the firmware.
- **OTA updates** through ElegantOTA (upload a new firmware from the browser).
- **Settings persisted** in flash (NVS) — they survive reboots.

---

## What's new (vs. the ESP8266 version)

- **Hardware**: migrated from the Wemos D1 mini (ESP8266) to an **ESP32** (more RAM, far more reliable HTTPS — no more long‑uptime TLS lock‑ups).
- **Price source**: switched from CoinGecko to **Coinbase**, because CoinGecko's free API now enforces tight rate limits / requires an API key. Coinbase's spot endpoint is keyless and account‑free.
- **TLS**: BearSSL → `WiFiClientSecure` (mbedTLS).
- **Time**: `NTPClient` → built‑in SNTP with a POSIX timezone string and a friendly timezone dropdown (automatic DST).
- **Storage**: SPIFFS + JSON → `Preferences` (NVS).
- **OTA**: hand‑written upload handler → **ElegantOTA**.
- **Non‑blocking price fetch**: the display and web UI stay responsive even when the network is slow or down.
- **New UI features**: currency selector (USD/EUR), persistent price chart, configurable settings, alarm status on the main page, brightness slider, WiFi reset, firmware version.

Current firmware version: **2.1.0**.

---

## Hardware

- ESP32 development board (ESP‑WROOM‑32; a D1‑mini form‑factor board works well)
- MAX7219 8‑digit 7‑segment display module
- A few jumper wires
- USB power (the display is powered from the board's 5 V rail)
- *(Optional)* 3D‑printed Bitcoin enclosure (see Thingiverse link above)

### Wiring

| MAX7219 | ESP32 |
|---------|-------|
| DIN     | GPIO23 |
| CLK     | GPIO18 |
| CS / LOAD | GPIO19 |
| VCC     | 5V (the board's `VCC` pin) |
| GND     | GND |

The ESP32 drives the data lines at 3.3 V logic while the MAX7219 is powered at 5 V — this works reliably (it is the same arrangement as on the ESP8266). The pins are bit‑banged, so you can change them in the sketch if needed (avoid input‑only pins 34–39, the flash pins, and strapping pins).

---

## Libraries

Install via the Arduino IDE Library Manager:

- **WiFiManager** (by tzapu)
- **ArduinoJson** (v7.x)
- **LedControl** (by Eberhard Fahle)
- **PubSubClient** (by Nick O'Leary)
- **ElegantOTA** (by Ayush Sharma, v3.x)

`WiFi`, `HTTPClient`, `WiFiClientSecure`, `WebServer`, `Preferences` and `time` are provided by the ESP32 core — nothing to install for those.

> **Important — LedControl fix for ESP32:** the LedControl library includes `<avr/pgmspace.h>`, which does not exist on the ESP32 and causes a compile error. Open `Arduino/libraries/LedControl/src/LedControl.h` and replace
> ```cpp
> #include <avr/pgmspace.h>
> ```
> with
> ```cpp
> #if defined(ESP32) || defined(ESP8266)
>   #include <pgmspace.h>
> #else
>   #include <avr/pgmspace.h>
> #endif
> ```
> (A future library update may overwrite this edit; just re‑apply it if so.)

---

## Build & flash (Arduino IDE)

1. Install the **ESP32 boards** package (Espressif) via the Boards Manager.
2. Keep `ESP32_BTCPRICE.ino` and `webserver.h` together in a folder named `ESP32_BTCPRICE`.
3. Apply the LedControl fix above.
4. Select **Tools → Board → ESP32 Dev Module**.
5. Set **Flash Size: 4MB**, **Partition Scheme: Default 4MB with spiffs** (leaves room for OTA).
6. Select the correct **Port**, then upload.

> If you get an "async webserver" error from ElegantOTA, force the synchronous mode with `-D ELEGANTOTA_USE_ASYNC_WEBSERVER=0` (it is the default for the standard `WebServer`).

---

## First‑time setup (WiFi)

On first boot the device has no saved WiFi, so it opens its own setup access point:

1. Connect a phone or PC to the **`BTCTICKERAP`** WiFi network (open network).
2. A captive portal usually opens automatically; otherwise browse to **`http://192.168.4.1`**.
3. Pick your home network and enter its password.
4. The device reboots and connects. Its local IP is printed on the serial monitor at **115200 baud**.

Credentials are stored and reused on subsequent boots. You can clear them anytime from the web UI (**Settings → Reset WiFi**) to reopen the portal.

---

## Web interface

Open `http://<device-ip>/` in a browser. The page is dark‑themed by default (toggleable) and organised in tabs:

- **Bitcoin Price** — current price and a chart of the on‑device price history.
- **MQTT Configuration** — broker IP, user and password (applied live, no reboot).
- **Alarm Configuration** — alarm time and enable toggle.
- **Brightness** — a 0–15 slider (applied when released).
- **Settings** — currency (USD/EUR), price/time toggle interval, timezone, MQTT topic, WiFi reset and firmware version.
- **Firmware Update** — opens the ElegantOTA page at `/update` to upload a new `.bin`.

The header always shows the time and whether the alarm is **ON** (with its time) or **OFF**.

> The price history is kept in RAM and survives page reloads, but resets on a device reboot/power cycle.

---

## Price source

The device polls Coinbase once per minute:

```
https://api.coinbase.com/v2/prices/BTC-USD/spot   (or BTC-EUR)
```

This endpoint needs no account and no API key. TLS certificate validation is disabled (`setInsecure()`) to keep things simple — acceptable for a read‑only price display.

---

## Wake‑up alarm (Home Assistant + Sonos)

The ESP32 does not play audio itself. At the alarm time it **publishes an MQTT message** on the configured topic (default `sonos/play_alarm_btcclock`), and a Home Assistant automation plays a track on a Sonos speaker.

The simplest reliable method is to add the track you want as a **Sonos favorite** (in the Sonos app, with your music service connected) and play it by name. Example automation:

```yaml
- id: '1727641210000'
  alias: Play alarm on Sonos via MQTT
  triggers:
    - trigger: mqtt
      topic: sonos/play_alarm_btcclock
  actions:
    - action: media_player.volume_set
      target:
        entity_id: media_player.sonosmove
      data:
        volume_level: 0.3
    - action: media_player.play_media
      target:
        entity_id: media_player.sonosmove
      data:
        media_content_type: favorite_item_id
        media_content_id: "Your Sonos favorite name"
  mode: single
```

> If you change the MQTT topic in the device Settings, update the automation's trigger topic to match.

MQTT details: the broker is the one used by Home Assistant (e.g. the **Mosquitto** add‑on) on port **1883** — *not* the Home Assistant web port (8123). Enter the broker's IP and a valid MQTT user/password in the MQTT tab.

---

## OTA updates

Open the **Firmware Update** tab and click *Open firmware updater* (or browse to `http://<device-ip>/update`). Compile a new `.bin` in the Arduino IDE (**Sketch → Export Compiled Binary**) and upload it. Remember to bump `FIRMWARE_VERSION` in the sketch for each release so the UI reflects the running version.

---

## Notes & limitations

- Price history lives in RAM (24 h at 15‑minute resolution) and resets on reboot. It can be moved to LittleFS for reboot persistence if needed.
- TLS runs in insecure mode (no certificate pinning).
- The on‑board power LED cannot be turned off in software; remove it (or its series resistor) physically if you want it dark.

---

## Credits

Original project and 3D‑printed enclosure by **Unco16**. Ported to ESP32 with additional features.
