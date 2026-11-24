/* ==========================================================================
 * galaxy_engine.cpp
 * --------------------------------------------------------------------------
 * Rendering strategy
 *   - A large nebula/star background is baked once into PSRAM and copied into
 *     the frame buffer every frame with a slow parallax offset (row memcpy).
 *   - Galaxy particles are updated with a fixed-point angle accumulator and a
 *     precomputed sin table; no per-particle sin/cos calls in the render loop.
 *   - Soft glows come from pre-generated alpha masks in PSRAM.
 * ========================================================================== */
#include "galaxy_engine.h"
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

namespace {

constexpr int16_t BG_PAD = 32;
constexpr int16_t BG_W = SCREEN_W + BG_PAD * 2;
constexpr int16_t BG_H = SCREEN_H + BG_PAD * 2;

constexpr float kTilt = 0.635f;        /* galaxy disc squash */

const uint16_t kGalaxyColors[] = {
  rgb565(0x6F, 0xD8, 0xFF),   /* ice cyan   */
  rgb565(0x3B, 0x8C, 0xFF),   /* blue       */
  rgb565(0x8A, 0x6B, 0xFF),   /* violet     */
  rgb565(0xB5, 0x6B, 0xFF),   /* purple     */
  rgb565(0xE9, 0xF5, 0xFF),   /* white-blue */
  rgb565(0x49, 0xE3, 0xE0),   /* teal       */
};
constexpr uint8_t kGalaxyColorCount = sizeof(kGalaxyColors) / sizeof(kGalaxyColors[0]);

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint8_t clamp255(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline uint16_t scale565(uint16_t c, uint8_t mul) {
  const uint16_t r = (uint16_t)(((c >> 11) & 31) * mul / 255);
  const uint16_t g = (uint16_t)(((c >> 5) & 63) * mul / 255);
  const uint16_t b = (uint16_t)((c & 31) * mul / 255);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline uint32_t hash2(int32_t x, int32_t y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 0x27D4EB2Du ^ (uint32_t)y * 0x165667B1u ^ seed * 0x9E3779B1u;
  h ^= h >> 15;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  return h;
}

float valueNoise(float x, float y, uint32_t seed) {
  const int32_t xi = (int32_t)floorf(x);
  const int32_t yi = (int32_t)floorf(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3.0f - 2.0f * fx);
  fy = fy * fy * (3.0f - 2.0f * fy);

  const float v00 = (hash2(xi, yi, seed) & 0xFF) * (1.0f / 255.0f);
  const float v10 = (hash2(xi + 1, yi, seed) & 0xFF) * (1.0f / 255.0f);
  const float v01 = (hash2(xi, yi + 1, seed) & 0xFF) * (1.0f / 255.0f);
  const float v11 = (hash2(xi + 1, yi + 1, seed) & 0xFF) * (1.0f / 255.0f);

  return lerpf(lerpf(v00, v10, fx), lerpf(v01, v11, fx), fy);
}

float fbm3(float x, float y, uint32_t seed) {
  return valueNoise(x, y, seed) * 0.55f
       + valueNoise(x * 2.03f, y * 2.01f, seed + 17) * 0.30f
       + valueNoise(x * 4.07f, y * 4.03f, seed + 41) * 0.15f;
}

} // namespace

/* -------------------------------------------------------------------------- */
/*  Setup                                                                     */
/* -------------------------------------------------------------------------- */
bool GalaxyEngine::begin(SpaceCanvas* canvas) {
  _canvas = canvas;
  if (!_canvas) return false;

  fx::seed((uint32_t)esp_random());

  if (!allocateBackground()) return false;
  prepare();

  _startMs = millis();
  _ready = true;
  return true;
}

void GalaxyEngine::prepare() {
  buildBackground();
  buildStars();
  buildGalaxy();
  buildPlanets();
  _ready = true;
}

bool GalaxyEngine::allocateBackground() {
  _bgW = BG_W;
  _bgH = BG_H;
  const size_t bytes = (size_t)_bgW * _bgH * sizeof(uint16_t);
  _bg = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!_bg) _bg = (uint16_t*)malloc(bytes);
  return _bg != nullptr;
}

void GalaxyEngine::buildBackground() {
  if (!_bg) return;

  /* Vignette is centred in background-buffer space so it lines up with the
   * visible screen after the parallax pad is accounted for. */
  const float cx = BG_PAD + SCREEN_W * 0.5f;
  const float cy = BG_PAD + SCREEN_H * 0.46f;

  for (int16_t y = 0; y < _bgH; ++y) {
    const float ty = (float)y / (_bgH - 1);
    const float baseR = lerpf(2.0f, 5.0f, ty);
    const float baseG = lerpf(3.5f, 7.0f, ty);
    const float baseB = lerpf(11.0f, 20.0f, ty);

    for (int16_t x = 0; x < _bgW; ++x) {
      const float nx = (x - cx) / (SCREEN_W * 0.72f);
      const float ny = (y - cy) / (SCREEN_H * 0.72f);
      const float vig = clampf(1.0f - 0.40f * (nx * nx + ny * ny), 0.35f, 1.0f);

      float n1 = fbm3(x * 0.0115f, y * 0.0115f, 11);
      float n2 = fbm3(x * 0.026f + 37.0f, y * 0.023f - 19.0f, 71);

      float cloudA = clampf((n1 * 0.62f + n2 * 0.38f - 0.38f) * 2.1f, 0.0f, 1.0f);
      float cloudB = clampf((n2 * 0.8f + n1 * 0.2f - 0.52f) * 2.4f, 0.0f, 1.0f);
      cloudA *= cloudA;
      cloudB *= cloudB;

      /* dark dust lane through the nebula */
      float dust = clampf(0.55f + 0.45f * sinf((x + y * 1.7f) * 0.021f), 0.0f, 1.0f);

      float r = baseR * vig;
      float g = baseG * vig;
      float b = baseB * vig;
      r += cloudA * 62.0f * dust;
      g += cloudA * 30.0f * dust;
      b += cloudA * 104.0f * dust;
      r += cloudB * 15.0f;
      g += cloudB * 54.0f;
      b += cloudB * 72.0f;

      /* faint warm dust near the outer edges */
      const float edge = clampf((fabsf(nx) + fabsf(ny) - 0.75f) * 2.0f, 0.0f, 1.0f);
      r += edge * 9.0f;
      g += edge * 5.0f;
      b += edge * 2.0f;

      _bg[(size_t)y * _bgW + x] = rgb565(clamp255((int)r), clamp255((int)g), clamp255((int)b));
    }
  }

  /* Baked distant star field: fixed points add depth without per-frame cost. */
  const uint16_t starColors[] = {
    0xFFFF,
    rgb565(0xDF, 0xF3, 0xFF),
    rgb565(0xB8, 0xCC, 0xFF),
    rgb565(0xFF, 0xE9, 0xC8),
    0xFFFF,
    rgb565(0xCF, 0xE8, 0xFF),
  };
  for (uint16_t i = 0; i < 620; ++i) {
    const int16_t x = (int16_t)(fx::rand() % _bgW);
    const int16_t y = (int16_t)(fx::rand() % _bgH);
    const uint8_t brightness = (uint8_t)(26 + (fx::rand() % 60));
    const uint16_t base = starColors[fx::rand() % (sizeof(starColors) / sizeof(starColors[0]))];
    _bg[(size_t)y * _bgW + x] = scale565(base, brightness);

    if (brightness > 62) {
      /* a second faint pixel for a softer dot */
      const int16_t x2 = (int16_t)(x + 1);
      if (x2 < _bgW) {
        _bg[(size_t)y * _bgW + x2] = scale565(base, (uint8_t)(brightness / 3));
      }
    }
  }

  /* A few brighter baked stars with a faint cross. */
  for (uint8_t i = 0; i < 26; ++i) {
    const int16_t x = (int16_t)(BG_PAD + fx::rand() % SCREEN_W);
    const int16_t y = (int16_t)(BG_PAD + fx::rand() % SCREEN_H);
    const uint16_t c = rgb565(0xEA, 0xF6, 0xFF);
    _bg[(size_t)y * _bgW + x] = c;
    if (x > 1)     _bg[(size_t)y * _bgW + x - 1] = rgb565(60, 82, 110);
    if (x < _bgW - 1) _bg[(size_t)y * _bgW + x + 1] = rgb565(60, 82, 110);
    if (y > 1)     _bg[(size_t)(y - 1) * _bgW + x] = rgb565(50, 70, 100);
    if (y < _bgH - 1) _bg[(size_t)(y + 1) * _bgW + x] = rgb565(50, 70, 100);
  }
}

void GalaxyEngine::buildStars() {
  const uint16_t tints[] = {
    0xFFFF,
    rgb565(0xDF, 0xF2, 0xFF),
    rgb565(0xAE, 0xC6, 0xFF),
    rgb565(0xFF, 0xE8, 0xC4),
    0xFFFF,
  };
  for (uint16_t i = 0; i < STAR_COUNT; ++i) {
    Star& s = _stars[i];
    const uint8_t layer = (uint8_t)(fx::rand() % 100);
    if (layer < 66) s.depth = 0;
    else if (layer < 91) s.depth = 1;
    else s.depth = 2;

    s.x = (float)(fx::rand() % SCREEN_W);
    s.y = (float)(fx::rand() % SCREEN_H);
    s.phase = (uint8_t)(fx::rand() & 0xFF);
    s.twinkleSpeed = (uint8_t)(1 + (fx::rand() % 3));
    s.color = tints[fx::rand() % 5];

    if (s.depth == 0) {
      s.bright = (uint8_t)(80 + fx::rand() % 80);
      s.vx = (fx::randFloat() - 0.5f) * 1.4f;
      s.vy = (fx::randFloat() - 0.5f) * 1.0f;
    } else if (s.depth == 1) {
      s.bright = (uint8_t)(120 + fx::rand() % 100);
      s.vx = (fx::randFloat() - 0.5f) * 5.0f;
      s.vy = (fx::randFloat() - 0.5f) * 3.5f;
    } else {
      s.bright = (uint8_t)(170 + fx::rand() % 86);
      s.vx = (fx::randFloat() - 0.5f) * 12.0f;
      s.vy = (fx::randFloat() - 0.5f) * 8.0f;
    }
  }
}

void GalaxyEngine::buildGalaxy() {
  const uint16_t arms = 3;
  const float maxR = 112.0f;

  for (uint16_t i = 0; i < GALAXY_PARTICLE_COUNT; ++i) {
    Particle& p = _particles[i];

    const float u = fx::randFloat();
    float r = 5.0f + powf(u, 0.62f) * maxR;
    p.radius = (uint8_t)clampf(r, 4.0f, maxR);

    const bool armParticle = (fx::rand() % 100) < 72;
    float theta;
    if (armParticle) {
      const uint8_t arm = (uint8_t)(i % arms);
      const float twist = 3.15f * (r / maxR);
      const float jitter = (fx::randFloat() - 0.5f) * (0.16f + 0.30f * (r / maxR));
      theta = (float)arm * (2.0f * (float)M_PI / arms) + twist + jitter;
    } else {
      theta = fx::randFloat() * 2.0f * (float)M_PI;
      p.radius = (uint8_t)clampf(r * (0.65f + fx::randFloat() * 0.45f), 5.0f, maxR);
    }

    p.angleQ8 = (int32_t)(theta / (2.0f * (float)M_PI) * (fx::ANGLE_STEPS * 256.0f));
    p.angleQ8 &= 0x1FFFF;

    /* Differential rotation: central particles sweep a little faster. */
    float speedUnits = 7.5f + 250.0f / (p.radius + 30.0f);
    p.speedQ8 = (uint16_t)(speedUnits * 256.0f);

    const float radial = p.radius / maxR;
    p.bright = (uint8_t)clamp255((int)(70 + (1.0f - radial) * 150.0f + fx::rand() % 36));

    uint8_t pick = (uint8_t)(fx::rand() % 100);
    if (p.radius < 16) {
      p.color = kGalaxyColors[(pick < 30) ? 4 : 0];
    } else if (pick < 12) {
      p.color = kGalaxyColors[4];
    } else if (pick < 32) {
      p.color = kGalaxyColors[0];
    } else if (pick < 62) {
      p.color = kGalaxyColors[1];
    } else if (pick < 82) {
      p.color = kGalaxyColors[2];
    } else if (pick < 94) {
      p.color = kGalaxyColors[3];
    } else {
      p.color = kGalaxyColors[5];
    }

    const uint8_t s = (uint8_t)(fx::rand() % 100);
    p.size = (s < 74) ? 0 : (s < 96 ? 1 : 2);
    p.twinklePhase = (uint16_t)(fx::rand() & 511);
  }
}

void GalaxyEngine::buildPlanets() {
  struct Def { int16_t radius, orbit; int16_t speed; uint16_t color; bool ring; uint16_t angle; };
  const Def defs[PLANET_COUNT] = {
    { 15,  92,  5, rgb565(0x3E, 0xA8, 0xB8), false, 40  },
    { 11,  72, -4, rgb565(0xD9, 0x92, 0x55), false, 210 },
    {  9, 122,  3, rgb565(0xB9, 0xE2, 0xFF), true,  130 },
    { 13,  56, -6, rgb565(0x88, 0x6B, 0xD9), false, 330 },
  };

  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    Planet& p = _planets[i];
    p.radius = defs[i].radius;
    p.orbit = defs[i].orbit;
    p.speedQ8 = (int16_t)(defs[i].speed * 256);
    p.angleQ8 = (int32_t)defs[i].angle << 8;
    p.color = defs[i].color;
    p.ring = defs[i].ring;

    const int16_t extent = p.ring ? (int16_t)(p.radius * 1.85f) : p.radius;
    const int16_t size = (int16_t)(extent * 2 + 2);
    if (p.w != size) {
      const size_t bytes = (size_t)size * size;
      p.pixels = (uint16_t*)heap_caps_malloc(bytes * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      p.alpha  = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p.pixels || !p.alpha) {
        free(p.pixels);
        free(p.alpha);
        p.pixels = nullptr;
        p.alpha = nullptr;
        continue;
      }
      p.w = p.h = size;
    }
    buildPlanetSprite(p, defs[i].color, defs[i].ring);
  }
}

