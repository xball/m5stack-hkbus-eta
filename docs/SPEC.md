# Spec — M5Stack HK Bus ETA

## 1. Purpose

Show real-time Hong Kong bus ETAs on an M5Stack Basic (ESP32) with a small screen and three buttons, without a phone.

## 2. Goals / non-goals

### Goals

- Display next 1–3 arrivals for user-selected route + stop favorites
- Support **KMB/LWB** and **Citybus (CTB)** via public APIs
- Configure favorites **on device** (NVS), not only via reflash
- Bilingual UI (Traditional Chinese primary, English toggle)
- Auto-refresh and manual refresh

### Non-goals (v1)

- Full city-wide fuzzy place search / 20 MB stop DB (see OpenClaw skill)
- Natural-language queries
- GPS / “nearby stops”
- Hardened TLS trust store (prototype uses `setInsecure()`)

## 3. Hardware target

| Property | Value |
|----------|--------|
| MCU | ESP32 dual-core @ 240 MHz |
| Flash | 4 MB (tested); 16 MB Basic v2.7 also possible with ini change |
| Display | 320×240 IPS, landscape |
| Input | Physical buttons A / B / C |
| Connectivity | Wi‑Fi 2.4 GHz, HTTPS |

## 4. User experience

### Main (ETA) screen

- Header: favorite index, route, Wi‑Fi status
- Stop name (TC + EN)
- Destination + next ETAs as clock time + minutes remaining
- Footer: button hints

| Input | Action |
|-------|--------|
| A (click) | Next favorite |
| B (click) | Force refresh (reconnect Wi‑Fi if needed) |
| C (click) | Toggle EN / 繁 |
| C (hold ~1 s) | Open menu |

### Menu

1. Add route / stop  
2. Delete current favorite  
3. Language  
4. Back  

### Add-route wizard

1. Operator: KMB or CTB  
2. Enter route (charset `0-9A-Z`, prefilled `40X`)  
3. Direction: outbound (`O`) / inbound (`I`)  
4. Load stop list from API → browse with A/C → **B** saves favorite  

Favorites: max **8**, persisted in NVS (`Preferences` namespace `hkbus`).

## 5. Data model

```text
Favorite {
  Operator op          // KMB | CTB
  char route[]         // e.g. "40X"
  char stop_id[]       // operator stop id
  char label_tc/en[]   // display names
  uint8_t service_type // KMB, usually 1
  char dir             // 'O' | 'I' | 0=any
}
```

## 6. External APIs

### KMB / LWB

| Use | Endpoint |
|-----|----------|
| Route stops | `GET .../kmb/route-stop/{route}/{outbound\|inbound}/{service_type}` |
| Stop name | `GET .../kmb/stop/{stop_id}` |
| ETA | `GET .../kmb/eta/{stop_id}/{route}/{service_type}` |
| Fallback | `GET .../kmb/stop-eta/{stop_id}` then filter by route + dir |

Base: `https://data.etabus.gov.hk/v1/transport/`

### Citybus

| Use | Endpoint |
|-----|----------|
| Route stops | `GET .../citybus/route-stop/CTB/{route}/{outbound\|inbound}` |
| Stop name | `GET .../citybus/stop/{stop_id}` |
| ETA | `GET .../citybus/eta/CTB/{stop_id}/{route}` |

Base: `https://rt.data.gov.hk/v2/transport/`

### ETA processing

1. Parse ISO-8601 timestamps with explicit `+08:00` offset → UTC epoch  
2. Device clock: NTP with `TZ=HKT-8`  
3. Filter by route and `dir` when set  
4. Drop departures already >45 s past  
5. Sort ascending by ETA; keep first 3  
6. Minutes display: **floor** `(eta - now) / 60` (app-like)

## 7. Software architecture

```text
main.cpp          UI modes + button FSM + Wi-Fi/NTP + refresh timer
bus_api.cpp       HTTPS client, JSON (ArduinoJson), ETA/stop helpers
favorites.cpp     NVS load/save/seed
ui.cpp            M5Unified / M5GFX drawing (efontTW)
config.h          Wi-Fi macros + shared types
```

### Constraints addressed

- **Stack overflow**: `RouteStopList` lives in BSS; filled by reference (never returned by value)  
- **Heap / TLS**: yield + watchdog kicks during HTTPS; JSON filter when parsing stop IDs only  
- **Loop stack**: `board_build.arduino.loop_task_stack_size = 16384`  
- **Flash**: `huge_app.csv` partition for CJK fonts  

## 8. Configuration

| Macro / setting | Meaning |
|-----------------|--------|
| `WIFI_SSID` / `WIFI_PASSWORD` | Station credentials |
| `REFRESH_INTERVAL_MS` | Auto-refresh period (default 60000 = 60 s) |
| `upload_port` in `platformio.ini` | Serial port for flash |

## 9. Acceptance criteria (v1)

- [ ] Connects to configured Wi‑Fi and syncs NTP  
- [ ] Shows ETA for seeded or user-added KMB favorite  
- [ ] Can add CTB favorite on device without reflash  
- [ ] Survives loading a full route stop list without reboot  
- [ ] Direction filter changes which destination/ETAs appear  
- [ ] TC glyphs via efontTW; EN line always visible as fallback  

## 10. Future work

- Wi‑Fi SoftAP / captive portal for SSID without reflash  
- CA-pinned TLS  
- SD-card or trimmed stop index for faster name resolve  
- Multi-route dashboard on one screen  
- Battery / sleep mode for portable use  

## 11. License & attribution

- Firmware: GPL-3.0 (repository license)  
- ETA data: KMB, LWB, Citybus via DATA.GOV.HK — follow open-data terms  
- Concept inspired by community HK bus ETA tools (e.g. OpenClaw skill), reimplemented for ESP32  
