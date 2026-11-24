/* ==========================================================================
 * ui_manager.h  -  HUD, menus and the touch demonstration screens
 * ========================================================================== */
#pragma once
#include "display_manager.h"
#include "touch_manager.h"
#include "galaxy_engine.h"
#include "effects.h"

enum class Screen : uint8_t {
  SPLASH,
  GALAXY,
  MENU,
  TOUCH_TEST,
  MULTITOUCH,
  GESTURE_TEST,
  PAINT,
};

class UiManager {
 public:
  void begin(DisplayManager* display, TouchManager* touch,
             GalaxyEngine* galaxy, Effects* effects);

  Screen screen() const { return _screen; }
  void enter(Screen next);
  void enterGalaxy();

  void drawSplash(SpaceCanvas& canvas, uint32_t now, uint32_t startMs);
  void drawGalaxyHud(SpaceCanvas& canvas, uint32_t now, uint16_t fps, bool debug);

  /* Returns true when the HUD consumed the event. */
  bool handleGalaxyTouch(const TouchEvent& ev);
  void handleEvent(const TouchEvent& ev);
  void update(uint32_t now);

 private:
  /* ---- composited HUD ------------------------------------------------ */
  void drawHudButton(SpaceCanvas& c, uint8_t index, bool pressed, uint32_t now);
  int8_t hitHudButton(int16_t x, int16_t y) const;
  void activateHudButton(uint8_t index);

  /* ---- shared direct-drawn chrome ------------------------------------ */
  void drawBackdrop(const char* title, const char* subtitle);
  void drawHeader(const char* title, const char* subtitle);
  void drawBackButton(bool pressed);
  void drawMenu();
  void drawMenuButton(uint8_t index, bool pressed);
  bool hitBack(int16_t x, int16_t y) const;
  bool hitMenuButton(int16_t x, int16_t y, uint8_t* index) const;
  void button(int16_t x, int16_t y, int16_t w, int16_t h, const char* text,
              uint16_t accent, bool pressed, const lgfx::IFont* font = &fonts::Font2);
  void markMenuDirty(uint8_t index, bool pressed);

  /* ---- five-target touch calibration --------------------------------- */
  void drawCalibration();
  void drawCalibrationInfo();
  void drawCalTarget(uint8_t index);
  int8_t hitCalTarget(int16_t x, int16_t y) const;
  void startCalibrationSequence();
  bool finishCalibrationSequence();

  /* ---- multitouch ----------------------------------------------------- */
  void drawMultiTouch();
  void drawMultiArea(bool force);
  void updateMultiTouch(uint32_t now);

  /* ---- gesture test --------------------------------------------------- */
  void drawGestureTest();
  void setGesture(int8_t index, uint16_t count);
  int8_t gestureIndexFor(TouchEventType t) const;

  /* ---- painting ------------------------------------------------------- */
  void drawPaintScreen();
  void drawPaintToolbar();
  void drawPaintCanvasBg();
  void paintSegment(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
  void paintDot(int16_t x, int16_t y);
  bool inPaintCanvas(int16_t x, int16_t y) const;
  int8_t hitPaintTool(int16_t x, int16_t y) const;
  void selectPaintTool(uint8_t tool);

  /* ---- helpers -------------------------------------------------------- */
  static uint16_t dim(uint16_t color, uint8_t amount);
  void textAt(const char* s, int16_t x, int16_t y, const lgfx::IFont* font,
              uint16_t color, textdatum_t datum = textdatum_t::top_left);

  DisplayManager* _display = nullptr;
  LGFX* _gfx = nullptr;
  TouchManager* _touch = nullptr;
  GalaxyEngine* _galaxy = nullptr;
  Effects* _effects = nullptr;

  Screen _screen = Screen::SPLASH;
  uint32_t _screenMs = 0;

  /* HUD */
  int8_t _hudPressed = -1;
  bool _hudCaptured = false;

  /* shared direct-screen state */
  int8_t _backPressed = false;

  /* menu */
  int8_t _menuPressed = -1;
  bool _menuButtonPressed[5] = {};

  /* five-target calibration */
  bool _calHit[5] = {};
  int16_t _calTouchX[5] = {};
  int16_t _calTouchY[5] = {};
  uint16_t _calRawX[5] = {};
  uint16_t _calRawY[5] = {};
  uint8_t _calHits = 0;
  int16_t _calLiveX = -1, _calLiveY = -1;
  uint16_t _calLiveRawX = 0, _calLiveRawY = 0;
  bool _calAuto = false;
  bool _calApplied = false;
  uint8_t _calStep = 0;
  uint16_t _calCapRawX[5] = {};
  uint16_t _calCapRawY[5] = {};

  /* multitouch */
  float _multiZoom = 1.0f;
  int16_t _multiBaseDist = 0;
  uint8_t _multiPrevActive = 0;
  uint32_t _multiDrawnAt = 0;

  /* gesture */
  int8_t _gestureIndex = -1;
  uint16_t _gestureCount = 0;
  uint32_t _gestureDetectedAt = 0;

  /* paint */
  uint16_t _paintColor = UI_ACCENT_CYAN;
  uint8_t _paintSize = 6;
  bool _painting = false;
  int16_t _paintLastX = -1, _paintLastY = -1;
  uint8_t _paintTool = 0;            /* 0..4 colours, 5..7 sizes */
};
