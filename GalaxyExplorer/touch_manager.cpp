/* ==========================================================================
 * touch_manager.cpp  -  FT6336G driver + gesture state machine
 * ========================================================================== */
#include "touch_manager.h"
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

namespace {
constexpr const char* kCalNvs = "touchcal";
}

namespace {

inline int16_t mapAxis(uint16_t raw, uint16_t rawMax, uint16_t outMax, bool invert) {
  if (raw >= rawMax) raw = (uint16_t)(rawMax - 1);
  if (invert) raw = (uint16_t)(rawMax - 1 - raw);
  return (int16_t)(((uint32_t)raw * (outMax - 1)) / (rawMax - 1));
}

} // namespace

void TouchManager::taskEntry(void* arg) {
  TouchManager* self = static_cast<TouchManager*>(arg);
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    self->process(millis());
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TOUCH_POLL_MS));
  }
}

bool TouchManager::begin() {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(60);

  Wire.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_I2C_FREQ);
  Wire.setTimeOut(30);

  _queue = xQueueCreate(48, sizeof(TouchEvent));
  if (!_queue) {
    _lastError = "event queue alloc failed";
    return false;
  }

  /* Put the controller in normal operating mode (polling). Some panels
   * ignore this write, so fall back to probing the status register. */
  bool modeSet = i2cWrite(0x00, 0x00);
  if (!modeSet) {
    delay(5);
    modeSet = i2cWrite(0x00, 0x00);
  }

  uint8_t id = 0;
  const bool idOk = i2cRead(0xA3, &id, 1);
  if (idOk) _chipId = id;

  if (!modeSet && !idOk) {
    _lastError = "FT6336 not responding at 0x38";
    return false;
  }
  if (!modeSet) _lastError = "FT6336 mode write failed (polling anyway)";
  _present = true;

  Preferences prefs;
  if (prefs.begin(kCalNvs, true)) {
    if (prefs.getBool("valid", false)) {
      _swapXY = prefs.getBool("swap", false);
      _invertX = prefs.getBool("invx", false);
      _invertY = prefs.getBool("invy", false);
#if SERIAL_DEBUG
      Serial.printf("[touch] stored calibration: swap=%d invX=%d invY=%d\n",
                    _swapXY ? 1 : 0, _invertX ? 1 : 0, _invertY ? 1 : 0);
#endif
    }
    prefs.end();
  }

  resetGestures(millis());
  updateSnapshot(nullptr, 0);

#if SERIAL_DEBUG
  Serial.printf("[touch] FT6336 at 0x%02X, chip id 0x%02X, i2c errors=%lu\n",
                TOUCH_I2C_ADDR, _chipId, (unsigned long)_i2cErrors);
#endif
  return true;
}

void TouchManager::start() {
  if (_started || !_present) return;
  _started = true;
#if TOUCH_ASYNC_TASK
  xTaskCreatePinnedToCore(taskEntry, "touch", 4096, this, 2, nullptr, TOUCH_TASK_CORE);
#endif
}

void TouchManager::update(uint32_t now) {
#if TOUCH_ASYNC_TASK
  (void)now;                 /* polling task handles everything */
#else
  process(now);
#endif
}

bool TouchManager::i2cRead(uint8_t reg, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    ++_i2cErrors;
    return false;
  }
  if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, len) != len) {
    ++_i2cErrors;
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) data[i] = (uint8_t)Wire.read();
  return true;
}

bool TouchManager::i2cWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  if (Wire.endTransmission() != 0) {
    ++_i2cErrors;
    return false;
  }
  return true;
}

