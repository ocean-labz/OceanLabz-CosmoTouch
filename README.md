# Solar System Explorer — OceanLabz ES3C28P

An interactive solar-system explorer for the **ES3C28P ESP32-S3 board** with its
**2.8" 240×320 IPS capacitive touchscreen** (ILI9341V + FT6336G).

The app opens on a pure-black star field with a glowing Sun, elliptical orbits
and all eight planets. A single tap on any planet flies the view toward it and
opens a large, rotating globe with atmosphere, surface detail and planet facts.
Back returns to the exact solar-system position and zoom. A separate demo suite
still exercises every capacitive-touch capability of the board.

---

## Hardware

| Function | Pins |
|---|---|
| Display (ILI9341V, SPI) | CS 10, DC 46, SCK 12, MOSI 11, MISO 13, BL 45 |
| Touch (FT6336G, I2C) | SDA 16, SCL 15, RST 18, INT 17, address `0x38` |
| RGB status LED | 42 (WS2812) |
| Battery ADC | 9 |
| BOOT button | 0 |
| Resolution | 240 × 320 portrait, IPS |

Pins live in [`GalaxyExplorer/pins.h`](GalaxyExplorer/pins.h). The display reset
is tied to CHIP_PU, so LovyanGFX uses a software reset (`pin_rst = -1`).

### Panel settings

The ES3C28P IPS panel requires **colour inversion on** and **BGR colour order**.
With inversion off, RGB565 `0x0000` (pure black) is displayed as off-white /
cream. Both settings are now enabled in `config.h`:

```cpp
#define SPACE_BG          0x0000
#define DISPLAY_INVERT    1
#define DISPLAY_RGB_ORDER 1
```

All full-screen clears, frame-buffer initialisation, splash/demo backgrounds and
planet scenes use `0x0000`. Nebula, stars, glows and planet atmospheres are drawn
as local colour over that black; no cream, grey, navy or gradient background
fill remains.

---

## Solar system

- **Pure black #000000** background, with sparse distant stars and a very faint
  localised nebula texture. No full-screen gradient wash.
- **Sun** at the centre: layered orange/gold corona, bright photosphere, subtle
  pulsation and short corona streamers.
- **Eight planets**, each on a slow elliptical orbit with a visible path, its own
  relative size, colour and procedural 256×128 surface map:
  Mercury (grey, cratered), Venus (yellow acid clouds), Earth (blue oceans,
  green/brown continents, white clouds, ice caps), Mars (rust deserts, dark
  regions, craters), Jupiter (banded gas giant with Great Red Spot), Saturn
  (pale gold bands plus a four-layer ring system), Uranus (pale cyan ice giant),
  Neptune (deep blue ice giant with a dark storm).
- Directional lighting, limb shading and an atmospheric rim make each world read
  as a sphere. Planet sprites slowly rotate, rebuilt one at a time to spread the
  small CPU cost across frames.
- Solar-corona dust, twinkling parallax stars and occasional shooting stars.
- Translucent bottom HUD: **CENTER / CALIBRATE / RESET** plus a fading event
  label. The title shows **SOLAR SYSTEM** and the hint **TAP A PLANET TO LAND**.

| Gesture | Result |
|---|---|
| Tap a planet | Immediately starts the approach and opens that planet |
| Tap empty space | Creates a small star and ripple |
| Drag | Pans the solar system with inertia; background parallax follows |
| Two-finger pinch | Zooms the system (0.52×–2.25×) around the pinch centre |
| Double tap | Supernova effect |
| Long press | Black-hole effect, released on lift |
| **CENTER** | Restores the view centre |
| **CALIBRATE** | Opens the five-target touch calibration screen |
| **RESET** | Rebuilds stars, dust and planet positions |
| Hold BOOT ~1.2 s | Opens touch calibration (while the app is running) |
| BOOT tap | Toggles the FPS/touch debug overlay |

Planet entry no longer requires a pinch and does not depend on two-point touch.

### Touch mapping

The FT6336G on this board reports the Y origin at the physical bottom while the
LCD is addressed from the top. That is why a bottom tap previously selected the
top of the screen. `config.h` now applies the correct transform:

```cpp
#define TOUCH_SWAP_XY   0
#define TOUCH_INVERT_X  0
#define TOUCH_INVERT_Y  1
#define TOUCH_RAW_MAX_X 240
#define TOUCH_RAW_MAX_Y 320
```

Every screen consumes the same mapped events, so the transform is consistent
across the solar system, planet globes, menus, painting and demos.

**Five-target calibration screen.** Open it from the HUD (**CALIBRATE**), the
demo menu (item 1), or by holding **BOOT** for about 1.2 seconds while the app
is running. It draws crosshairs
at the exact requested coordinates:

- `(0, 0)` top-left
- `(239, 0)` top-right
- `(0, 319)` bottom-left
- `(239, 319)` bottom-right
- `(120, 160)` centre

