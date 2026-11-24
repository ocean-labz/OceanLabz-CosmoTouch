/* ==========================================================================
 * planet_scene.cpp  -  Planet approach + detailed globe exploration
 * --------------------------------------------------------------------------
 * The globe is sampled directly from the 256x128 equirectangular surface
 * already stored in PSRAM. No new texture is allocated when a planet opens.
 * ========================================================================== */
#include "planet_scene.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t kBlack  = SPACE_BG;
constexpr uint16_t kWhite  = 0xFFFF;
constexpr uint16_t kCyan   = rgb565(0x54, 0xE6, 0xFF);
constexpr uint16_t kGold   = rgb565(0xFF, 0xD4, 0x5E);
constexpr uint16_t kOrange = rgb565(0xFF, 0x8A, 0x4C);
constexpr uint16_t kRed    = rgb565(0xFF, 0x4A, 0x30);
constexpr uint16_t kGreen  = rgb565(0x5E, 0xFF, 0xB8);
constexpr uint16_t kBlue   = rgb565(0x68, 0x9A, 0xFF);

constexpr uint32_t APPROACH_MS = 1500;
constexpr uint32_t EXIT_MS = 900;
constexpr int16_t GLOBE_CX = 120;
constexpr int16_t GLOBE_CY = 101;
constexpr int16_t GLOBE_R = 82;

constexpr int16_t BACK_X = 45;
constexpr int16_t BACK_Y = 288;
constexpr int16_t BACK_W = 150;
constexpr int16_t BACK_H = 24;

inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline uint8_t r8(uint16_t c) { return (uint8_t)(((c >> 11) & 31) * 255 / 31); }
inline uint8_t g8(uint16_t c) { return (uint8_t)(((c >> 5) & 63) * 255 / 63); }
inline uint8_t b8(uint16_t c) { return (uint8_t)((c & 31) * 255 / 31); }

inline uint16_t mix565(uint16_t a, uint16_t b, float t) {
  t = clampf(t, 0.0f, 1.0f);
  return rgb565((uint8_t)(r8(a) + (r8(b) - r8(a)) * t),
                (uint8_t)(g8(a) + (g8(b) - g8(a)) * t),
                (uint8_t)(b8(a) + (b8(b) - b8(a)) * t));
}

inline uint16_t shade565(uint16_t c, int shade) {
  if (shade < 0) shade = 0;
  if (shade > 255) shade = 255;
  const uint16_t r = (uint16_t)(((c >> 11) & 31) * shade / 255);
  const uint16_t g = (uint16_t)(((c >> 5) & 63) * shade / 255);
  const uint16_t b = (uint16_t)((c & 31) * shade / 255);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

inline uint32_t hashU32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

uint16_t specialColor(PlanetKind k) {
  switch (k) {
    case PlanetKind::MERCURY: return rgb565(0xFF, 0xE0, 0x9A);
    case PlanetKind::VENUS:   return rgb565(0xFF, 0xD8, 0x72);
    case PlanetKind::EARTH:   return kGreen;
    case PlanetKind::MARS:    return kOrange;
    case PlanetKind::JUPITER: return rgb565(0xF0, 0x8A, 0x60);
    case PlanetKind::SATURN:  return kGold;
    case PlanetKind::URANUS:  return rgb565(0x9C, 0xFF, 0xF0);
    case PlanetKind::NEPTUNE:
    default:                  return kBlue;
  }
}

const char* specialName(PlanetKind k) {
  switch (k) {
    case PlanetKind::MERCURY: return "SOLAR FLARE";
    case PlanetKind::VENUS:   return "ACID CLOUD SURGE";
    case PlanetKind::EARTH:   return "AURORA SURGE";
    case PlanetKind::MARS:    return "DUST STORM RISING";
    case PlanetKind::JUPITER: return "GREAT RED SPOT STORM";
    case PlanetKind::SATURN:  return "RING SHIMMER";
    case PlanetKind::URANUS:  return "METHANE AURORA";
    case PlanetKind::NEPTUNE:
    default:                  return "SUPERSONIC WINDS";
  }
}

} // namespace

