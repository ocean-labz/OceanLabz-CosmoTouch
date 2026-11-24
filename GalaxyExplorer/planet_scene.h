/* ==========================================================================
 * planet_scene.h  -  Tap-to-enter planet approach and globe explorer
 * --------------------------------------------------------------------------
 * A single tap on a solar-system planet begins a camera-style descent, then
 * opens a full-screen globe with rotation, zoom, markers and planet facts.
 * The solar-system camera state is never modified, so BACK restores it.
 * ========================================================================== */
#pragma once
#include "space_canvas.h"
#include "galaxy_engine.h"
#include "touch_manager.h"

class PlanetScene {
 public:
  bool begin(SpaceCanvas* canvas, GalaxyEngine* galaxy);

  bool start(uint8_t index, uint32_t now);
  void update(uint32_t now, float dt);
  void render(uint32_t now);
  void handleTouch(const TouchEvent& ev);

  bool active() const { return _state != State::OFF; }
  bool exploring() const { return _state == State::EXPLORE; }
  uint8_t planetIndex() const { return _planet; }

  static constexpr int16_t PANEL_Y = 210;

 private:
  enum class State : uint8_t { OFF, APPROACH, EXPLORE, EXIT };

  struct Spark {
    float x, y, vx, vy;
    float gravity;
    uint16_t life, maxLife;
    uint16_t color;
    uint8_t size;
  };

  struct Marker {
    bool used = false;
    int16_t x = 0, y = 0;
    uint32_t born = 0;
  };

  void beginExit(uint32_t now);
  void spawnSparks(uint8_t count, uint16_t colorA, uint16_t colorB);
  void updateSparks(float dt);
  void triggerSpecial(uint32_t now);
  void setToast(const char* text, uint16_t color, uint16_t ms);

  void drawApproach(uint32_t now);
  void drawExplore(uint32_t now);
  void drawExit(uint32_t now);
  void drawWarp(float t);
  void drawStarfield(uint32_t now);
  void drawGlobe(int16_t cx, int16_t cy, int16_t R, uint32_t now);
  void drawRingSystem(int16_t cx, int16_t cy, int16_t R, uint8_t alpha, bool front);
  void drawSparks();
  void drawMarkers(uint32_t now);
  void drawSpecial(uint32_t now);
  void drawInfoPanel(uint32_t now);
  void drawWrappedText(const char* text, int16_t x, int16_t y,
                       int16_t maxChars, uint8_t maxLines,
                       uint16_t color, uint8_t alpha);
  void addMarker(int16_t x, int16_t y);

  bool hitBack(int16_t x, int16_t y) const;
  bool insideGlobe(int16_t x, int16_t y) const;
  bool globeLonLat(int16_t x, int16_t y, float& lon, float& lat) const;
  const char* earthLabelAt(int16_t x, int16_t y) const;
  int16_t globeRadius() const;
  uint16_t accent() const;
  PlanetKind kind() const;

  SpaceCanvas* _canvas = nullptr;
  GalaxyEngine* _galaxy = nullptr;

  State _state = State::OFF;
  uint8_t _planet = 0;
  uint32_t _startMs = 0;
  uint32_t _exploreMs = 0;

  int16_t _startX = 120, _startY = 132;
  int16_t _startR = 5;

  int16_t _lonLUT[513] = {};
  int16_t _latLUT[513] = {};
  float _rot = 0.0f;
  int16_t _pitch = 0;
  float _zoom = 1.0f, _zoomTarget = 1.0f;
  float _pinchAccum = 1.0f;

  bool _dragging = false;
  int16_t _dragX = 0, _dragY = 0;
  bool _backPressed = false;

  Spark _sparks[30] = {};
  Marker _markers[6] = {};

  uint32_t _specialStart = 0;
  uint32_t _specialUntil = 0;

  char _toast[32] = {0};
  uint32_t _toastUntil = 0;
  uint16_t _toastColor = UI_ACCENT_CYAN;
};
