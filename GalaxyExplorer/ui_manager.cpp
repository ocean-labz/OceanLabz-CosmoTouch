/* ==========================================================================
 * ui_manager.cpp
 * ========================================================================== */
#include "ui_manager.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t kBg      = 0x0000;   /* deep space is pure #000000 */
constexpr uint16_t kPanel   = rgb565(0x0A, 0x12, 0x22);
constexpr uint16_t kPanelHi = rgb565(0x14, 0x26, 0x3E);
constexpr uint16_t kText    = rgb565(0xCF, 0xE6, 0xFF);
constexpr uint16_t kMuted   = rgb565(0x72, 0x91, 0xAE);
constexpr uint16_t kCyan    = UI_ACCENT_CYAN;
constexpr uint16_t kPurple  = UI_ACCENT_PURPLE;
constexpr uint16_t kYellow  = UI_ACCENT_YELLOW;
constexpr uint16_t kRed     = rgb565(0xFF, 0x5A, 0x4A);
constexpr uint16_t kWhite   = 0xFFFF;

/* HUD bottom bar */
constexpr int16_t kBarY = SCREEN_H - HUD_BAR_H;      /* 272 */
constexpr int16_t kBtnY = 280;
constexpr int16_t kBtnH = 32;
constexpr int16_t kBtnW = 74;
const int16_t kBtnX[3] = {6, 83, 160};
const char* const kBtnText[3] = {"CENTER", "CALIBRATE", "RESET"};
const uint16_t kBtnAccent[3] = {kCyan, kPurple, kYellow};

/* Menu */
constexpr int16_t kMenuX = 16;
constexpr int16_t kMenuW = 208;
constexpr int16_t kMenuH = 36;
constexpr int16_t kMenuY[5] = {68, 114, 160, 206, 252};
const char* const kMenuText[5] = {
  "1   CALIBRATE", "2   MULTI TOUCH", "3   GESTURE TEST",
  "4   TOUCH PAINT", "5   SOLAR SYSTEM"
};
const uint16_t kMenuAccent[5] = {kCyan, kPurple, kYellow, kCyan, kWhite};

/* Five-target touch calibration */
constexpr int16_t kCalX[5] = {0, 239, 0, 239, 120};
constexpr int16_t kCalY[5] = {0, 0, 319, 319, 160};
const char* const kCalName[5] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT",
                                 "BOTTOM-RIGHT", "CENTER"};
constexpr uint16_t kCalGreen = rgb565(0x54, 0xFF, 0x9E);
constexpr int16_t kInfoY = 42, kInfoH = 56;

/* Painting */
const uint16_t kPaintColors[5] = {
  rgb565(0x54, 0xE6, 0xFF), rgb565(0xA4, 0x7A, 0xFF), rgb565(0xFF, 0xD4, 0x5E),
  0xFFFF, rgb565(0xFF, 0x6F, 0xA8)
};
const uint8_t kPaintSizes[3] = {3, 6, 10};
constexpr int16_t kPaintCanvasX = 6, kPaintCanvasY = 88;
constexpr int16_t kPaintCanvasW = 228, kPaintCanvasH = 184;

/* Gestures */
const char* const kGestureNames[8] = {
  "TAP", "DOUBLE", "LONG", "DRAG", "SWIPE L", "SWIPE R", "SWIPE U", "SWIPE D"
};

inline uint32_t hashU32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  return x;
}

} // namespace

/* -------------------------------------------------------------------------- */
/*  Setup / navigation                                                        */
/* -------------------------------------------------------------------------- */
void UiManager::begin(DisplayManager* display, TouchManager* touch,
                      GalaxyEngine* galaxy, Effects* effects) {
  _display = display;
  _touch = touch;
  _galaxy = galaxy;
  _effects = effects;
  _gfx = display ? &display->gfx() : nullptr;
  _screen = Screen::SPLASH;
  _screenMs = millis();
}

void UiManager::enter(Screen next) {
  if (!_gfx) return;
  _screen = next;
  _screenMs = millis();
  _backPressed = false;
  _menuPressed = -1;
  memset(_menuButtonPressed, 0, sizeof(_menuButtonPressed));

  switch (next) {
    case Screen::MENU:
      drawMenu();
      break;
    case Screen::TOUCH_TEST:
      _calHits = 0;
      _calLiveX = _calLiveY = -1;
      _calLiveRawX = _calLiveRawY = 0;
      startCalibrationSequence();
      break;
    case Screen::MULTITOUCH:
      _multiZoom = 1.0f;
      _multiBaseDist = 0;
      _multiPrevActive = 0;
      _multiDrawnAt = 0;
      drawMultiTouch();
      break;
    case Screen::GESTURE_TEST:
      _gestureIndex = -1;
      _gestureCount = 0;
      drawGestureTest();
      break;
    case Screen::PAINT:
      _painting = false;
      _paintLastX = _paintLastY = -1;
      drawPaintScreen();
      break;
    case Screen::GALAXY:
    case Screen::SPLASH:
      break;
  }
}