/* Reads the contact list. Returns the number of valid contacts (newest first). */
uint8_t TouchManager::readRaw(TouchPoint out[TOUCH_DRIVER_MAX_POINTS],
                              uint16_t& rx, uint16_t& ry) {
  uint8_t status = 0;
  if (!i2cRead(0x02, &status, 1)) return 0xFF;   /* comm error: keep state */

  uint8_t reported = status & 0x0F;
  if (reported > TOUCH_DRIVER_MAX_POINTS) reported = TOUCH_DRIVER_MAX_POINTS;
  /* Keep the last-contact raw values. The gesture layer emits TAP/UP after
   * the finger has lifted, and the calibration screen reads getRaw() from
   * those events. Clearing here made every recorded corner read 0,0. */
  if (!reported) return 0;

  uint8_t raw[6 * TOUCH_DRIVER_MAX_POINTS] = {};
  if (!i2cRead(0x03, raw, (uint8_t)(reported * 6))) return 0xFF;

  uint8_t valid = 0;
  for (uint8_t i = 0; i < reported; ++i) {
    const uint8_t* p = raw + i * 6;
    const uint8_t ev = (p[0] >> 6) & 0x03;
    if (ev == 0x01) continue;                     /* lift event */

    uint16_t rawX = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
    uint16_t rawY = (uint16_t)(((p[2] & 0x0F) << 8) | p[3]);

    /* some panels return 0x0FFF for unused contacts */
    if (rawX >= 0x0FFF || rawY >= 0x0FFF) continue;

    uint16_t cx = rawX, cy = rawY;
    uint16_t mx = TOUCH_RAW_MAX_X, my = TOUCH_RAW_MAX_Y;
    if (_swapXY) {
      uint16_t t = cx; cx = cy; cy = t;
      t = mx; mx = my; my = t;
    }
    out[valid].x = mapAxis(cx, mx, SCREEN_W, _invertX);
    out[valid].y = mapAxis(cy, my, SCREEN_H, _invertY);
    out[valid].down = true;
    if (valid == 0) { rx = rawX; ry = rawY; }
    ++valid;
  }
  if (valid > _maxPointsSeen) _maxPointsSeen = valid;
  return valid;
}

void TouchManager::updateSnapshot(const TouchPoint pts[TOUCH_DRIVER_MAX_POINTS], uint8_t count) {
  portENTER_CRITICAL(&_mux);
  _snapshotCount = count;
  for (uint8_t i = 0; i < TOUCH_DRIVER_MAX_POINTS; ++i) {
    _snapshot[i] = pts ? pts[i] : TouchPoint{};
  }
  portEXIT_CRITICAL(&_mux);
}

void TouchManager::emit(TouchEventType type, uint32_t now, int16_t x, int16_t y,
                        uint8_t fingers, int16_t dx, int16_t dy,
                        uint16_t scaleQ8, int16_t x2, int16_t y2) {
  TouchEvent ev;
  ev.type = type;
  ev.time = now;
  ev.x = x;
  ev.y = y;
  ev.dx = dx;
  ev.dy = dy;
  ev.x2 = x2;
  ev.y2 = y2;
  ev.scaleQ8 = scaleQ8;
  ev.fingers = fingers;
  portENTER_CRITICAL(&_mux);
  ev.rawX = _rawX;
  ev.rawY = _rawY;
  portEXIT_CRITICAL(&_mux);
  if (_queue) xQueueSend(_queue, &ev, 0);
}

void TouchManager::resetGestures(uint32_t now) {
  _finger = FingerState{};
  _pinchActive = false;
  _pinchLastDist = 0;
  (void)now;
}

