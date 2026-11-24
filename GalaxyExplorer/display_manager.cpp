/* ==========================================================================
 * display_manager.cpp
 * ========================================================================== */
#include "display_manager.h"
#include <string.h>
#include <esp_heap_caps.h>

LGFX::LGFX() {
  {
    auto cfg = _bus.config();
    cfg.spi_host    = SPI2_HOST;
    cfg.spi_mode    = 0;
    cfg.freq_write  = DISPLAY_SPI_FREQ;
    cfg.freq_read   = DISPLAY_SPI_READ_FREQ;
    cfg.spi_3wire   = false;
    cfg.use_lock    = true;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk    = LCD_SCK;
    cfg.pin_mosi    = LCD_MOSI;
    cfg.pin_miso    = LCD_MISO;
    cfg.pin_dc      = LCD_DC;
    _bus.config(cfg);
  }
  {
    auto cfg = _panel.config();
    cfg.pin_cs           = LCD_CS;
    cfg.pin_rst          = -1;            /* reset is tied to CHIP_PU */
    cfg.pin_busy         = -1;
    cfg.memory_width     = SCREEN_W;
    cfg.memory_height    = SCREEN_H;
    cfg.panel_width      = SCREEN_W;
    cfg.panel_height     = SCREEN_H;
    cfg.offset_x         = 0;
    cfg.offset_y         = 0;
    cfg.offset_rotation  = 0;
    cfg.readable         = true;
    cfg.invert           = DISPLAY_INVERT;
    cfg.rgb_order        = DISPLAY_RGB_ORDER;
    cfg.dlen_16bit       = false;
    cfg.bus_shared       = false;
    _panel.config(cfg);
  }
  {
    auto cfg = _light.config();
    cfg.pin_bl      = LCD_BL;
    cfg.invert      = false;              /* backlight is active HIGH */
    cfg.freq        = 12000;
    cfg.pwm_channel = 7;
    _light.config(cfg);
  }
  _panel.setBus(&_bus);
  _panel.setLight(&_light);
  setPanel(&_panel);
}

bool DisplayManager::begin() {
  _lcd.init();
  _lcd.setRotation(DISPLAY_ROTATION);
  _lcd.setColorDepth(16);
  /* The software canvas stores logical RGB565 values. Tell LovyanGFX so that
   * pushImage() byte order matches the panel instead of treating the buffer
   * as a pre-swapped image. */
  _lcd.setSwapBytes(true);
  _lcd.fillScreen(SPACE_BG);
  _lcd.setTextWrap(false);
  _lcd.setBrightness(_brightness);
  return _lcd.width() == SCREEN_W && _lcd.height() == SCREEN_H;
}

bool DisplayManager::allocateFrameBuffer() {
  const size_t bytes = (size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t);

  /* Internal DMA-capable RAM is faster for the software renderer and lets
   * LovyanGFX use DMA for the SPI transfer. Fall back to PSRAM.            */
  _frame = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  _frameInPsram = false;
  if (!_frame) {
    _frame = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _frameInPsram = (_frame != nullptr);
  }
  if (!_frame) return false;

  memset(_frame, 0, bytes);
  return _canvas.begin(_frame, SCREEN_W, SCREEN_H);
}

void DisplayManager::present() {
  if (!_frame) return;
  _lcd.pushImage(0, 0, SCREEN_W, SCREEN_H, _frame);
}

void DisplayManager::setBrightness(uint8_t value) {
  _brightness = value;
  _lcd.setBrightness(value);
}

void DisplayManager::backlightOff() { _lcd.setBrightness(0); }
void DisplayManager::backlightOn()  { _lcd.setBrightness(_brightness); }