void UiManager::enterGalaxy() {
  if (_effects) _effects->endBlackHole();
  if (_galaxy) _galaxy->setGravity(false, 0, 0);
  enter(Screen::GALAXY);
}

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */
uint16_t UiManager::dim(uint16_t color, uint8_t amount) {
  const uint16_t r = ((color >> 11) & 31) * amount / 255;
  const uint16_t g = ((color >> 5) & 63) * amount / 255;
  const uint16_t b = (color & 31) * amount / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void UiManager::textAt(const char* s, int16_t x, int16_t y, const lgfx::IFont* font,
                       uint16_t color, textdatum_t datum) {
  if (!_gfx) return;
  _gfx->setFont(font);
  _gfx->setTextColor(color);
  _gfx->setTextDatum(datum);
  _gfx->drawString(s, x, y);
  _gfx->setTextDatum(textdatum_t::top_left);
}

void UiManager::button(int16_t x, int16_t y, int16_t w, int16_t h, const char* text,
                       uint16_t accent, bool pressed, const lgfx::IFont* font) {
  const int16_t r = (int16_t)(h / 2);
  _gfx->fillSmoothRoundRect(x, y, w, h, r, pressed ? dim(accent, 70) : kPanel);
  _gfx->drawRoundRect(x, y, w, h, r, accent);
  if (pressed) _gfx->drawRoundRect(x + 1, y + 1, w - 2, h - 2, (int16_t)(r - 1), dim(accent, 170));
  textAt(text, (int16_t)(x + w / 2), (int16_t)(y + h / 2), font,
         pressed ? kWhite : accent, textdatum_t::middle_center);
}

void UiManager::drawBackdrop(const char* title, const char* subtitle) {
  _gfx->fillScreen(kBg);

  /* faint top accent line */
  _gfx->fillRect(0, 0, SCREEN_W, 1, dim(kCyan, 50));
  _gfx->fillRect(0, 1, SCREEN_W, 1, dim(kCyan, 22));

  for (uint16_t i = 0; i < 90; ++i) {
    const uint32_t h = hashU32(i * 2654435761u + 17);
    const int16_t x = (int16_t)(h % SCREEN_W);
    const int16_t y = (int16_t)((h >> 9) % SCREEN_H);
    const uint16_t c = ((h >> 22) & 1) ? rgb565(0x6E, 0x84, 0xA8)
                                       : ((h >> 23) & 1 ? rgb565(0xA9, 0xBF, 0xE0) : rgb565(0x4A, 0x58, 0x78));
    _gfx->drawPixel(x, y, c);
    if ((h & 0x3F) == 0) {
      _gfx->drawPixel((int16_t)(x + 1), y, dim(c, 110));
      _gfx->drawPixel(x, (int16_t)(y + 1), dim(c, 90));
    }
  }

  /* header */
  _gfx->fillRect(10, 12, 3, 20, kCyan);
  textAt(title, 20, 11, &fonts::Font2, kText, textdatum_t::top_left);
  if (subtitle) textAt(subtitle, 20, 29, &fonts::Font0, kMuted, textdatum_t::top_left);
  drawBackButton(_backPressed);

  _gfx->fillRect(0, 40, SCREEN_W, 1, dim(kCyan, 40));
}

void UiManager::drawHeader(const char* title, const char* subtitle) {
  drawBackdrop(title, subtitle);
}

void UiManager::drawBackButton(bool pressed) {
  button(166, 7, 68, 25, "BACK", kCyan, pressed, &fonts::Font2);
}

bool UiManager::hitBack(int16_t x, int16_t y) const {
  return x >= 166 && x < 234 && y >= 7 && y < 32;
}

/* -------------------------------------------------------------------------- */
/*  Menu                                                                      */
/* -------------------------------------------------------------------------- */
void UiManager::drawMenu() {
  drawBackdrop("TOUCH DEMO", "CAPACITIVE TOUCHSCREEN");

  for (uint8_t i = 0; i < 5; ++i) drawMenuButton(i, _menuButtonPressed[i]);

  textAt("SELECT A DEMONSTRATION", 120, 296, &fonts::Font0, kMuted, textdatum_t::top_center);

  char info[40];
  snprintf(info, sizeof(info), "FT6336  ID 0x%02X  MAX %u POINTS",
           _touch ? _touch->chipId() : 0,
           _touch ? _touch->maxPointsSeen() : 0);
  textAt(info, 120, 308, &fonts::Font0, dim(kCyan, 160), textdatum_t::top_center);
}

void UiManager::drawMenuButton(uint8_t index, bool pressed) {
  if (index >= 5) return;
  button(kMenuX, kMenuY[index], kMenuW, kMenuH, kMenuText[index],
         kMenuAccent[index], pressed, &fonts::Font2);
}

bool UiManager::hitMenuButton(int16_t x, int16_t y, uint8_t* index) const {
  for (uint8_t i = 0; i < 5; ++i) {
    if (x >= kMenuX && x < kMenuX + kMenuW &&
        y >= kMenuY[i] && y < kMenuY[i] + kMenuH) {
      *index = i;
      return true;
    }
  }
  return false;
}

void UiManager::markMenuDirty(uint8_t index, bool pressed) {
  if (_screen != Screen::MENU || index >= 5) return;
  _menuButtonPressed[index] = pressed;
  drawMenuButton(index, pressed);
}

/* -------------------------------------------------------------------------- */
/*  Touch test                                                                */
/* -------------------------------------------------------------------------- */
void UiManager::drawCalTarget(uint8_t index) {
  if (!_gfx || index >= 5) return;
  const int16_t x = kCalX[index];
  const int16_t y = kCalY[index];
  const bool next = _calAuto && index == _calStep;
  const bool captured = _calHit[index];
  const uint16_t color = next ? kYellow : (captured ? kCalGreen : kCyan);

  if (next) _gfx->drawCircle(x, y, 29, dim(kYellow, 130));
  _gfx->drawCircle(x, y, 22, dim(color, 110));
  _gfx->drawCircle(x, y, 14, color);
  _gfx->drawCircle(x, y, 7, dim(color, 180));
  _gfx->drawFastHLine((int16_t)(x - 28), y, 56, dim(color, 150));
  _gfx->drawFastVLine(x, (int16_t)(y - 28), 56, dim(color, 150));
  _gfx->fillCircle(x, y, 3, captured ? kCalGreen : color);

  char buf[24];
  if (next) snprintf(buf, sizeof(buf), "TAP");
  else snprintf(buf, sizeof(buf), "%d,%d%s", kCalX[index], kCalY[index],
                captured ? " OK" : "");
  const uint16_t tc = next ? kYellow : (captured ? kCalGreen : dim(kText, 190));
  if (index == 1 || index == 3) {
    textAt(buf, 232, index == 1 ? 22 : 298, &fonts::Font0, tc, textdatum_t::top_right);
  } else if (index == 4) {
    textAt(buf, 120, 182, &fonts::Font0, tc, textdatum_t::top_center);
  } else {
    textAt(buf, 8, index == 0 ? 22 : 298, &fonts::Font0, tc, textdatum_t::top_left);
  }
}

void UiManager::drawCalibrationInfo() {
  if (!_gfx) return;
  _gfx->fillSmoothRoundRect(28, 194, 184, 42, 9, rgb565(0x03, 0x07, 0x0F));
  _gfx->drawRoundRect(28, 194, 184, 42, 9, dim(kCyan, 120));

  char buf[48];
  if (_calAuto) {
    snprintf(buf, sizeof(buf), "STEP %u/5  TAP %s",
             (unsigned)(_calStep + 1), kCalName[_calStep < 5 ? _calStep : 4]);
    textAt(buf, 120, 199, &fonts::Font0, kYellow, textdatum_t::top_center);
    if (_calLiveX < 0) snprintf(buf, sizeof(buf), "RAW  --  --");
    else snprintf(buf, sizeof(buf), "RAW  X %03u  Y %03u", _calLiveRawX,
                  _calLiveRawY);
    textAt(buf, 120, 211, &fonts::Font0, dim(kText, 180), textdatum_t::top_center);
    if (_calLiveX < 0) buf[0] = 0;
    else snprintf(buf, sizeof(buf), "MAPPED %03d,%03d", _calLiveX, _calLiveY);
    textAt(buf, 120, 223, &fonts::Font0, dim(kCyan, 190), textdatum_t::top_center);
    return;
  }

  if (_calApplied) {
    snprintf(buf, sizeof(buf), "CALIBRATED - TAP TO VERIFY");
    textAt(buf, 120, 199, &fonts::Font0, kCalGreen, textdatum_t::top_center);
    snprintf(buf, sizeof(buf), "X:%s %s  Y:%s %s",
             _touch && _touch->swapXY() ? "rawY" : "rawX",
             _touch && _touch->invertX() ? "INV" : "NRM",
             _touch && _touch->swapXY() ? "rawX" : "rawY",
             _touch && _touch->invertY() ? "INV" : "NRM");
    textAt(buf, 120, 211, &fonts::Font0, dim(kText, 180), textdatum_t::top_center);
    snprintf(buf, sizeof(buf), "TARGETS %u/5", (unsigned)_calHits);
    textAt(buf, 120, 223, &fonts::Font0,
           _calHits == 5 ? kCalGreen : kPurple, textdatum_t::top_center);
    return;
  }

  if (_calStep >= 5) {
    textAt("CALIBRATION FAILED", 120, 199, &fonts::Font0, kRed,
           textdatum_t::top_center);
    textAt("CHECK CORNER TAPS, PRESS REDO", 120, 211, &fonts::Font0, kMuted,
           textdatum_t::top_center);
    return;
  }

  snprintf(buf, sizeof(buf), "TARGETS %u/5", (unsigned)_calHits);
  textAt(buf, 120, 199, &fonts::Font0,
         _calHits == 5 ? kCalGreen : kPurple, textdatum_t::top_center);
  if (_calLiveX < 0) snprintf(buf, sizeof(buf), "MAPPED --  RAW --");
  else snprintf(buf, sizeof(buf), "MAPPED %03d,%03d", _calLiveX, _calLiveY);
  textAt(buf, 120, 211, &fonts::Font0, _calLiveX < 0 ? kMuted : kCyan,
         textdatum_t::top_center);
  if (_calLiveX < 0) buf[0] = 0;
  else snprintf(buf, sizeof(buf), "RAW %03u,%03u", _calLiveRawX, _calLiveRawY);
  textAt(buf, 120, 223, &fonts::Font0, dim(kText, 180), textdatum_t::top_center);
}

void UiManager::drawCalibration() {
  if (!_gfx) return;
  _gfx->fillScreen(kBg);
  _gfx->fillRect(0, 0, SCREEN_W, 1, dim(kCyan, 80));
  textAt("TOUCH CALIBRATION", 120, 3, &fonts::Font2, kText, textdatum_t::top_center);
  textAt(_calAuto ? "TAP THE HIGHLIGHTED TARGET" : "VERIFY EACH CROSSHAIR",
         120, 17, &fonts::Font0, _calAuto ? kYellow : kMuted,
         textdatum_t::top_center);
  button(58, 29, 52, 22, "REDO", kPurple, false, &fonts::Font2);
  button(130, 29, 52, 22, "EXIT", kCyan, false, &fonts::Font2);

  for (uint8_t i = 0; i < 5; ++i) drawCalTarget(i);
  drawCalibrationInfo();
}

int8_t UiManager::hitCalTarget(int16_t x, int16_t y) const {
  int8_t best = -1;
  int32_t bestD = 0x7FFFFFFF;
  for (uint8_t i = 0; i < 5; ++i) {
    const int32_t dx = x - kCalX[i];
    const int32_t dy = y - kCalY[i];
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 <= 30 * 30 && d2 < bestD) {
      bestD = d2;
      best = (int8_t)i;
    }
  }
  return best;
}

