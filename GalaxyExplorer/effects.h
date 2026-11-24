/* ==========================================================================
 * effects.h  -  Ripples, tap stars, supernova, black hole, shooting stars
 * ========================================================================== */
#pragma once
#include "space_canvas.h"
#include "galaxy_engine.h"

class Effects {
 public:
  void begin(SpaceCanvas* canvas, GalaxyEngine* engine);
  void reset();

  void update(uint32_t now, float dt);
  void render(uint32_t now);

  /* Interactions ------------------------------------------------------- */
  void touchRipple(int16_t x, int16_t y, uint16_t color);
  void createStar(int16_t x, int16_t y);
  void supernova(int16_t x, int16_t y);
  void startBlackHole(int16_t x, int16_t y);
  void moveBlackHole(int16_t x, int16_t y);
  void endBlackHole();
  void spawnShootingStar(uint32_t now);

  /* HUD label ---------------------------------------------------------- */
  const char* label() const { return _label; }
  uint16_t labelColor() const { return _labelColor; }
  uint8_t labelAlpha(uint32_t now) const;
  void setLabel(const char* text, uint16_t color, uint16_t durationMs);

  bool blackHoleActive() const { return _bhActive; }
  int16_t blackHoleX() const { return (int16_t)_bhX; }
  int16_t blackHoleY() const { return (int16_t)_bhY; }

 private:
  struct BurstParticle {
    float x, y, vx, vy;
    float drag;
    uint16_t lifeMs, maxLifeMs;
    uint16_t color;
    uint8_t size;
    uint8_t kind;        /* 0 spark, 1 supernova ember */
  };

  struct Ripple {
    bool active = false;
    float x = 0, y = 0;
    uint32_t start = 0;
    uint16_t durationMs = 700;
    uint16_t maxRadius = 60;
    uint16_t color = 0xFFFF;
  };

  struct Shooter {
    bool active = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    uint32_t start = 0;
    uint16_t lifeMs = 1100;
    uint16_t color = 0xFFFF;
  };

  struct CreatedStar {
    bool used = false;
    int16_t x = 0, y = 0;
    uint32_t born = 0;
    uint16_t color = 0xFFFF;
    uint8_t phase = 0;
  };

  struct Swirl {
    bool active = false;
    float angle = 0;
    float radius = 0;
    float angular = 0;
    float radial = 0;
    uint8_t brightness = 0;
  };

  void spawnSwirl(uint8_t count);
  void spawnBurst(int16_t x, int16_t y, uint8_t count, float speedMin,
                  float speedMax, uint16_t colorA, uint16_t colorB,
                  uint16_t lifeMin, uint16_t lifeMax, uint8_t size, uint8_t kind);
  void addRipple(int16_t x, int16_t y, uint16_t maxRadius, uint16_t durationMs, uint16_t color);
  void drawRipples(uint32_t now);
  void drawParticles(uint32_t now);
  void drawShooters(uint32_t now);
  void drawCreatedStars(uint32_t now);
  void drawBlackHole(uint32_t now);

  SpaceCanvas* _canvas = nullptr;
  GalaxyEngine* _engine = nullptr;

  BurstParticle _particles[EFFECT_PARTICLE_MAX] = {};
  Ripple _ripples[RIPPLE_MAX] = {};
  Shooter _shooters[SHOOTING_STAR_MAX] = {};
  CreatedStar _stars[CREATED_STAR_MAX] = {};
  Swirl _swirl[96] = {};

  uint8_t _starCursor = 0;
  uint32_t _nextShooter = 0;

  bool _bhActive = false;
  float _bhX = 0, _bhY = 0;
  float _bhIntensity = 0;
  uint32_t _bhReleaseAt = 0;
  uint32_t _bhSpawnAccum = 0;

  uint32_t _flashStart = 0;
  uint16_t _flashDuration = 150;

  char _label[20] = {0};
  uint32_t _labelStart = 0;
  uint32_t _labelUntil = 0;
  uint16_t _labelColor = UI_ACCENT_CYAN;
};