void GalaxyEngine::buildPlanetSprite(Planet& p, uint16_t baseColor, bool ring) {
  const int16_t size = p.w;
  const float R = (float)p.radius;
  const uint16_t br = (uint16_t)(((baseColor >> 11) & 31) * 255 / 31);
  const uint16_t bg = (uint16_t)(((baseColor >> 5) & 63) * 255 / 63);
  const uint16_t bb = (uint16_t)((baseColor & 31) * 255 / 31);

  for (int16_t py = 0; py < size; ++py) {
    for (int16_t px = 0; px < size; ++px) {
      const float dx = (px + 0.5f - size * 0.5f) / R;
      const float dy = (py + 0.5f - size * 0.5f) / R;
      uint16_t out = 0;
      uint8_t outA = 0;

      if (ring) {
        const float ex = dx / 1.78f;
        const float ey = dy / (1.78f * 0.34f);
        const float rr = sqrtf(ex * ex + ey * ey);
        if (rr > 0.74f && rr < 1.02f) {
          const float edge = clampf(1.0f - fabsf(rr - 0.88f) / 0.16f, 0.0f, 1.0f);
          const float band = 0.72f + 0.28f * sinf(rr * 42.0f);
          out = rgb565((uint8_t)(188 * band), (uint8_t)(196 * band), (uint8_t)(214 * band));
          outA = (uint8_t)(115 * edge);
        }
      }

      const float d2 = dx * dx + dy * dy;
      if (d2 <= 1.0f) {
        const float z = sqrtf(1.0f - d2);
        float diff = -dx * 0.52f - dy * 0.62f + z * 0.58f;
        diff = clampf(diff, 0.0f, 1.0f);
        float shade = 0.13f + 0.95f * diff;
        shade *= 0.93f + 0.07f * sinf(dy * 9.0f + dx * 3.0f);
        const float rim = powf(1.0f - z, 2.4f) * 0.55f;
        const int r8 = (int)(br * shade + 90.0f * rim);
        const int g8 = (int)(bg * shade + 130.0f * rim);
        const int b8 = (int)(bb * shade + 170.0f * rim);
        out = rgb565(clamp255(r8), clamp255(g8), clamp255(b8));
        const float edge = clampf((1.0f - sqrtf(d2)) * R * 2.0f, 0.0f, 1.0f);
        outA = (uint8_t)(edge * 255.0f);
      }

      p.pixels[(size_t)py * size + px] = out;
      p.alpha[(size_t)py * size + px] = outA;
    }
  }
}

