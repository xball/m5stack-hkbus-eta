# HK Bus ETA — M5Stack Basic Core

Live Hong Kong bus arrival times (**KMB / LWB / Citybus**) on an [M5Stack ESP32 Basic Core](https://shop.m5stack.com/products/esp32-basic-core-lot-development-kit-v2-7).

Lightweight C++ firmware (PlatformIO). Favorites-based UX sized for ESP32 — not a port of the full [hk-bus-eta-skill](https://github.com/tomfong/hk-bus-eta-skill) Python/SQLite agent package, but it uses the **same public ETA APIs**.

**Spec:** [docs/SPEC.md](docs/SPEC.md)

## Features

- Wi‑Fi HTTPS to official DATA.GOV.HK / etabus APIs
- On-device **add route → pick direction → pick stop** (saved in NVS, up to 8)
- **1-bus or 2-bus screen** (left + right) — **long-press A** to toggle; preference saved in NVS
- In 2-bus mode, stop / destination / status text clipped to **10 Chinese characters** per row (column clip so panes do not overlap)
- Favorites and layout preference **survive normal firmware upload** (do not erase flash / NVS)
- 320×240 UI, Traditional Chinese font + English fallback
- **A** next · **B** refresh · **C** language · **long-press C** menu
- Auto-refresh (default 60 s, `REFRESH_INTERVAL_MS` in `include/config.h`)

## On-screen controls

| Input | Action |
|-------|--------|
| A click | Next favorite |
| **A long-press** | Toggle **1-bus / 2-bus** layout |
| B click | Refresh |
| C click | EN / 繁 |
| C long-press | Menu |

In **2-bus** mode: left = current favorite, right = next in the list (need ≥2 stops). Long names are truncated to 10 CJK characters per field so the two columns stay readable.

## Hardware

| Item | Notes |
|------|--------|
| Board | M5Stack Basic / Core (ESP32), tested on Basic with **4 MB** flash |
| Display | 2.0″ 320×240 IPS |
| USB | CP210x or CH9102 — install driver if no COM port |
| Network | 2.4 GHz Wi‑Fi |

## Quick start

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html).
2. Set Wi‑Fi in [`include/config.h`](include/config.h):

```cpp
#define WIFI_SSID "YourNetwork"
#define WIFI_PASSWORD "YourPassword"
```

3. Build & upload (set `upload_port` in `platformio.ini` if needed):

```bash
pio run -t upload
pio device monitor
```

4. On device: **long-press C** → **新增路線/選站** → enter route (e.g. `40X`) → direction → stop → **B** to save.

First boot seeds **40X** at 烏溪沙站 and 葵涌邨總站 if NVS is empty.

### Buttons (route entry)

| Button | Action |
|--------|--------|
| A | Change character (0–9, A–Z) |
| C | Next character |
| Long-press C | Delete last character |
| B | Confirm |

## Project layout

```
include/config.h      Wi-Fi + types
include/bus_api.h     ETA / stop-list API
include/favorites.h   NVS favorites + dual-pane flag
include/ui.h
src/main.cpp          Modes, buttons, refresh
src/bus_api.cpp       KMB + Citybus HTTPS + JSON
src/favorites.cpp
src/ui.cpp
docs/SPEC.md          Product / technical spec
platformio.ini
```

## Data sources

- [KMB/LWB ETA](https://data.gov.hk/en-data/dataset/hk-td-tis_21-etakmb) — `https://data.etabus.gov.hk/v1/transport/kmb/`
- [Citybus ETA](https://data.gov.hk/en-data/dataset/ctb-eta-transport-realtime-eta) — `https://rt.data.gov.hk/v2/transport/citybus/`

## Notes

- TLS uses `setInsecure()` for prototype simplicity; embed a CA for production.
- Flash layout: `huge_app` partition, 4 MB (override in `platformio.ini` for 16 MB modules).
- NVS (`Preferences` namespace `hkbus`) holds favorites and the dual-pane flag. A normal `pio run -t upload` keeps them; full flash erase / NVS clear does not.
- Do **not** commit real Wi‑Fi passwords.

## License

[GPL-3.0](LICENSE) — see repository license. Bus data remains subject to DATA.GOV.HK / operator terms.