/* -------------------------------------------------------------------------- */
/*  Setup                                                                     */
/* -------------------------------------------------------------------------- */
bool PlanetScene::begin(SpaceCanvas* canvas, GalaxyEngine* galaxy) {
  _canvas = canvas;
  _galaxy = galaxy;
  if (!_canvas || !_galaxy) return false;

  /* Orthographic sphere lookup tables. Longitude and latitude depend only on
   * the normalised screen offset, so the per-pixel loop stays cheap. */
  for (int i = 0; i < 513; ++i) {
    float t = (i - 256) / 256.0f;
    t = clampf(t, -1.0f, 1.0f);
    const float z = sqrtf(fmaxf(0.0f, 1.0f - t * t));
    int u = (int)((atan2f(t, z) / (2.0f * (float)M_PI) + 0.5f) * 256.0f);
    if (u < 0) u += 256;
    if (u > 255) u -= 256;
    _lonLUT[i] = (int16_t)u;

    const float lat = asinf(t);
    int v = (int)((lat / (float)M_PI + 0.5f) * GalaxyEngine::PLANET_TEX_H);
    if (v < 0) v = 0;
    if (v >= GalaxyEngine::PLANET_TEX_H) v = GalaxyEngine::PLANET_TEX_H - 1;
    _latLUT[i] = (int16_t)v;
  }
  return true;
}

bool PlanetScene::start(uint8_t index, uint32_t now) {
  if (_state != State::OFF || !_canvas || !_galaxy) return false;
  if (index >= PLANET_COUNT || !_galaxy->planetSurface(index)) return false;

  _planet = index;
  _state = State::APPROACH;
  _startMs = now;
  _exploreMs = 0;
  _galaxy->planetScreenPos(index, _startX, _startY);
  _startR = _galaxy->planetRadius(index);

  _rot = (float)(fx::rand() & 255);
  _pitch = 0;
  _zoom = _zoomTarget = 1.0f;
  _pinchAccum = 1.0f;
  _dragging = false;
  _dragX = _dragY = 0;
  _backPressed = false;

  memset(_markers, 0, sizeof(_markers));
  memset(_sparks, 0, sizeof(_sparks));
  _specialStart = _specialUntil = 0;
  _toast[0] = 0;
  _toastUntil = 0;
#if SERIAL_DEBUG
  Serial.printf("[PLANET] selected id=%u '%s' from (%d,%d) r=%d\n",
                (unsigned)index, _galaxy->planetInfo(index).name,
                (int)_startX, (int)_startY, (int)_startR);
  Serial.printf("[PLANET] state=APPROACH zoom started, heap=%lu largest=%lu\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
  return true;
}

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */
PlanetKind PlanetScene::kind() const {
  return _galaxy ? _galaxy->planetKind(_planet) : PlanetKind::EARTH;
}

uint16_t PlanetScene::accent() const {
  return _galaxy ? _galaxy->planetAccent(_planet) : kCyan;
}

int16_t PlanetScene::globeRadius() const {
  int16_t r = (int16_t)(GLOBE_R * _zoom);
  if (r > 210) r = 210;
  return r;
}

bool PlanetScene::insideGlobe(int16_t x, int16_t y) const {
  const int16_t r = globeRadius();
  const int32_t dx = x - GLOBE_CX;
  const int32_t dy = y - GLOBE_CY;
  return dx * dx + dy * dy <= (int32_t)r * r;
}

bool PlanetScene::hitBack(int16_t x, int16_t y) const {
  return x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H;
}

void PlanetScene::setToast(const char* text, uint16_t color, uint16_t ms) {
  strncpy(_toast, text, sizeof(_toast) - 1);
  _toast[sizeof(_toast) - 1] = 0;
  _toastColor = color;
  _toastUntil = millis() + ms;
}

/* -------------------------------------------------------------------------- */
/*  Particles                                                                 */
/* -------------------------------------------------------------------------- */
void PlanetScene::spawnSparks(uint8_t count, uint16_t colorA, uint16_t colorB) {
  uint8_t spawned = 0;
  for (uint8_t i = 0; i < sizeof(_sparks) / sizeof(_sparks[0]) && spawned < count; ++i) {
    Spark& s = _sparks[i];
    if (s.life) continue;
    const uint32_t h = hashU32(fx::rand() ^ (i * 2654435761u) ^ (uint32_t)_planet);
    const float a = (h & 511) * (2.0f * (float)M_PI / 512.0f);
    const float spd = 18.0f + (float)((h >> 9) % 55u);
    const float rr = 25.0f + (float)((h >> 3) % 60u);
    s.x = GLOBE_CX + cosf(a) * rr;
    s.y = GLOBE_CY + sinf(a) * rr;
    s.vx = cosf(a) * spd;
    s.vy = sinf(a) * spd;
    s.gravity = 7.0f;
    s.maxLife = (uint16_t)(700 + (h >> 18) % 900u);
    s.life = s.maxLife;
    s.color = (h & 1) ? colorA : colorB;
    s.size = (h & 3) == 0 ? 2 : 1;
    ++spawned;
  }
}

void PlanetScene::updateSparks(float dt) {
  const float dtMs = dt * 1000.0f;
  for (uint8_t i = 0; i < sizeof(_sparks) / sizeof(_sparks[0]); ++i) {
    Spark& s = _sparks[i];
    if (!s.life) continue;
    if (s.life > dtMs) s.life = (uint16_t)(s.life - dtMs);
    else {
      s.life = 0;
      continue;
    }
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.vy += s.gravity * dt;
    s.vx *= 0.988f;
  }
}

void PlanetScene::triggerSpecial(uint32_t now) {
  _specialStart = now;
  _specialUntil = now + 2000;
  const PlanetKind k = kind();
  spawnSparks(26, specialColor(k), kWhite);
  setToast(specialName(k), specialColor(k), 1900);
}

void PlanetScene::addMarker(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < sizeof(_markers) / sizeof(_markers[0]); ++i) {
    if (_markers[i].used) continue;
    _markers[i].used = true;
    _markers[i].x = x;
    _markers[i].y = y;
    _markers[i].born = millis();
    setToast("SURFACE MARKER SET", accent(), 1200);
    spawnSparks(8, accent(), kWhite);
    return;
  }
}