/* -------------------------------------------------------------------------- */
/*  Reset / interaction                                                       */
/* -------------------------------------------------------------------------- */
void GalaxyEngine::reset(bool reseed) {
  _cx = SCREEN_W * 0.5f;
  _cy = 132.0f;
  _velX = _velY = 0.0f;
  _zoom = _zoomTarget = 1.0f;
  _bgOffX = _bgOffY = BG_PAD;
  _gravityStrength = 0.0f;
  _gravityTarget = 0.0f;
  _gravityX = _gravityY = 0;

  if (reseed) {
    fx::seed((uint32_t)esp_random());
    buildStars();
    buildGalaxy();
    for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
      _planets[i].angleQ8 = (int32_t)(fx::rand() & 511) << 8;
    }
  }
  _startMs = millis();
}

void GalaxyEngine::moveBy(int16_t dx, int16_t dy) {
  _cx += dx;
  _cy += dy;
  _cx = clampf(_cx, 26.0f, SCREEN_W - 26.0f);
  _cy = clampf(_cy, 46.0f, SCREEN_H - HUD_BAR_H - 30.0f);
  _velX = _velX * 0.55f + dx * 14.0f;
  _velY = _velY * 0.55f + dy * 14.0f;
  _velX = clampf(_velX, -600.0f, 600.0f);
  _velY = clampf(_velY, -600.0f, 600.0f);
}

