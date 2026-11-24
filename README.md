# Galaxy Explorer — OceanLabz ES3C28P

An interactive deep-space explorer for the **ES3C28P ESP32-S3 board** with its
**2.8" 240×320 IPS capacitive touchscreen** (ILI9341V + FT6336G).

A three-armed particle galaxy rotates over a procedurally lit nebula, with
twinkling parallax stars, orbiting planets, shooting stars, and a touch layer
that reacts to taps, drags, long presses, double taps, and two-finger pinches.
A dedicated demo suite exercises every capacitive-touch capability of the board.

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

Pin assignments live in [`GalaxyExplorer/pins.h`](GalaxyExplorer/pins.h) and are
used unchanged throughout. The display reset is tied to CHIP_PU, so the panel
driver uses a software reset (`pin_rst = -1`).

---

## What the application does

### Galaxy screen
- Baked 304×384 procedural nebula background in PSRAM, copied with a slow
  parallax offset each frame.
- 250 dynamic stars in three depth layers; varied twinkle speeds, drift speeds,
  and brightnesses, plus 620 dim baked background stars.
- 1,150-particle 3-arm spiral with differential rotation, a pulsing multi-layer
  core, arm knots, and additive glow.
- Four shaded planets with orbital motion, atmospheric glow, and a ringed world.
- Random shooting stars with fading trails.
- Translucent bottom HUD with **GALAXY / TOUCH TEST / RESET** buttons and a
  fading event label ("STAR CREATED", "SUPERNOVA", …).
- Splash screen: **OCEANLABZ / SPACE EXPLORER**, touch to skip.

### Touch interaction
| Gesture | Result |
|---|---|
| Tap | Creates a bright star, expanding ripple, particle burst, "STAR CREATED" |
| Double tap | Supernova: expanding luminous rings, 130+ particles, brief low-alpha flash |
| Long press | Black hole: dark event horizon, purple/cyan accretion ring, spiralling particles, stars bend inward; fades out on release |
| Drag | Moves the galaxy with inertia and friction; background parallax follows |
| Two-finger pinch | Smooth zoom (0.52×–2.25×), anchored on the pinch centre |
| Any touch down | Immediate ripple for tactile feedback |

If a particular FT6336 module only reports one contact, pinch silently does
nothing and every other feature remains usable. The **MULTI TOUCH** demo reports
the maximum contact count the controller actually delivered.

### Touch demo suite
1. **Touch Test** — live X/Y, raw controller coordinates, event count, glowing
   brush, continuous lines, clear.
2. **Multi-Touch** — coloured contact circles, active/max contact count, live
   distance/pinch zoom (a grid scales with your fingers).
3. **Gesture Test** — detects tap, double tap, long press, drag, and all four
   swipes, with a name readout, pictogram, and per-gesture highlighting.
4. **Touch Painting** — five colours, three brush sizes, glow strokes, clear.
5. **Galaxy** — returns to the live galaxy.

Navigation: **TOUCH TEST** opens the demo menu, **BACK** from each demo returns
to it, and item 5 returns to the galaxy.

### Diagnostics
- BOOT button (GPIO 0) toggles an FPS/touch debug overlay on the galaxy screen.
- Holding BOOT during reset also starts with the overlay enabled.
- Serial (115200 baud) prints display size, PSRAM/frame-buffer placement, touch
  chip ID, max contacts seen, I²C error count, and free heap at startup.
- RGB LED shows a subtle status colour per screen.

---

## Repository layout

```
GalaxyExplorer/
  GalaxyExplorer.ino    application init + main loop
  pins.h                confirmed ES3C28P pin map
  config.h              all compile-time tuning + touch calibration
  display_manager.*     LovyanGFX ILI9341 setup, frame buffer, present()
  touch_manager.*       FT6336 I2C driver, async polling, gesture engine
  space_canvas.*        RGB565 canvas, glow masks, primitives, 5×7 font
  galaxy_engine.*       nebula, stars, galaxy, planets
  effects.*             ripples, tap stars, supernova, black hole, shooters
  ui_manager.*          composited HUD, menus, touch demo screens
HardwareCheck/
  HardwareCheck.ino     independent Stage-1 display/touch diagnostic
```

---

## Build

### Arduino IDE
1. Install **ESP32 Arduino core 3.x** (tested with 3.3.12).
2. Install **LovyanGFX** (tested 1.3.0) and **Adafruit NeoPixel** (tested 1.15.5)
   from Library Manager. NeoPixel is only used for the optional RGB status LED
   (`ENABLE_RGB_LED 1` in `config.h`).
3. Open `GalaxyExplorer/GalaxyExplorer.ino`.
4. Select **ESP32S3 Dev Module** with:
   - Flash Size: **16 MB**
   - PSRAM: **OPI PSRAM**
   - Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)** (or any ≥2 MB app)
   - CPU Frequency: **240 MHz**
   - Flash Mode: **QIO 80 MHz**
5. Upload. There is no board-specific variant to select; all pins are explicit.

### arduino-cli
```bash
arduino-cli lib install LovyanGFX "Adafruit NeoPixel"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CPUFreq=240"
arduino-cli compile --fqbn "$FQBN" GalaxyExplorer
arduino-cli upload  --fqbn "$FQBN" -p COM5 GalaxyExplorer   # use your port
```
The local copy used during development is `tools/arduino-cli` (not committed).

