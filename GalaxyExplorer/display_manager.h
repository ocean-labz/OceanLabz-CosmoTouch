/* ==========================================================================
 * display_manager.h  -  LovyanGFX panel setup + PSRAM frame buffer
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "space_canvas.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX();

 private:
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
};

class DisplayManager {
 public:
  bool begin();
  bool allocateFrameBuffer();

  inline LGFX& gfx() { return _lcd; }
  inline SpaceCanvas& canvas() { return _canvas; }
  inline uint16_t* frameBuffer() { return _frame; }
  inline bool frameInPsram() const { return _frameInPsram; }

  /* Push the whole frame buffer in one SPI transaction. */
  void present();

  void setBrightness(uint8_t value);
  void backlightOff();
  void backlightOn();

 private:
  LGFX _lcd;
  SpaceCanvas _canvas;
  uint16_t* _frame = nullptr;
  bool _frameInPsram = false;
  uint8_t _brightness = BACKLIGHT_DEFAULT;
};