The screen runs a guided five-point calibration. It highlights one target at a
time; tap the highlighted physical crosshair. Raw X/Y are captured from the
controller, then the firmware derives both orientation and direction from the
actual samples: it checks which raw axis changes across the top edge, which
changes down the left edge, and whether either value decreases toward 239/319.
Nothing assumes that only Y is mirrored. The result is applied immediately and
stored in NVS, so it survives reset. **REDO** repeats the sequence; **EXIT**
returns to the solar system. After calibration, tapping the five crosshairs
verifies the mapping and displays the applied axis transform.

Raw coordinates are retained after finger lift, so calibration records the
corner values from the `TAP` event rather than reading `0,0` after release.

---

## Planet exploration

A single tap starts a short camera-style approach: the selected planet grows
from its orbit to a large globe over a darkening, warp-streaked star field,
then fades into the orbital view.

The exploration screen shows:

- A large detailed globe filling most of the upper display, with atmospheric
  glow, directional lighting, a terminator shadow and a thin atmospheric rim.
- Saturn's rings drawn both behind and in front of the globe.
- Slowly rotating surface detail sampled from the same procedural map used in
  the solar system.
- A black information panel with planet name, type, atmospheric description,
  surface/environment description, one interesting fact, and **BACK TO SOLAR
  SYSTEM**.

| Planet | Type | Atmosphere / surface | Fact |
|---|---|---|---|
| Mercury | Terrestrial | Almost no atmosphere; grey cratered rock | Day 430 °C, night −180 °C |
| Venus | Terrestrial | CO₂ and sulphuric-acid clouds; volcanic plains | Hottest planet, about 465 °C |
| Earth | Terrestrial | Nitrogen–oxygen atmosphere; oceans and continents | The only known world with life |
| Mars | Terrestrial | Thin CO₂; rust deserts, craters, ancient valleys | Olympus Mons, the tallest volcano |
| Jupiter | Gas giant | Hydrogen/helium bands; no solid surface | Great Red Spot, an ancient storm |
| Saturn | Gas giant | Hydrogen/helium; pale gold bands and icy rings | Rings are billions of icy particles |
| Uranus | Ice giant | Hydrogen/helium/methane; cold, smooth cyan world | Rotates on its side after a collision |
| Neptune | Ice giant | Hydrogen/helium/methane; deep blue, dark storms | Winds exceed 2,000 km/h |

| Gesture | Result |
|---|---|
| Drag | Rotates the globe horizontally and tilts the view vertically |
| Pinch out | Enlarges the globe (1.0×–1.8×) |
| Pinch in | Zooms out; a firm pinch-in at minimum zoom also exits |
| Tap the globe | Places a glowing surface marker |
| Long press | Triggers a planet-specific phenomenon (aurora, dust storm, ring shimmer, red-spot storm, etc.) |
| **BACK TO SOLAR SYSTEM** | Animated return through black |

Entering a planet never modifies the solar-system camera. Back restores the
previous position, zoom, stars and orbits; the application is not restarted.

---

## Touch demo suite

1. **Calibrate** — five-target mapping test with raw and mapped coordinates.
2. **Multi Touch** — coloured contact circles, active/max contact count and live
   distance/pinch zoom.
3. **Gesture Test** — tap, double tap, long press, drag and four swipe directions.
4. **Touch Paint** — five colours, three brush sizes, glow strokes and clear.
5. **Solar System** — returns to the live system.

The HUD CALIBRATE button opens the demo menu; each demo's BACK returns there.

---

## Diagnostics

- BOOT tap toggles an FPS/touch overlay on the solar-system screen.
- Holding BOOT for ~1.2 s opens touch calibration (also enables the overlay).
  Do **not** hold BOOT through reset: on ESP32-S3 that enters the ROM download
  mode instead of starting the sketch.
- Serial at 115200 baud prints display size, frame-buffer placement, touch chip
  ID, max contacts seen, I²C error count and free heap.
- RGB LED shows a subtle status colour per screen.
- `HardwareCheck/HardwareCheck.ino` remains an independent display/touch test.

---

## Repository layout

```
GalaxyExplorer/
  GalaxyExplorer.ino    application init, routing, main loop
  pins.h                ES3C28P pin map
  config.h              display/touch calibration and tuning
  display_manager.*     LovyanGFX ILI9341 setup, frame buffer, present()
  touch_manager.*       FT6336 I2C driver, async polling, gesture engine
  space_canvas.*        RGB565 canvas, glow masks, primitives, 5×7 font
  galaxy_engine.*       Sun, orbits, stars, eight textured planets
  planet_scene.*        tap-to-enter approach and globe explorer
  effects.*             ripples, stars, supernova, black hole, shooters
  ui_manager.*          HUD, menus, five-target calibration, demos
HardwareCheck/
  HardwareCheck.ino     independent display/touch diagnostic
```

---

## Build

### Arduino IDE

1. Install **ESP32 Arduino core 3.x** (tested with 3.3.12).
2. Install **LovyanGFX** (tested 1.3.0) and **Adafruit NeoPixel** (tested 1.15.5).
   NeoPixel is only needed for the optional RGB LED (`ENABLE_RGB_LED 1`).