void GalaxyEngine::zoomBy(float factor, int16_t focusX, int16_t focusY) {
  if (factor <= 0.0f) return;
  const float oldZoom = _zoomTarget;
  _zoomTarget = clampf(_zoomTarget * factor, 0.52f, 2.25f);
  const float applied = _zoomTarget / oldZoom;
  _cx = focusX + (_cx - focusX) * applied;
  _cy = focusY + (_cy - focusY) * applied;
  _cx = clampf(_cx, -60.0f, SCREEN_W + 60.0f);
  _cy = clampf(_cy, -40.0f, SCREEN_H + 40.0f);
}

void GalaxyEngine::setGravity(bool active, int16_t x, int16_t y) {
  _gravityTarget = active ? 1.0f : 0.0f;
  _gravityX = x;
  _gravityY = y;
}

void GalaxyEngine::updateGravityPosition(int16_t x, int16_t y) {
  _gravityX = x;
  _gravityY = y;
}

/* -------------------------------------------------------------------------- */
/*  Update                                                                    */
/* -------------------------------------------------------------------------- */
void GalaxyEngine::update(uint32_t now, float dt) {
  if (dt > 0.06f) dt = 0.06f;
  (void)now;

  /* inertia after dragging */
  _cx += _velX * dt;
  _cy += _velY * dt;
  const float friction = expf(-4.2f * dt);
  _velX *= friction;
  _velY *= friction;
  if (fabsf(_velX) < 0.6f) _velX = 0.0f;
  if (fabsf(_velY) < 0.6f) _velY = 0.0f;
  _cx = clampf(_cx, 0.0f, SCREEN_W);
  _cy = clampf(_cy, 0.0f, SCREEN_H - HUD_BAR_H);

  /* smooth zoom */
  const float zf = clampf(dt * 9.0f, 0.0f, 1.0f);
  _zoom += (_zoomTarget - _zoom) * zf;

  /* slow background drift + parallax opposite to galaxy offset */
  _bgOffX = BG_PAD + sinf(now * 0.00011f) * 13.0f + (SCREEN_W * 0.5f - _cx) * 0.10f;
  _bgOffY = BG_PAD + cosf(now * 0.00009f) * 9.0f + (132.0f - _cy) * 0.08f;
  _bgOffX = clampf(_bgOffX, 1.0f, BG_W - SCREEN_W - 2.0f);
  _bgOffY = clampf(_bgOffY, 1.0f, BG_H - SCREEN_H - 2.0f);

  /* stars */
  for (uint16_t i = 0; i < STAR_COUNT; ++i) {
    Star& s = _stars[i];
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    if (s.x < -2.0f) s.x += SCREEN_W + 4.0f;
    else if (s.x > SCREEN_W + 2.0f) s.x -= SCREEN_W + 4.0f;
    if (s.y < -2.0f) s.y += SCREEN_H + 4.0f;
    else if (s.y > SCREEN_H + 2.0f) s.y -= SCREEN_H + 4.0f;
  }

  /* galaxy differential rotation */
  const float dtMs = dt * 1000.0f;
  for (uint16_t i = 0; i < GALAXY_PARTICLE_COUNT; ++i) {
    Particle& p = _particles[i];
    p.angleQ8 += (int32_t)((float)p.speedQ8 * dtMs / 1000.0f);
    if (p.angleQ8 > (fx::ANGLE_STEPS * 256)) p.angleQ8 -= (fx::ANGLE_STEPS * 256);
  }

  /* planets orbit */
  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    Planet& p = _planets[i];
    p.angleQ8 += (int32_t)((float)p.speedQ8 * dt);
    while (p.angleQ8 < 0) p.angleQ8 += (fx::ANGLE_STEPS * 256);
    while (p.angleQ8 >= (fx::ANGLE_STEPS * 256)) p.angleQ8 -= (fx::ANGLE_STEPS * 256);
  }

  /* gravity strength ramp */
  const float gf = clampf(dt * 8.0f, 0.0f, 1.0f);
  _gravityStrength += (_gravityTarget - _gravityStrength) * gf;
  if (_gravityStrength < 0.004f) _gravityStrength = 0.0f;
}