void TouchManager::process(uint32_t now) {
  TouchPoint pts[TOUCH_DRIVER_MAX_POINTS] = {};
  uint16_t rx = 0, ry = 0;
  const uint8_t count = readRaw(pts, rx, ry);
  if (count == 0xFF) return;                    /* transient I2C failure */
  if (count > 0) {
    _rawX = rx;
    _rawY = ry;
  }

  /* ---------------- two-finger pinch ---------------- */
  if (count >= 2) {
    const int16_t dx = pts[0].x - pts[1].x;
    const int16_t dy = pts[0].y - pts[1].y;
    const int16_t dist = (int16_t)sqrtf((float)dx * dx + (float)dy * dy);
    const int16_t cx = (int16_t)((pts[0].x + pts[1].x) / 2);
    const int16_t cy = (int16_t)((pts[0].y + pts[1].y) / 2);

    if (!_pinchActive) {
      _pinchActive = true;
      _pinchLastDist = dist > 4 ? dist : 4;
      _finger.active = false;
      _finger.dragFired = false;
      _finger.longFired = false;
      _tapPending = false;
      emit(TouchEventType::PINCH_START, now, cx, cy, count, 0, 0, 256,
           pts[1].x, pts[1].y);
    } else if (abs((int)(dist - _pinchLastDist)) >= 1) {
      uint16_t scale = _pinchLastDist > 0
                     ? (uint16_t)(((uint32_t)dist * 256) / _pinchLastDist)
                     : 256;
      if (scale < 64) scale = 64;
      if (scale > 1024) scale = 1024;
      _pinchLastDist = dist;
      emit(TouchEventType::PINCH, now, cx, cy, count, 0, 0, scale,
           pts[1].x, pts[1].y);
    }
    updateSnapshot(pts, count);
    return;
  }

  if (_pinchActive) {
    _pinchActive = false;
    emit(TouchEventType::PINCH_END, now, pts[0].x, pts[0].y, count);
    if (count == 1) {
      /* Re-seed the single-finger state so the remaining finger does not
       * generate a phantom drag or tap. */
      _finger.active = true;
      _finger.x = _finger.lastX = _finger.startX = pts[0].x;
      _finger.y = _finger.lastY = _finger.startY = pts[0].y;
      _finger.downTime = now;
      _finger.maxDist = 0;
      _finger.longFired = false;
      _finger.dragFired = false;
      _finger.suppressTap = true;   /* lifting fingers from a pinch is not a tap */
    } else {
      _finger.active = false;
    }
    updateSnapshot(pts, count);
    return;
  }

  /* ---------------- single-contact gestures ---------------- */
  if (count == 0) {
    if (_finger.active) {
      const uint32_t duration = now - _finger.downTime;
      const int16_t netX = (int16_t)(_finger.x - _finger.startX);
      const int16_t netY = (int16_t)(_finger.y - _finger.startY);

      emit(TouchEventType::UP, now, _finger.x, _finger.y, 0, netX, netY);

      if (_finger.dragFired) {
        emit(TouchEventType::DRAG_END, now, _finger.x, _finger.y, 0, netX, netY);
      }
      if (_finger.longFired) {
        emit(TouchEventType::LONG_RELEASE, now, _finger.x, _finger.y, 0);
      }

      const int16_t absX = (int16_t)abs(netX);
      const int16_t absY = (int16_t)abs(netY);
      if (duration <= GESTURE_SWIPE_MAX_MS &&
          (absX >= GESTURE_SWIPE_MIN_PX || absY >= GESTURE_SWIPE_MIN_PX)) {
        if (absX >= absY) {
          emit(netX > 0 ? TouchEventType::SWIPE_RIGHT : TouchEventType::SWIPE_LEFT,
               now, _finger.x, _finger.y, 0, netX, netY);
        } else {
          emit(netY > 0 ? TouchEventType::SWIPE_DOWN : TouchEventType::SWIPE_UP,
               now, _finger.x, _finger.y, 0, netX, netY);
        }
      } else if (!_finger.dragFired && !_finger.longFired && !_finger.suppressTap &&
                 duration <= GESTURE_TAP_MAX_MS &&
                 _finger.maxDist <= GESTURE_TAP_MOVE_PX) {
        const int16_t tapDistX = (int16_t)abs(_finger.x - _lastTapX);
        const int16_t tapDistY = (int16_t)abs(_finger.y - _lastTapY);
        if (_tapPending &&
            (now - _lastTapTime) <= GESTURE_DOUBLE_MS &&
            tapDistX <= GESTURE_DOUBLE_PX && tapDistY <= GESTURE_DOUBLE_PX) {
          emit(TouchEventType::DOUBLE_TAP, now, _finger.x, _finger.y, 0);
          _tapPending = false;
        } else {
          emit(TouchEventType::TAP, now, _finger.x, _finger.y, 0);
          _tapPending = true;
          _lastTapTime = now;
          _lastTapX = _finger.x;
          _lastTapY = _finger.y;
        }
      } else {
        _tapPending = false;
      }

      _finger.active = false;
      updateSnapshot(pts, 0);
    } else {
      updateSnapshot(pts, 0);
    }
    return;
  }

  /* one contact */
  const TouchPoint p = pts[0];

  if (!_finger.active) {
    _finger.active = true;
    _finger.x = _finger.lastX = _finger.startX = p.x;
    _finger.y = _finger.lastY = _finger.startY = p.y;
    _finger.downTime = now;
    _finger.maxDist = 0;
    _finger.longFired = false;
    _finger.dragFired = false;
    _finger.suppressTap = false;
    emit(TouchEventType::DOWN, now, p.x, p.y, 1);
    updateSnapshot(pts, 1);
    return;
  }

  const int16_t mdx = (int16_t)(p.x - _finger.x);
  const int16_t mdy = (int16_t)(p.y - _finger.y);
  if (mdx || mdy) {
    _finger.x = p.x;
    _finger.y = p.y;
    const int16_t sx = (int16_t)(p.x - _finger.startX);
    const int16_t sy = (int16_t)(p.y - _finger.startY);
    const int16_t moved = (int16_t)sqrtf((float)sx * sx + (float)sy * sy);
    if (moved > _finger.maxDist) _finger.maxDist = moved;

    emit(TouchEventType::MOVE, now, p.x, p.y, 1, mdx, mdy);

    if (!_finger.dragFired && moved > GESTURE_DRAG_START_PX) {
      _finger.dragFired = true;
      emit(TouchEventType::DRAG_START, now, p.x, p.y, 1, mdx, mdy);
    } else if (_finger.dragFired) {
      emit(TouchEventType::DRAG, now, p.x, p.y, 1, mdx, mdy);
    }
  }

  if (!_finger.longFired && !_finger.dragFired &&
      (now - _finger.downTime) >= GESTURE_LONG_MS &&
      _finger.maxDist <= GESTURE_LONG_MOVE_PX) {
    _finger.longFired = true;
    emit(TouchEventType::LONG_PRESS, now, _finger.startX, _finger.startY, 1);
  }

  updateSnapshot(pts, 1);
}