void UiManager::startCalibrationSequence() {
  _calAuto = true;
  _calApplied = false;
  _calStep = 0;
  _calHits = 0;
  _calLiveX = _calLiveY = -1;
  _calLiveRawX = _calLiveRawY = 0;
  memset(_calHit, 0, sizeof(_calHit));
  memset(_calTouchX, 0, sizeof(_calTouchX));
  memset(_calTouchY, 0, sizeof(_calTouchY));
  memset(_calRawX, 0, sizeof(_calRawX));
  memset(_calRawY, 0, sizeof(_calRawY));
  memset(_calCapRawX, 0, sizeof(_calCapRawX));
  memset(_calCapRawY, 0, sizeof(_calCapRawY));
  drawCalibration();
}

/* Derive the orientation from five raw samples taken at known physical
 * targets. Nothing is assumed about X/Y order or direction: both axes are
 * chosen from which raw component actually changes across the top and left
 * edges, then reversed only when the corner values decrease toward 239/319. */
bool UiManager::finishCalibrationSequence() {
  if (!_touch) return false;

  const int tlx = _calCapRawX[0], tly = _calCapRawY[0];
  const int trx = _calCapRawX[1], tryy = _calCapRawY[1];
  const int blx = _calCapRawX[2], bly = _calCapRawY[2];
  const int brx = _calCapRawX[3], bry = _calCapRawY[3];
  const int crx = _calCapRawX[4], cry = _calCapRawY[4];

  int hx = trx - tlx; if (hx < 0) hx = -hx;
  int hy = tryy - tly; if (hy < 0) hy = -hy;
  int vx = blx - tlx; if (vx < 0) vx = -vx;
  int vy = bly - tly; if (vy < 0) vy = -vy;

  const bool swapH = hy > hx;
  const bool swapV = vx > vy;
  if (swapH != swapV) return false;      /* corner order was not consistent */

  const int hspan = swapH ? hy : hx;
  const int vspan = swapH ? vx : vy;
  if (hspan < 100 || vspan < 140) return false;

  const bool invX = swapH ? (tly > tryy) : (tlx > trx);
  const bool invY = swapH ? (tlx > blx) : (tly > bly);

  /* The centre sample catches a bad corner tap even when the four corners
   * happen to fit one of the eight transforms. */
  const int cxRaw = swapH ? cry : crx;
  const int cyRaw = swapH ? crx : cry;
  const int mx = invX ? (TOUCH_RAW_MAX_X - 1 - cxRaw) : cxRaw;
  const int my = invY ? (TOUCH_RAW_MAX_Y - 1 - cyRaw) : cyRaw;
  const int mappedCx = mx * (SCREEN_W - 1) / (TOUCH_RAW_MAX_X - 1);
  const int mappedCy = my * (SCREEN_H - 1) / (TOUCH_RAW_MAX_Y - 1);
  const int centreErrX = mappedCx > 120 ? mappedCx - 120 : 120 - mappedCx;
  const int centreErrY = mappedCy > 160 ? mappedCy - 160 : 160 - mappedCy;
  if (centreErrX > 30 || centreErrY > 30) return false;

  _touch->setOrientation(swapH, invX, invY);

#if SERIAL_DEBUG
  Serial.printf("[touch] calibrated TL(%d,%d) TR(%d,%d) BL(%d,%d) BR(%d,%d)"
                " C(%d,%d) -> swap=%d invX=%d invY=%d centreErr=(%d,%d)\n",
                tlx, tly, trx, tryy, blx, bly, brx, bry, crx, cry,
                swapH ? 1 : 0, invX ? 1 : 0, invY ? 1 : 0,
                centreErrX, centreErrY);
#endif
  return true;
}