/* -------------------------------------------------------------------------- */
/*  Rendering                                                                 */
/* -------------------------------------------------------------------------- */
void GalaxyEngine::render(uint32_t now) {
  if (!_canvas || !_bg) return;
  SpaceCanvas& c = *_canvas;

  c.copyBlock(_bg, _bgW, (int16_t)_bgOffX, (int16_t)_bgOffY, SCREEN_W, SCREEN_H);

  drawStars(now, false);
  drawGalaxy(now);
  drawPlanets(now);
  drawStars(now, true);
}

void GalaxyEngine::drawStars(uint32_t now, bool foreground) {
  SpaceCanvas& c = *_canvas;
  const uint16_t ta = (uint16_t)(now >> 4);
  const bool g = _gravityStrength > 0.01f;

  for (uint16_t i = 0; i < STAR_COUNT; ++i) {
    const Star& s = _stars[i];
    if ((s.depth == 2) != foreground) continue;

    float x = s.x, y = s.y;
    if (g) {
      const float dx = x - _gravityX;
      const float dy = y - _gravityY;
      const float d2 = dx * dx + dy * dy;
      if (d2 < 8100.0f && d2 > 1.0f) {
        const float d = sqrtf(d2);
        const float pull = (90.0f - d) / 90.0f * 26.0f * _gravityStrength;
        x -= dx / d * pull;
        y -= dy / d * pull;
      }
    }

    const int16_t idx = (int16_t)(s.phase + ta * s.twinkleSpeed);
    const int16_t sv = fx::sinT((uint16_t)idx);
    int a = (int)s.bright + ((int)s.bright * sv >> 11);
    if (a < 14) a = 14;
    if (a > 255) a = 255;

    const int16_t xi = (int16_t)x;
    const int16_t yi = (int16_t)y;
    if ((uint16_t)xi >= SCREEN_W || (uint16_t)yi >= SCREEN_H) continue;

    if (foreground && s.bright > 150) {
      c.addGlow(xi, yi, 6, s.color, (uint8_t)(a / 3));
      c.addPixel(xi, yi, 0xFFFF, (uint8_t)a);
      c.addPixel((int16_t)(xi - 1), yi, s.color, (uint8_t)(a / 3));
      c.addPixel((int16_t)(xi + 1), yi, s.color, (uint8_t)(a / 3));
      c.addPixel(xi, (int16_t)(yi - 1), s.color, (uint8_t)(a / 3));
      c.addPixel(xi, (int16_t)(yi + 1), s.color, (uint8_t)(a / 3));
    } else if (s.depth == 0) {
      c.addPixel(xi, yi, s.color, (uint8_t)a);
    } else {
      c.addPixel(xi, yi, s.color, (uint8_t)a);
      c.addPixel((int16_t)(xi + 1), yi, s.color, (uint8_t)(a / 2));
      c.addPixel(xi, (int16_t)(yi + 1), s.color, (uint8_t)(a / 3));
    }
  }
}