/* -------------------------------------------------------------------------- */
/*  Update                                                                    */
/* -------------------------------------------------------------------------- */
void PlanetScene::update(uint32_t now, float dt) {
  if (dt > 0.06f) dt = 0.06f;

  switch (_state) {
    case State::APPROACH: {
      _rot += dt * 5.0f;
      if (_rot >= 256.0f) _rot -= 256.0f;
#if SERIAL_DEBUG
      /* Occasional progress only - never one line per frame. */
      static uint8_t progressStep = 0;
      const uint32_t elapsed = now - _startMs;
      const uint8_t step = (uint8_t)(elapsed * 4 / APPROACH_MS);
      if (step > progressStep) {
        progressStep = step;
        Serial.printf("[PLANET] zoom progress %u%%  elapsed=%lu ms\n",
                      (unsigned)(step * 25), (unsigned long)elapsed);
      }
#else
      const uint32_t elapsed = now - _startMs;
#endif
      /* Definite completion condition: wall-clock deadline, independent of
       * frames, touch input or the previous state of the scene. */
      if (elapsed >= APPROACH_MS) {
        _state = State::EXPLORE;
        _exploreMs = now;
        setToast("ORBITAL VIEW LOCKED", accent(), 1500);
#if SERIAL_DEBUG
        progressStep = 0;
        Serial.printf("[PLANET] zoom completed (%lu ms) -> EXPLORATION\n",
                      (unsigned long)elapsed);
#endif
      }
      break;
    }

    case State::EXPLORE:
      if (!_dragging) {
        _rot += dt * 1.15f;
        if (_rot >= 256.0f) _rot -= 256.0f;
      }
      _zoom += (_zoomTarget - _zoom) * clampf(dt * 8.0f, 0.0f, 1.0f);
      updateSparks(dt);
      break;

    case State::EXIT:
      if (now - _startMs >= EXIT_MS) {
        _state = State::OFF;
#if SERIAL_DEBUG
        Serial.println("[PLANET] returned to SOLAR SYSTEM");
#endif
      }
      break;

    case State::OFF:
    default:
      break;
  }
}

void PlanetScene::beginExit(uint32_t now) {
  if (_state != State::EXPLORE) return;
  _state = State::EXIT;
  _startMs = now;
  _dragging = false;
  _backPressed = false;
#if SERIAL_DEBUG
  Serial.println("[PLANET] back pressed -> RETURNING TO SYSTEM");
#endif
}

