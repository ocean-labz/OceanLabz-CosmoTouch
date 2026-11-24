/* ==========================================================================
 * GALAXY EXPLORER  -  OceanLabz ES3C28P (ESP32-S3)
 * --------------------------------------------------------------------------
 * Interactive galaxy / deep-space explorer for the 2.8" 240x320 IPS
 * capacitive touch board (ILI9341V + FT6336G).
 *
 * Architecture
 *   display_manager  LovyanGFX panel + full-frame RGB565 buffer
 *   space_canvas     software canvas, glow masks, 5x7 font
 *   touch_manager    FT6336 I2C driver, async polling, gesture recognition
 *   galaxy_engine    nebula, stars, spiral galaxy, planets
 *   effects          ripples, tap stars, supernova, black hole, shooters
 *   ui_manager       composited HUD + touch demo screens
 * ========================================================================== */
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "config.h"
#include "pins.h"

#if ENABLE_RGB_LED
#include <Adafruit_NeoPixel.h>
#endif

#include "display_manager.h"
#include "touch_manager.h"
#include "galaxy_engine.h"
#include "effects.h"
#include "planet_scene.h"
#include "ui_manager.h"

DisplayManager display;
TouchManager touch;
GalaxyEngine galaxy;
Effects effects;
PlanetScene planetScene;
UiManager ui;

#if ENABLE_RGB_LED
Adafruit_NeoPixel statusLed(RGB_LED_COUNT, RGB_LED, NEO_GRB + NEO_KHZ800);
#endif

static uint32_t splashStart = 0;
static uint32_t splashLastDraw = 0;
static uint32_t lastFrame = 0;
static uint32_t fpsWindow = 0;
static uint16_t fpsFrames = 0;
static uint16_t fps = 0;

static bool debugOverlay = false;
static bool bootPrev = false;
static bool bootLongHandled = false;
static uint32_t bootDownAt = 0;
static const uint32_t BOOT_CAL_MS = 1200;
static uint32_t ledLastAt = 0;

/* -------------------------------------------------------------------------- */
static void fatalError(const char* message) {
  LGFX& g = display.gfx();
  g.fillScreen(TFT_BLACK);
  g.setTextDatum(textdatum_t::top_center);
  g.setFont(&fonts::Font2);
  g.setTextColor(rgb565(0x54, 0xE6, 0xFF));
  g.drawString("OCEANLABZ", 120, 108);
  g.setTextColor(TFT_RED);
  g.drawString("STARTUP ERROR", 120, 132);
  g.setFont(&fonts::Font0);
  g.setTextColor(TFT_LIGHTGREY);
  g.drawString(message, 120, 156);
  g.drawString("CHECK PSRAM / FLASH SETTINGS", 120, 170);
  g.setTextDatum(textdatum_t::top_left);
  while (true) delay(1000);
}