void GalaxyEngine::drawGalaxy(uint32_t now) {
  SpaceCanvas& c = *_canvas;

  const int16_t cx = (int16_t)_cx;
  const int16_t cy = (int16_t)_cy;
  const float pulse = 0.5f + 0.5f * sinf(now * 0.0017f);
  const float zoom = _zoom;
  const uint16_t ta = (uint16_t)(now >> 4);

  /* wide, faint halo (mask bank tops out at 128 px, which is plenty) */
  c.addGlow(cx, cy, 128, rgb565(0x2B, 0x1F, 0x66), 34);
  c.addGlow(cx, cy, 104, rgb565(0x2F, 0x55, 0xB8), 32);

  const bool g = _gravityStrength > 0.01f;

  for (uint16_t i = 0; i < GALAXY_PARTICLE_COUNT; ++i) {
    const Particle& p = _particles[i];
    const uint16_t angle = (uint16_t)(p.angleQ8 >> 8);
    const int16_t cs = fx::cosT(angle);
    const int16_t sn = fx::sinT(angle);
    const int16_t rr = (int16_t)(p.radius * zoom);

    int16_t x = (int16_t)(cx + fx::mulQ10(cs, rr));
    int16_t y = (int16_t)(cy + (int16_t)(((int32_t)fx::mulQ10(sn, rr) * 650) >> 10));

    if (g) {
      const int16_t dx = (int16_t)(x - _gravityX);
      const int16_t dy = (int16_t)(y - _gravityY);
      const int32_t d2 = (int32_t)dx * dx + (int32_t)dy * dy;
      if (d2 < 4900 && d2 > 4) {
        const float d = sqrtf((float)d2);
        const float pull = (70.0f - d) / 70.0f * 16.0f * _gravityStrength;
        x = (int16_t)(x - dx / d * pull - dy / d * pull * 0.45f);
        y = (int16_t)(y - dy / d * pull + dx / d * pull * 0.45f);
      }
    }

    if (x < -6 || x >= SCREEN_W + 6 || y < -6 || y >= SCREEN_H + 6) continue;

    const int16_t sv = fx::sinT((uint16_t)(p.twinklePhase + ta * 2));
    int a = (int)p.bright + ((int)p.bright * sv >> 12);
    if (a < 24) a = 24;
    if (a > 255) a = 255;

    int16_t diam = (p.size == 0) ? 5 : (p.size == 1 ? 8 : 13);
    if (zoom > 1.4f) diam = (int16_t)(diam + 2);
    if (p.bright > 200 && p.size == 2) {
      c.addGlow(x, y, 16, p.color, (uint8_t)(a / 4));
    }
    c.addGlow(x, y, diam, p.color, (uint8_t)a);
  }

  /* bright pulsing core */
  const int16_t core = (int16_t)((34 + 10 * pulse) * zoom);
  c.addGlow(cx, cy, (int16_t)(core * 3), rgb565(0x55, 0x46, 0xC8), 70);
  c.addGlow(cx, cy, (int16_t)(core * 2), rgb565(0x77, 0x9C, 0xFF), 120);
  c.addGlow(cx, cy, core, rgb565(0xD9, 0xEC, 0xFF), (uint8_t)(170 + 50 * pulse));
  c.addGlow(cx, cy, (int16_t)(core * 0.62f), 0xFFFF, 220);
  c.addGlow(cx, cy, 124, rgb565(0x26, 0x2A, 0x78), (uint8_t)(26 + 10 * pulse));
}

void GalaxyEngine::drawPlanets(uint32_t now) {
  (void)now;
  SpaceCanvas& c = *_canvas;
  const float zoom = _zoom;

  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    Planet& p = _planets[i];
    if (!p.pixels || !p.alpha) continue;

    const uint16_t angle = (uint16_t)(p.angleQ8 >> 8);
    const int16_t cs = fx::cosT(angle);
    const int16_t sn = fx::sinT(angle);
    const int16_t orbit = (int16_t)(p.orbit * zoom);
    const int16_t x = (int16_t)(_cx + fx::mulQ10(cs, orbit));
    const int16_t y = (int16_t)(_cy + (int16_t)(((int32_t)fx::mulQ10(sn, orbit) * 600) >> 10));

    if (x < -p.w || x > SCREEN_W + p.w || y < -p.h || y > SCREEN_H + p.h) continue;

    c.addGlow(x, y, p.radius * 4, p.color, 26);
    c.blendSprite(p.pixels, p.alpha, p.w, p.h,
                  (int16_t)(x - p.w / 2), (int16_t)(y - p.h / 2));
  }
}
