# s3jam — ESP32-S3 "Page Carousel" Demo Firmware

**s3jam** is an Arduino test/demo project for the **Waveshare ESP32-S3-LCD-1.28 (non-touch)** development board. It turns the little round-screen board into a self-running gadget that cycles through a carousel of independent "pages": sensor readouts, radio scanners, 3-D animations, and small pixel-art life simulations (a frog pond and an isometric city).

The project is organized as a collection of loosely-coupled modules built on top of Waveshare's official LCD/IMU driver stack. Each page is a self-contained `.h`/`.cpp` pair following a common lifecycle contract, and a small scheduler in the main sketch rotates between the enabled pages on fixed timers.

- **Repository:** <https://github.com/bitseq27/s3jam> (single squashed commit `43dcfde`, branch `main`)
- **Sketch entry point:** `S3_jam.ino`
- **Language/framework:** C++ / Arduino (ESP32 core), no external library dependencies beyond the built-in `WiFi` and `BLE` libraries

---

## Table of Contents

1. [Hardware](#1-hardware)
2. [Repository Layout](#2-repository-layout)
3. [Architecture Overview](#3-architecture-overview)
4. [Page Scheduler (`S3_jam.ino`)](#4-page-scheduler-s3_jamino)
5. [Core Services](#5-core-services)
6. [Page Modules](#6-page-modules)
7. [Waveshare Driver Stack](#7-waveshare-driver-stack)
8. [Graphics Pipeline & Memory Budget](#8-graphics-pipeline--memory-budget)
9. [Building & Flashing](#9-building--flashing)
10. [Configuration & Customization](#10-configuration--customization)
11. [How to Add a New Page](#11-how-to-add-a-new-page)
12. [Serial Logging](#12-serial-logging)
13. [Known Quirks & Caveats](#13-known-quirks--caveats)
14. [References](#14-references)

---

## 1. Hardware

### 1.1 Board: Waveshare ESP32-S3-LCD-1.28 (non-touch version)

Per the Waveshare wiki (supplied as `ESP32-S3-LCD-1.28 - Waveshare Wiki.pdf`):

| Component | Detail |
|---|---|
| SoC | **ESP32-S3R2** — dual-core Xtensa 32-bit LX7 @ up to 240 MHz, 512 KB SRAM, 384 KB ROM |
| PSRAM | **2 MB**, stacked in package (OPI) |
| Flash | **16 MB** external NOR flash (W25Q128JVSIQ) |
| Radio | 2.4 GHz Wi-Fi (802.11 b/g/n) + Bluetooth 5 (LE), onboard ceramic antenna |
| Display | **1.28″ round IPS LCD, 240 × 240**, 65K colors (RGB565), driver **GC9A01A**, 4-wire SPI (up to 80 MHz) |
| IMU | **QMI8658** 6-axis (3-axis accelerometer ±2/±4/±8/±16 g, 3-axis gyroscope ±16…±2048 °/s, 16-bit) on I²C |
| Battery | MX1.25 2-pin connector for a **3.7 V lithium cell**; ETA6096 charge/discharge chip; voltage divided 200 kΩ / 100 kΩ into the battery ADC pin |
| USB | Type-C via **CH343P** USB-to-UART bridge (to ESP32-S3 GPIO43/GPIO44) with auto-download circuit — used for flashing and serial logs |
| Power | ME6217C33M5G LDO (800 mA) |
| Buttons | **RESET** and **BOOT** (BOOT held during reset ⇒ download mode) |
| Expansion | 1.27 mm pitch header breaking out most GPIOs; VSYS accepts 5 V |
| Display size | Ø 32.4 mm, pixel pitch 0.135 × 0.135 mm |

### 1.2 Pin map (wiki pin table, matches `DEV_Config.h` exactly)

| Function | GPIO | Defined in `DEV_Config.h` as |
|---|---|---|
| LCD D/C (data/command) | 8 | `LCD_DC_PIN` |
| LCD CS (SPI chip select) | 9 | `LCD_CS_PIN` |
| LCD CLK (SPI clock) | 10 | `LCD_CLK_PIN` |
| LCD MOSI (SPI data out) | 11 | `LCD_MOSI_PIN` |
| LCD MISO | not connected | `LCD_MISO_PIN` = `-1` |
| LCD RST (hardware reset) | 12 | `LCD_RST_PIN` |
| LCD backlight (PWM) | 40 | `LCD_BL_PIN` |
| I²C SDA (to QMI8658) | 6 | `DEV_SDA_PIN` |
| I²C SCL (to QMI8658) | 7 | `DEV_SCL_PIN` |
| Battery voltage ADC | 1 | `BAT_ADC_PIN` |
| IMU INT1 / INT2 | 47 / 48 | not used by this project |
| BOOT button | 0 | not used by this project |
| UART TX / RX (CH343P) | 43 / 44 | default `Serial` |

Bus configuration established in `DEV_Module_Init()`:

- **SPI:** VSPI (= FSPI on ESP32-S3), **80 MHz**, MSB-first, SPI mode 0; bulk writes use `transferBytes()` (DMA-friendly).
- **I²C:** pins 6/7, **400 kHz**.
- **ADC:** 12-bit resolution; battery read via `analogReadMilliVolts()`.
- **Backlight:** LEDC PWM via `analogWrite()`, set to **50 %** at startup (`DEV_SET_Backlight(50)`).

---

## 2. Repository Layout

The repository is a flat Arduino sketch folder (no `src/`, no `library.properties`, no build files).

### Application core

| File | Role |
|---|---|
| `S3_jam.ino` | Main sketch: page scheduler (`setup()` / `loop()`), page registry, enable flags, per-page durations |
| `display_buffer.h/.cpp` | Framebuffer allocation + high-level display helpers (auto-scaled centered text, text-fit utilities) |

### Page modules (one `.h`/`.cpp` pair per screen)

| Files | Page | Summary |
|---|---|---|
| `sensor_page.*` | Sensor Data | QMI8658 accel/gyro readout + battery voltage |
| `wifi_scan_page.*` | Wi-Fi Scan | Scans for networks, shows count (logs SSIDs) |
| `ble_scan_page.*` | BLE Scan | Scans for BLE advertisers, shows unique count |
| `temperature_page.*` | Temperature | ESP32 internal die temperature |
| `starfield.*` | Starfield | 5,000-star 3-D warp animation with FPS/CPU overlay |
| `cube.*` | Cube | Shaded, rotating solid 3-D cube |
| `uptime.*` | Uptime | `HH:MM:SS` (or `Xd HH:MM:SS`) since boot |
| `frog_page.*` | Frog | Animated pixel-art frog pond "life sim" with day/night cycle |
| `city_page.*` | City | Isometric pixel city with traffic, pedestrians, and a ticker |
| `wifi_connect.*` | Wi-Fi Connect | Connects to a hardcoded AP, shows status |

### Waveshare driver stack (vendored, lightly modified)

| Files | Role |
|---|---|
| `DEV_Config.h/.cpp` | Hardware abstraction: GPIO, SPI, I²C, ADC, delays, backlight PWM |
| `LCD_1in28.h/.cpp` | GC9A01A LCD driver: reset, init register sequence, windowing, full-frame and partial push |
| `QMI8658.h/.cpp` | QMI8658 IMU driver: config, scaled accel/gyro/temp reads, wake-on-motion |
| `GUI_Paint.h/.cpp` | 2-D drawing library operating on an in-RAM image buffer (points, lines, rectangles, circles, text, numbers, bitmaps) |
| `fonts.h`, `font8/12/16/20/24.cpp` | STMicroelectronics-style ASCII bitmap fonts (`sFONT`, 1 bpp) |
| `ImageData.h/.cpp` | ~2.2 MB of const image/icon arrays carried over from Waveshare demos (not referenced by the jam pages) |
| `LCD_Test.h` | Aggregate convenience header (pulls in all driver headers). Declares `LCD_1in28_test()` but the implementing `LCD_Test.cpp` is **not** in the repo |
| `Debug.h` | `Debug(...)` printf wrapper macro (`DEV_DEBUG = 1`) |

### Misc

| File | Role |
|---|---|
| `!git push.bat` | Windows helper script that re-inits git history and force-pushes to `origin/main` (destructive — rewrites remote history) |

---

## 3. Architecture Overview

The code is layered: the sketch only knows about pages; pages only know about the framebuffer and drawing helpers; the Waveshare stack talks to hardware.

```
┌──────────────────────────────────────────────────────────────────┐
│  S3_jam.ino — page scheduler                                     │
│  (page registry · enable flags · durations · enter/leave/loop)   │
└───────┬──────────────────────────────────────────────────────────┘
        │ <page>_enter() / <page>_loop(now) / <page>_exit()
┌───────▼──────────────────────────────────────────────────────────┐
│  Page modules                                                    │
│  sensor · wifi_scan · ble_scan · temperature · starfield ·       │
│  cube · uptime · frog · city · wifi_connect                      │
└───────┬─────────────────────────────┬────────────────────────────┘
        │ direct pixel writes         │ Paint_* drawing API
        │ (frog/city/starfield,       │ (sensor/cube/uptime/…)
        │  pre-swapped RGB565)        │
┌───────▼─────────────────────────────▼────────────────────────────┐
│  display_buffer — BlackImage framebuffer (240×240×RGB565)        │
│  + drawScaledTextPage() / fitTextScale() / drawCenteredScaled…   │
├──────────────────────────────────────────────────────────────────┤
│  GUI_Paint (2-D raster ops)  ·  fonts (Font8…Font24)             │
├──────────────────────────────────────────────────────────────────┤
│  LCD_1in28 (GC9A01A driver)   QMI8658 (IMU driver)               │
├──────────────────────────────────────────────────────────────────┤
│  DEV_Config — SPI 80 MHz · I²C 400 kHz · ADC · PWM backlight     │
└──────────────────────────────────────────────────────────────────┘
```

Every frame a page produces ends up in the single shared framebuffer `BlackImage` and is pushed to the panel with `LCD_1IN28_Display()` (full frame) or `LCD_1IN28_DisplayWindows()` (partial region — used by the sensor page to avoid flicker).

### Page lifecycle contract

Each page module exposes some subset of:

| Function | Called when | Purpose |
|---|---|---|
| `*_init()` | Once in `setup()` | Expensive one-time work (radio init, sensor detection) |
| `*_enter()` | Each time the page becomes active | Draw first frame, reset timers/state |
| `*_loop(now)` | Every scheduler pass while active | Animation/refresh, throttled internally |
| `*_exit()` | Only when leaving the page (registered in `leavePage()`) | Free resources, stop radios |

Only three pages currently implement `exit` (`starfield`, `city`, `wifi_connect`), and `leavePage()` in the sketch is hardcoded to call exactly those.

---

## 4. Page Scheduler (`S3_jam.ino`)

The sketch defines ten pages in an enum and a fixed rotation order (`pageOrder[]`):

```
PAGE_ACCEL → PAGE_WIFI → PAGE_BT → PAGE_TEMP → PAGE_STARFIELD →
PAGE_CUBE → PAGE_UPTIME → PAGE_FROG → PAGE_CITY → PAGE_WIFICONNECT → (wrap)
```

### Enable flags & durations (as committed)

| Page | Enabled? | Duration on screen | Behavior on expiry |
|---|---|---|---|
| `PAGE_ACCEL` (Sensor Data) | ❌ `false` | 3 s | next enabled page |
| `PAGE_WIFI` (Wi-Fi Scan) | ❌ `false` | 3 s | next enabled page |
| `PAGE_BT` (BLE Scan) | ❌ `false` | 3 s | next enabled page |
| `PAGE_TEMP` (Temperature) | ❌ `false` | 3 s | next enabled page |
| `PAGE_STARFIELD` | ✅ `true` | 5 s | next enabled page |
| `PAGE_CUBE` | ❌ `false` | 1 s | next enabled page |
| `PAGE_UPTIME` | ✅ `true` | 3 s | next enabled page |
| `PAGE_FROG` | ✅ `true` | 240 s (4 min) | next enabled page |
| `PAGE_CITY` | ✅ `true` | 30 s | next enabled page |
| `PAGE_WIFICONNECT` | ❌ `false` | 30 s | next enabled page |

So out of the box the device cycles: **Starfield → Uptime → Frog → City → Starfield → …**

### Scheduler mechanics

- `isPageEnabled()` maps each page ID to its compile-time `*_ENABLED` macro.
- `firstEnabledPage()` picks the boot page (Starfield with default flags); if *nothing* is enabled it falls back to `PAGE_ACCEL`.
- `nextEnabledPage(current)` walks `pageOrder[]` circularly from the current index and returns the next enabled page.
- `enterPage()` dispatches to the page's `*_enter()` and stamps `pageStartMs = millis()`.
- `leavePage()` dispatches `*_exit()` for starfield/city/wifi-connect.
- `loop()` is a `switch` on `currentPage`: it calls the page's `*_loop(now)`, checks `now - pageStartMs >= <DURATION>`, and advances with `goToNextPage()`. A `delay(1)` yields to the RTOS idle task each pass.
- **Wi-Fi pre-fetch quirk:** while `PAGE_ACCEL` is active *and* `PAGE_WIFI` is enabled, the loop calls `wifiScanPage_preStart(now, pageStartMs)`, which kicks off an **asynchronous** Wi-Fi scan 1 s into the accel page so results are ready (no dead air) when the Wi-Fi page is entered.

### `setup()` sequence

1. `Serial.begin(115200)`
2. `displayBuffer_init()` — PSRAM probe, framebuffer allocation, LCD + Paint init
3. `sensorPage_init()` — QMI8658 detection/init (tolerates a missing IMU)
4. `wifiScanPage_init()` — Wi-Fi to STA mode, persistent config off
5. `bleScanPage_init()` — `BLEDevice::init("ESP32-S3-LCD")` (runs even if the BLE page is disabled)
6. `enterPage(firstEnabledPage())`

---

## 5. Core Services

### 5.1 `display_buffer.cpp` — framebuffer & text helpers

- **`BlackImage`** — the single 240 × 240 × 16-bit framebuffer (`Imagesize = 115,200 bytes`).
- Allocation strategy in `displayBuffer_init()`:
  1. `heap_caps_malloc(Imagesize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)` — prefers DMA-capable internal RAM for fast SPI pushes
  2. falls back to `ps_malloc()` (PSRAM)
  3. falls back to `malloc()`
  4. if all fail → logs and `exit(0)`
- `psramInit()` is called first and the result logged.
- Then: `DEV_Module_Init()` → `LCD_1IN28_Init(HORIZONTAL)` → `LCD_1IN28_Clear(WHITE)` → `Paint_NewImage(..., 0, WHITE)` with `Paint_SetScale(65)` (65K-color RGB565 mode) and `ROTATE_0`.
- **`drawScaledTextPage(text, color, halfSize)`** — the signature "big text" renderer used by the count/temperature/uptime pages:
  - Clears the buffer to black.
  - Measures the *tight ink bounding box* of the whole string rendered in `Font24` (skips whitespace margins so short strings scale up more).
  - Chooses the largest integer `scale` that fits the crop into 240 × 240 (`halfSize` ⇒ scale halved).
  - Draws each set glyph pixel as a `scale × scale` block, centered; pushes the full frame.
- **`fitTextScale(text, maxWidth)`** — largest scale 6…1 such that `len × Font24.Width × scale ≤ maxWidth` (width-only fit; used by the Wi-Fi Connect page).
- **`drawCenteredScaledLine(text, centerY, scale, color)`** — draws one pre-scaled centered line into the current buffer without clearing/pushing.

### 5.2 `DEV_Config.cpp` — hardware abstraction

Thin wrappers over Arduino-ESP32 APIs:

- GPIO: `DEV_Digital_Write/Read`, `DEV_GPIO_Mode`, `DEV_KEY_Config` (INPUT_PULLUP)
- SPI: `DEV_SPI_WriteByte`, `DEV_SPI_Write_nByte` (→ `transferBytes`, DMA)
- I²C: byte/register/n-byte read+write helpers with error-tolerant reads (return zeros on NAK)
- ADC: `DEC_ADC_Read()` → `analogReadMilliVolts(BAT_ADC_PIN)`
- Backlight: `DEV_SET_Backlight(0..100)` → `analogWrite(LCD_BL_PIN, value × 2.55)`
- Delays: `DEV_Delay_ms/us`

---

## 6. Page Modules

### 6.1 Sensor page (`sensor_page.cpp`) — accelerometer/gyro + battery

- **Robust IMU bring-up:** `detectQMI8658()` probes both possible addresses (`0x6A`, `0x6B`), reads WHO_AM_I (register `0x00`) and revision (register `0x0A`), accepts any value that is not `0x00`/`0xFF`, retries 5× at 50 ms intervals, and finally falls back to a full I²C bus scan (addresses 1–127) for diagnostics. If nothing is found the page still runs in a **"No IMU"** mode with zeroed values.
- On success it calls `QMI8658_init()` (see §7.3).
- **Layout:** four horizontal color bands — red title band ("Sensor Data" / "No IMU"), green accelerometer band, gray gyro band, blue battery band. Labels are drawn once in `Font16` on `enter`.
- **Refresh:** every `ACCEL_REFRESH_MS = 250 ms` it re-reads the IMU (`QMI8658_read_xyz` → g and °/s) and the battery ADC, redraws only the value columns, and pushes **two partial windows** via `LCD_1IN28_DisplayWindows()` (values area `120,50 → 210,200` and battery area `130,200 → 220,220`) to minimize flicker.
- **Battery math:** `result(mV) × (3.3 / 4096 × 3)` — the ×3 accounts for the 200 kΩ/100 kΩ divider (see caveat §13.3 about the residual 3.3/4096 factor).

### 6.2 Wi-Fi scan page (`wifi_scan_page.cpp`)

- `init()`: `WiFi.persistent(false)`, STA mode, disconnect.
- `preStart(now, pageStartMs)`: called from the **accel page's** loop; 1 s in (`WIFI_SCAN_START_OFFSET_MS`) it starts an **async** scan (`WiFi.scanNetworks(true)`).
- `enter()`: `finishWifiScan()` waits up to 3 s for the async result, falls back to a synchronous scan, and retries once if 0 networks were found. Logs the full list (SSID / channel / RSSI, hidden SSIDs shown as `<hidden>`) to serial.
- **Display:** the network count as a huge green (`0x07E0`) number (half-size auto-scaled text on black).
- Cleans up with `WiFi.scanDelete()` after every pass.

### 6.3 BLE scan page (`ble_scan_page.cpp`)

- `init()`: `BLEDevice::init("ESP32-S3-LCD")` — BLE stack comes up at boot.
- `enter()`: performs a **3 s active scan** (`BLEScan::start(3, false)`), deduplicates advertisements by MAC address (capacity `MAX_BLE_DEVICES = 200`), logs each unique device (name or `<unknown>`, address, RSSI), and displays the unique count as a huge blue (`0x001F`) number.

### 6.4 Temperature page (`temperature_page.cpp`)

- Reads the ESP32-S3 **internal die temperature** with `temperatureRead()`, sanity-clamps to −40…125 °C, logs it, and renders `"xx.xC"` full-size in red (`0xF800`) via `drawScaledTextPage()`. Drawn once on entry (3 s page).

### 6.5 Starfield (`starfield.cpp`) — 5,000-star warp effect

The most performance-tuned module; it bypasses GUI_Paint and writes **pre-byte-swapped RGB565** straight into `BlackImage`.

- **Stars:** 5,000 entries of an 8-byte fixed-point struct `{x, y (±60), px, py, z (25…255), psize, visible}` ≈ 40 KB static RAM.
- **Projection:** integer perspective via a precomputed inverse-Z lookup table: `invZ[z] = (FOCAL=120) << 8 / z`; screen offset `= (coord × invZ[z]) >> 8`.
- **Motion:** each star's `z` decreases by `STAR_SPEED = 2` per frame; stars recycle at `z ≤ 25 + speed` or when they leave the screen.
- **Depth cueing:** `z < 50` → 2×2 white; `z < 100` → `0xCE59`; `z < 180` → `0x8410`; farther → dim `0x4208` with a deterministic 1-in-5 twinkle skip. Each star also draws a "tail" pixel at its position 4 speed-steps back.
- **Rotation:** the whole field slowly rotates about the center. A new random target angular speed (±0.00025…0.0008 rad/ms) is chosen every 5 s and approached with exponential smoothing (τ = 1200 ms) — the field visibly "banks" into turns. Can be compiled out with `STARFIELD_ROTATION_ENABLED 0`.
- **Telemetry overlay:** centered `FPS:xxx CPU:xx%` line at y=105 drawn with a direct `Font16` blitter. FPS is measured over 1 s windows; CPU% = last render time ÷ actual frame period.
- **Pacing:** `STAR_FRAME_MS = 33` (~30 fps); full-frame `memset` clear + full-frame push each tick. `enter()` reseeds RNG and rebuilds the LUT; `exit()` just marks the effect inactive (the loop re-enters if called while inactive).

### 6.6 Cube (`cube.cpp`) — shaded rotating 3-D cube

- Classic software renderer through the Paint API:
  - 8 unit vertices, 6 faces with fixed colors (red, green, blue, yellow, cyan, magenta).
  - Per frame: XYZ Euler rotation (angles advanced by spin speeds), perspective divide `3/(z+3)`, screen scale 35 px.
  - **Painter's algorithm:** faces sorted by average vertex Z (descending) and drawn far-to-near.
  - **Shading:** brightness `= 0.35 + 0.65 × |n·z|` from the face normal, applied per-channel to RGB565 (`shadeRGB565`).
  - Faces filled with a scanline quad-fill (`fillQuad`: per-row edge intersection + sort + span fill via `Paint_DrawLine`).
- Spin speeds ease toward random targets (lerp 0.025/frame) re-rolled every 2–4 s, so the cube drifts and tumbles organically.
- Paced at `CUBE_FRAME_MS = 33` (~30 fps); the page itself only stays up for 1 s by default, so you typically see one face-tumble burst per cycle.

### 6.7 Uptime page (`uptime.cpp`)

- Formats `millis()/1000` as `Xd HH:MM:SS` (days shown only when > 0) or `HH:MM:SS`, rendered white full-size via `drawScaledTextPage()`.
- Redraws once per second (`UPTIME_REFRESH_MS = 1000`).

### 6.8 Frog page (`frog_page.cpp`) — pixel-art life sim (~600 lines)

A hand-animated frog sitting on a lily pad in a pond, drawn on a logical **60 × 60 grid** scaled ×4 (`FS = 4`) to fill the 240 × 240 panel. Everything is direct pixel/rect writes with byte-swapped RGB565 (`putPix`, `fillRectPx`, grid-space `gpx`, `gEllipse`).

- **State machine — 13 states:** `IDLE, BLINK, WINK, YAWN, SMILE, SLEEP, LOOK, WHISTLE, SING, LOVE, CATCH, EAT, BURP`. Transitions are weighted-random (idle ≈ 24 %, blink 12 %, look 10 %, smile 10 %, then wink/yawn/sleep/whistle/sing/love at 7–8 % each) with per-state durations (blink 320 ms, sleep 6.5–9.5 s, sing 4 s, …). `CATCH → EAT → BURP` is a scripted chain triggered by the fly lifecycle.
- **Entities:**
  - **Fly** — spawns every 7–15 s, buzzes around; when it wanders close, the frog snaps its tongue (`CATCH`), eats (`EAT`, 1.3 s), then burps.
  - **Fish** — leaps out of the water every 3–9 s along an arc, leaving expanding **ripples**.
  - **Music notes** — emitted while whistling/singing; **Zzz** bubbles while sleeping; **water drops** during the tongue snap.
  - **Clouds** (3, drifting at different speeds), **sky stars** (45, at night), **moon** with craters (night).
- **Day/night cycle:** first night falls 30–70 s after entry and lasts 15–26 s; during night the palette swaps (dark sky/grass/water constants), stars and moon appear, and the frog is forced to `SLEEP`. Next night is scheduled 45–90 s after the previous one ends.
- Paced at ~30 fps (33 ms gate in `frogPage_loop`). Default page duration is **4 minutes**, making it the "campfire" screen of the carousel.

### 6.9 City page (`city_page.cpp`) — isometric pixel city (~700 lines)

A "Habbo / retro-sim" style isometric city rendered from a **12 × 12 character tile map** (`MAP[]`) with tile half-width 9 / half-height 5, origin at (120, 72).

- **Tile legend:** `G` grass · `R` road (with center-lane dots) · `W` water · `h` house (H=14) · `t` tower (H=30) · `c` casino (H=16, lit marquee) · `g` glass office (H=22) · `b` bar (H=9) · `p` palm tree · `y` yacht · anything else = pavement.
- **Two-pass rendering:** the static city (sky gradient, baked clouds, ground diamonds, buildings drawn back-to-front in isometric order, foreground promenade) is **baked once into an offscreen layer** `cityLayer` (115,200 B, `ps_malloc` in PSRAM, freed in `cityPage_exit()`).
- **Per frame (~30 fps):** `memcpy` the baked layer into `BlackImage`, then draw dynamic content:
  - **4 cars** and **8 pedestrians** (two-frame walk cycle) ping-pong along road rows/columns 3 and 8 with per-entity speeds and phases.
  - **4 traffic lights** at the intersections alternating red/green every 1.5 s (staggered per corner).
  - **Flickering tower windows** (700 ms cadence), **waving rooftop flag** on the tall tower, one **drifting cloud**, and **marina shimmer** (sine-wobbled water glints).
  - **Ticker** at the bottom (y=222, `Font8`): `CITY MM:SS pop8` — elapsed time and population.
- Re-seeds car/pedestrian phases on each entry so every visit looks slightly different.

### 6.10 Wi-Fi connect page (`wifi_connect.cpp`)

- ⚠️ Contains **hardcoded credentials**: `WIFI_CONNECT_SSID "bitseq"` / `WIFI_CONNECT_PASS "00000000"` (see §13.1).
- `enter()`: power-cycles the radio (OFF → STA), disables modem sleep, `WiFi.begin(...)`, drops TX power to `WIFI_POWER_8_5dBm`, starts a 50 s timeout (`WIFI_CONNECT_TIMEOUT_MS`).
- `loop()`: polls `WiFi.status()`; draws animated **"Connecting…"** (1–3 cycling dots, redrawn every 500 ms), then **"LIVE"** (connected, logs the IP) or **"Failed"** (auth failure / no SSID / timeout). The SSID is shown underneath in `Font16`. Text is centered with `fitTextScale()`/`drawCenteredScaledLine()`.
- `exit()`: disconnects and restores plain STA mode so later scan pages behave.
- The connection is not used for anything else — the page is a connectivity smoke-test.

---

## 7. Waveshare Driver Stack

### 7.1 `LCD_1in28.cpp` — GC9A01A panel driver

- `LCD_1IN28_Init(Scan_dir)`: hardware reset (RST pulse, CS held low afterwards — CS is effectively permanently selected), sets scan direction, then sends the full GC9A01 vendor init register sequence (`0xEF/0xEB/0xFE/0x84…0x8F`, power/gamma blocks `0xF0…0xF3`, `0x62/0x63/0x64/0x66/0x67/0x74`, etc.), ending with Sleep-Out (`0x11`, 120 ms) and Display-On (`0x29`).
  - COLMOD `0x3A = 0x05` → **16-bit/pixel RGB565**; `0x35` → tearing-effect output; `0x21` → display inversion on (normal for this IPS panel).
- `LCD_1IN28_SetAttributes(dir)`: MADCTL (`0x36`) = `0xC8` for `HORIZONTAL`, `0x68` for `VERTICAL`. The project always uses `HORIZONTAL` (240 × 240 either way).
- `LCD_1IN28_SetWindows(x0,y0,x1,y1)`: CASET `0x2A` / RASET `0x2B` / RAMWR `0x2C`.
- `LCD_1IN28_Clear(color)`: fills `BlackImage` with the byte-swapped color and pushes all 240 rows.
- `LCD_1IN28_Display(image)`: full-frame push, **row by row** via `DEV_SPI_Write_nByte()` (480 B per transfer, DMA-backed).
- `LCD_1IN28_DisplayWindows(x0,y0,x1,y1,image)`: pushes only rows `y0…y1-1`, columns `x0…x1-1` from the same framebuffer — the sensor page uses this for flicker-free value updates.
- `LCD_1IN28_DisplayPoint(x,y,color)`: single-pixel write (unused by pages).

### 7.2 Byte order

The panel expects **big-endian RGB565**. Two write paths exist and both produce that order:

1. **GUI_Paint path** — in 65K mode (`Paint_SetScale(65)`), `Paint_SetPixel()` writes the color **high byte first** into the byte-addressed buffer, so the buffer is already wire-format and `LCD_1IN28_Display()` can stream it raw.
2. **Direct path** — the frog, city, and starfield modules write pixels manually with their own `swap16()` helpers for speed (avoiding Paint's bounds/rotate logic), e.g. `BlackImage[y*240+x] = swap16(rgb565)`.

Anything drawn with the direct path therefore uses the same buffer semantics; the two styles can be mixed within one frame (the city page does exactly that — direct writes for the scene, `Paint_DrawString_EN` for the ticker).

### 7.3 `QMI8658.cpp` — IMU driver

- Tries slave addresses `0x6A` then `0x6B`; verifies WHO_AM_I == `0x05` (5 retries per address).
- `QMI8658_init()` configuration:
  - `CTRL1 = 0x60` (address auto-increment, big-endian data)
  - Accel **+8 g @ 1000 Hz**, Gyro **±512 °/s @ 1000 Hz** (`QMI8658_CONFIG_ACCGYR_ENABLE`)
  - Magnetometer slot configured for AKM09918 @ 125 Hz and AttitudeEngine @ 128 Hz (not enabled — no magnetometer on this board)
  - Dumps CTRL1–CTRL7 to serial for verification
- API: `QMI8658_read_xyz(acc[3], gyro[3], &tim_count)` (scaled g / °/s), `QMI8658_read_acc_xyz`, `QMI8658_read_gyro_xyz`, `QMI8658_read_xyz_raw`, `QMI8658_readTemp`, `QMI8658_enableSensors`, `QMI8658_enableWakeOnMotion` (unused by the pages).

### 7.4 `GUI_Paint.cpp` — 2-D raster library

Standard Waveshare "Paint" library operating on a caller-owned buffer:

- Buffer management: `Paint_NewImage`, `Paint_SelectImage`, `Paint_SetRotate` (0/90/180/270), `Paint_SetMirroring`, `Paint_SetScale` (2/4/16/65 = 1/2/4/16-bit color depth), `Paint_SetPixel`, `Paint_Clear`, `Paint_ClearWindows`
- Primitives: `Paint_DrawPoint` (1×1…8×8 dots, filled/hollow), `Paint_DrawLine` (solid/dashed), `Paint_DrawRectangle`, `Paint_DrawCircle` (filled/hollow)
- Text: `Paint_DrawChar`, `Paint_DrawString_EN` (ASCII via `sFONT`), `Paint_DrawString_CN` (GB2312 via `cFONT` — unused), `Paint_DrawNum` (floats with N decimals — used for sensor values), `Paint_DrawTime`
- Images: `Paint_DrawBitMap`, `Paint_DrawBitMap_Block`, `Paint_DrawImage`, `Paint_BmpWindows`
- Named RGB565 colors: `WHITE`, `BLACK`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `CYAN`, `MAGENTA`, `GRAY`, …

### 7.5 Fonts

`font8/12/16/20/24.cpp` provide STMicro-style 1-bpp ASCII bitmap fonts (`sFONT {table, Width, Height}`, glyphs for `' '`…`'~'`, MSB-first rows). Usage in this project:

| Font | Used by |
|---|---|
| `Font8` | City ticker |
| `Font12` | (available, unused) |
| `Font16` | Sensor page labels/values, starfield overlay, Wi-Fi Connect SSID |
| `Font20` | (available, unused) |
| `Font24` | All auto-scaled "big text" pages (base size for `drawScaledTextPage`/`fitTextScale`) |

### 7.6 `ImageData.cpp`

~2.2 MB of const arrays inherited from Waveshare's demo bundle (status icons `Signal816`, `Msg816`, `Bat816`, `Bluetooth88`, `GPRS88`, `Alarm88`, and full-screen images `gImage_*` for several Waveshare displays). **None of the jam pages reference them.** They compile into the sketch; with the ESP32 Arduino core's `--gc-sections` linking they are usually stripped from the final binary, but the file dominates compile time and repo size and could be removed if desired.

---

## 8. Graphics Pipeline & Memory Budget

### Frame flow

```
page_loop(now)
   │  (throttled to ~30 fps for animations, 0.25–1 Hz for readouts)
   ▼
draw into BlackImage  ── Paint_* API (auto byte-swap)  ──or──  direct pixel writes (manual swap16)
   ▼
LCD_1IN28_Display(BlackImage)          → full 240×240 push (~115 KB over SPI @80 MHz ≈ 12 ms)
LCD_1IN28_DisplayWindows(x0,y0,x1,y1)  → partial push (sensor page only)
```

### Approximate RAM budget

| Item | Size | Location |
|---|---|---|
| `BlackImage` framebuffer | 115,200 B | Internal DMA RAM (preferred), else PSRAM |
| `cityLayer` offscreen city | 115,200 B | PSRAM (allocated on entry, freed on exit) |
| `stars[5000]` + inv-Z LUT | ~40 KB + 512 B | Static .bss |
| BLE scan result cache / Wi-Fi stack | variable (tens of KB) | heap |
| Page state (frog entities, cars, peds, …) | < 1 KB total | Static |

The dual 115 KB buffers fit comfortably thanks to the S3R2's 2 MB PSRAM; the DMA-capable primary buffer keeps full-frame pushes near SPI line rate.

---

## 9. Building & Flashing

### 9.1 Prerequisites

- **Arduino IDE** (1.8.x or 2.x)
- **esp32 by Espressif Systems** board package — the Waveshare wiki pins its demos at **2.0.12**; this sketch only uses stable APIs (WiFi, BLE, Wire, SPI, PSRAM) and also builds against core 3.x.
- USB Type-A → Type-C cable.

### 9.2 Get the sketch into a properly named folder

Arduino requires the sketch folder to match the `.ino` name. The repo clones as `s3jam/` but the sketch is `S3_jam.ino`, so:

```bash
git clone https://github.com/bitseq27/s3jam
mv s3jam S3_jam        # folder must be named S3_jam
```

(Alternatively let the IDE auto-create the matching folder when it prompts on open.)

### 9.3 Board settings (Tools menu → "ESP32S3 Dev Module")

| Option | Value | Why |
|---|---|---|
| Board | **ESP32S3 Dev Module** | ESP32-S3R2 |
| Flash Size | **16 MB** | W25Q128 onboard |
| Partition Scheme | a 16 MB scheme with **≥ 3 MB app** (e.g. *"16M Flash (3MB APP/9.9MB FAT)"*) | BLE + Wi-Fi + image data won't fit the default 1.3 MB app slot |
| PSRAM | **OPI PSRAM** | S3R2 stacked PSRAM is octal; needed for the city layer / framebuffer fallback |
| CPU Frequency | 240 MHz | animation pages assume it |
| USB CDC On Boot | Disabled (default) is fine | `Serial` goes out through the CH343P UART bridge (GPIO43/44); enable only if you wire native USB |
| Upload Mode | UART0 / auto | board has an auto-download circuit |

### 9.4 Flash

1. Plug in the board; pick the CH343 serial port (Windows: Device Manager → Ports; Linux: `/dev/ttyUSB*` or `/dev/ttyACM*`; macOS: install the CH34x driver if the port is missing).
2. **Verify**, then **Upload**.
3. Open Serial Monitor at **115200 baud** to watch boot logs (PSRAM init, GPIO init, IMU detection).

**If flashing fails or the port won't enumerate** (from the wiki FAQ): hold **BOOT**, tap **RESET**, release RESET, then release BOOT to force download mode; or hold RESET > 1 s and retry. First compiles of large sketches are slow — that's normal.

### 9.5 Expected behavior after flashing

With the committed flags the board boots into the **Starfield** page and cycles Starfield (5 s) → Uptime (3 s) → Frog (4 min) → City (30 s) → repeat. All pages also log to serial at 115200.

---

## 10. Configuration & Customization

All primary knobs are `#define`s at the top of `S3_jam.ino`:

```cpp
#define PAGE_ACCEL_ENABLED       false   // flip to true/false to include a page
...
#define STARFIELD_PAGE_DURATION_MS   5000  // how long each page stays up
```

Other useful constants:

| What | Where | Default |
|---|---|---|
| Wi-Fi credentials | `wifi_connect.cpp` → `WIFI_CONNECT_SSID` / `WIFI_CONNECT_PASS` | `"bitseq"` / `"00000000"` |
| Wi-Fi connect timeout | `wifi_connect.cpp` → `WIFI_CONNECT_TIMEOUT_MS` | 50 s |
| BLE scan length | `ble_scan_page.cpp` → `BLE_SCAN_SECONDS` | 3 s |
| Sensor refresh rate | `sensor_page.cpp` → `ACCEL_REFRESH_MS` | 250 ms |
| Starfield density/speed/rotation | `starfield.cpp` → `NUM_STARS`, `STAR_SPEED`, `STARFIELD_ROTATION_ENABLED`, `STAR_ROT_*` | 5000 / 2 / on |
| Animation frame pacing | `STAR_FRAME_MS`, `CUBE_FRAME_MS`, frog/city 33 ms gates | ~30 fps |
| Backlight brightness | `DEV_Config.cpp` → `DEV_GPIO_Init()` calls `DEV_SET_Backlight(50)`; `display_buffer.cpp` has a commented-out override | 50 % |
| IMU ranges/ODR | `QMI8658.cpp` → `QMI8658_init()` | ±8 g, ±512 dps, 1 kHz |
| City map | `city_page.cpp` → `MAP[]` character grid | 12×12 |
| Frog behavior weights | `frog_page.cpp` → `advanceState()` thresholds | weighted random |

---

## 11. How to Add a New Page

1. **Create `my_page.h` / `my_page.cpp`** following the lifecycle contract:

   ```cpp
   // my_page.h
   #pragma once
   void myPage_enter(void);
   void myPage_loop(unsigned long now);   // optional
   void myPage_exit(void);                // optional
   ```

   ```cpp
   // my_page.cpp
   #include "my_page.h"
   #include "display_buffer.h"            // BlackImage + Paint/LCD/fonts via LCD_Test.h chain

   void myPage_enter(void) {
     drawScaledTextPage("HELLO", WHITE, true);   // quick big-text screen
   }

   void myPage_loop(unsigned long now) {
     // throttled animation; write to BlackImage, then LCD_1IN28_Display(BlackImage);
   }
   ```

2. **Register it in `S3_jam.ino`:**
   - add `PAGE_MY` to the enum,
   - add it to `pageOrder[]`,
   - add `PAGE_MY_ENABLED` / `MY_PAGE_DURATION_MS` macros,
   - add cases to `isPageEnabled()`, `enterPage()`, the `loop()` switch, and (if it has cleanup) `leavePage()`.
3. **Include the header** at the top of the sketch and call any `myPage_init()` from `setup()` if needed.
4. For fast full-screen animation, prefer direct buffer writes with byte-swapped RGB565 (see starfield/city patterns) over per-pixel `Paint_SetPixel`.

---

## 12. Serial Logging

Serial monitor @ **115200 baud**. Notable output:

| Source | Messages |
|---|---|
| Boot | `PSRAM is correctly initialized` / `PSRAM not available`, `GPIO Init successful!` |
| Sensor init | I²C probe results (`WHO_AM_I reg 0x00/0x0A = …`), full bus scan fallback, `QMI8658_init done` / `QMI8658 not detected…` |
| Wi-Fi scan | numbered SSID list with channel + RSSI, `Wi-Fi networks found: N` |
| BLE scan | numbered device list (name / address / RSSI), `Bluetooth device count: N` |
| Temperature | `ESP32 internal temperature: xx.xx C` |
| Wi-Fi connect | `WiFi: connecting to "…"`, `WiFi: LIVE (connected)` + IP, or `WiFi: Failed (timeout)` |

---

## 13. Known Quirks & Caveats

1. **Hardcoded Wi-Fi credentials** — `wifi_connect.cpp` embeds an SSID and passphrase in source. Change them before sharing/flashing publicly; ideally move to a config header excluded from VCS.
2. **Sketch folder name mismatch** — repo folder `s3jam` vs sketch `S3_jam.ino`; the IDE will prompt to relocate (see §9.2).
3. **Battery voltage scaling** — `sensor_page.cpp` multiplies `analogReadMilliVolts()` by `3.3/4096 × 3`. The `×3` correctly undoes the 200 kΩ/100 kΩ divider, but the `3.3/4096` factor belongs to *raw* ADC counts (the Waveshare demo used `analogRead`); applied to millivolts it makes the displayed voltage read ≈ 0.81× the true value. Correct formula: `mV × 3 / 1000.0`.
4. **`LCD_Test.cpp` is absent** — `LCD_Test.h` declares `int LCD_1in28_test(void);` but no implementation ships. Nothing calls it; the header is used purely as an aggregate include (`DEV_Config` + `GUI_Paint` + `ImageData` + `LCD_1in28` + `QMI8658`). Don't call `LCD_1in28_test()` or the link will fail.
5. **BLE stack always initialized** — `bleScanPage_init()` runs in `setup()` even when `PAGE_BT_ENABLED` is `false`, costing BLE controller RAM. Guard it with the enable macro if you never use the BLE page.
6. **`ImageData.cpp` weight** — 2.2 MB of unused demo imagery. Usually stripped by `--gc-sections`, but it slows builds; safe to delete together with its externs in `ImageData.h` if you don't plan to use the Waveshare demo assets.
7. **Driver windowing quirk** — in `LCD_1IN28_SetWindows()` the RASET high byte sends `(Xend-1)>>8` instead of `(Yend-1)>>8` (inherited from the vendor demo). Harmless at 240 px (both high bytes are 0), but fix it before porting to larger panels.
8. **All pages disabled ⇒** `firstEnabledPage()` returns `PAGE_ACCEL`, whose `enter` runs regardless of its own flag — the device would sit on the sensor page forever.
9. **`!git push.bat`** — destructive one-click history rewrite + force push (`git push -f`). Kept for the author's workflow; don't run it casually.
10. **Frog page dominance** — at 240 s the frog page accounts for ~72 % of carousel time by default; adjust `FROG_PAGE_DURATION_MS` if you want the other screens seen more often.
11. **⚠️ WiFi Hardware Bug** — The ESP32-S3 WiFi hardware on this board has a known issue where using WiFi at full transmit power causes connection drops and instability. This repository mitigates the issue by setting reduced transmit power (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`) in the `wifi_connect.cpp` page. The 8.5 dBm setting is significantly below maximum power (~20 dBm) and provides stable connections. If you modify WiFi-related code or add new WiFi functionality, maintain this reduced power setting to avoid connection issues.

---

## 14. References

- Waveshare wiki (PDF included with this project): *ESP32-S3-LCD-1.28* — usage instructions, Arduino/MicroPython setup, demos, FAQ. Online: <https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28>
- Product page: <https://www.waveshare.com/esp32-s3-lcd-1.28.htm>
- Schematic: <https://files.waveshare.com/wiki/ESP32-S3-LCD-1.28/Esp32-s3-lcd-.128-sch.pdf>
- Waveshare demo bundle (source of the vendored drivers): <https://files.waveshare.com/wiki/ESP32-S3-LCD-1.28/ESP32-S3-LCD-1.28-Demo.zip>
- Datasheets: [GC9A01A](https://files.waveshare.com/wiki/common/GC9A01A.pdf) · [QMI8658A](https://files.waveshare.com/wiki/common/QMI8658A_Datasheet_Rev_A.pdf) · [ESP32-S3](https://files.waveshare.com/wiki/common/Esp32-s3_datasheet_en.pdf)
- Arduino-ESP32 docs: <https://docs.espressif.com/projects/arduino-esp32/en/latest/index.html>

*Licenses: driver/GUI/font files carry Waveshare MIT and STMicroelectronics BSD-style notices (see file headers). This documentation was generated from source inspection of commit `43dcfde` and the supplied Waveshare wiki PDF.*