/* -------------------------------------------------------------------------- */
/*  Multi-touch                                                               */
/* -------------------------------------------------------------------------- */
void UiManager::drawMultiTouch() {
  drawBackdrop("MULTI TOUCH", "TWO-POINT CAPACITIVE PANEL");

  _gfx->fillSmoothRoundRect(6, kInfoY, 228, 56, 9, kPanel);
  _gfx->drawRoundRect(6, kInfoY, 228, 56, 9, dim(kPurple, 130));

  _multiPrevActive = 0;
  _multiBaseDist = 0;
  _multiDrawnAt = 0;
  drawMultiArea(true);

  button(124, 278, 110, 30, "BACK", kPurple, false, &fonts::Font2);
}

void UiManager::drawMultiArea(bool force) {
  (void)force;
  TouchPoint p0, p1;
  if (_touch) _touch->getPoints(p0, p1);
  const uint8_t count = _touch ? _touch->activeCount() : 0;

  constexpr int16_t ax = 6, ay = 104, aw = 228, ah = 152;

  _gfx->fillSmoothRoundRect(ax, ay, aw, ah, 8, rgb565(0x04, 0x07, 0x10));
  _gfx->drawRoundRect(ax, ay, aw, ah, 8, dim(kPurple, 110));

  /* grid, scaled by the pinched zoom */
  const int16_t cx = ax + aw / 2;
  const int16_t cy = ay + ah / 2;
  const float stepF = 22.0f * _multiZoom;
  const int16_t step = (int16_t)stepF;
  if (step >= 6) {
    _gfx->setClipRect(ax + 1, ay + 1, aw - 2, ah - 2);
    for (int16_t gx = cx; gx < ax + aw; gx = (int16_t)(gx + step)) {
      _gfx->drawFastVLine(gx, ay + 1, ah - 2, rgb565(0x0E, 0x14, 0x2A));
      if (ax + aw - (gx + step) < step) break;
    }
    for (int16_t gx = (int16_t)(cx - step); gx > ax; gx = (int16_t)(gx - step)) {
      _gfx->drawFastVLine(gx, ay + 1, ah - 2, rgb565(0x0E, 0x14, 0x2A));
    }
    for (int16_t gy = cy; gy < ay + ah; gy = (int16_t)(gy + step)) {
      _gfx->drawFastHLine(ax + 1, gy, aw - 2, rgb565(0x0E, 0x14, 0x2A));
      if (ay + ah - (gy + step) < step) break;
    }
    for (int16_t gy = (int16_t)(cy - step); gy > ay; gy = (int16_t)(gy - step)) {
      _gfx->drawFastHLine(ax + 1, gy, aw - 2, rgb565(0x0E, 0x14, 0x2A));
    }
    _gfx->drawFastVLine(cx, ay + 1, ah - 2, rgb565(0x18, 0x22, 0x40));
    _gfx->drawFastHLine(ax + 1, cy, aw - 2, rgb565(0x18, 0x22, 0x40));
    _gfx->clearClipRect();
  }

  if (count == 0) {
    textAt("PRESS TWO FINGERS", cx, cy - 10, &fonts::Font2, dim(kPurple, 190),
           textdatum_t::middle_center);
    textAt("PINCH IN / OUT TO ZOOM", cx, cy + 10, &fonts::Font0, kMuted,
           textdatum_t::middle_center);
  } else {
    if (count >= 2) {
      _gfx->drawWideLine(p0.x, p0.y, p1.x, p1.y, 1.0f, dim(kCyan, 150));
    }
    const TouchPoint pts[2] = {p0, p1};
    const uint16_t colors[2] = {kCyan, kPurple};
    for (uint8_t i = 0; i < count && i < 2; ++i) {
      _gfx->drawCircle(pts[i].x, pts[i].y, 20, dim(colors[i], 130));
      _gfx->drawCircle(pts[i].x, pts[i].y, 14, colors[i]);
      _gfx->fillCircle(pts[i].x, pts[i].y, 6, colors[i]);
      _gfx->fillCircle(pts[i].x, pts[i].y, 2, kWhite);
      char buf[4];
      snprintf(buf, sizeof(buf), "%u", i + 1);
      textAt(buf, pts[i].x, (int16_t)(pts[i].y - 26), &fonts::Font0, colors[i],
             textdatum_t::middle_center);
    }
  }

  /* info rows */
  char buf[32];
  snprintf(buf, sizeof(buf), "MAX POINTS   %u", _touch ? _touch->maxPointsSeen() : 0);
  textAt(buf, 14, 46, &fonts::Font2, kText, textdatum_t::top_left);
  snprintf(buf, sizeof(buf), "ACTIVE       %u", count);
  textAt(buf, 14, 64, &fonts::Font2, count ? kCyan : kMuted, textdatum_t::top_left);
  snprintf(buf, sizeof(buf), "ZOOM  %.2fX", _multiZoom);
  textAt(buf, 14, 82, &fonts::Font2, kPurple, textdatum_t::top_left);
}

void UiManager::updateMultiTouch(uint32_t now) {
  TouchPoint p0, p1;
  if (!_touch) return;
  _touch->getPoints(p0, p1);
  const uint8_t count = _touch->activeCount();

  if (count >= 2) {
    const int16_t dx = p0.x - p1.x;
    const int16_t dy = p0.y - p1.y;
    const int16_t dist = (int16_t)sqrtf((float)dx * dx + (float)dy * dy);
    if (_multiBaseDist == 0 || _multiPrevActive < 2) {
      _multiBaseDist = dist > 8 ? dist : 8;
      _multiZoom = 1.0f;
    } else {
      float z = (float)dist / _multiBaseDist;
      if (z < 0.35f) z = 0.35f;
      if (z > 2.6f) z = 2.6f;
      _multiZoom = z;
    }
  } else if (count < 2) {
    _multiBaseDist = 0;
  }

  const bool changed = count != _multiPrevActive;
  _multiPrevActive = count;
  if ((count > 0 && now - _multiDrawnAt >= 40) || changed) {
    drawMultiArea(true);
    _multiDrawnAt = now;
  }
}

/* -------------------------------------------------------------------------- */
/*  Gesture test                                                              */
/* -------------------------------------------------------------------------- */
int8_t UiManager::gestureIndexFor(TouchEventType t) const {
  switch (t) {
    case TouchEventType::TAP: return 0;
    case TouchEventType::DOUBLE_TAP: return 1;
    case TouchEventType::LONG_PRESS: return 2;
    case TouchEventType::DRAG_START: return 3;
    case TouchEventType::SWIPE_LEFT: return 4;
    case TouchEventType::SWIPE_RIGHT: return 5;
    case TouchEventType::SWIPE_UP: return 6;
    case TouchEventType::SWIPE_DOWN: return 7;
    default: return -1;
  }
}