/* -------------------------------------------------------------------------- */
/*  Touch                                                                     */
/* -------------------------------------------------------------------------- */
void PlanetScene::handleTouch(const TouchEvent& ev) {
  if (_state == State::APPROACH) {
    if (ev.type == TouchEventType::TAP && millis() - _startMs > 380) {
      _state = State::EXPLORE;
      _exploreMs = millis();
      setToast("ORBITAL VIEW LOCKED", accent(), 1200);
#if SERIAL_DEBUG
      Serial.println("[PLANET] zoom skipped by tap -> EXPLORATION");
#endif
    }
    return;
  }
  if (_state != State::EXPLORE) return;

  switch (ev.type) {
    case TouchEventType::DOWN:
      if (hitBack(ev.x, ev.y)) {
        _backPressed = true;
      } else if (ev.y < PANEL_Y) {
        _dragging = true;
        _dragX = ev.x;
        _dragY = ev.y;
      }
      break;

    case TouchEventType::UP:
      _dragging = false;
      _backPressed = false;
      break;

    case TouchEventType::MOVE:
    case TouchEventType::DRAG:
      if (_dragging && ev.fingers == 1) {
        _rot -= ev.dx * 0.95f;
        while (_rot < 0.0f) _rot += 256.0f;
        while (_rot >= 256.0f) _rot -= 256.0f;
        _pitch = (int16_t)(_pitch + ev.dy * 0.62f);
        if (_pitch < -55) _pitch = -55;
        if (_pitch > 55) _pitch = 55;
        _dragX = ev.x;
        _dragY = ev.y;
      }
      break;

    case TouchEventType::TAP:
      if (hitBack(ev.x, ev.y)) {
        beginExit(millis());
      } else if (insideGlobe(ev.x, ev.y)) {
        addMarker(ev.x, ev.y);
      }
      break;

    case TouchEventType::LONG_PRESS:
      if (ev.y < PANEL_Y) triggerSpecial(millis());
      break;

    case TouchEventType::PINCH_START:
      _dragging = false;
      _pinchAccum = 1.0f;
      break;

    case TouchEventType::PINCH: {
      const float f = ev.scaleQ8 / 256.0f;
      if (f > 0.5f && f < 2.0f) {
        _zoomTarget = clampf(_zoomTarget * f, 1.0f, 1.80f);
        _pinchAccum *= f;
      }
      break;
    }

    case TouchEventType::PINCH_END:
      if (_pinchAccum < 0.78f && _zoom <= 1.03f) {
        beginExit(millis());
      } else if (_pinchAccum > 1.20f) {
        setToast("SURFACE DETAIL ENHANCED", accent(), 1100);
      }
      break;

    default:
      break;
  }
}

/* -------------------------------------------------------------------------- */
/*  Rendering                                                                 */
/* -------------------------------------------------------------------------- */
void PlanetScene::render(uint32_t now) {
  if (!_canvas || !_galaxy) return;
  switch (_state) {
    case State::APPROACH: drawApproach(now); break;
    case State::EXPLORE:  drawExplore(now);  break;
    case State::EXIT:     drawExit(now);     break;
    case State::OFF:
    default: break;
  }
}

void PlanetScene::drawApproach(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  _galaxy->render(now);

  const float t = clampf((float)(now - _startMs) / APPROACH_MS, 0.0f, 1.0f);
  const float e = t * t * (3.0f - 2.0f * t);
  const uint16_t ac = accent();
  const PlanetInfo& info = _galaxy->planetInfo(_planet);

  c.blendFill(kBlack, (uint8_t)(e * 165.0f));
  drawWarp(t * e);

  const float cx = lerpf((float)_startX, (float)GLOBE_CX, e);
  const float cy = lerpf((float)_startY, (float)GLOBE_CY, e);
  const int16_t R = (int16_t)lerpf((float)_startR, (float)GLOBE_R, powf(e, 0.55f));

  c.addGlow((int16_t)cx, (int16_t)cy, (int16_t)(R * 2.4f), ac, (uint8_t)(42.0f * e));
  if (kind() == PlanetKind::SATURN) {
    drawRingSystem((int16_t)cx, (int16_t)cy, R, (uint8_t)(125.0f * e), false);
  }
  drawGlobe((int16_t)cx, (int16_t)cy, R, now);
  if (kind() == PlanetKind::SATURN) {
    drawRingSystem((int16_t)cx, (int16_t)cy, R, (uint8_t)(150.0f * e), true);
  }

  for (uint8_t i = 0; i < 3; ++i) {
    c.addCircleOutline((int16_t)cx, (int16_t)cy, (int16_t)(R + 2 + i * 3), ac,
                       (uint8_t)((88 - i * 22) * e));
  }

  if (t < 0.64f) {
    const uint8_t a = (uint8_t)(255.0f * (1.0f - t / 0.64f));
    c.drawText(120, 30, "APPROACHING", 1, kWhite, a, TextAlign::CENTER);
    c.drawText(120, 42, info.name, 2, ac, a, TextAlign::CENTER);
    c.drawText(120, 60, info.type, 1, rgb565(0xA8, 0xC4, 0xDE), a, TextAlign::CENTER);
  }

  if (t > 0.82f) {
    const uint8_t a = (uint8_t)((t - 0.82f) / 0.18f * 255.0f);
    c.blendFill(kBlack, a);
  }
}

