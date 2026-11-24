/* ==========================================================================
 * effects.cpp  -  Interactive space effects layer
 * ========================================================================== */
#include "effects.h"
#include <math.h>
#include <string.h>

namespace {
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kCyan  = rgb565(0x54, 0xE6, 0xFF);
constexpr uint16_t kViolet = rgb565(0xA4, 0x7A, 0xFF);
constexpr uint16_t kPurple = rgb565(0x7A, 0x3C, 0xE8);
constexpr uint16_t kYellow = rgb565(0xFF, 0xD4, 0x5E);
constexpr uint16_t kBlue   = rgb565(0x6F, 0xA8, 0xFF);

inline float clampf2(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

void Effects::begin(SpaceCanvas* canvas, GalaxyEngine* engine) {
  _canvas = canvas;
  _engine = engine;
  reset();
  _nextShooter = millis() + 1200;
}

void Effects::reset() {
  memset(_particles, 0, sizeof(_particles));
  memset(_ripples, 0, sizeof(_ripples));
  memset(_shooters, 0, sizeof(_shooters));
  memset(_stars, 0, sizeof(_stars));
  memset(_swirl, 0, sizeof(_swirl));
  _starCursor = 0;
  _bhActive = false;
  _bhIntensity = 0.0f;
  _bhReleaseAt = 0;
  _bhSpawnAccum = 0;
  _flashStart = 0;
  _label[0] = 0;
  _labelStart = _labelUntil = 0;
  if (_engine) _engine->setGravity(false, 0, 0);
}

/* -------------------------------------------------------------------------- */
/*  Spawning                                                                  */
/* -------------------------------------------------------------------------- */
void Effects::spawnBurst(int16_t x, int16_t y, uint8_t count, float speedMin,
                         float speedMax, uint16_t colorA, uint16_t colorB,
                         uint16_t lifeMin, uint16_t lifeMax, uint8_t size, uint8_t kind) {
  uint8_t spawned = 0;
  for (uint16_t i = 0; i < EFFECT_PARTICLE_MAX && spawned < count; ++i) {
    BurstParticle& p = _particles[i];
    if (p.lifeMs) continue;
    const float ang = fx::randFloat() * 2.0f * (float)M_PI;
    const float spd = speedMin + fx::randFloat() * (speedMax - speedMin);
    p.x = (float)x;
    p.y = (float)y;
    p.vx = cosf(ang) * spd;
    p.vy = sinf(ang) * spd;
    p.drag = 0.7f + fx::randFloat() * 1.4f;
    p.maxLifeMs = (uint16_t)(lifeMin + fx::rand() % (lifeMax - lifeMin + 1));
    p.lifeMs = p.maxLifeMs;
    p.color = (fx::rand() & 1) ? colorA : colorB;
    p.size = size;
    p.kind = kind;
    ++spawned;
  }
}

void Effects::addRipple(int16_t x, int16_t y, uint16_t maxRadius,
                        uint16_t durationMs, uint16_t color) {
  for (uint8_t i = 0; i < RIPPLE_MAX; ++i) {
    Ripple& r = _ripples[i];
    if (r.active) continue;
    r.active = true;
    r.x = (float)x;
    r.y = (float)y;
    r.start = millis();
    r.durationMs = durationMs;
    r.maxRadius = maxRadius;
    r.color = color;
    return;
  }
}

void Effects::spawnSwirl(uint8_t count) {
  uint8_t spawned = 0;
  for (uint8_t i = 0; i < 96 && spawned < count; ++i) {
    Swirl& s = _swirl[i];
    if (s.active) continue;
    s.active = true;
    s.angle = fx::randFloat() * 2.0f * (float)M_PI;
    s.radius = 16.0f + fx::randFloat() * 46.0f;
    s.angular = 1.6f + fx::randFloat() * 2.2f;
    s.radial = -(11.0f + fx::randFloat() * 17.0f);
    s.brightness = (uint8_t)(120 + fx::rand() % 120);
    ++spawned;
  }
}

/* -------------------------------------------------------------------------- */
/*  Interaction API                                                           */
/* -------------------------------------------------------------------------- */
void Effects::touchRipple(int16_t x, int16_t y, uint16_t color) {
  addRipple(x, y, 44, 620, color);
}

void Effects::createStar(int16_t x, int16_t y) {
  CreatedStar& s = _stars[_starCursor];
  _starCursor = (uint8_t)((_starCursor + 1) % CREATED_STAR_MAX);
  s.used = true;
  s.x = x;
  s.y = y;
  s.born = millis();
  s.color = (fx::rand() & 3) ? kWhite : kCyan;
  s.phase = (uint8_t)(fx::rand() & 0xFF);

  spawnBurst(x, y, 22, 26.0f, 105.0f, kCyan, kWhite, 420, 900, 0, 0);
  addRipple(x, y, 38, 700, kCyan);
  setLabel("STAR CREATED", kCyan, 1250);
}

void Effects::supernova(int16_t x, int16_t y) {
  spawnBurst(x, y, 110, 55.0f, 225.0f, kWhite, kYellow, 650, 1500, 1, 1);
  spawnBurst(x, y, 26, 20.0f, 70.0f, kViolet, kCyan, 900, 1800, 2, 1);
  addRipple(x, y, 155, 1050, kWhite);
  addRipple(x, y, 96, 680, kCyan);
  addRipple(x, y, 42, 380, kYellow);
  _flashStart = millis();
  _flashDuration = 150;
  setLabel("SUPERNOVA", kYellow, 1600);
}

void Effects::startBlackHole(int16_t x, int16_t y) {
  _bhActive = true;
  _bhX = x;
  _bhY = y;
  _bhIntensity = 0.0f;
  _bhReleaseAt = 0;
  _bhSpawnAccum = 0;
  spawnSwirl(40);
  if (_engine) _engine->setGravity(true, x, y);
  setLabel("BLACK HOLE", kViolet, 1300);
}

void Effects::moveBlackHole(int16_t x, int16_t y) {
  _bhX = x;
  _bhY = y;
  if (_engine) _engine->updateGravityPosition(x, y);
}

void Effects::endBlackHole() {
  if (!_bhActive) return;
  _bhActive = false;
  _bhReleaseAt = millis();
  if (_engine) _engine->setGravity(false, (int16_t)_bhX, (int16_t)_bhY);
}

void Effects::spawnShootingStar(uint32_t now) {
  for (uint8_t i = 0; i < SHOOTING_STAR_MAX; ++i) {
    Shooter& s = _shooters[i];
    if (s.active) continue;

    const bool fromLeft = (fx::rand() & 1) != 0;
    const float speed = 190.0f + fx::randFloat() * 110.0f;
    const float ang = 0.42f + fx::randFloat() * 0.30f;   /* downward diagonal */
    s.active = true;
    s.x = fromLeft ? -12.0f : (float)(SCREEN_W + 12);
    s.y = 8.0f + fx::randFloat() * (SCREEN_H * 0.45f);
    s.vx = cosf(ang) * speed * (fromLeft ? 1.0f : -1.0f);
    s.vy = sinf(ang) * speed;
    s.start = now;
    s.lifeMs = (uint16_t)(900 + fx::rand() % 600);
    s.color = (fx::rand() & 1) ? kWhite : kCyan;
    return;
  }
}

void Effects::setLabel(const char* text, uint16_t color, uint16_t durationMs) {
  strncpy(_label, text, sizeof(_label) - 1);
  _label[sizeof(_label) - 1] = 0;
  _labelColor = color;
  _labelStart = millis();
  _labelUntil = _labelStart + durationMs;
}

uint8_t Effects::labelAlpha(uint32_t now) const {
  if (!_label[0] || now >= _labelUntil) return 0;
  const uint32_t elapsed = now - _labelStart;
  const uint32_t remaining = _labelUntil - now;
  float a = 1.0f;
  if (elapsed < 140) a = elapsed / 140.0f;
  if (remaining < 320) a = fminf(a, remaining / 320.0f);
  return (uint8_t)(a * 255.0f);
}

/* -------------------------------------------------------------------------- */
/*  Update                                                                    */
/* -------------------------------------------------------------------------- */
void Effects::update(uint32_t now, float dt) {
  if (dt > 0.06f) dt = 0.06f;
  const float dtMs = dt * 1000.0f;

  for (uint16_t i = 0; i < EFFECT_PARTICLE_MAX; ++i) {
    BurstParticle& p = _particles[i];
    if (!p.lifeMs) continue;
    p.lifeMs = (p.lifeMs > dtMs) ? (uint16_t)(p.lifeMs - dtMs) : 0;
    if (!p.lifeMs) continue;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    const float f = clampf2(1.0f - p.drag * dt, 0.86f, 0.995f);
    p.vx *= f;
    p.vy *= f;
  }

  /* black hole ramp */
  const float target = _bhActive ? 1.0f : 0.0f;
  const float ramp = clampf2(dt * (_bhActive ? 5.5f : 3.2f), 0.0f, 1.0f);
  _bhIntensity += (target - _bhIntensity) * ramp;
  if (!_bhActive && _bhIntensity < 0.012f) _bhIntensity = 0.0f;

  if (_bhActive && _bhIntensity > 0.12f) {
    _bhSpawnAccum += (uint32_t)dtMs;
    if (_bhSpawnAccum >= 34) {
      _bhSpawnAccum = 0;
      spawnSwirl(2);
    }
  }

  for (uint8_t i = 0; i < 96; ++i) {
    Swirl& s = _swirl[i];
    if (!s.active) continue;
    s.angle += s.angular * dt;
    s.radius += s.radial * dt;
    if (s.radius < 7.0f) {
      if (_bhActive) {
        s.radius = 48.0f + fx::randFloat() * 30.0f;
        s.angle = fx::randFloat() * 2.0f * (float)M_PI;
        s.radial = -(11.0f + fx::randFloat() * 17.0f);
        s.angular = 1.6f + fx::randFloat() * 2.2f;
      } else {
        s.active = false;
      }
    }
  }

  if (now >= _nextShooter) {
    spawnShootingStar(now);
    _nextShooter = now + 1700 + (fx::rand() % 4300);
  }

  for (uint8_t i = 0; i < SHOOTING_STAR_MAX; ++i) {
    Shooter& s = _shooters[i];
    if (!s.active) continue;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    if (now - s.start > s.lifeMs ||
        s.x < -80 || s.x > SCREEN_W + 80 || s.y > SCREEN_H + 40) {
      s.active = false;
    }
  }

  (void)_bhReleaseAt;
}

/* -------------------------------------------------------------------------- */
/*  Rendering                                                                 */
/* -------------------------------------------------------------------------- */
void Effects::render(uint32_t now) {
  if (!_canvas) return;
  drawRipples(now);
  drawCreatedStars(now);
  drawBlackHole(now);
  drawParticles(now);
  drawShooters(now);

  if (_flashStart) {
    const uint32_t e = now - _flashStart;
    if (e < _flashDuration) {
      const uint8_t a = (uint8_t)(46.0f * (1.0f - (float)e / _flashDuration));
      if (a) _canvas->blendFill(0xFFFF, a);
    } else {
      _flashStart = 0;
    }
  }
}

void Effects::drawRipples(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  for (uint8_t i = 0; i < RIPPLE_MAX; ++i) {
    Ripple& r = _ripples[i];
    if (!r.active) continue;
    const uint32_t elapsed = now - r.start;
    if (elapsed >= r.durationMs) { r.active = false; continue; }

    const float t = (float)elapsed / r.durationMs;
    const float radius = r.maxRadius * (t * (2.0f - t));
    const uint8_t a = (uint8_t)((1.0f - t) * (1.0f - t) * 205.0f);
    if (a < 3 || radius < 1.0f) continue;

    const int16_t ri = (int16_t)radius;
    c.addCircleOutline((int16_t)r.x, (int16_t)r.y, ri, r.color, a);
    if (ri > 2) c.addCircleOutline((int16_t)r.x, (int16_t)r.y, (int16_t)(ri - 1), r.color, (uint8_t)(a / 2));
    if (ri > 5) c.addGlow((int16_t)r.x, (int16_t)r.y, (int16_t)(ri * 1.6f), r.color, (uint8_t)(a / 7));
  }
}

void Effects::drawParticles(uint32_t now) {
  (void)now;
  SpaceCanvas& c = *_canvas;
  for (uint16_t i = 0; i < EFFECT_PARTICLE_MAX; ++i) {
    BurstParticle& p = _particles[i];
    if (!p.lifeMs) continue;
    const float lifeT = (float)p.lifeMs / p.maxLifeMs;
    const uint8_t a = (uint8_t)(lifeT * 235.0f);
    if (a < 4) continue;

    const int16_t x = (int16_t)p.x;
    const int16_t y = (int16_t)p.y;
    if (x < -8 || x > SCREEN_W + 8 || y < -8 || y > SCREEN_H + 8) continue;

    if (p.kind == 1) {
      const int16_t tx = (int16_t)(p.x - p.vx * 0.022f);
      const int16_t ty = (int16_t)(p.y - p.vy * 0.022f);
      c.addLine(x, y, tx, ty, p.color, (uint8_t)(a / 2));
      c.addGlow(x, y, p.size == 2 ? 8 : 5, p.color, a);
    } else {
      c.addGlow(x, y, p.size == 0 ? 5 : 8, p.color, a);
      c.addPixel(x, y, kWhite, (uint8_t)(a / 2));
    }
  }
}

void Effects::drawCreatedStars(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  for (uint8_t i = 0; i < CREATED_STAR_MAX; ++i) {
    CreatedStar& s = _stars[i];
    if (!s.used) continue;
    const uint32_t age = now - s.born;
    const float grow = fminf(1.0f, age / 260.0f);
    const int16_t idx = (int16_t)(s.phase + (now >> 4) * 2);
    const int16_t tw = fx::sinT((uint16_t)idx);
    const uint8_t a = (uint8_t)((150 + (tw >> 3)) * grow);
    const uint8_t len = (uint8_t)((4 + (tw >> 8)) * grow);

    c.addGlow(s.x, s.y, 12, s.color, (uint8_t)(a / 3));
    c.addGlow(s.x, s.y, 5, 0xFFFF, a);
    c.addPixel(s.x, s.y, 0xFFFF, 255);
    c.addLine((int16_t)(s.x - len), s.y, (int16_t)(s.x + len), s.y, s.color, (uint8_t)(a / 3));
    c.addLine(s.x, (int16_t)(s.y - len), s.x, (int16_t)(s.y + len), s.color, (uint8_t)(a / 3));
  }
}

void Effects::drawShooters(uint32_t now) {
  SpaceCanvas& c = *_canvas;
  for (uint8_t i = 0; i < SHOOTING_STAR_MAX; ++i) {
    Shooter& s = _shooters[i];
    if (!s.active) continue;
    const uint32_t age = now - s.start;
    float lifeT = 1.0f - (float)age / s.lifeMs;
    if (lifeT < 0.0f) lifeT = 0.0f;
    const uint8_t headA = (uint8_t)(fminf(1.0f, lifeT * 2.4f) * 220.0f);

    for (uint8_t seg = 0; seg < 9; ++seg) {
      const float back = 0.010f * (seg + 1);
      const int16_t x0 = (int16_t)(s.x - s.vx * back * (seg + 1) * 0.55f);
      const int16_t y0 = (int16_t)(s.y - s.vy * back * (seg + 1) * 0.55f);
      const uint8_t a = (uint8_t)(headA * (1.0f - seg / 9.0f) * 0.55f);
      if (a < 3) continue;
      c.addGlow(x0, y0, seg < 2 ? 6 : 5, s.color, a);
    }
    c.addGlow((int16_t)s.x, (int16_t)s.y, 10, s.color, headA);
    c.addGlow((int16_t)s.x, (int16_t)s.y, 5, 0xFFFF, headA);
    c.addPixel((int16_t)s.x, (int16_t)s.y, 0xFFFF, 255);
  }
}

void Effects::drawBlackHole(uint32_t now) {
  if (_bhIntensity <= 0.01f) return;
  SpaceCanvas& c = *_canvas;
  const int16_t x = (int16_t)_bhX;
  const int16_t y = (int16_t)_bhY;
  /* event-horizon shadow */
  const int16_t R = 24;
  for (int16_t dy = -R; dy <= R; ++dy) {
    const int16_t ty = (int16_t)(y + dy);
    if ((uint16_t)ty >= SCREEN_H) continue;
    const int16_t w = (int16_t)sqrtf((float)(R * R - dy * dy));
    for (int16_t dx = -w; dx <= w; ++dx) {
      const int16_t tx = (int16_t)(x + dx);
      if ((uint16_t)tx >= SCREEN_W) continue;
      const float d = sqrtf((float)(dx * dx + dy * dy)) / R;
      const uint8_t a = (uint8_t)((1.0f - d * 0.35f) * 150.0f * _bhIntensity);
      c.blendPixel(tx, ty, 0x0000, a);
    }
  }

  /* purple / cyan event horizon */
  const uint16_t baseAngle = (uint16_t)(now >> 3);
  for (uint8_t layer = 0; layer < 2; ++layer) {
    const int16_t rr = (int16_t)(R + 2 + layer * 3);
    const uint16_t col = layer ? kCyan : kViolet;
    c.addCircleOutline(x, y, rr, col, (uint8_t)(150 * _bhIntensity));
    for (uint8_t seg = 0; seg < 70; ++seg) {
      const uint16_t a = (uint16_t)(baseAngle * (layer ? -2 : 2) + seg * (fx::ANGLE_STEPS / 70));
      const int16_t px = (int16_t)(x + fx::mulQ10(fx::cosT(a), rr * 1024));
      const int16_t py = (int16_t)(y + fx::mulQ10(fx::sinT(a), rr * 1024));
      c.addPixel(px, py, col, (uint8_t)(230 * _bhIntensity));
      if (seg < 22) {
        c.addGlow(px, py, 5, col, (uint8_t)(120 * _bhIntensity));
      }
    }
  }
  c.addGlow(x, y, 78, kPurple, (uint8_t)(42 * _bhIntensity));
  c.addGlow(x, y, 44, kCyan, (uint8_t)(30 * _bhIntensity));

  /* accretion swirl */
  for (uint8_t i = 0; i < 96; ++i) {
    Swirl& s = _swirl[i];
    if (!s.active) continue;
    const int16_t sx = (int16_t)(_bhX + cosf(s.angle) * s.radius);
    const int16_t sy = (int16_t)(_bhY + sinf(s.angle) * s.radius * 0.92f);
    if ((uint16_t)sx >= SCREEN_W || (uint16_t)sy >= SCREEN_H) continue;
    const float rr = clampf2((s.radius - 7.0f) / 60.0f, 0.0f, 1.0f);
    const uint8_t a = (uint8_t)(s.brightness * (0.35f + 0.65f * (1.0f - rr)) * _bhIntensity);
    c.addGlow(sx, sy, 5, (i & 1) ? kCyan : kViolet, a);
    if (s.radius < 26.0f) c.addPixel(sx, sy, 0xFFFF, (uint8_t)(a / 2));
  }
}
