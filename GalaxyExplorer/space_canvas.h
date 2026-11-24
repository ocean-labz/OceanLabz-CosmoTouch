/* ==========================================================================
 * space_canvas.h  -  Software RGB565 canvas + particle drawing primitives
 * --------------------------------------------------------------------------
 * The galaxy is rendered into a full-screen RGB565 buffer (internal RAM when
 * available) and pushed to the ILI9341 in one shot, which avoids flicker and
 * keeps the SPI bus busy only once per displayed frame.
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t rgb565(uint32_t hex) {
  return rgb565((uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex);
}

namespace fx {

static constexpr uint16_t ANGLE_STEPS = 512;   /* full turn */
static constexpr int16_t  Q10         = 1024;

extern int16_t sinTable[ANGLE_STEPS];

void init();

inline int16_t sinT(uint16_t a) { return sinTable[a & (ANGLE_STEPS - 1)]; }
inline int16_t cosT(uint16_t a) { return sinTable[(a + ANGLE_STEPS / 4) & (ANGLE_STEPS - 1)]; }

/* signed Q10 multiply (values stay small enough for int32 safely) */
inline int16_t mulQ10(int16_t s, int16_t v) { return (int16_t)(((int32_t)s * v) >> 10); }

/* Fast integer pseudo-random (xorshift32) - called from the touch task too. */
uint32_t rand();
void seed(uint32_t s);
uint16_t randRange(uint16_t lo, uint16_t hi);
float randFloat();

} // namespace fx

enum class TextAlign : uint8_t { LEFT, CENTER, RIGHT };

class SpaceCanvas {
 public:
  bool begin(uint16_t* buffer, int16_t w, int16_t h);

  inline uint16_t* buffer() const { return _buf; }
  inline int16_t width()  const { return _w; }
  inline int16_t height() const { return _h; }

  void fill(uint16_t color);
  void blendFill(uint16_t color, uint8_t alpha);

  void setPixel(int16_t x, int16_t y, uint16_t color);
  inline uint16_t getPixel(int16_t x, int16_t y) const;

  void addPixel(int16_t x, int16_t y, uint16_t color, uint8_t alpha);
  void blendPixel(int16_t x, int16_t y, uint16_t color, uint8_t alpha);

  void addLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               uint16_t color, uint8_t alpha);
  void addCircleOutline(int16_t cx, int16_t cy, int16_t r,
                        uint16_t color, uint8_t alpha);
  void addCircleFill(int16_t cx, int16_t cy, int16_t r,
                     uint16_t color, uint8_t alpha);
  void addHLine(int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t alpha);

  void blendRect(int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t color, uint8_t alpha);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void blendRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                      uint16_t color, uint8_t alpha);
  void strokeRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                       uint16_t color, uint8_t alpha);

  /* Soft radial glow from a pre-generated mask bank (PSRAM). */
  void addGlow(int16_t cx, int16_t cy, int16_t diameter,
               uint16_t color, uint8_t intensity);

  /* Copy a premultiplied-alpha sprite (planet surfaces). */
  void blendSprite(const uint16_t* pixels, const uint8_t* alpha,
                   int16_t w, int16_t h, int16_t dx, int16_t dy,
                   uint8_t globalAlpha = 255);

  /* Copy an RGB565 block (background layer / scroll). */
  void copyBlock(const uint16_t* src, int16_t srcStride,
                 int16_t srcX, int16_t srcY, int16_t w, int16_t h);

  /* 5x7 bitmap font. scale 1 = 5x7 glyph, 2 = 10x14, 3 = 15x21. */
  static int16_t textWidth(const char* text, uint8_t scale);
  void drawText(int16_t x, int16_t y, const char* text, uint8_t scale,
                uint16_t color, uint8_t alpha = 255,
                TextAlign align = TextAlign::LEFT);

 private:
  void buildGlowMasks();
  const uint8_t* chooseGlow(int16_t diameter, int16_t* found);

  uint16_t* _buf = nullptr;
  int16_t _w = 0, _h = 0;

  static constexpr uint8_t GLOW_LEVELS = 10;
  static const uint16_t kGlowSizes[GLOW_LEVELS];
  uint8_t* _glow[GLOW_LEVELS] = {};
};