void PlanetScene::drawWarp(float t) {
  if (t < 0.02f) return;
  SpaceCanvas& c = *_canvas;
  const uint8_t a = (uint8_t)(t * 105.0f);
  for (uint8_t i = 0; i < 38; ++i) {
    const uint32_t h = hashU32(i * 2654435761u + 31u);
    const uint16_t ang = (uint16_t)((h & 511) + (uint16_t)(t * 90.0f));
    const float r0 = 26.0f + (float)((h >> 9) % 140u) + t * 80.0f;
    const float len = 8.0f + (float)((h >> 16) % 38u) * (0.35f + t);
    /* mulQ10() returns the scaled offset directly; pre-multiplying the radius
     * by 1024 overflowed the int16 result and produced wrapped coordinates
     * thousands of pixels away, which made addLine() spin forever. */
    const int16_t x0 = (int16_t)(120 + fx::mulQ10(fx::cosT(ang), (int32_t)r0));
    const int16_t y0 = (int16_t)(101 + fx::mulQ10(fx::sinT(ang), (int32_t)r0));
    const int16_t x1 = (int16_t)(120 + fx::mulQ10(fx::cosT(ang), (int32_t)(r0 + len)));
    const int16_t y1 = (int16_t)(101 + fx::mulQ10(fx::sinT(ang), (int32_t)(r0 + len)));
    c.addLine(x0, y0, x1, y1, (i & 3) ? kWhite : kCyan, a);
  }
}

void PlanetScene::drawStarfield(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  for (uint16_t i = 0; i < 140; ++i) {
    const uint32_t h = hashU32(i * 2246822519u + 7u + _planet * 313u);
    const int16_t x = (int16_t)(h % SCREEN_W);
    const int16_t y = (int16_t)((h >> 9) % PANEL_Y);
    const int16_t tw = fx::sinT((uint16_t)((h >> 4) + (now >> 4)));
    int a = 45 + (int)((h >> 18) % 115u) + (tw >> 5);
    if (a < 20) a = 20;
    if (a > 210) a = 210;
    const uint16_t col = ((h >> 24) & 1) ? rgb565(0xB8, 0xD4, 0xFF) : kWhite;
    c.addPixel(x, y, col, (uint8_t)a);
    if ((h & 0xFF) == 0) c.addGlow(x, y, 5, col, (uint8_t)(a / 3));
  }
}

