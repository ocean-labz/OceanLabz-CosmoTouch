/* ==========================================================================
 * galaxy_engine.h  -  Stars, nebula, spiral galaxy and planets
 * ========================================================================== */
#pragma once
#include "space_canvas.h"

enum class PlanetKind : uint8_t {
  MERCURY,
  VENUS,
  EARTH,
  MARS,
  JUPITER,
  SATURN,
  URANUS,
  NEPTUNE,
};

struct PlanetInfo {
  const char* name;
  const char* type;
  const char* atmosphere;
  const char* surface;
  const char* fact;
  uint16_t accent;
};

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

  /* Planet access ------------------------------------------------------ */
  static constexpr int16_t PLANET_TEX_W = 256;
  static constexpr int16_t PLANET_TEX_H = 128;

  int8_t planetAt(int16_t x, int16_t y, int16_t pad = 9) const;
  int8_t selectedPlanet() const { return _selected; }
  void selectPlanet(int8_t index);
  void clearSelection() { _selected = -1; }

  bool planetScreenPos(uint8_t index, int16_t& x, int16_t& y) const;
  int16_t planetRadius(uint8_t index) const;
  const uint16_t* planetSurface(uint8_t index) const;
  PlanetKind planetKind(uint8_t index) const;
  uint16_t planetAccent(uint8_t index) const;
  const PlanetInfo& planetInfo(uint8_t index) const;

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
    uint16_t* surface = nullptr;     /* 256x128 equirectangular texture */
    int16_t w = 0, h = 0;
    int16_t radius = 0;
    int16_t orbit = 0;
    int32_t angleQ8 = 0;
    int16_t speedQ8 = 0;
    uint16_t color = 0;
    PlanetKind kind = PlanetKind::EARTH;
    uint8_t spin = 0;
    bool ring = false;
  };

  bool allocateBackground();
  void prepare();
  void buildBackground();
  void buildPlanets();
  void buildStars();
  void buildGalaxy();
  void buildPlanetTexture(Planet& p);
  void buildPlanetSprite(Planet& p);
  void computePlanetPos(const Planet& p, int16_t& x, int16_t& y) const;

  void drawStars(uint32_t now, bool foreground);
  void drawOrbits();
  void drawSun(uint32_t now);
  void drawSolarDust(uint32_t now);
  void drawPlanets(uint32_t now);
  void drawSelection(uint32_t now);

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
  uint32_t _lastSpin = 0;
  uint8_t _spinCursor = 0;
  bool _ready = false;
  int8_t _selected = -1;
};
