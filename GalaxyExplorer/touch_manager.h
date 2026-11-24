/* ==========================================================================
 * touch_manager.h  -  FT6336G I2C driver + gesture recognition
 * --------------------------------------------------------------------------
 * The FT6336G reports up to two simultaneous contacts. It is polled on a
 * dedicated low-priority task so gestures keep working while the main loop
 * is busy pushing a frame to the display.
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

enum class TouchEventType : uint8_t {
  DOWN,
  UP,
  MOVE,
  TAP,
  DOUBLE_TAP,
  LONG_PRESS,
  LONG_RELEASE,
  DRAG_START,
  DRAG,
  DRAG_END,
  SWIPE_LEFT,
  SWIPE_RIGHT,
  SWIPE_UP,
  SWIPE_DOWN,
  PINCH_START,
  PINCH,
  PINCH_END,
};

struct TouchEvent {
  TouchEventType type;
  uint32_t time;
  int16_t  x, y;       /* mapped screen coordinates (centre for pinch) */
  int16_t  dx, dy;     /* movement delta / swipe displacement */
  int16_t  x2, y2;     /* second contact (multi-touch demos) */
  uint16_t scaleQ8;    /* pinch increment: 256 == 1.0 */
  uint16_t rawX, rawY; /* controller coordinates at event time */
  uint8_t  fingers;
};

struct TouchPoint {
  int16_t x;
  int16_t y;
  bool    down;
};

class TouchManager {
 public:
  bool begin();
  void start();                 /* launches the polling task when enabled */
  void update(uint32_t now);    /* sync polling fallback / no-op when async */

  bool pop(TouchEvent& out);

  void getPoints(TouchPoint& p0, TouchPoint& p1) const;
  uint8_t activeCount() const;

  const char* lastError() const { return _lastError; }
  uint8_t chipId() const { return _chipId; }
  uint8_t maxPointsSeen() const { return _maxPointsSeen; }
  bool chipPresent() const { return _present; }
  uint32_t i2cErrors() const { return _i2cErrors; }

  /* Raw panel coordinates of the most recent contact (diagnostics and
   * calibration). Retained after lift so TAP handlers can read them. */
  void getRaw(uint16_t& rx, uint16_t& ry) const;

  /* Runtime orientation override, derived from the five-point calibration.
   * The compile-time values remain the boot default. */
  void setOrientation(bool swapXY, bool invertX, bool invertY);
  bool swapXY() const { return _swapXY; }
  bool invertX() const { return _invertX; }
  bool invertY() const { return _invertY; }

 private:
  struct FingerState {
    bool active = false;
    int16_t x = 0, y = 0;
    int16_t startX = 0, startY = 0;
    int16_t lastX = 0, lastY = 0;
    uint32_t downTime = 0;
    int16_t maxDist = 0;
    bool longFired = false;
    bool dragFired = false;
    bool suppressTap = false;
  };

  bool i2cRead(uint8_t reg, uint8_t* data, uint8_t len);
  bool i2cWrite(uint8_t reg, uint8_t value);
  uint8_t readRaw(TouchPoint out[TOUCH_DRIVER_MAX_POINTS], uint16_t& rx, uint16_t& ry);
  void process(uint32_t now);
  void emit(TouchEventType type, uint32_t now, int16_t x, int16_t y,
            uint8_t fingers, int16_t dx = 0, int16_t dy = 0,
            uint16_t scaleQ8 = 256, int16_t x2 = -1, int16_t y2 = -1);
  void updateSnapshot(const TouchPoint pts[TOUCH_DRIVER_MAX_POINTS], uint8_t count);
  void resetGestures(uint32_t now);

  static void taskEntry(void* arg);

  QueueHandle_t _queue = nullptr;
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  TouchPoint _snapshot[TOUCH_DRIVER_MAX_POINTS] = {};
  uint8_t _snapshotCount = 0;
  uint16_t _rawX = 0, _rawY = 0;
  bool _swapXY = TOUCH_SWAP_XY != 0;
  bool _invertX = TOUCH_INVERT_X != 0;
  bool _invertY = TOUCH_INVERT_Y != 0;

  FingerState _finger;
  bool _pinchActive = false;
  int16_t _pinchLastDist = 0;
  uint32_t _lastTapTime = 0;
  int16_t _lastTapX = 0, _lastTapY = 0;
  bool _tapPending = false;

  bool _present = false;
  bool _started = false;
  uint8_t _chipId = 0;
  uint8_t _maxPointsSeen = 0;
  uint32_t _i2cErrors = 0;
  const char* _lastError = "";
};