void PlanetScene::drawGlobe(int16_t cx, int16_t cy, int16_t R, uint32_t now) {
  (void)now;
  if (R < 2) return;
  const uint16_t* tex = _galaxy->planetSurface(_planet);
  if (!tex) return;
  if (R > 240) R = 240;

  uint16_t* fb = _canvas->buffer();
  const int16_t w = _canvas->width();
  const int16_t h = _canvas->height();
  const int32_t R2 = (int32_t)R * R;
  const uint16_t ac = accent();

  int16_t top = (int16_t)(cy - R);
  int16_t bottom = (int16_t)(cy + R);
  if (top < 0) top = 0;
  if (bottom >= h) bottom = (int16_t)(h - 1);

  const int rot = (int)_rot & 255;
  const int pitchRows = ((int)_pitch * GalaxyEngine::PLANET_TEX_H) / 180;

  for (int16_t y = top; y <= bottom; ++y) {
    const int16_t yy = (int16_t)(y - cy);
    const int32_t row2 = R2 - (int32_t)yy * yy;
    if (row2 <= 0) continue;
    const int16_t span = (int16_t)sqrtf((float)row2);

    int16_t x0 = (int16_t)(cx - span);
    int16_t x1 = (int16_t)(cx + span);
    if (x0 < 0) x0 = 0;
    if (x1 >= w) x1 = (int16_t)(w - 1);

    const int ny = (yy * 256) / R;
    int liy = ny + 256;
    if (liy < 0) liy = 0;
    if (liy > 512) liy = 512;
    int v = _latLUT[liy] + pitchRows;
    if (v < 0) v = 0;
    if (v >= GalaxyEngine::PLANET_TEX_H) v = GalaxyEngine::PLANET_TEX_H - 1;
    const uint16_t* trow = tex + v * GalaxyEngine::PLANET_TEX_W;
    uint16_t* orow = fb + (size_t)y * w;

    for (int16_t x = x0; x <= x1; ++x) {
      const int16_t xx = (int16_t)(x - cx);
      const int nx = (xx * 256) / R;
      int li = nx + 256;
      if (li < 0) li = 0;
      if (li > 512) li = 512;
      const int u = (_lonLUT[li] + rot) & 255;
      uint16_t col = trow[u];

      /* Directional light from the upper-left/front, with a dark terminator. */
      int diff = 154 - ((nx * 118) >> 8) - ((ny * 96) >> 8);
      if (diff < 20) diff = 20;
      if (diff > 252) diff = 252;
      const int d2n = (nx * nx + ny * ny) >> 8;
      const int limb = 172 + (((256 - d2n) * 82) >> 8);
      col = shade565(col, (diff * limb) >> 8);

      /* A thin atmospheric rim helps the sphere read against black space. */
      if (d2n > 210) {
        const float rim = (d2n - 210) / 46.0f;
        col = mix565(col, ac, rim * 0.48f);
      }
      orow[x] = col;
    }
  }
}

void PlanetScene::drawRingSystem(int16_t cx, int16_t cy, int16_t R,
                                 uint8_t alpha, bool front) {
  if (alpha == 0 || R < 4) return;
  SpaceCanvas& c = *_canvas;
  const int rx = (int)R * 1.78f;
  const int ry = (int)(rx * 0.31f);
  const uint16_t ac = accent();

  for (uint8_t layer = 0; layer < 4; ++layer) {
    const int lrx = (int)(rx * (0.72f + layer * 0.12f));
    const int lry = (int)(ry * (0.72f + layer * 0.12f));
    const uint16_t col = mix565(rgb565(0xE8, 0xDC, 0xBE), ac, 0.20f + layer * 0.04f);
    for (uint16_t seg = 0; seg < 128; ++seg) {
      const uint16_t a = (uint16_t)(seg * (fx::ANGLE_STEPS / 128));
      const int16_t sy = fx::sinT(a);
      if (front ? (sy <= 0) : (sy > 0)) continue;
      const int16_t px = (int16_t)(cx + fx::mulQ10(fx::cosT(a), lrx));
      const int16_t py = (int16_t)(cy + fx::mulQ10(sy, lry));
      c.addPixel(px, py, col, (uint8_t)(alpha * (0.33f + layer * 0.15f)));
      if (layer == 1) c.addPixel((int16_t)(px + 1), py, col, (uint8_t)(alpha / 4));
    }
  }
}

void PlanetScene::drawExplore(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  c.fill(kBlack);
  drawStarfield(now);

  const int16_t R = globeRadius();
  const uint16_t ac = accent();

  /* The atmosphere is localised around the globe; the rest stays #000000. */
  c.addGlow(GLOBE_CX, GLOBE_CY, (int16_t)(R * 2.1f), ac, 26);
  if (kind() == PlanetKind::SATURN) {
    drawRingSystem(GLOBE_CX, GLOBE_CY, R, 115, false);
  }
  drawGlobe(GLOBE_CX, GLOBE_CY, R, now);
  if (kind() == PlanetKind::SATURN) {
    drawRingSystem(GLOBE_CX, GLOBE_CY, R, 150, true);
  }
  for (uint8_t i = 0; i < 2; ++i) {
    c.addCircleOutline(GLOBE_CX, GLOBE_CY, (int16_t)(R + 2 + i * 3), ac,
                       (uint8_t)(70 - i * 25));
  }

  drawSpecial(now);
  drawSparks();
  drawMarkers(now);

  if (_toast[0] && now < _toastUntil) {
    const uint32_t remain = _toastUntil - now;
    uint8_t a = 220;
    if (remain < 300) a = (uint8_t)(remain * 220 / 300);
    const int16_t tw = (int16_t)(SpaceCanvas::textWidth(_toast, 1) + 16);
    c.blendRoundRect((int16_t)(120 - tw / 2), 190, tw, 15, 7,
                     rgb565(0x01, 0x03, 0x08), (uint8_t)(a * 0.70f));
    c.drawText(120, 194, _toast, 1, _toastColor, a, TextAlign::CENTER);
  }

  drawInfoPanel(now);

  if (_exploreMs && now - _exploreMs < 420) {
    const uint8_t a = (uint8_t)(255.0f * (1.0f - (now - _exploreMs) / 420.0f));
    c.blendFill(kBlack, a);
  }
}