> **PSRAM is required.** The nebula layer (~233 KB), glow masks (~34 KB) and
> planet sprites live in PSRAM; the 153 KB frame buffer prefers internal
> DMA-capable RAM and falls back to PSRAM. With PSRAM disabled the app will
> report an allocation failure over Serial instead of drawing garbage.

---

## Stage 1: verify the hardware first

Flash **`HardwareCheck/HardwareCheck.ino`** (same board settings) before judging
the galaxy app. It shows:

- 240×320 resolution confirmation and a 7-colour bar test (checks inversion and
  RGB/BGR order).
- FT6336 chip-ID registers `0xA3` and `0xA8` plus an I²C bus scan on Serial.
- Live raw and mapped coordinates, contact count, and maximum contacts seen.
- Up to two coloured crosshairs for verifying simultaneous touches.

Tap all four corners. The crosshair must land exactly under your finger.

### Calibration switches
All in [`GalaxyExplorer/config.h`](GalaxyExplorer/config.h) (and mirrored at the
top of `HardwareCheck.ino`):

| Symptom | Change |
|---|---|
| Touch X/Y axes are rotated | `TOUCH_SWAP_XY 1` |
| Touch X axis mirrored | `TOUCH_INVERT_X 1` |
| Touch Y axis mirrored | `TOUCH_INVERT_Y 1` |
| Crosshair scale is off | adjust `TOUCH_RAW_MAX_X/Y` |
| Display colours look like a negative | `DISPLAY_INVERT 1` |
| Red and blue are swapped | `DISPLAY_RGB_ORDER 1` |
| Sparkle at high SPI speed | lower `DISPLAY_SPI_FREQ` to `40000000` |

After changing touch macros, re-flash both sketches. `TOUCH TEST` in the app also
shows raw and mapped values, so calibration can be checked in situ.

---

## Architecture & performance notes

**Rendering path.** The galaxy is fully software-rendered into a 240×320 RGB565
buffer and pushed in a single SPI transfer per frame, which eliminates tearing
and flicker. Background pixels are a row-wise memcpy from the PSRAM nebula layer;
particles use pre-generated alpha-glow masks and a 512-entry sine table, so the
per-frame loop contains no per-particle trigonometry. Planet surfaces are shaded
once at boot. UI demo screens draw directly and update only the regions that
change (trails, coordinate text, menus).

**Concurrency.** A single low-priority task on core 0 polls the FT6336 every
8 ms and feeds a fixed-size event queue; the render loop on core 1 never blocks
on I²C. This is the only extra task, and it exists so taps and drags stay
responsive while a 153 KB frame is being pushed. `TOUCH_ASYNC_TASK 0` switches
back to synchronous polling if preferred.

**Memory.** Everything is allocated once at boot. There are no per-frame
`malloc`/`new` calls, no `String`s in the animation path, and allocation
failures are reported rather than dereferenced. Frame-buffer and background
placement are printed on Serial.

**Tuning.** `config.h` exposes particle/star counts, frame interval, gesture
thresholds, zoom limits, SPI clock, and backlight level. The FPS overlay
(BOOT button) is the measurement to use when changing them. The target is
25–30 fps at 60 MHz SPI; if the measured rate is low, first reduce
`GALAXY_PARTICLE_COUNT`, then lower `DISPLAY_SPI_FREQ` only if there is visual
corruption.

---

## Verification status

Compile-verified with `arduino-cli` and ESP32 core **3.3.12** for the exact S3
board options above: both sketches build with `--warnings all` and no warnings.
Flash use is ~489 KB of a 3 MB app partition; static RAM ~64 KB, leaving room for
the internal frame buffer.

**Not yet verified on physical hardware** — no ES3C28P was attached in the
development environment. The following must be exercised with `HardwareCheck`
and then the app: display orientation/colour, touch mapping, two-point reporting,
FPS, and actual motion smoothness. The design choices above are aimed at those
targets, but no measured FPS is claimed. Use the BOOT-button overlay to record it.

On-device checklist:
- [ ] HardwareCheck shows 240×320 and correct colours
- [ ] Crosshair tracks the finger exactly (both axes, all corners)
- [ ] FT6336 reports 2 contacts when two fingers are used
- [ ] Galaxy renders continuously, ~25–30 fps
- [ ] Tap / double tap / long press / drag / pinch behave as described
- [ ] Touch Test, paint, gestures, and back navigation work
- [ ] 10+ minutes of interaction with no reset or allocation failure

---

## Troubleshooting

- **Blank white display:** confirm the 16 MB / OPI PSRAM / 240 MHz board options
  and that the app's Serial log reports the 240×320 panel size.
- **Touch dead, animation fine:** check Serial for "FT6336 not responding"; the
  `HardwareCheck` I²C scan should list `0x38`. Verify the ribbon is seated.
- **Colours inverted or wrong:** toggle `DISPLAY_INVERT` / `DISPLAY_RGB_ORDER`.
- **Touch offset/rotated:** use the four calibration macros above; do not change
  the pin map.
- **Random SPI speckles:** reduce `DISPLAY_SPI_FREQ` to 40 MHz.
- **Low FPS:** lower `GALAXY_PARTICLE_COUNT` / `STAR_COUNT` in `config.h`; check
  that the frame buffer was reported as internal RAM, not PSRAM.

---

Built by OceanLabz. LovyanGFX © its authors; Adafruit NeoPixel © Adafruit.