static void enterGalaxy(uint32_t now) {
  ui.enter(Screen::GALAXY);
  galaxy.reset(true);
  effects.reset();
  effects.setLabel("TAP A PLANET TO LAND", rgb565(0x54, 0xE6, 0xFF), 2200);
  lastFrame = now - FRAME_TARGET_MS;
  fpsWindow = now;
  fpsFrames = 0;
#if SERIAL_DEBUG
  Serial.printf("[app] galaxy start  heap=%lu  psram=%lu\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
}

static void drawBootScreen() {
  LGFX& g = display.gfx();
  g.fillScreen(TFT_BLACK);
  g.setFont(&fonts::Font2);
  g.setTextColor(rgb565(0x54, 0xE6, 0xFF));
  g.setTextDatum(textdatum_t::top_center);
  g.drawString("OCEANLABZ", 120, 120);
  g.setTextColor(rgb565(0x72, 0x91, 0xAE));
  g.setFont(&fonts::Font0);
  g.drawString("GALAXY EXPLORER BOOTING", 120, 146);
  g.setTextDatum(textdatum_t::top_left);
}

/* -------------------------------------------------------------------------- */
static void handleGalaxyEvent(const TouchEvent& ev) {
  switch (ev.type) {
    case TouchEventType::DOWN:
      effects.touchRipple(ev.x, ev.y, rgb565(0x54, 0xE6, 0xFF));
      break;

    case TouchEventType::TAP: {
      const int8_t hit = galaxy.planetAt(ev.x, ev.y, 12);
      if (hit >= 0) {
        galaxy.selectPlanet(hit);
        effects.endBlackHole();
        if (planetScene.start((uint8_t)hit, millis())) {
          effects.touchRipple(ev.x, ev.y, galaxy.planetAccent((uint8_t)hit));
        } else {
          effects.setLabel("PLANET DATA UNAVAILABLE", TFT_RED, 1500);
#if SERIAL_DEBUG
          Serial.printf("[PLANET] start rejected for id=%d at %d,%d\n",
                        (int)hit, (int)ev.x, (int)ev.y);
#endif
        }
      } else {
        galaxy.clearSelection();
        effects.createStar(ev.x, ev.y);
      }
      break;
    }

    case TouchEventType::DOUBLE_TAP:
      effects.supernova(ev.x, ev.y);
      break;

    case TouchEventType::LONG_PRESS:
      effects.startBlackHole(ev.x, ev.y);
      break;

    case TouchEventType::LONG_RELEASE:
      effects.endBlackHole();
      break;

    case TouchEventType::MOVE:
      if (effects.blackHoleActive()) {
        effects.moveBlackHole(ev.x, ev.y);
      } else if (ev.fingers == 1) {
        galaxy.moveBy(ev.dx, ev.dy);
      }
      break;

    case TouchEventType::PINCH_START:
      /* A second finger cancels an active long-press black hole so it can
       * never be left behind. */
      effects.endBlackHole();
      effects.touchRipple(ev.x, ev.y, rgb565(0xA4, 0x7A, 0xFF));
      break;

    case TouchEventType::PINCH: {
      /* Pinch stays a solar-system zoom gesture. Planet entry is a single tap. */
      const float factor = ev.scaleQ8 / 256.0f;
      galaxy.zoomBy(factor, ev.x, ev.y);
      break;
    }

    case TouchEventType::PINCH_END:
      effects.setLabel("ZOOM SET", rgb565(0x54, 0xE6, 0xFF), 700);
      break;

    default:
      break;
  }
}

static void drainTouch(uint32_t now) {
  (void)now;
  TouchEvent ev;
  while (touch.pop(ev)) {
    if (planetScene.active()) {
      planetScene.handleTouch(ev);
    } else if (ui.screen() == Screen::GALAXY) {
      if (!ui.handleGalaxyTouch(ev)) handleGalaxyEvent(ev);
    } else if (ui.screen() == Screen::SPLASH) {
      if (ev.type == TouchEventType::TAP) enterGalaxy(millis());
    } else {
      ui.handleEvent(ev);
    }
  }
}

static void pollBootButton(uint32_t now) {
  const bool pressed = digitalRead(BOOT_BTN) == LOW;
  if (pressed && !bootPrev) {
    bootDownAt = now;
    bootLongHandled = false;
  }
  /* A long press while the app is running opens calibration. Holding BOOT
   * through reset cannot be used on ESP32-S3: GPIO0 low at reset makes the
   * ROM bootloader enter download mode instead of running the sketch. */
  if (pressed && !bootLongHandled && (now - bootDownAt) >= BOOT_CAL_MS) {
    bootLongHandled = true;
    debugOverlay = true;
#if SERIAL_DEBUG
    Serial.println("[ui] BOOT long press -> touch calibration");
#endif
    if (ui.screen() != Screen::TOUCH_TEST) ui.enter(Screen::TOUCH_TEST);
  }
  if (!pressed && bootPrev && !bootLongHandled && (now - bootDownAt) < 700) {
    debugOverlay = !debugOverlay;
  }
  bootPrev = pressed;
}

static void updateStatusLed(uint32_t now) {
#if ENABLE_RGB_LED
  if (now - ledLastAt < 50) return;
  ledLastAt = now;
  const float breathe = 0.45f + 0.55f * (0.5f + 0.5f * sinf(now * 0.0022f));
  uint8_t r = 0, g = 0, b = 0;
  switch (ui.screen()) {
    case Screen::GALAXY:
    case Screen::SPLASH:
      r = (uint8_t)(12 * breathe); g = (uint8_t)(120 * breathe); b = (uint8_t)(190 * breathe);
      break;
    case Screen::PAINT:
      r = (uint8_t)(150 * breathe); g = (uint8_t)(110 * breathe); b = (uint8_t)(10 * breathe);
      break;
    case Screen::GESTURE_TEST:
      r = (uint8_t)(120 * breathe); g = (uint8_t)(35 * breathe); b = (uint8_t)(160 * breathe);
      break;
    default:
      r = (uint8_t)(50 * breathe); g = (uint8_t)(30 * breathe); b = (uint8_t)(120 * breathe);
      break;
  }
  statusLed.setPixelColor(0, statusLed.Color(r, g, b));
  statusLed.show();
#else
  (void)now;
#endif
}

/* -------------------------------------------------------------------------- */
void setup() {
  Serial.begin(115200);
#if SERIAL_DEBUG
  delay(120);
  Serial.println();
  Serial.println("==============================================");
  Serial.println(" OCEANLABZ  GALAXY EXPLORER  -  ES3C28P");
  Serial.println(" ESP32-S3  /  ILI9341V 240x320  /  FT6336G");
  Serial.println("==============================================");
  Serial.printf("PSRAM: %lu bytes free, internal: %lu bytes free\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif

  pinMode(BOOT_BTN, INPUT_PULLUP);
  bootPrev = digitalRead(BOOT_BTN) == LOW;

#if SERIAL_DEBUG
  if (ESP.getPsramSize() == 0) {
    Serial.println("[app] WARNING: PSRAM disabled in board settings; expect allocation failures");
  }
#endif

  fx::init();

  if (!display.begin()) {
#if SERIAL_DEBUG
    Serial.printf("[display] UNEXPECTED SIZE %dx%d (expected %dx%d)\n",
                  (int)display.gfx().width(), (int)display.gfx().height(),
                  SCREEN_W, SCREEN_H);
#endif
    fatalError("PANEL IS NOT 240X320");
  }
  if (!display.allocateFrameBuffer()) {
#if SERIAL_DEBUG
    Serial.println("[display] FATAL: frame buffer allocation failed");
#endif
    fatalError("FRAME BUFFER ALLOC FAILED");
  }
#if SERIAL_DEBUG
  Serial.printf("[display] frame buffer in %s, %dx%d, SPI %lu Hz\n",
                display.frameInPsram() ? "PSRAM" : "internal RAM",
                (int)display.gfx().width(), (int)display.gfx().height(),
                (unsigned long)DISPLAY_SPI_FREQ);
#endif
  drawBootScreen();

  if (!touch.begin()) {
#if SERIAL_DEBUG
    Serial.printf("[touch] init warning: %s (i2c errors=%lu)\n",
                  touch.lastError(), (unsigned long)touch.i2cErrors());
#endif
  }
  touch.start();

#if ENABLE_RGB_LED
  statusLed.begin();
  statusLed.setBrightness(60);
  statusLed.show();
#endif

  drawBootScreen();
  if (!galaxy.begin(&display.canvas())) {
#if SERIAL_DEBUG
    Serial.println("[galaxy] FATAL: background allocation failed");
#endif
    fatalError("NEBULA BUFFER ALLOC FAILED");
  }
  effects.begin(&display.canvas(), &galaxy);
  planetScene.begin(&display.canvas(), &galaxy);
  ui.begin(&display, &touch, &galaxy, &effects);

#if SERIAL_DEBUG
  Serial.printf("[touch] chip 0x%02X, max points seen %u, errors %lu\n",
                touch.chipId(), touch.maxPointsSeen(), (unsigned long)touch.i2cErrors());
  Serial.printf("[app] ready  heap=%lu  psram=%lu\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif

  splashStart = millis();
  splashLastDraw = 0;
  ui.enter(Screen::SPLASH);
}

/* -------------------------------------------------------------------------- */
void loop() {
  const uint32_t now = millis();

  pollBootButton(now);
  touch.update(now);
  drainTouch(now);
  updateStatusLed(now);

  switch (ui.screen()) {
    case Screen::SPLASH: {
      if (now - splashLastDraw >= 33) {
        ui.drawSplash(display.canvas(), now, splashStart);
        display.present();
        splashLastDraw = now;
      }
      if (now - splashStart >= 2600) enterGalaxy(now);
      delay(1);
      break;
    }

    case Screen::GALAXY: {
      if (now - lastFrame < FRAME_TARGET_MS) {
        delay(1);                    /* yield while waiting for the next frame */
        break;
      }

      float dt = (now - lastFrame) * 0.001f;
      if (dt > 0.06f) dt = 0.06f;
      lastFrame = now;

      galaxy.update(now, dt);
      effects.update(now, dt);

      if (planetScene.active()) {
        planetScene.update(now, dt);
        if (planetScene.active()) {
          planetScene.render(now);
        } else {
          /* The descent back to the galaxy finished during update(). */
          galaxy.clearSelection();
          effects.setLabel("SOLAR SYSTEM RESTORED", rgb565(0x54, 0xE6, 0xFF), 1200);
          galaxy.render(now);
          effects.render(now);
          ui.drawGalaxyHud(display.canvas(), now, fps, debugOverlay);
        }
      } else {
        galaxy.render(now);
        effects.render(now);
        ui.drawGalaxyHud(display.canvas(), now, fps, debugOverlay);
      }
      display.present();

      ++fpsFrames;
      if (now - fpsWindow >= 1000) {
        fps = (uint16_t)((uint32_t)fpsFrames * 1000 / (now - fpsWindow));
        fpsFrames = 0;
        fpsWindow = now;
      }
      break;
    }

    default: {
      ui.update(now);
      delay(1);
      break;
    }
  }
}
