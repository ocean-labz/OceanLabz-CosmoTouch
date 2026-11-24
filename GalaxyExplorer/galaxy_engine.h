/* ==========================================================================
 * galaxy_engine.h  -  Stars, nebula, spiral galaxy and planets
 * ========================================================================== */
#pragma once
#include "space_canvas.h"

class GalaxyEngine {
 public:
  bool begin(SpaceCanvas* canvas);
  void reset(bool reseed = true);

  void update(uint32_t now, float dt);
  void render(uint32_t now);

  /* Interaction -------------------------------------------------------- */
  void moveBy(int16_t dx, int16_t dy);
  void zoomBy(float factor, int16_t focusX, int16_t focusY);
  void setGravity(bool active, int16_t x, int16_t y);
  void updateGravityPosition(int16_t x, int16_t y);

  float zoom() const { return _zoom; }
  int16_t centerX() const { return (int16_t)_cx; }
  int16_t centerY() const { return (int16_t)_cy; }
  uint16_t particleCount() const { return GALAXY_PARTICLE_COUNT; }
  bool ready() const { return _ready; }

 private:
  struct Star {
    float x, y;
    float vx, vy;
    uint8_t depth;
    uint8_t phase;
    uint8_t twinkleSpeed;
    uint8_t bright;
    uint16_t color;
  };

  struct Particle {
    int32_t angleQ8;
    uint16_t speedQ8;
    uint16_t twinklePhase;
    uint8_t radius;
    uint8_t size;
    uint8_t bright;
    uint16_t color;
  };

  struct Planet {
    uint16_t* pixels = nullptr;
    uint8_t* alpha = nullptr;
    int16_t w = 0, h = 0;
    int16_t radius = 0;
    int16_t orbit = 0;
    int32_t angleQ8 = 0;
    int16_t speedQ8 = 0;
    uint16_t color = 0;
    bool ring = false;
  };

  bool allocateBackground();
  void prepare();
  void buildBackground();
  void buildPlanets();
  void buildStars();
  void buildGalaxy();
  void buildPlanetSprite(Planet& p, uint16_t baseColor, bool ring);

  void drawStars(uint32_t now, bool foreground);
  void drawGalaxy(uint32_t now);
  void drawPlanets(uint32_t now);

  SpaceCanvas* _canvas = nullptr;
  uint16_t* _bg = nullptr;
  int16_t _bgW = 0, _bgH = 0;

  Star _stars[STAR_COUNT] = {};
  Particle _particles[GALAXY_PARTICLE_COUNT] = {};
  Planet _planets[PLANET_COUNT] = {};

  float _cx = SCREEN_W * 0.5f;
  float _cy = 132.0f;
  float _velX = 0.0f, _velY = 0.0f;
  float _zoom = 1.0f, _zoomTarget = 1.0f;
  float _bgOffX = 32.0f, _bgOffY = 32.0f;

  float _gravityStrength = 0.0f;
  float _gravityTarget = 0.0f;
  int16_t _gravityX = 0, _gravityY = 0;

  uint32_t _startMs = 0;
  bool _ready = false;
};