void PlanetScene::drawSparks() {
  SpaceCanvas& c = *_canvas;
  for (uint8_t i = 0; i < sizeof(_sparks) / sizeof(_sparks[0]); ++i) {
    const Spark& s = _sparks[i];
    if (!s.life) continue;
    const uint8_t a = (uint8_t)(s.life * 220 / s.maxLife);
    const int16_t x = (int16_t)s.x;
    const int16_t y = (int16_t)s.y;
    if ((uint16_t)x >= SCREEN_W || (uint16_t)y >= PANEL_Y) continue;
    c.addGlow(x, y, s.size == 2 ? 7 : 5, s.color, a);
    c.addLine(x, y, (int16_t)(s.x - s.vx * 0.018f), (int16_t)(s.y - s.vy * 0.018f),
              s.color, (uint8_t)(a / 2));
    c.addPixel(x, y, kWhite, (uint8_t)(a / 2));
  }
}

void PlanetScene::drawMarkers(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  const uint16_t ac = accent();
  for (uint8_t i = 0; i < sizeof(_markers) / sizeof(_markers[0]); ++i) {
    Marker& m = _markers[i];
    if (!m.used) continue;
    const uint32_t age = now - m.born;
    if (age > 8000) {
      m.used = false;
      continue;
    }
    const uint8_t fade = age > 6500 ? (uint8_t)(220 * (8000 - age) / 1500) : 220;
    const int16_t pulse = (int16_t)((fx::sinT((uint16_t)(now >> 4) + i * 37) * 2) >> 10);
    const int16_t r = (int16_t)(5 + pulse);
    c.addGlow(m.x, m.y, 12, ac, (uint8_t)(fade / 3));
    c.addCircleOutline(m.x, m.y, r, ac, fade);
    c.addCircleOutline(m.x, m.y, (int16_t)(r + 4), ac, (uint8_t)(fade / 3));
    c.addLine((int16_t)(m.x - 9), m.y, (int16_t)(m.x - 3), m.y, kWhite, fade);
    c.addLine((int16_t)(m.x + 3), m.y, (int16_t)(m.x + 9), m.y, kWhite, fade);
    c.addLine(m.x, (int16_t)(m.y - 9), m.x, (int16_t)(m.y - 3), kWhite, fade);
    c.addLine(m.x, (int16_t)(m.y + 3), m.x, (int16_t)(m.y + 9), kWhite, fade);
  }
}

void PlanetScene::drawSpecial(uint32_t now) {
  if (!_specialStart || now >= _specialUntil) return;
  SpaceCanvas& c = *_canvas;
  const float t = clampf((float)(now - _specialStart) / (_specialUntil - _specialStart),
                         0.0f, 1.0f);
  const float pulse = sinf(t * (float)M_PI);
  const uint16_t col = specialColor(kind());
  const int16_t R = globeRadius();

  c.addGlow(GLOBE_CX, GLOBE_CY, (int16_t)(R * 2.2f), col, (uint8_t)(38 * pulse));
  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t r = (int16_t)(R + 6 + i * 8 + t * 12);
    c.addCircleOutline(GLOBE_CX, GLOBE_CY, r, col,
                       (uint8_t)((110 - i * 24) * (1.0f - t)));
  }

  switch (kind()) {
    case PlanetKind::EARTH:
    case PlanetKind::URANUS: {
      for (uint8_t i = 0; i < 3; ++i) {
        const int16_t y = (int16_t)(35 + i * 13 + sinf(now * 0.002f + i) * 4.0f);
        c.blendRect(28, y, 184, 2, col, (uint8_t)(38 * pulse));
      }
      break;
    }
    case PlanetKind::MARS:
    case PlanetKind::NEPTUNE: {
      for (uint8_t i = 0; i < 4; ++i) {
        const int16_t y = (int16_t)(64 + i * 20);
        const int16_t x = (int16_t)((now / 9 + i * 47) % 220 - 30);
        c.addHLine(x, y, 46, col, (uint8_t)(46 * pulse));
      }
      break;
    }
    case PlanetKind::JUPITER:
      c.addGlow((int16_t)(GLOBE_CX + R / 3), (int16_t)(GLOBE_CY + R / 5),
                34, rgb565(0xE0, 0x4A, 0x2E), (uint8_t)(95 * pulse));
      break;
    case PlanetKind::SATURN:
      for (uint8_t i = 0; i < 3; ++i) {
        c.addCircleOutline(GLOBE_CX, GLOBE_CY, (int16_t)(R + 34 + i * 16), col,
                           (uint8_t)(65 * pulse));
      }
      break;
    default:
      for (uint8_t i = 0; i < 4; ++i) {
        const int16_t x = (int16_t)(GLOBE_CX + sinf(now * 0.002f + i) * R * 0.7f);
        c.addLine(x, 22, (int16_t)(x + 6), 72, col, (uint8_t)(55 * pulse));
      }
      break;
  }
}