bool TouchManager::pop(TouchEvent& out) {
  if (!_queue) return false;
  return xQueueReceive(_queue, &out, 0) == pdTRUE;
}

void TouchManager::getPoints(TouchPoint& p0, TouchPoint& p1) const {
  portENTER_CRITICAL(&_mux);
  p0 = _snapshot[0];
  p1 = _snapshot[1];
  portEXIT_CRITICAL(&_mux);
}

uint8_t TouchManager::activeCount() const {
  portENTER_CRITICAL(&_mux);
  const uint8_t c = _snapshotCount;
  portEXIT_CRITICAL(&_mux);
  return c;
}

void TouchManager::getRaw(uint16_t& rx, uint16_t& ry) const {
  portENTER_CRITICAL(&_mux);
  rx = _rawX;
  ry = _rawY;
  portEXIT_CRITICAL(&_mux);
}

void TouchManager::setOrientation(bool swapXY, bool invertX, bool invertY) {
  portENTER_CRITICAL(&_mux);
  _swapXY = swapXY;
  _invertX = invertX;
  _invertY = invertY;
  portEXIT_CRITICAL(&_mux);

  Preferences prefs;
  if (prefs.begin(kCalNvs, false)) {
    prefs.putBool("valid", true);
    prefs.putBool("swap", swapXY);
    prefs.putBool("invx", invertX);
    prefs.putBool("invy", invertY);
    prefs.end();
  }
}