void UiManager::setGesture(int8_t index, uint16_t count) {
  _gestureIndex = index;
  _gestureCount = count;
  _gestureDetectedAt = millis();
  drawGestureTest();
}

void UiManager::drawGestureTest() {
  drawBackdrop("GESTURE TEST", "TRY EACH GESTURE ON THE PANEL");

  _gfx->fillSmoothRoundRect(12, 42, 216, 86, 9, kPanel);
  const uint16_t accent = _gestureIndex >= 0 ? kMenuAccent[_gestureIndex < 4 ? 0 : 2] : dim(kCyan, 120);
  _gfx->drawRoundRect(12, 42, 216, 86, 9, accent);
  textAt("DETECTED", 120, 47, &fonts::Font0, kMuted, textdatum_t::top_center);
  textAt(_gestureIndex >= 0 ? kGestureNames[_gestureIndex] : "WAITING",
         120, 58, &fonts::Font4, _gestureIndex >= 0 ? kText : dim(kText, 100),
         textdatum_t::top_center);

  /* small gesture pictogram */
  const int16_t ix = 120, iy = 106;
  if (_gestureIndex == 0 || _gestureIndex == 1) {
    _gfx->drawCircle(ix - (_gestureIndex ? 10 : 0), iy, 6, kCyan);
    _gfx->fillCircle(ix - (_gestureIndex ? 10 : 0), iy, 2, kCyan);
    if (_gestureIndex == 1) {
      _gfx->drawCircle(ix + 10, iy, 6, kPurple);
      _gfx->fillCircle(ix + 10, iy, 2, kPurple);
    }
  } else if (_gestureIndex == 2) {
    _gfx->drawCircle(ix, iy, 12, kPurple);
    _gfx->drawCircle(ix, iy, 8, kPurple);
    _gfx->fillCircle(ix, iy, 3, kPurple);
  } else if (_gestureIndex >= 3) {
    int16_t x0 = ix - 22, y0 = iy, x1 = ix + 22, y1 = iy;
    if (_gestureIndex == 3) { x0 = ix - 22; y0 = iy + 8; x1 = ix + 20; y1 = iy - 8; }
    if (_gestureIndex == 4) { x0 = ix + 22; x1 = ix - 22; }
    if (_gestureIndex == 6) { x0 = ix; y0 = iy + 12; x1 = ix; y1 = iy - 12; }
    if (_gestureIndex == 7) { x0 = ix; y0 = iy - 12; x1 = ix; y1 = iy + 12; }
    const uint16_t c = (_gestureIndex == 3) ? kYellow : kCyan;
    _gfx->drawWideLine(x0, y0, x1, y1, 1.4f, c);
    if (_gestureIndex == 3) {
      _gfx->fillCircle(x1, y1, 4, c);
    } else {
      _gfx->drawLine(x1, y1, (int16_t)(x1 - 5), (int16_t)(y1 + 5), c);
      _gfx->drawLine(x1, y1, (int16_t)(x1 + (x1 > x0 ? -5 : 5)), (int16_t)(y1 - 5), c);
      if (_gestureIndex >= 4) {
        _gfx->drawLine(x1, y1, (int16_t)(x1 + (x1 > x0 ? -5 : 5)), (int16_t)(y1 + 5), c);
      }
    }
  }

  for (uint8_t i = 0; i < 8; ++i) {
    const int16_t x = (i & 1) ? 124 : 12;
    const int16_t y = (int16_t)(136 + (i >> 1) * 32);
    const bool on = (i == _gestureIndex);
    _gfx->fillSmoothRoundRect(x, y, 104, 24, 12, on ? dim(kMenuAccent[i < 4 ? 0 : 2], 70) : kPanel);
    _gfx->drawRoundRect(x, y, 104, 24, 12, on ? kCyan : dim(kCyan, 80));
    textAt(kGestureNames[i], (int16_t)(x + 52), (int16_t)(y + 12), &fonts::Font0,
           on ? kWhite : kMuted, textdatum_t::middle_center);
  }

  char buf[20];
  snprintf(buf, sizeof(buf), "GESTURES  %u", (unsigned)_gestureCount);
  textAt(buf, 12, 266, &fonts::Font0, dim(kText, 140), textdatum_t::top_left);
  textAt("SWIPE ANYWHERE", 232, 266, &fonts::Font0, dim(kCyan, 140),
         textdatum_t::top_right);
  textAt("DRAG IS DETECTED AFTER 12 PX", 120, 282, &fonts::Font0, kMuted,
         textdatum_t::top_center);
}

/* -------------------------------------------------------------------------- */
/*  Painting                                                                  */
/* -------------------------------------------------------------------------- */
void UiManager::drawPaintScreen() {
  drawBackdrop("TOUCH PAINT", "DRAW WITH YOUR FINGER");
  drawPaintToolbar();
  drawPaintCanvasBg();
  button(6, 278, 110, 30, "CLEAR", kYellow, false, &fonts::Font2);
  button(124, 278, 110, 30, "BACK", kCyan, false, &fonts::Font2);
}

void UiManager::drawPaintToolbar() {
  _gfx->fillRect(0, 32, SCREEN_W, 54, kBg);

  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t x = (int16_t)(8 + i * 27);
    const bool sel = (_paintColor == kPaintColors[i]);
    _gfx->fillSmoothRoundRect(x, 36, 24, 24, 7, kPaintColors[i]);
    _gfx->drawRoundRect(x, 36, 24, 24, 7, sel ? kWhite : dim(kPaintColors[i], 120));
    if (sel) _gfx->drawRoundRect(x + 2, 38, 20, 20, 5, kWhite);
  }

  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t x = (int16_t)(160 + i * 26);
    const bool sel = (_paintSize == kPaintSizes[i]);
    _gfx->fillSmoothRoundRect(x, 36, 22, 22, 11, sel ? kPanelHi : kPanel);
    _gfx->drawRoundRect(x, 36, 22, 22, 11, sel ? kCyan : dim(kCyan, 90));
    _gfx->fillCircle((int16_t)(x + 11), 47, kPaintSizes[i] < 6 ? 2 : (kPaintSizes[i] < 10 ? 3 : 4),
                     sel ? kWhite : kMuted);
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "BRUSH %u PX", _paintSize);
  textAt(buf, 8, 68, &fonts::Font0, dim(kText, 150), textdatum_t::top_left);
  textAt("COLOUR        SIZE", 232, 68, &fonts::Font0, dim(kCyan, 140),
         textdatum_t::top_right);
}