3. Open `GalaxyExplorer/GalaxyExplorer.ino`.
4. Select **ESP32S3 Dev Module** with:
   - Flash Size: **16 MB**
   - PSRAM: **OPI PSRAM**
   - Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
   - CPU Frequency: **240 MHz**
   - Flash Mode: **QIO 80 MHz**
5. Upload.

### arduino-cli

```bash
arduino-cli lib install LovyanGFX "Adafruit NeoPixel"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CPUFreq=240"
arduino-cli compile --warnings all --fqbn "$FQBN" GalaxyExplorer
arduino-cli upload  --fqbn "$FQBN" -p COM5 GalaxyExplorer
```

> **PSRAM is required.** The nebula layer, eight 256×128 surface maps (512 KB),
> glow masks and planet sprites live in PSRAM; the 153 KB frame buffer prefers
> internal DMA-capable RAM and falls back to PSRAM. All allocations happen once
> at boot. Opening a planet allocates nothing.

---

## Verification status

Compile-verified with `arduino-cli` and ESP32 core **3.3.12** for the exact S3
options above, with `--warnings all` and no warnings. The app uses **502,363
bytes flash (15%)** and **50,804 bytes static RAM (15%)**. `HardwareCheck` builds
at 400,587 bytes flash and 25,388 bytes static RAM.

**Physical verification is still required.** No ES3C28P was attached in the
development environment, so the following have not been measured or observed:
panel inversion/colour order, actual touch direction at all five targets,
two-point reporting, real FPS, and motion smoothness. The touch defaults are
based on the reported bottom/top inversion; the guided calibration derives the
final axis order/direction from raw corner samples on the actual panel. Panel
inversion/BGR follows the board's documented requirements and still needs a
visual black-level check.

On-device checklist:

- [ ] Top-left touch lands on `(0,0)`
- [ ] Top-right touch lands on `(239,0)`
- [ ] Bottom-left touch lands on `(0,319)`
- [ ] Bottom-right touch lands on `(239,319)`
- [ ] Centre touch lands on `(120,160)`
- [ ] Background is visibly pure black, not cream/off-white
- [ ] Colour bars in `HardwareCheck` are correct (inversion/BGR)
- [ ] Sun, eight planets, orbital paths and Saturn's rings are visible
- [ ] Planets have distinct surfaces and spherical shading
- [ ] One tap selects and opens every planet
- [ ] Each exploration screen shows the correct globe and facts
- [ ] Drag rotation, pinch zoom, marker and long-press effect work
- [ ] BACK and pinch-in restore the same solar-system view
- [ ] Animation stays responsive at the target frame rate
- [ ] 10+ minutes of interaction with no reset or allocation failure

---

## Performance notes

The scene is fully software-rendered into a 240×320 RGB565 buffer and pushed in
one SPI transfer per frame. Background pixels come from a PSRAM background layer;
stars and planets use pre-generated glow masks and a 512-entry sine table. Planet
maps are generated once at boot. The globe uses precomputed longitude/latitude
tables instead of per-pixel `atan2`/`asin`. Planet sprites are re-baked one at a
time for slow axial rotation, avoiding a per-frame trig cost. There are no
per-frame allocations and no blocking animation delays.

The FT6336 is polled by a dedicated core-0 task every 8 ms into a fixed-size
event queue, so touch remains responsive while the core-1 loop pushes a frame.
`TOUCH_ASYNC_TASK 0` switches to synchronous polling.

**Tuning.** `config.h` exposes star/dust counts, frame interval, gesture
thresholds, zoom limits, SPI clock and backlight. Use the BOOT overlay to measure
FPS before changing counts; no measured FPS is claimed here.

---

## Troubleshooting

- **Background is white/cream:** confirm `DISPLAY_INVERT 1`; this panel needs
  inversion on. If the whole image looks negative instead, set it to `0`.
- **Red and blue swapped:** `DISPLAY_RGB_ORDER 1` is correct for this panel; set
  it to `0` only if a board revision uses RGB order.
- **Touch is mirrored or rotated:** hold BOOT for ~1.2 s while the app runs
  (or tap **CALIBRATE** on the HUD) and complete the
guided five-target calibration. It measures X swap and both directions from
the raw corners, then saves the result. The values in `config.h` are only the
factory defaults used before the first calibration.
- **Calibration says FAILED:** tap the highlighted crosshair more precisely;
  the firmware rejects samples whose corner spans do not agree.
- **Crosshair scale is off:** adjust `TOUCH_RAW_MAX_X/Y`; the calibration
  corrects direction and axis order, not an unusual raw full-scale.
- **Blank white display:** check the 16 MB / OPI PSRAM / 240 MHz board options
  and the Serial panel-size report.
- **Touch dead, animation fine:** Serial should show the FT6336 at `0x38`; run
  `HardwareCheck` and check the ribbon.
- **Random SPI speckles:** reduce `DISPLAY_SPI_FREQ` to 40 MHz.
- **Low FPS:** lower `STAR_COUNT` or `GALAXY_PARTICLE_COUNT`; verify the frame
  buffer was placed in internal RAM.

---

Built by OceanLabz. LovyanGFX © its authors; Adafruit NeoPixel © Adafruit.