void PlanetScene::drawInfoPanel(uint32_t now) {
  (void)now;
  SpaceCanvas& c = *_canvas;
  const uint16_t ac = accent();
  const PlanetInfo& info = _galaxy->planetInfo(_planet);

  c.fillRect(0, PANEL_Y, SCREEN_W, SCREEN_H - PANEL_Y, kBlack);
  c.addHLine(0, PANEL_Y, SCREEN_W, ac, 145);
  c.addHLine(0, (int16_t)(PANEL_Y + 1), SCREEN_W, ac, 42);

  c.drawText(10, 214, info.name, 2, ac, 255);
  c.drawText(230, 218, info.type, 1, rgb565(0x9A, 0xB8, 0xD4), 220, TextAlign::RIGHT);

  drawWrappedText(info.atmosphere, 10, 233, 37, 2, rgb565(0xD2, 0xE6, 0xF8), 212);
  drawWrappedText(info.surface, 10, 252, 37, 2, rgb565(0xC4, 0xDA, 0xEE), 205);
  drawWrappedText(info.fact, 10, 272, 37, 1, rgb565(0xFF, 0xE4, 0x9A), 220);

  const bool pressed = _backPressed;
  c.blendRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 12,
                   pressed ? shade565(ac, 170) : rgb565(0x0A, 0x12, 0x22), 235);
  c.strokeRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 12, ac, 215);
  c.drawText((int16_t)(BACK_X + BACK_W / 2), (int16_t)(BACK_Y + 8),
             "BACK TO SOLAR SYSTEM", 1, pressed ? kWhite : ac, 255,
             TextAlign::CENTER);
}

void PlanetScene::drawWrappedText(const char* text, int16_t x, int16_t y,
                                  int16_t maxChars, uint8_t maxLines,
                                  uint16_t color, uint8_t alpha) {
  if (!text) return;
  char line[48];
  int n = 0;
  uint8_t lineNo = 0;
  const char* p = text;

  while (*p && lineNo < maxLines) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* w = p;
    while (*p && *p != ' ') ++p;
    const int len = (int)(p - w);
    if (n > 0 && n + 1 + len > maxChars) {
      line[n] = 0;
      _canvas->drawText(x, (int16_t)(y + lineNo * 9), line, 1, color, alpha);
      ++lineNo;
      n = 0;
      if (lineNo >= maxLines) break;
    }
    if (n > 0) line[n++] = ' ';
    for (int i = 0; i < len && n < (int)sizeof(line) - 1 && n < maxChars; ++i) {
      line[n++] = w[i];
    }
  }
  if (n > 0 && lineNo < maxLines) {
    line[n] = 0;
    _canvas->drawText(x, (int16_t)(y + lineNo * 9), line, 1, color, alpha);
  }
}

void PlanetScene::drawExit(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  const float t = clampf((float)(now - _startMs) / EXIT_MS, 0.0f, 1.0f);

  if (t < 0.45f) {
    drawExplore(now);
    c.blendFill(kBlack, (uint8_t)(t / 0.45f * 255.0f));
  } else {
    _galaxy->render(now);
    c.blendFill(kBlack, (uint8_t)((1.0f - (t - 0.45f) / 0.55f) * 255.0f));
  }
}