void UiManager::drawPaintCanvasBg() {
  _gfx->fillSmoothRoundRect(kPaintCanvasX, kPaintCanvasY, kPaintCanvasW, kPaintCanvasH,
                            8, rgb565(0x05, 0x08, 0x11));
  _gfx->setClipRect(kPaintCanvasX + 1, kPaintCanvasY + 1, kPaintCanvasW - 2, kPaintCanvasH - 2);
  for (int16_t x = kPaintCanvasX + 20; x < kPaintCanvasX + kPaintCanvasW; x += 24) {
    _gfx->drawFastVLine(x, kPaintCanvasY + 1, kPaintCanvasH - 2, rgb565(0x0A, 0x10, 0x20));
  }
  for (int16_t y = kPaintCanvasY + 20; y < kPaintCanvasY + kPaintCanvasH; y += 24) {
    _gfx->drawFastHLine(kPaintCanvasX + 1, y, kPaintCanvasW - 2, rgb565(0x0A, 0x10, 0x20));
  }
  for (uint16_t i = 0; i < 26; ++i) {
    const uint32_t h = hashU32(i * 92837111u + 5);
    _gfx->drawPixel((int16_t)(kPaintCanvasX + 4 + (h % (kPaintCanvasW - 8))),
                    (int16_t)(kPaintCanvasY + 4 + ((h >> 8) % (kPaintCanvasH - 8))),
                    rgb565(0x30, 0x3E, 0x58));
  }
  _gfx->clearClipRect();
  _gfx->drawRoundRect(kPaintCanvasX, kPaintCanvasY, kPaintCanvasW, kPaintCanvasH, 8,
                      dim(kCyan, 100));
}

bool UiManager::inPaintCanvas(int16_t x, int16_t y) const {
  return x >= kPaintCanvasX + 2 && x < kPaintCanvasX + kPaintCanvasW - 2 &&
         y >= kPaintCanvasY + 2 && y < kPaintCanvasY + kPaintCanvasH - 2;
}

int8_t UiManager::hitPaintTool(int16_t x, int16_t y) const {
  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t bx = (int16_t)(8 + i * 27);
    if (x >= bx && x < bx + 24 && y >= 36 && y < 60) return (int8_t)i;
  }
  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t bx = (int16_t)(160 + i * 26);
    if (x >= bx && x < bx + 22 && y >= 36 && y < 58) return (int8_t)(5 + i);
  }
  return -1;
}

void UiManager::selectPaintTool(uint8_t tool) {
  if (tool < 5) {
    _paintColor = kPaintColors[tool];
  } else if (tool < 8) {
    _paintSize = kPaintSizes[tool - 5];
  }
  drawPaintToolbar();
}

void UiManager::paintDot(int16_t x, int16_t y) {
  _gfx->setClipRect(kPaintCanvasX + 1, kPaintCanvasY + 1, kPaintCanvasW - 2, kPaintCanvasH - 2);
  _gfx->fillSmoothCircle(x, y, (int16_t)(_paintSize / 2 + 3), dim(_paintColor, 45));
  _gfx->fillSmoothCircle(x, y, (int16_t)(_paintSize / 2), _paintColor);
  _gfx->fillCircle(x, y, 1, kWhite);
  _gfx->clearClipRect();
}

void UiManager::paintSegment(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  _gfx->setClipRect(kPaintCanvasX + 1, kPaintCanvasY + 1, kPaintCanvasW - 2, kPaintCanvasH - 2);
  _gfx->drawWideLine(x0, y0, x1, y1, (float)(_paintSize / 2 + 3), dim(_paintColor, 45));
  _gfx->drawWideLine(x0, y0, x1, y1, (float)(_paintSize / 2), _paintColor);
  _gfx->clearClipRect();
}

/* -------------------------------------------------------------------------- */
/*  Events                                                                    */
/* -------------------------------------------------------------------------- */
void UiManager::handleEvent(const TouchEvent& ev) {
  if (!_gfx) return;

  switch (_screen) {
    case Screen::MENU: {
      switch (ev.type) {
        case TouchEventType::DOWN: {
          uint8_t idx;
          if (hitMenuButton(ev.x, ev.y, &idx)) {
            _menuPressed = (int8_t)idx;
            markMenuDirty(idx, true);
          } else if (hitBack(ev.x, ev.y)) {
            _backPressed = true;
            drawBackButton(true);
          }
          break;
        }
        case TouchEventType::UP:
          if (_menuPressed >= 0) {
            const uint8_t idx = (uint8_t)_menuPressed;
            _menuPressed = -1;
            markMenuDirty(idx, false);
          }
          if (_backPressed) {
            _backPressed = false;
            drawBackButton(false);
          }
          break;
        case TouchEventType::TAP: {
          uint8_t idx;
          if (hitMenuButton(ev.x, ev.y, &idx)) {
            switch (idx) {
              case 0: enter(Screen::TOUCH_TEST); break;
              case 1: enter(Screen::MULTITOUCH); break;
              case 2: enter(Screen::GESTURE_TEST); break;
              case 3: enter(Screen::PAINT); break;
              default: enterGalaxy(); break;
            }
          } else if (hitBack(ev.x, ev.y)) {
            enterGalaxy();
          }
          break;
        }
        default: break;
      }
      break;
    }

    case Screen::TOUCH_TEST: {
      if (ev.type == TouchEventType::TAP &&
          ev.x >= 130 && ev.x < 182 && ev.y >= 29 && ev.y < 51) {
        enterGalaxy();
        break;
      }
      if (ev.type == TouchEventType::TAP &&
          ev.x >= 58 && ev.x < 110 && ev.y >= 29 && ev.y < 51) {
        startCalibrationSequence();
        break;
      }

      switch (ev.type) {
        case TouchEventType::DOWN:
        case TouchEventType::MOVE:
          _calLiveX = ev.x;
          _calLiveY = ev.y;
          _calLiveRawX = ev.rawX;
          _calLiveRawY = ev.rawY;
          drawCalibrationInfo();
          break;
        case TouchEventType::TAP: {
          if (_calAuto) {
            if (_calStep < 5) {
              _calCapRawX[_calStep] = ev.rawX;
              _calCapRawY[_calStep] = ev.rawY;
              _calHit[_calStep] = true;
              ++_calStep;
            }
            if (_calStep >= 5) {
              _calApplied = finishCalibrationSequence();
              _calAuto = false;
              _calHits = 0;
              memset(_calHit, 0, sizeof(_calHit));
            }
            drawCalibration();
          } else {
            const int8_t target = hitCalTarget(ev.x, ev.y);
            if (target >= 0) {
              if (!_calHit[target]) {
                _calHit[target] = true;
                ++_calHits;
              }
              _calTouchX[target] = ev.x;
              _calTouchY[target] = ev.y;
              _calRawX[target] = ev.rawX;
              _calRawY[target] = ev.rawY;
              _calLiveX = ev.x;
              _calLiveY = ev.y;
              _calLiveRawX = ev.rawX;
              _calLiveRawY = ev.rawY;
              drawCalibration();
            } else {
              _calLiveX = ev.x;
              _calLiveY = ev.y;
              _calLiveRawX = ev.rawX;
              _calLiveRawY = ev.rawY;
              drawCalibrationInfo();
            }
          }
          break;
        }
        default:
          break;
      }
      break;
    }

    case Screen::MULTITOUCH: {
      if (ev.type == TouchEventType::TAP) {
        if (hitBack(ev.x, ev.y) || (ev.x >= 124 && ev.x < 234 && ev.y >= 278 && ev.y < 308)) {
          enter(Screen::MENU);
        }
      }
      break;
    }

    case Screen::GESTURE_TEST: {
      if (ev.type == TouchEventType::TAP && (hitBack(ev.x, ev.y))) {
        enter(Screen::MENU);
        return;
      }
      const int8_t idx = gestureIndexFor(ev.type);
      if (idx >= 0) {
        setGesture(idx, (uint16_t)(_gestureCount + 1));
      }
      break;
    }

    case Screen::PAINT: {
      const bool inCanvas = inPaintCanvas(ev.x, ev.y);
      switch (ev.type) {
        case TouchEventType::DOWN:
          if (inCanvas) {
            _painting = true;
            _paintLastX = ev.x;
            _paintLastY = ev.y;
            paintDot(ev.x, ev.y);
          }
          break;
        case TouchEventType::MOVE:
          if (_painting && inCanvas) {
            if (_paintLastX >= 0) {
              paintSegment(_paintLastX, _paintLastY, ev.x, ev.y);
            } else {
              paintDot(ev.x, ev.y);
            }
            _paintLastX = ev.x;
            _paintLastY = ev.y;
          } else if (!inCanvas) {
            _paintLastX = _paintLastY = -1;
          }
          break;
        case TouchEventType::UP:
          _painting = false;
          _paintLastX = _paintLastY = -1;
          break;
        case TouchEventType::DRAG_START:
          if (inCanvas) {
            _painting = true;
            _paintLastX = ev.x;
            _paintLastY = ev.y;
          }
          break;
        case TouchEventType::TAP: {
          if (hitBack(ev.x, ev.y) || (ev.x >= 124 && ev.x < 234 && ev.y >= 278 && ev.y < 308)) {
            enter(Screen::MENU);
            break;
          }
          if (ev.x >= 6 && ev.x < 116 && ev.y >= 278 && ev.y < 308) {
            drawPaintCanvasBg();
            break;
          }
          const int8_t tool = hitPaintTool(ev.x, ev.y);
          if (tool >= 0) selectPaintTool((uint8_t)tool);
          break;
        }
        default:
          break;
      }
      break;
    }

    default:
      break;
  }
}

