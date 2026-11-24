/* ==========================================================================
 * space_canvas.cpp
 * ========================================================================== */
#include "space_canvas.h"
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>

/* -------------------------------------------------------------------------- */
/*  Trig / random                                                             */
/* -------------------------------------------------------------------------- */
namespace fx {

int16_t sinTable[ANGLE_STEPS];

void init() {
  for (uint16_t i = 0; i < ANGLE_STEPS; ++i) {
    sinTable[i] = (int16_t)lroundf(sinf((float)i * 2.0f * (float)M_PI / ANGLE_STEPS) * Q10);
  }
}

static uint32_t s_randState = 0x9E3779B9u;

void seed(uint32_t s) { s_randState = s ? s : 0x1234567u; }

uint32_t rand() {
  uint32_t x = s_randState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s_randState = x;
  return x;
}

uint16_t randRange(uint16_t lo, uint16_t hi) {
  if (hi <= lo) return lo;
  return (uint16_t)(lo + (rand() % (uint32_t)(hi - lo + 1)));
}

float randFloat() { return (rand() & 0xFFFF) / 65535.0f; }

} // namespace fx

/* -------------------------------------------------------------------------- */
/*  5x7 bitmap font                                                           */
/* -------------------------------------------------------------------------- */
namespace {

const uint8_t kGlyphSpace[7] = {0, 0, 0, 0, 0, 0, 0};
const uint8_t kGlyphA[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
const uint8_t kGlyphB[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
const uint8_t kGlyphC[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
const uint8_t kGlyphD[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
const uint8_t kGlyphE[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
const uint8_t kGlyphF[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
const uint8_t kGlyphG[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
const uint8_t kGlyphH[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
const uint8_t kGlyphI[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
const uint8_t kGlyphJ[7] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C};
const uint8_t kGlyphK[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
const uint8_t kGlyphL[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
const uint8_t kGlyphM[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
const uint8_t kGlyphN[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
const uint8_t kGlyphO[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
const uint8_t kGlyphP[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
const uint8_t kGlyphQ[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
const uint8_t kGlyphR[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
const uint8_t kGlyphS[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
const uint8_t kGlyphT[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
const uint8_t kGlyphU[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
const uint8_t kGlyphV[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
const uint8_t kGlyphW[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
const uint8_t kGlyphX[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
const uint8_t kGlyphY[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
const uint8_t kGlyphZ[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};

const uint8_t kGlyph0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
const uint8_t kGlyph1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
const uint8_t kGlyph2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
const uint8_t kGlyph3[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
const uint8_t kGlyph4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
const uint8_t kGlyph5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
const uint8_t kGlyph6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
const uint8_t kGlyph7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
const uint8_t kGlyph8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
const uint8_t kGlyph9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};

const uint8_t kGlyphColon[7] = {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00};
const uint8_t kGlyphDot[7]   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
const uint8_t kGlyphDash[7]  = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
const uint8_t kGlyphSlash[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
const uint8_t kGlyphBang[7]  = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
const uint8_t kGlyphPlus[7]  = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
const uint8_t kGlyphPercent[7] = {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13};
const uint8_t kGlyphLPar[7]  = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
const uint8_t kGlyphRPar[7]  = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
const uint8_t kGlyphQues[7]  = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
const uint8_t kGlyphComma[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
const uint8_t kGlyphEq[7]    = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00};
const uint8_t kGlyphStar[7]  = {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00};
const uint8_t kGlyphLt[7]    = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02};
const uint8_t kGlyphGt[7]    = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08};
const uint8_t kGlyphHash[7]  = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A};
const uint8_t kGlyphAmp[7]   = {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D};

struct GlyphEntry { char c; const uint8_t* rows; };
const GlyphEntry kGlyphs[] = {
  {' ', kGlyphSpace}, {'A', kGlyphA}, {'B', kGlyphB}, {'C', kGlyphC}, {'D', kGlyphD},
  {'E', kGlyphE}, {'F', kGlyphF}, {'G', kGlyphG}, {'H', kGlyphH}, {'I', kGlyphI},
  {'J', kGlyphJ}, {'K', kGlyphK}, {'L', kGlyphL}, {'M', kGlyphM}, {'N', kGlyphN},
  {'O', kGlyphO}, {'P', kGlyphP}, {'Q', kGlyphQ}, {'R', kGlyphR}, {'S', kGlyphS},
  {'T', kGlyphT}, {'U', kGlyphU}, {'V', kGlyphV}, {'W', kGlyphW}, {'X', kGlyphX},
  {'Y', kGlyphY}, {'Z', kGlyphZ},
  {'0', kGlyph0}, {'1', kGlyph1}, {'2', kGlyph2}, {'3', kGlyph3}, {'4', kGlyph4},
  {'5', kGlyph5}, {'6', kGlyph6}, {'7', kGlyph7}, {'8', kGlyph8}, {'9', kGlyph9},
  {':', kGlyphColon}, {'.', kGlyphDot}, {'-', kGlyphDash}, {'/', kGlyphSlash},
  {'!', kGlyphBang}, {'+', kGlyphPlus}, {'%', kGlyphPercent}, {'(', kGlyphLPar},
  {')', kGlyphRPar}, {'?', kGlyphQues}, {',', kGlyphComma}, {'=', kGlyphEq},
  {'*', kGlyphStar}, {'<', kGlyphLt}, {'>', kGlyphGt}, {'#', kGlyphHash},
  {'&', kGlyphAmp},
};

const uint8_t* glyphFor(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (const auto& g : kGlyphs) {
    if (g.c == c) return g.rows;
  }
  return kGlyphQues;
}

} // namespace

/* -------------------------------------------------------------------------- */
/*  Glow masks                                                                */
/* -------------------------------------------------------------------------- */
const uint16_t SpaceCanvas::kGlowSizes[SpaceCanvas::GLOW_LEVELS] =
    {5, 8, 12, 16, 24, 32, 48, 64, 96, 128};

/* -------------------------------------------------------------------------- */
/*  Canvas                                                                    */
/* -------------------------------------------------------------------------- */
bool SpaceCanvas::begin(uint16_t* buffer, int16_t w, int16_t h) {
  _buf = buffer;
  _w = w;
  _h = h;
  if (!_buf) return false;
  buildGlowMasks();
  return true;
}

void SpaceCanvas::buildGlowMasks() {
  for (uint8_t lvl = 0; lvl < GLOW_LEVELS; ++lvl) {
    const uint16_t size = kGlowSizes[lvl];
    const uint16_t bytes = (uint16_t)(size * size);
    uint8_t* mask = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mask) mask = (uint8_t*)malloc(bytes);
    if (!mask) {
      _glow[lvl] = nullptr;
      continue;
    }
    const float half = size * 0.5f;
    for (uint16_t y = 0; y < size; ++y) {
      for (uint16_t x = 0; x < size; ++x) {
        const float dx = (x + 0.5f - half) / half;
        const float dy = (y + 0.5f - half) / half;
        const float d2 = dx * dx + dy * dy;
        float a = 0.0f;
        if (d2 < 1.0f) {
          a = expf(-3.4f * d2) * (1.0f - d2);
          if (a > 1.0f) a = 1.0f;
        }
        mask[y * size + x] = (uint8_t)(a * 255.0f);
      }
    }
    _glow[lvl] = mask;
  }
}

const uint8_t* SpaceCanvas::chooseGlow(int16_t diameter, int16_t* found) {
  if (diameter < kGlowSizes[0]) diameter = kGlowSizes[0];
  for (uint8_t i = 0; i < GLOW_LEVELS; ++i) {
    if (kGlowSizes[i] >= diameter) {
      *found = (int16_t)kGlowSizes[i];
      return _glow[i];
    }
  }
  *found = (int16_t)kGlowSizes[GLOW_LEVELS - 1];
  return _glow[GLOW_LEVELS - 1];
}

void SpaceCanvas::addGlow(int16_t cx, int16_t cy, int16_t diameter,
                          uint16_t color, uint8_t intensity) {
  if (!intensity) return;
  int16_t size = 0;
  const uint8_t* mask = chooseGlow(diameter, &size);
  if (!mask) {
    /* Allocation fallback: a plain additive disc instead of a soft glow. */
    addCircleFill(cx, cy, (int16_t)(diameter / 3), color, intensity);
    return;
  }

  const int16_t half = size >> 1;
  int16_t x0 = cx - half;
  int16_t y0 = cy - half;
  int16_t sx = 0, sy = 0, ex = size, ey = size;
  if (x0 < 0) { sx = -x0; x0 = 0; }
  if (y0 < 0) { sy = -y0; y0 = 0; }
  if (x0 + (ex - sx) > _w) ex = sx + (_w - x0);
  if (y0 + (ey - sy) > _h) ey = sy + (_h - y0);
  if (ex <= sx || ey <= sy) return;

  const uint8_t cr = (color >> 11) & 0x1F;
  const uint8_t cg = (color >> 5) & 0x3F;
  const uint8_t cb = color & 0x1F;

  for (int16_t my = sy; my < ey; ++my) {
    uint16_t* dst = _buf + (size_t)(y0 + my - sy) * _w + x0;
    const uint8_t* mrow = mask + (size_t)my * size;
    for (int16_t mx = sx; mx < ex; ++mx) {
      const uint8_t a = (uint8_t)(((uint16_t)mrow[mx] * intensity) >> 8);
      if (!a) { ++dst; continue; }
      const uint16_t d = *dst;
      uint16_t r = ((d >> 11) & 0x1F) + ((cr * a) >> 8);
      uint16_t g = ((d >> 5) & 0x3F) + ((cg * a) >> 8);
      uint16_t b = (d & 0x1F) + ((cb * a) >> 8);
      if (r > 31) r = 31;
      if (g > 63) g = 63;
      if (b > 31) b = 31;
      *dst++ = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
}

void SpaceCanvas::fill(uint16_t color) {
  const size_t n = (size_t)_w * _h;
  for (size_t i = 0; i < n; ++i) _buf[i] = color;
}

void SpaceCanvas::blendFill(uint16_t color, uint8_t alpha) {
  for (int16_t y = 0; y < _h; ++y) {
    for (int16_t x = 0; x < _w; ++x) blendPixel(x, y, color, alpha);
  }
}

void SpaceCanvas::setPixel(int16_t x, int16_t y, uint16_t color) {
  if ((uint16_t)x < (uint16_t)_w && (uint16_t)y < (uint16_t)_h)
    _buf[(size_t)y * _w + x] = color;
}

uint16_t SpaceCanvas::getPixel(int16_t x, int16_t y) const {
  if ((uint16_t)x >= (uint16_t)_w || (uint16_t)y >= (uint16_t)_h) return 0;
  return _buf[(size_t)y * _w + x];
}

void SpaceCanvas::addPixel(int16_t x, int16_t y, uint16_t color, uint8_t alpha) {
  if ((uint16_t)x >= (uint16_t)_w || (uint16_t)y >= (uint16_t)_h || !alpha) return;
  uint16_t* p = &_buf[(size_t)y * _w + x];
  const uint16_t d = *p;
  uint16_t r = ((d >> 11) & 0x1F) + (((((color >> 11) & 0x1F)) * alpha) >> 8);
  uint16_t g = ((d >> 5) & 0x3F) + (((((color >> 5) & 0x3F)) * alpha) >> 8);
  uint16_t b = (d & 0x1F) + (((color & 0x1F) * alpha) >> 8);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  *p = (uint16_t)((r << 11) | (g << 5) | b);
}

void SpaceCanvas::blendPixel(int16_t x, int16_t y, uint16_t color, uint8_t alpha) {
  if ((uint16_t)x >= (uint16_t)_w || (uint16_t)y >= (uint16_t)_h || !alpha) return;
  if (alpha == 255) { _buf[(size_t)y * _w + x] = color; return; }
  uint16_t* p = &_buf[(size_t)y * _w + x];
  const uint16_t d = *p;
  int32_t dr = (d >> 11) & 0x1F;
  int32_t dg = (d >> 5) & 0x3F;
  int32_t db = d & 0x1F;
  const int32_t sr = (color >> 11) & 0x1F;
  const int32_t sg = (color >> 5) & 0x3F;
  const int32_t sb = color & 0x1F;
  dr += ((sr - dr) * alpha) >> 8;
  dg += ((sg - dg) * alpha) >> 8;
  db += ((sb - db) * alpha) >> 8;
  *p = (uint16_t)((dr << 11) | (dg << 5) | db);
}

void SpaceCanvas::addHLine(int16_t x, int16_t y, int16_t w,
                           uint16_t color, uint8_t alpha) {
  if ((uint16_t)y >= (uint16_t)_h) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > _w) w = _w - x;
  if (w <= 0) return;
  for (int16_t i = 0; i < w; ++i) addPixel((int16_t)(x + i), y, color, alpha);
}

void SpaceCanvas::addLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint16_t color, uint8_t alpha) {
  int16_t dx = (int16_t)abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = (int16_t)-abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  while (true) {
    addPixel(x0, y0, color, alpha);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = (int16_t)(2 * err);
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void SpaceCanvas::addCircleOutline(int16_t cx, int16_t cy, int16_t r,
                                   uint16_t color, uint8_t alpha) {
  if (r <= 0) { addPixel(cx, cy, color, alpha); return; }
  int16_t x = r, y = 0, err = 1 - r;
  while (x >= y) {
    addPixel((int16_t)(cx + x), (int16_t)(cy + y), color, alpha);
    addPixel((int16_t)(cx + y), (int16_t)(cy + x), color, alpha);
    addPixel((int16_t)(cx - y), (int16_t)(cy + x), color, alpha);
    addPixel((int16_t)(cx - x), (int16_t)(cy + y), color, alpha);
    addPixel((int16_t)(cx - x), (int16_t)(cy - y), color, alpha);
    addPixel((int16_t)(cx - y), (int16_t)(cy - x), color, alpha);
    addPixel((int16_t)(cx + y), (int16_t)(cy - x), color, alpha);
    addPixel((int16_t)(cx + x), (int16_t)(cy - y), color, alpha);
    ++y;
    if (err < 0) err += 2 * y + 1;
    else { --x; err += 2 * (y - x) + 1; }
  }
}

void SpaceCanvas::addCircleFill(int16_t cx, int16_t cy, int16_t r,
                                uint16_t color, uint8_t alpha) {
  if (r <= 0) { addPixel(cx, cy, color, alpha); return; }
  int16_t x = 0, y = r, err = 1 - r;
  while (x <= y) {
    addHLine((int16_t)(cx - y), (int16_t)(cy + x), (int16_t)(2 * y + 1), color, alpha);
    addHLine((int16_t)(cx - y), (int16_t)(cy - x), (int16_t)(2 * y + 1), color, alpha);
    addHLine((int16_t)(cx - x), (int16_t)(cy + y), (int16_t)(2 * x + 1), color, alpha);
    addHLine((int16_t)(cx - x), (int16_t)(cy - y), (int16_t)(2 * x + 1), color, alpha);
    ++x;
    if (err < 0) err += 2 * x + 1;
    else { --y; err += 2 * (x - y) + 1; }
  }
}

void SpaceCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _w) w = _w - x;
  if (y + h > _h) h = _h - y;
  for (int16_t row = 0; row < h; ++row) {
    uint16_t* p = _buf + (size_t)(y + row) * _w + x;
    for (int16_t i = 0; i < w; ++i) p[i] = color;
  }
}

void SpaceCanvas::blendRect(int16_t x, int16_t y, int16_t w, int16_t h,
                            uint16_t color, uint8_t alpha) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _w) w = _w - x;
  if (y + h > _h) h = _h - y;
  for (int16_t row = 0; row < h; ++row) {
    for (int16_t i = 0; i < w; ++i) blendPixel((int16_t)(x + i), (int16_t)(y + row), color, alpha);
  }
}

void SpaceCanvas::blendRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                 int16_t r, uint16_t color, uint8_t alpha) {
  if (r <= 0) { blendRect(x, y, w, h, color, alpha); return; }
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  blendRect((int16_t)(x + r), y, (int16_t)(w - 2 * r), h, color, alpha);
  blendRect(x, (int16_t)(y + r), r, (int16_t)(h - 2 * r), color, alpha);
  blendRect((int16_t)(x + w - r), (int16_t)(y + r), r, (int16_t)(h - 2 * r), color, alpha);
  for (int16_t row = 0; row < r; ++row) {
    const float dy = (float)(r - row) - 0.5f;
    int16_t inset = (int16_t)(r - sqrtf((float)(r * r) - dy * dy) + 0.5f);
    int16_t rw = (int16_t)(r - inset);
    blendRect((int16_t)(x + inset), (int16_t)(y + row), rw, 1, color, alpha);
    blendRect((int16_t)(x + w - r), (int16_t)(y + row), rw, 1, color, alpha);
    blendRect((int16_t)(x + inset), (int16_t)(y + h - 1 - row), rw, 1, color, alpha);
    blendRect((int16_t)(x + w - r), (int16_t)(y + h - 1 - row), rw, 1, color, alpha);
  }
}

void SpaceCanvas::strokeRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                  int16_t r, uint16_t color, uint8_t alpha) {
  if (r < 1) { r = 1; }
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  addHLine((int16_t)(x + r), y, (int16_t)(w - 2 * r), color, alpha);
  addHLine((int16_t)(x + r), (int16_t)(y + h - 1), (int16_t)(w - 2 * r), color, alpha);
  addLine((int16_t)(x), (int16_t)(y + r), (int16_t)(x), (int16_t)(y + h - 1 - r), color, alpha);
  addLine((int16_t)(x + w - 1), (int16_t)(y + r), (int16_t)(x + w - 1), (int16_t)(y + h - 1 - r), color, alpha);
  /* quarter-circle corners, drawn as connected segments */
  const int16_t cx[4] = {(int16_t)(x + r), (int16_t)(x + w - 1 - r),
                         (int16_t)(x + r), (int16_t)(x + w - 1 - r)};
  const int16_t cy[4] = {(int16_t)(y + r), (int16_t)(y + r),
                         (int16_t)(y + h - 1 - r), (int16_t)(y + h - 1 - r)};
  const int8_t sx[4] = {-1, 1, -1, 1};
  const int8_t sy[4] = {-1, -1, 1, 1};

  for (uint8_t corner = 0; corner < 4; ++corner) {
    int16_t prevX = 0, prevY = 0;
    for (uint8_t k = 0; k <= 8; ++k) {
      const uint16_t a = (uint16_t)(k * (fx::ANGLE_STEPS / 4) / 8);
      const int16_t ox = (int16_t)(((int32_t)fx::cosT(a) * r) >> 10);
      const int16_t oy = (int16_t)(((int32_t)fx::sinT(a) * r) >> 10);
      const int16_t px = (int16_t)(cx[corner] + sx[corner] * ox);
      const int16_t py = (int16_t)(cy[corner] + sy[corner] * oy);
      if (k) addLine(prevX, prevY, px, py, color, alpha);
      prevX = px;
      prevY = py;
    }
  }
}

void SpaceCanvas::blendSprite(const uint16_t* pixels, const uint8_t* alpha,
                              int16_t w, int16_t h, int16_t dx, int16_t dy,
                              uint8_t globalAlpha) {
  for (int16_t y = 0; y < h; ++y) {
    const int16_t ty = (int16_t)(dy + y);
    if ((uint16_t)ty >= (uint16_t)_h) continue;
    const uint16_t* srow = pixels + (size_t)y * w;
    const uint8_t* arow = alpha + (size_t)y * w;
    for (int16_t x = 0; x < w; ++x) {
      const int16_t tx = (int16_t)(dx + x);
      if ((uint16_t)tx >= (uint16_t)_w) continue;
      uint16_t a = arow[x];
      if (!a) continue;
      if (globalAlpha != 255) a = (uint16_t)((a * globalAlpha) >> 8);
      blendPixel(tx, ty, srow[x], (uint8_t)a);
    }
  }
}

void SpaceCanvas::copyBlock(const uint16_t* src, int16_t srcStride,
                            int16_t srcX, int16_t srcY, int16_t w, int16_t h) {
  if (w > _w) w = _w;
  if (h > _h) h = _h;
  for (int16_t y = 0; y < h; ++y) {
    memcpy(_buf + (size_t)y * _w,
           src + (size_t)(srcY + y) * srcStride + srcX,
           (size_t)w * sizeof(uint16_t));
  }
}

/* -------------------------------------------------------------------------- */
/*  Text                                                                      */
/* -------------------------------------------------------------------------- */
int16_t SpaceCanvas::textWidth(const char* text, uint8_t scale) {
  if (!text) return 0;
  const int16_t len = (int16_t)strlen(text);
  if (len <= 0) return 0;
  return (int16_t)((len * 6 - 1) * scale);
}

void SpaceCanvas::drawText(int16_t x, int16_t y, const char* text, uint8_t scale,
                           uint16_t color, uint8_t alpha, TextAlign align) {
  if (!text || scale == 0) return;
  const int16_t total = textWidth(text, scale);
  if (align == TextAlign::CENTER) x -= total / 2;
  else if (align == TextAlign::RIGHT) x -= total;

  int16_t cursorX = x;
  for (const char* p = text; *p; ++p) {
    const uint8_t* glyph = glyphFor(*p);
    if (*p != ' ') {
      for (uint8_t row = 0; row < 7; ++row) {
        const uint8_t bits = glyph[row];
        if (!bits) continue;
        for (uint8_t col = 0; col < 5; ++col) {
          if (bits & (0x10 >> col)) {
            if (scale == 1) {
              blendPixel((int16_t)(cursorX + col), (int16_t)(y + row), color, alpha);
            } else {
              blendRect((int16_t)(cursorX + col * scale), (int16_t)(y + row * scale),
                        scale, scale, color, alpha);
            }
          }
        }
      }
    }
    cursorX = (int16_t)(cursorX + 6 * scale);
  }
}
