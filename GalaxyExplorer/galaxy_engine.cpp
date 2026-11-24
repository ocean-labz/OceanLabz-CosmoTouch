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

constexpr int32_t kOrbitSquashQ10 = 660;   /* elliptical solar-system view */

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint8_t clamp255(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline uint8_t r8(uint16_t c) { return (uint8_t)(((c >> 11) & 31) * 255 / 31); }
inline uint8_t g8(uint16_t c) { return (uint8_t)(((c >> 5) & 63) * 255 / 63); }
inline uint8_t b8(uint16_t c) { return (uint8_t)((c & 31) * 255 / 31); }

inline uint16_t mix565(uint16_t a, uint16_t b, float t) {
  t = clampf(t, 0.0f, 1.0f);
  return rgb565(
      (uint8_t)(r8(a) + (r8(b) - r8(a)) * t),
      (uint8_t)(g8(a) + (g8(b) - g8(a)) * t),
      (uint8_t)(b8(a) + (b8(b) - b8(a)) * t));
}

inline uint16_t scale565f(uint16_t c, float m) {
  return rgb565(clamp255((int)(r8(c) * m)), clamp255((int)(g8(c) * m)),
                clamp255((int)(b8(c) * m)));
}

inline uint16_t blend565(uint16_t dst, uint16_t src, uint8_t a) {
  const int ia = 255 - a;
  return rgb565(
      (uint8_t)((r8(dst) * ia + r8(src) * a) / 255),
      (uint8_t)((g8(dst) * ia + g8(src) * a) / 255),
      (uint8_t)((b8(dst) * ia + b8(src) * a) / 255));
}

inline float smoothstepf(float e0, float e1, float x) {
  const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline uint32_t hash2(int32_t x, int32_t y, uint32_t seed);
float fbm3(float x, float y, uint32_t seed);

/* -------------------------------------------------------------------------- */
/*  Planet metadata                                                           */
/* -------------------------------------------------------------------------- */
const PlanetInfo kPlanetInfo[PLANET_COUNT] = {
  { "MERCURY", "TERRESTRIAL",
    "Almost no atmosphere; trace sodium and helium.",
    "Grey cratered rock, cliffs and scorched plains.",
    "Day 430 C, night -180 C.",
    rgb565(0xB8, 0xB4, 0xAA) },
  { "VENUS", "TERRESTRIAL",
    "Thick carbon dioxide with sulphuric acid clouds.",
    "Volcanic plains under a crushing greenhouse.",
    "Hottest planet: about 465 C.",
    rgb565(0xF2, 0xC8, 0x78) },
  { "EARTH", "TERRESTRIAL",
    "Nitrogen-oxygen atmosphere with water clouds.",
    "Liquid oceans, continents and rich ecosystems.",
    "The only known world with life.",
    rgb565(0x54, 0xE6, 0xFF) },
  { "MARS", "TERRESTRIAL",
    "Thin carbon dioxide atmosphere; dusty and cold.",
    "Rust-red deserts, craters and ancient valleys.",
    "Olympus Mons, the tallest volcano.",
    rgb565(0xFF, 0x8A, 0x4C) },
  { "JUPITER", "GAS GIANT",
    "Hydrogen and helium with violent storm bands.",
    "No solid surface, only deepening cloud layers.",
    "Great Red Spot: an ancient storm.",
    rgb565(0xE8, 0xB4, 0x86) },
  { "SATURN", "GAS GIANT",
    "Hydrogen and helium with ammonia cloud bands.",
    "Pale golden gas giant inside icy rings.",
    "Its rings are billions of icy particles.",
    rgb565(0xFF, 0xD4, 0x5E) },
  { "URANUS", "ICE GIANT",
    "Hydrogen, helium and methane; methane is cyan.",
    "Cold, smooth ice giant with faint bands.",
    "Rotates on its side after a collision.",
    rgb565(0xA8, 0xF0, 0xEA) },
  { "NEPTUNE", "ICE GIANT",
    "Hydrogen, helium and methane; deep blue.",
    "Ice giant with dark storms and fast winds.",
    "Winds exceed 2,000 km/h.",
    rgb565(0x4C, 0x7C, 0xFF) },
};

/* Projected ring sample in planet-sprite / disc space. */
bool ringSample(float dx, float dy, uint16_t accent, uint16_t& out, uint8_t& a) {
  const float ex = dx / 1.78f;
  const float ey = dy / (1.78f * 0.34f);
  const float rr = sqrtf(ex * ex + ey * ey);
  if (rr < 0.72f || rr > 1.03f) return false;
  const float edge = clampf(1.0f - fabsf(rr - 0.87f) / 0.16f, 0.0f, 1.0f);
  const float band = 0.68f + 0.32f * sinf(rr * 44.0f);
  out = mix565(rgb565(0xBE, 0xB0, 0x98), accent, 0.34f);
  out = scale565f(out, band);
  a = (uint8_t)(132.0f * edge);
  return a > 2;
}

/* -------------------------------------------------------------------------- */
/*  Procedural equirectangular textures                                       */
/* -------------------------------------------------------------------------- */
void bakePlanetTexture(PlanetKind kind, uint16_t* tex) {
  constexpr int W = GalaxyEngine::PLANET_TEX_W;
  constexpr int H = GalaxyEngine::PLANET_TEX_H;
  const uint32_t seed = 0x51A7u + (uint32_t)kind * 977u;

  for (int y = 0; y < H; ++y) {
    const float v = (y + 0.5f) / H;

    for (int x = 0; x < W; ++x) {
      const uint16_t ang = (uint16_t)(((uint32_t)x * 512u) / W) & 511u;
      const float ca = fx::cosT(ang) * (1.0f / 1024.0f);
      const float sa = fx::sinT(ang) * (1.0f / 1024.0f);
      const float n1 = fbm3(ca * 2.15f + 7.0f, sa * 2.15f + v * 4.6f, seed);
      const float n2 = fbm3(ca * 5.30f + 3.0f, sa * 5.30f + v * 11.5f, seed + 11);
      uint16_t c;

      switch (kind) {
        case PlanetKind::MERCURY: {
          const float h = clampf(n1 * 0.80f + n2 * 0.30f, 0.0f, 1.0f);
          c = mix565(rgb565(0x3E, 0x3C, 0x39), rgb565(0xB9, 0xB4, 0xA9), h);
          if (n2 < 0.39f) c = mix565(c, rgb565(0x4A, 0x47, 0x43), 0.55f);
          if (n2 > 0.67f) c = mix565(c, rgb565(0xD8, 0xD3, 0xC7),
                                    clampf((n2 - 0.67f) * 1.5f, 0.0f, 0.55f));
          const float rays = fabsf(sinf(ca * 8.0f + v * 19.0f + n1 * 3.0f));
          if (rays < 0.045f) c = mix565(c, rgb565(0x74, 0x6E, 0x65), 0.30f);
          break;
        }
        case PlanetKind::VENUS: {
          const float swirl = fbm3(ca * 3.1f + n2 * 2.7f, sa * 3.1f + v * 13.0f, seed + 61);
          const float bands = 0.5f + 0.5f * sinf(v * 27.0f + swirl * 5.5f);
          c = mix565(rgb565(0xA9, 0x78, 0x35), rgb565(0xF8, 0xDF, 0xA1), bands);
          c = mix565(c, rgb565(0xE8, 0xC2, 0x72), clampf((swirl - 0.38f) * 1.4f, 0.0f, 0.65f));
          c = mix565(c, rgb565(0xFF, 0xF0, 0xC8), clampf((n2 - 0.64f) * 1.8f, 0.0f, 0.40f));
          if (v < 0.06f || v > 0.94f) c = mix565(c, rgb565(0xD8, 0xB8, 0x7E), 0.55f);
          break;
        }
        case PlanetKind::EARTH: {
          c = rgb565((uint8_t)(6 + n2 * 16.0f), (uint8_t)(26 + n2 * 24.0f),
                     (uint8_t)(68 + n2 * 48.0f));
          const float land = n1 - 0.515f;
          if (land > 0.0f) {
            const float h = clampf(land * 3.4f, 0.0f, 1.0f);
            c = mix565(rgb565(0x1B, 0x5C, 0x36), rgb565(0x8E, 0x84, 0x4F), h);
            c = mix565(c, rgb565(0xA8, 0x96, 0x68),
                       clampf((n2 - 0.56f) * 1.0f, 0.0f, 0.50f));
            if (h > 0.70f) c = mix565(c, rgb565(0xD0, 0xD2, 0xC6), (h - 0.70f) * 1.4f);
          } else if (land > -0.028f) {
            c = mix565(c, rgb565(0x2A, 0xA0, 0xB4), 0.52f);
          }
          if (v < 0.052f) c = mix565(c, rgb565(0xE4, 0xF4, 0xFF), 1.0f - v / 0.052f);
          if (v > 0.948f) c = mix565(c, rgb565(0xE4, 0xF4, 0xFF), (v - 0.948f) / 0.052f);
          if (v > 0.075f && v < 0.925f) {
            const float cl = fbm3(ca * 4.7f + 21.0f, sa * 4.7f + v * 10.5f, seed + 29);
            const float a = clampf((cl - 0.60f) * 2.8f, 0.0f, 0.72f);
            if (a > 0.0f) c = mix565(c, rgb565(0xE8, 0xF4, 0xFF), a);
          }
          break;
        }
        case PlanetKind::MARS: {
          const float h = clampf(n1 * 0.78f + n2 * 0.34f, 0.0f, 1.0f);
          c = mix565(rgb565(0x4C, 0x20, 0x18), rgb565(0xC6, 0x62, 0x37), h);
          if (n2 < 0.38f) c = mix565(c, rgb565(0x48, 0x22, 0x1A), 0.55f);
          if (n2 > 0.66f) c = mix565(c, rgb565(0xE2, 0x9A, 0x66),
                                    clampf((n2 - 0.66f) * 1.3f, 0.0f, 0.45f));
          const float canyon = fabsf(sinf(v * 18.0f + n1 * 6.0f + ca * 3.0f));
          if (canyon < 0.075f) c = mix565(c, rgb565(0x5C, 0x26, 0x1E), 0.42f);
          if (v < 0.06f) c = mix565(c, rgb565(0xE6, 0xE0, 0xD4), 1.0f - v / 0.06f);
          if (v > 0.94f) c = mix565(c, rgb565(0xE6, 0xE0, 0xD4), (v - 0.94f) / 0.06f);
          break;
        }
        case PlanetKind::JUPITER: {
          const float turb = fbm3(ca * 2.8f + 2.0f, sa * 2.8f + v * 16.0f, seed + 13);
          const float bands = 0.5f + 0.5f * sinf(v * 35.0f + turb * 4.2f + 0.4f);
          const float fine = 0.5f + 0.5f * sinf(v * 91.0f + turb * 2.0f);
          c = mix565(rgb565(0x8A, 0x58, 0x38), rgb565(0xF0, 0xD2, 0xA4), bands);
          c = mix565(c, rgb565(0xE4, 0xB0, 0x78), fine * 0.32f);
          c = mix565(c, rgb565(0xC0, 0x8A, 0x62),
                     clampf((turb - 0.58f) * 1.6f, 0.0f, 0.35f));
          if (v < 0.08f || v > 0.92f) c = mix565(c, rgb565(0x8E, 0x72, 0x5A), 0.55f);
          break;
        }
        case PlanetKind::SATURN: {
          const float bands = 0.5f + 0.5f * sinf(v * 38.0f + n1 * 3.0f + 0.6f);
          const float fine = 0.5f + 0.5f * sinf(v * 84.0f + n2 * 2.1f);
          c = mix565(rgb565(0x92, 0x70, 0x48), rgb565(0xF4, 0xDC, 0xAC), bands);
          c = mix565(c, rgb565(0xDC, 0xBC, 0x86), fine * 0.34f);
          c = mix565(c, rgb565(0x6E, 0x52, 0x38),
                     clampf((n2 - 0.58f) * 1.3f, 0.0f, 0.30f));
          if (v < 0.09f || v > 0.91f) c = mix565(c, rgb565(0xA8, 0x8A, 0x64), 0.55f);
          break;
        }
        case PlanetKind::URANUS: {
          const float bands = 0.5f + 0.5f * sinf(v * 21.0f + n1 * 1.7f);
          c = mix565(rgb565(0x62, 0xB8, 0xBE), rgb565(0xC2, 0xF2, 0xF0), bands);
          c = mix565(c, rgb565(0x8E, 0xD8, 0xDC),
                     clampf((n2 - 0.48f) * 0.75f, 0.0f, 0.28f));
          if (v < 0.10f || v > 0.90f) c = mix565(c, rgb565(0xB4, 0xE8, 0xEA), 0.35f);
          break;
        }
        case PlanetKind::NEPTUNE:
        default: {
          const float turb = fbm3(ca * 3.3f + 5.0f, sa * 3.3f + v * 12.0f, seed + 43);
          const float bands = 0.5f + 0.5f * sinf(v * 28.0f + turb * 4.0f + 0.2f);
          c = mix565(rgb565(0x12, 0x2A, 0x82), rgb565(0x4E, 0x86, 0xE8), bands);
          c = mix565(c, rgb565(0x2A, 0x4C, 0xB4),
                     clampf((turb - 0.50f) * 1.2f, 0.0f, 0.42f));
          c = mix565(c, rgb565(0x9E, 0xC4, 0xFF),
                     clampf((n2 - 0.68f) * 1.4f, 0.0f, 0.30f));
          if (v < 0.08f || v > 0.92f) c = mix565(c, rgb565(0x68, 0x92, 0xD8), 0.35f);
          break;
        }
      }

      tex[y * W + x] = c;
    }
  }

  /* Craters are stamped afterwards so they read as surface relief. */
  if (kind == PlanetKind::MERCURY || kind == PlanetKind::MARS) {
    const uint8_t count = (kind == PlanetKind::MERCURY) ? 58 : 38;
    const uint16_t dark = (kind == PlanetKind::MERCURY) ? rgb565(0x4C, 0x49, 0x45)
                                                       : rgb565(0x4A, 0x22, 0x1A);
    const uint16_t light = (kind == PlanetKind::MERCURY) ? rgb565(0xD8, 0xD3, 0xC7)
                                                        : rgb565(0xD8, 0x8C, 0x5A);
    for (uint8_t i = 0; i < count; ++i) {
      const uint32_t h = hash2(i, (int32_t)kind, seed + 77u);
      const int cu = (int)(h & 0xFF);
      const int cv = 16 + (int)((h >> 8) % 96);
      const int cr = (kind == PlanetKind::MERCURY) ? (1 + (int)((h >> 16) % 4))
                                                   : (2 + (int)((h >> 16) % 4));
      for (int dy = -cr - 1; dy <= cr + 1; ++dy) {
        for (int dx = -cr - 1; dx <= cr + 1; ++dx) {
          const float d = sqrtf((float)(dx * dx + dy * dy));
          if (d > cr + 1.0f) continue;
          const int px = (cu + dx + W) % W;
          const int py = cv + dy;
          if (py < 0 || py >= H) continue;
          uint16_t& t = tex[py * W + px];
          if (d < cr) t = mix565(t, dark, 0.38f * (1.0f - d / (cr + 1.0f)));
          else if (d < cr + 1.0f) t = mix565(t, light, 0.22f * (cr + 1.0f - d));
        }
      }
    }
  }

  /* Large storm ovals are painted after the cloud bands so they stay crisp. */
  auto stampSpot = [&](int cu, int cv, int ru, int rv, uint16_t core, uint16_t rim, float strength) {
    for (int dy = -rv - 2; dy <= rv + 2; ++dy) {
      for (int dx = -ru - 2; dx <= ru + 2; ++dx) {
        const float ex = (float)dx / (float)(ru + 2);
        const float ey = (float)dy / (float)(rv + 2);
        const float d = sqrtf(ex * ex + ey * ey);
        if (d > 1.0f) continue;
        const int px = (cu + dx + W) % W;
        const int py = cv + dy;
        if (py < 0 || py >= H) continue;
        uint16_t& t = tex[py * W + px];
        const float edge = smoothstepf(0.55f, 1.0f, d);
        t = mix565(t, core, strength * (1.0f - edge * 0.55f));
        if (d > 0.78f) t = mix565(t, rim, 0.20f * strength);
      }
    }
  };

  if (kind == PlanetKind::JUPITER) {
    stampSpot((int)(W * 0.72f), (int)(H * 0.61f), 19, 9,
              rgb565(0xC8, 0x4A, 0x2E), rgb565(0xF2, 0xB8, 0x8A), 0.88f);
  } else if (kind == PlanetKind::NEPTUNE) {
    stampSpot((int)(W * 0.30f), (int)(H * 0.64f), 12, 6,
              rgb565(0x10, 0x22, 0x62), rgb565(0x8A, 0xB4, 0xE8), 0.72f);
  }
}

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

  /* Pure black space. Nebula colour is additive and strictly local, so most
   * of the buffer stays exact #000000. */
  for (int16_t y = 0; y < _bgH; ++y) {
    for (int16_t x = 0; x < _bgW; ++x) {
      const float n1 = fbm3(x * 0.0115f, y * 0.0115f, 11);
      const float n2 = fbm3(x * 0.026f + 37.0f, y * 0.023f - 19.0f, 71);
      const float n3 = fbm3(x * 0.0055f + 91.0f, y * 0.0055f + 13.0f, 137);

      /* Keep colour strictly local. These two soft zones leave the rest of
       * the background at exact #000000 instead of washing the whole panel. */
      const float dx1 = (x - _bgW * 0.24f) / 86.0f;
      const float dy1 = (y - _bgH * 0.30f) / 92.0f;
      const float dx2 = (x - _bgW * 0.82f) / 78.0f;
      const float dy2 = (y - _bgH * 0.70f) / 84.0f;
      const float zone1 = clampf(1.0f - sqrtf(dx1 * dx1 + dy1 * dy1), 0.0f, 1.0f);
      const float zone2 = clampf(1.0f - sqrtf(dx2 * dx2 + dy2 * dy2), 0.0f, 1.0f);
      const float zone = zone1 * zone1 + zone2 * zone2 * 0.80f;
      if (zone < 0.01f) {
        _bg[(size_t)y * _bgW + x] = SPACE_BG;
        continue;
      }

      const float cloudA = smoothstepf(0.57f, 0.83f, n1 * 0.68f + n2 * 0.32f) * zone;
      const float cloudB = smoothstepf(0.60f, 0.86f, n2 * 0.72f + n1 * 0.28f) * zone;
      if (cloudA < 0.01f && cloudB < 0.01f) {
        _bg[(size_t)y * _bgW + x] = SPACE_BG;
        continue;
      }

      /* Violet / cyan / magenta nebula zones, kept inside the cloud mask. */
      const float a = cloudA * cloudA;
      const float b = cloudB * cloudB;
      const float tint = n3;
      float r = a * (52.0f + tint * 58.0f) + b * (12.0f + tint * 22.0f);
      float g = a * (16.0f + tint * 36.0f) + b * (36.0f + tint * 52.0f);
      float bl = a * (104.0f + tint * 76.0f) + b * (78.0f + tint * 54.0f);

      _bg[(size_t)y * _bgW + x] = rgb565(
          clamp255((int)r), clamp255((int)g), clamp255((int)bl));
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
    const uint8_t brightness = (uint8_t)(30 + (fx::rand() % 62));
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
  /* Sparse solar corona dust, not a spiral galaxy. These motes sit just above
   * the photosphere and make the Sun feel active without hiding the planets. */
  for (uint16_t i = 0; i < GALAXY_PARTICLE_COUNT; ++i) {
    Particle& p = _particles[i];
    p.radius = (uint8_t)(19 + (fx::rand() % 16));
    p.angleQ8 = (int32_t)(fx::randFloat() * (fx::ANGLE_STEPS * 256.0f));
    p.angleQ8 &= 0x1FFFF;
    p.speedQ8 = (uint16_t)((2 + (fx::rand() % 4)) * 256);
    p.bright = (uint8_t)(40 + fx::rand() % 110);
    p.color = ((fx::rand() & 3) == 0) ? rgb565(0xFF, 0xF2, 0xC8)
                                     : rgb565(0xFF, 0xB8, 0x4A);
    p.size = (fx::rand() & 7) == 0 ? 1 : 0;
    p.twinklePhase = (uint16_t)(fx::rand() & 511);
  }
}

void GalaxyEngine::buildPlanets() {
  struct Def {
    int16_t radius, orbit;
    int16_t speed;
    PlanetKind kind;
    bool ring;
    uint16_t angle;
  };
  const Def defs[PLANET_COUNT] = {
    {  3,  30,   8, PlanetKind::MERCURY, false,  18 },
    {  5,  40,   6, PlanetKind::VENUS,   false,  86 },
    {  5,  51,   4, PlanetKind::EARTH,   false, 154 },
    {  4,  63,   3, PlanetKind::MARS,    false, 222 },
    { 12,  80,   2, PlanetKind::JUPITER, false, 290 },
    { 10,  94,   2, PlanetKind::SATURN,  true,  358 },
    {  7, 104,   1, PlanetKind::URANUS,  false, 426 },
    {  7, 112,   1, PlanetKind::NEPTUNE, false, 494 },
  };

  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    Planet& p = _planets[i];
    p.radius = defs[i].radius;
    p.orbit = defs[i].orbit;
    p.speedQ8 = (int16_t)(defs[i].speed * 256);
    p.angleQ8 = (int32_t)defs[i].angle << 8;
    p.kind = defs[i].kind;
    p.ring = defs[i].ring;
    p.color = kPlanetInfo[i].accent;

    if (!p.surface) {
      p.surface = (uint16_t*)heap_caps_malloc(
          (size_t)PLANET_TEX_W * PLANET_TEX_H * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!p.surface) continue;
    bakePlanetTexture(p.kind, p.surface);

    const int16_t extent = p.ring ? (int16_t)(p.radius * 1.78f) : p.radius;
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
    buildPlanetSprite(p);
  }
}

void GalaxyEngine::buildPlanetSprite(Planet& p) {
  if (!p.surface || !p.pixels || !p.alpha) return;
  const int16_t size = p.w;
  const float R = (float)p.radius;
  const int rot = (int)((p.spin + p.orbit * 3 + p.radius * 7) & 255);

  for (int16_t py = 0; py < size; ++py) {
    for (int16_t px = 0; px < size; ++px) {
      const float dx = (px + 0.5f - size * 0.5f) / R;
      const float dy = (py + 0.5f - size * 0.5f) / R;
      uint16_t out = 0;
      uint8_t outA = 0;

      /* Rear half of the ring first. */
      if (p.ring) {
        uint16_t rc = 0;
        uint8_t ra = 0;
        if (ringSample(dx, dy, p.color, rc, ra)) {
          out = rc;
          outA = ra;
        }
      }

      const float d2 = dx * dx + dy * dy;
      if (d2 <= 1.0f) {
        const float z = sqrtf(1.0f - d2);
        const float lon = atan2f(dx, z);
        const float lat = asinf(clampf(dy, -1.0f, 1.0f));
        int u = (int)((lon / (2.0f * (float)M_PI) + 0.5f) * PLANET_TEX_W) + rot;
        u &= (PLANET_TEX_W - 1);
        int v = (int)((lat / (float)M_PI + 0.5f) * PLANET_TEX_H);
        v = (int)clampf((float)v, 0.0f, (float)(PLANET_TEX_H - 1));
        out = p.surface[v * PLANET_TEX_W + u];

        const float diff = clampf(-dx * 0.52f - dy * 0.60f + z * 0.58f, 0.0f, 1.0f);
        const float shade = (0.16f + 0.92f * diff) * (0.82f + 0.18f * z);
        out = scale565f(out, shade);

        const float rim = powf(1.0f - z, 3.0f) * 0.70f;
        out = mix565(out, p.color, rim);

        /* Coverage: opaque across the disc, soft only at the limb. */
        const float edge = clampf((1.0f - sqrtf(d2)) * R * 1.25f, 0.0f, 1.0f);
        outA = (uint8_t)(edge * 255.0f);
      }

      /* Front half of the ring lies over the lower hemisphere. */
      if (p.ring && d2 <= 1.0f && dy > 0.04f) {
        uint16_t rc = 0;
        uint8_t ra = 0;
        if (ringSample(dx, dy, p.color, rc, ra)) {
          out = blend565(out, rc, ra);
          outA = 255;
        }
      }

      p.pixels[(size_t)py * size + px] = out;
      p.alpha[(size_t)py * size + px] = outA;
    }
  }
}

/* -------------------------------------------------------------------------- */
/*  Planet queries / selection                                                */
/* -------------------------------------------------------------------------- */
const PlanetInfo& GalaxyEngine::planetInfo(uint8_t index) const {
  return kPlanetInfo[index < PLANET_COUNT ? index : 0];
}

int16_t GalaxyEngine::planetRadius(uint8_t index) const {
  return index < PLANET_COUNT ? _planets[index].radius : 0;
}

const uint16_t* GalaxyEngine::planetSurface(uint8_t index) const {
  return index < PLANET_COUNT ? _planets[index].surface : nullptr;
}

PlanetKind GalaxyEngine::planetKind(uint8_t index) const {
  return index < PLANET_COUNT ? _planets[index].kind : PlanetKind::EARTH;
}

uint16_t GalaxyEngine::planetAccent(uint8_t index) const {
  return index < PLANET_COUNT ? _planets[index].color : UI_ACCENT_CYAN;
}

void GalaxyEngine::computePlanetPos(const Planet& p, int16_t& x, int16_t& y) const {
  const uint16_t angle = (uint16_t)(p.angleQ8 >> 8);
  const int16_t orbit = (int16_t)(p.orbit * _zoom);
  x = (int16_t)(_cx + fx::mulQ10(fx::cosT(angle), orbit));
  y = (int16_t)(_cy + (int16_t)(((int32_t)fx::mulQ10(fx::sinT(angle), orbit) *
                                 kOrbitSquashQ10) >> 10));
}

bool GalaxyEngine::planetScreenPos(uint8_t index, int16_t& x, int16_t& y) const {
  if (index >= PLANET_COUNT) return false;
  computePlanetPos(_planets[index], x, y);
  return true;
}

int8_t GalaxyEngine::planetAt(int16_t x, int16_t y, int16_t pad) const {
  int8_t best = -1;
  int32_t bestD = 0x7FFFFFFF;
  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    const Planet& p = _planets[i];
    if (!p.pixels) continue;
    int16_t px, py;
    computePlanetPos(p, px, py);
    const int32_t dx = x - px;
    const int32_t dy = y - py;
    const int32_t hit = (int32_t)(p.radius + pad + (p.ring ? 8 : 0));
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 <= hit * hit && d2 < bestD) {
      bestD = d2;
      best = (int8_t)i;
    }
  }
  return best;
}

void GalaxyEngine::selectPlanet(int8_t index) {
  if (index < -1 || index >= PLANET_COUNT) return;
  _selected = index;
}

/* -------------------------------------------------------------------------- */
/*  Reset / interaction                                                       */
/* -------------------------------------------------------------------------- */
void GalaxyEngine::reset(bool reseed) {
  _selected = -1;
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
    /* Keep the eight starting angles spread around the Sun so every planet is
     * immediately visible and selectable; their different speeds then drift
     * them into new configurations. */
    for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
      _planets[i].angleQ8 = (int32_t)(18 + i * 68) << 8;
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

  /* Subtle axial rotation without per-frame trig: rebuild one tiny sprite at
   * a time, spreading the work across frames. */
  if (now - _lastSpin >= 140) {
    _lastSpin = now;
    Planet& p = _planets[_spinCursor];
    p.spin = (uint8_t)(p.spin + 1);
    buildPlanetSprite(p);
    _spinCursor = (uint8_t)((_spinCursor + 1) % PLANET_COUNT);
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
  drawOrbits();
  drawSolarDust(now);
  drawSun(now);
  drawPlanets(now);
  drawStars(now, true);
  drawSelection(now);
}

void GalaxyEngine::drawSelection(uint32_t now) {
  if (_selected < 0 || _selected >= PLANET_COUNT) return;
  const Planet& p = _planets[_selected];
  if (!p.surface) return;

  int16_t x, y;
  computePlanetPos(p, x, y);
  const PlanetInfo& info = kPlanetInfo[_selected];
  const uint16_t accent = info.accent;

  const int16_t sv = fx::sinT((uint16_t)(now >> 4));
  const int16_t rad = (int16_t)(p.radius + 7 + ((sv * 2) >> 10));
  SpaceCanvas& c = *_canvas;
  c.addGlow(x, y, p.radius * 4, accent, 24);
  c.addCircleOutline(x, y, rad, accent, 145);
  c.addCircleOutline(x, y, (int16_t)(rad + 3), accent, 55);

  /* Four tiny selection ticks. */
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t a = (uint16_t)(i * (fx::ANGLE_STEPS / 4) + (now >> 6));
    const int16_t tx = (int16_t)(x + fx::mulQ10(fx::cosT(a), (rad + 5)));
    const int16_t ty = (int16_t)(y + fx::mulQ10(fx::sinT(a), (rad + 5)));
    c.addPixel(tx, ty, 0xFFFF, 220);
    c.addPixel((int16_t)(tx + 1), ty, accent, 120);
  }

  int16_t ly = (int16_t)(y + p.radius + (p.ring ? p.radius * 0.55f : 0.0f) + 8);
  if (ly > 220) ly = (int16_t)(y - p.radius - 20);
  ly = (int16_t)clampf((float)ly, 28.0f, 236.0f);
  const int16_t w = (int16_t)(SpaceCanvas::textWidth(info.name, 1) + 14);
  c.blendRoundRect((int16_t)(x - w / 2), ly, w, 14, 7,
                   rgb565(0x01, 0x03, 0x08), 170);
  c.drawText(x, (int16_t)(ly + 3), info.name, 1, accent, 245, TextAlign::CENTER);
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

void GalaxyEngine::drawOrbits() {
  SpaceCanvas& c = *_canvas;
  const int16_t cx = (int16_t)_cx;
  const int16_t cy = (int16_t)_cy;

  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    const Planet& p = _planets[i];
    const int16_t orbit = (int16_t)(p.orbit * _zoom);
    const uint16_t tint = rgb565(0x4C, 0x70, 0x8C);
    for (uint16_t seg = 0; seg < 128; ++seg) {
      const uint16_t a = (uint16_t)(seg * (fx::ANGLE_STEPS / 128));
      const int16_t x = (int16_t)(cx + fx::mulQ10(fx::cosT(a), orbit));
      const int16_t y = (int16_t)(cy + (int16_t)(((int32_t)fx::mulQ10(fx::sinT(a), orbit) *
                                                  kOrbitSquashQ10) >> 10));
      c.addPixel(x, y, tint, 68);
    }
  }
}

void GalaxyEngine::drawSolarDust(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  const int16_t cx = (int16_t)_cx;
  const int16_t cy = (int16_t)_cy;
  const uint16_t ta = (uint16_t)(now >> 4);

  for (uint16_t i = 0; i < GALAXY_PARTICLE_COUNT; ++i) {
    const Particle& p = _particles[i];
    const uint16_t angle = (uint16_t)(p.angleQ8 >> 8);
    const int16_t rr = (int16_t)(p.radius * _zoom);
    const int16_t x = (int16_t)(cx + fx::mulQ10(fx::cosT(angle), rr));
    const int16_t y = (int16_t)(cy + (int16_t)(((int32_t)fx::mulQ10(fx::sinT(angle), rr) *
                                                 kOrbitSquashQ10) >> 10));
    const int16_t sv = fx::sinT((uint16_t)(p.twinklePhase + ta * 2));
    int a = (int)p.bright + ((int)p.bright * sv >> 12);
    if (a < 28) a = 28;
    if (a > 170) a = 170;
    c.addPixel(x, y, p.color, (uint8_t)a);
    if (p.size == 1) c.addPixel((int16_t)(x + 1), y, p.color, (uint8_t)(a / 3));
  }
}

void GalaxyEngine::drawSun(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  const int16_t cx = (int16_t)_cx;
  const int16_t cy = (int16_t)_cy;
  const float pulse = 0.5f + 0.5f * sinf(now * 0.0017f);
  const int16_t R = (int16_t)(17 * _zoom);

  /* Layered corona. The mask bank tops out at 128 px. */
  c.addGlow(cx, cy, 128, rgb565(0xFF, 0x8A, 0x24), (uint8_t)(18 + pulse * 8));
  c.addGlow(cx, cy, 94, rgb565(0xFF, 0xAE, 0x3C), 58);
  c.addGlow(cx, cy, 58, rgb565(0xFF, 0xD8, 0x78), 118);
  c.addGlow(cx, cy, 34, rgb565(0xFF, 0xF0, 0xC0), 210);
  c.addCircleFill(cx, cy, R, rgb565(0xFF, 0xD8, 0x7A), 255);
  c.addCircleFill((int16_t)(cx - R / 5), (int16_t)(cy - R / 5),
                  (int16_t)(R * 0.62f), rgb565(0xFF, 0xF8, 0xE0), 225);

  /* Fine corona streamers, kept sparse. */
  for (uint8_t i = 0; i < 24; ++i) {
    const uint16_t a = (uint16_t)(i * (fx::ANGLE_STEPS / 24) + (now >> 7));
    const int16_t r0 = (int16_t)(R + 3);
    const int16_t r1 = (int16_t)(R + 6 + (i & 3) + pulse * 4);
    const int16_t x0 = (int16_t)(cx + fx::mulQ10(fx::cosT(a), r0));
    const int16_t y0 = (int16_t)(cy + fx::mulQ10(fx::sinT(a), r0));
    const int16_t x1 = (int16_t)(cx + fx::mulQ10(fx::cosT(a), r1));
    const int16_t y1 = (int16_t)(cy + fx::mulQ10(fx::sinT(a), r1));
    c.addLine(x0, y0, x1, y1, rgb565(0xFF, 0xC8, 0x64), 75);
  }
}

void GalaxyEngine::drawPlanets(uint32_t now) {
  (void)now;
  SpaceCanvas& c = *_canvas;

  for (uint8_t i = 0; i < PLANET_COUNT; ++i) {
    Planet& p = _planets[i];
    if (!p.pixels || !p.alpha) continue;

    int16_t x, y;
    computePlanetPos(p, x, y);

    if (x < -p.w || x > SCREEN_W + p.w || y < -p.h || y > SCREEN_H + p.h) continue;

    c.addGlow(x, y, p.radius * 4, p.color, 26);
    c.blendSprite(p.pixels, p.alpha, p.w, p.h,
                  (int16_t)(x - p.w / 2), (int16_t)(y - p.h / 2));
  }
}