void UiManager::update(uint32_t now) {
  switch (_screen) {
    case Screen::MULTITOUCH:
      updateMultiTouch(now);
      break;
    default:
      break;
  }
}

/* -------------------------------------------------------------------------- */
/*  Galaxy HUD                                                                */
/* -------------------------------------------------------------------------- */
int8_t UiManager::hitHudButton(int16_t x, int16_t y) const {
  if (y < kBtnY || y >= kBtnY + kBtnH) return -1;
  for (uint8_t i = 0; i < 3; ++i) {
    if (x >= kBtnX[i] && x < kBtnX[i] + kBtnW) return (int8_t)i;
  }
  return -1;
}

void UiManager::activateHudButton(uint8_t index) {
  if (!_galaxy || !_effects) return;
  switch (index) {
    case 0:
      _galaxy->reset(false);
      _effects->setLabel("VIEW CENTERED", kCyan, 1100);
      break;
    case 1:
      _effects->endBlackHole();
      _galaxy->setGravity(false, 0, 0);
      enter(Screen::MENU);
      break;
    case 2:
      _galaxy->reset(true);
      _effects->reset();
      _effects->setLabel("GALAXY RESET", kYellow, 1300);
      break;
    default:
      break;
  }
}

void UiManager::drawHudButton(SpaceCanvas& c, uint8_t index, bool pressed, uint32_t now) {
  (void)now;
  const int16_t x = kBtnX[index];
  const uint16_t accent = kBtnAccent[index];
  c.blendRoundRect(x, kBtnY, kBtnW, kBtnH, 8, pressed ? dim(accent, 55) : rgb565(0x08, 0x0E, 0x1A), 232);
  c.strokeRoundRect(x, kBtnY, kBtnW, kBtnH, 8, accent, pressed ? 255 : 120);
  c.addHLine((int16_t)(x + 14), (int16_t)(kBtnY + 1), 46, accent, pressed ? 230 : 80);
  c.drawText((int16_t)(x + kBtnW / 2), 292, kBtnText[index], 1,
             pressed ? kWhite : accent, pressed ? 255 : 220, TextAlign::CENTER);
  if (pressed) {
    c.addGlow((int16_t)(x + kBtnW / 2), (int16_t)(kBtnY + kBtnH / 2), 34, accent, 40);
  }
}

bool UiManager::handleGalaxyTouch(const TouchEvent& ev) {
  const bool inBar = ev.y >= kBarY;

  switch (ev.type) {
    case TouchEventType::DOWN:
      _hudCaptured = inBar;
      if (inBar) {
        _hudPressed = hitHudButton(ev.x, ev.y);
        return true;
      }
      return false;

    case TouchEventType::TAP:
      if (_hudCaptured || inBar) {
        const int8_t btn = hitHudButton(ev.x, ev.y);
        _hudPressed = -1;
        _hudCaptured = false;
        if (btn >= 0) activateHudButton((uint8_t)btn);
        return true;
      }
      return false;

    case TouchEventType::UP:
      if (_hudCaptured) {
        _hudCaptured = false;
        _hudPressed = -1;
        return true;
      }
      return false;

    case TouchEventType::MOVE:
    case TouchEventType::DRAG:
    case TouchEventType::DRAG_START:
    case TouchEventType::DRAG_END:
    case TouchEventType::LONG_PRESS:
    case TouchEventType::LONG_RELEASE:
    case TouchEventType::DOUBLE_TAP:
    case TouchEventType::PINCH_START:
    case TouchEventType::PINCH:
    case TouchEventType::PINCH_END:
      return _hudCaptured || inBar;

    default:
      return false;
  }
}

void UiManager::drawGalaxyHud(SpaceCanvas& c, uint32_t now, uint16_t fps, bool debug) {
  /* bottom panel */
  c.blendRect(0, kBarY, SCREEN_W, HUD_BAR_H, rgb565(0x02, 0x04, 0x0A), 214);
  for (int16_t i = 0; i < 5; ++i) {
    c.blendRect(0, (int16_t)(kBarY + i), SCREEN_W, 1, rgb565(0x22, 0x60, 0x90), (uint8_t)(26 - i * 4));
  }
  c.addHLine(0, kBarY, SCREEN_W, kCyan, 90);

  for (uint8_t i = 0; i < 3; ++i) {
    drawHudButton(c, i, _hudPressed == (int8_t)i, now);
  }

  /* title */
  c.blendRoundRect(4, 4, 196, 26, 9, rgb565(0x02, 0x05, 0x0C), 110);
  c.drawText(10, 8, "SOLAR SYSTEM", 2, rgb565(0xD2, 0xEC, 0xFF), 205);
  c.addHLine(10, 24, 182, kCyan, 34);
  c.drawText(10, 29, "TAP A PLANET TO LAND", 1, rgb565(0x8F, 0xB8, 0xD8), 135);

  /* touch hint icon, pulsing */
  const int16_t pulse = (int16_t)((fx::sinT((uint16_t)(now >> 4)) * 2) >> 10);
  c.addCircleOutline(222, 17, (int16_t)(6 + pulse), kCyan, 120);
  c.addCircleOutline(222, 17, 3, kCyan, 180);
  c.addPixel(222, 17, kWhite, 230);

  /* toast label */
  const uint8_t la = _effects ? _effects->labelAlpha(now) : 0;
  if (la) {
    const char* label = _effects->label();
    const int16_t w = (int16_t)(SpaceCanvas::textWidth(label, 2) + 22);
    c.blendRoundRect((int16_t)(120 - w / 2), 242, w, 22, 11, rgb565(0x01, 0x03, 0x08),
                     (uint8_t)((uint16_t)la * 150 >> 8));
    c.drawText(120, 247, label, 2, _effects->labelColor(), la, TextAlign::CENTER);
  }

  if (debug) {
    char buf[40];
    snprintf(buf, sizeof(buf), "FPS %u   PARTICLES %u", fps, (unsigned)GALAXY_PARTICLE_COUNT);
    c.drawText(8, 42, buf, 1, kYellow, 210);
    TouchPoint p0, p1;
    if (_touch) _touch->getPoints(p0, p1);
    const uint8_t n = _touch ? _touch->activeCount() : 0;
    snprintf(buf, sizeof(buf), "TOUCH %u  %d,%d  ZOOM %d%%", n, p0.x, p0.y,
             (int)(_galaxy ? _galaxy->zoom() * 100.0f : 100.0f));
    c.drawText(8, 52, buf, 1, kYellow, 190);
  }
}

/* -------------------------------------------------------------------------- */
/*  Splash                                                                    */
/* -------------------------------------------------------------------------- */
void UiManager::drawSplash(SpaceCanvas& c, uint32_t now, uint32_t startMs) {
  const uint32_t elapsed = now - startMs;
  const float t = fminf(1.0f, (float)elapsed / 2600.0f);

  c.fill(0x0000);

  /* nebula glows */
  const uint8_t nebA = (uint8_t)(40 + 30 * t);
  c.addGlow(120, 150, 128, rgb565(0x2A, 0x1C, 0x60), nebA);
  c.addGlow(70, 210, 96, rgb565(0x10, 0x32, 0x58), (uint8_t)(nebA / 2));
  c.addGlow(190, 80, 96, rgb565(0x24, 0x14, 0x48), 40);

  /* deterministic star field */
  for (uint16_t i = 0; i < 130; ++i) {
    const uint32_t h = hashU32(i * 2246822519u + 7);
    const int16_t x = (int16_t)(h % SCREEN_W);
    const int16_t y = (int16_t)((h >> 9) % SCREEN_H);
    const int16_t tw = fx::sinT((uint16_t)((h >> 4) + (now >> 4)));
    int a = 60 + ((h >> 18) % 130) + (tw >> 4);
    if (a < 30) a = 30;
    if (a > 235) a = 235;
    c.addPixel(x, y, ((h >> 24) & 1) ? rgb565(0xBB, 0xD4, 0xFF) : 0xFFFF, (uint8_t)a);
    if ((h & 0x7F) == 0) {
      c.addGlow(x, y, 5, rgb565(0xA8, 0xD8, 0xFF), (uint8_t)(a / 3));
    }
  }

  /* rotating orbit ring behind the logo */
  const uint16_t ringAngle = (uint16_t)(now >> 3);
  for (uint8_t arc = 0; arc < 3; ++arc) {
    const int16_t radius = (int16_t)(54 + arc * 16 + t * 14);
    const uint16_t accent = (arc == 0) ? kCyan : (arc == 1 ? kPurple : kYellow);
    for (uint8_t seg = 0; seg < 52; ++seg) {
      const uint16_t a = (uint16_t)(ringAngle * (arc + 1) + seg * (fx::ANGLE_STEPS / 52));
      const int16_t x = (int16_t)(120 + fx::mulQ10(fx::cosT(a), radius * 1024));
      const int16_t y = (int16_t)(146 + fx::mulQ10(fx::sinT(a), radius * 1024));
      c.addPixel(x, y, accent, (uint8_t)(50 + arc * 12));
    }
  }

  /* fade the logo in and out */
  float fade = 1.0f;
  if (elapsed < 420) fade = elapsed / 420.0f;
  if (t > 0.88f) fade = fmaxf(0.0f, 1.0f - (t - 0.88f) / 0.12f);
  const uint8_t alpha = (uint8_t)(fade * 255.0f);

  if (alpha > 0) {
    c.drawText(120, 108, "OCEANLABZ", 3, rgb565(0xEA, 0xF6, 0xFF), alpha, TextAlign::CENTER);
    c.addHLine(48, 136, 144, kCyan, (uint8_t)(alpha / 2));
    c.addGlow(120, 136, 5, kCyan, (uint8_t)(alpha * 0.7f));
    c.drawText(120, 150, "SOLAR SYSTEM", 2, rgb565(0xA8, 0xD8, 0xFF), alpha,
               TextAlign::CENTER);
    c.drawText(120, 178, "EIGHT WORLDS TO EXPLORE", 1, rgb565(0x66, 0x86, 0xA8),
               (uint8_t)(alpha * 0.7f), TextAlign::CENTER);
  }

  /* progress */
  const uint16_t barW = 124;
  c.blendRect((int16_t)(120 - barW / 2), 284, barW, 2, rgb565(0x14, 0x22, 0x3C), 200);
  const int16_t prog = (int16_t)(barW * t);
  c.blendRect((int16_t)(120 - barW / 2), 284, prog, 2, kCyan, 210);
  c.addGlow((int16_t)(120 - barW / 2 + prog), 285, 8, kCyan, 120);
  c.drawText(120, 296, "TOUCH TO ENTER", 1, rgb565(0x5E, 0x7C, 0x9A), 130,
             TextAlign::CENTER);
}
