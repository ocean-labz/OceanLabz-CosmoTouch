/* ==========================================================================
 * ES3C28P  -  Stage 1 hardware check
 * --------------------------------------------------------------------------
 * Independent diagnostic for the OceanLabz ES3C28P board:
 *   - ILI9341V 240x320 SPI display, colour-bar and orientation test
 *   - FT6336G I2C touch controller, chip id / contact count / raw + mapped
 *     coordinates
 *   - live multi-touch (two finger) verification
 *
 * Flash this before the full Galaxy Explorer if the display or touch mapping
 * needs to be confirmed. Nothing here depends on the main application.
 *
 * Serial monitor: 115200 baud.
 * ========================================================================== */
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <LovyanGFX.hpp>

/* ---- Board pins (ES3C28P) ------------------------------------------------ */
#define LCD_CS      10
#define LCD_DC      46
#define LCD_SCK     12
#define LCD_MOSI    11
#define LCD_MISO    13
#define LCD_BL    45

#define TOUCH_SDA   16
#define TOUCH_SCL   15
#define TOUCH_RST   18
#define TOUCH_INT   17
#define TOUCH_ADDR  0x38

/* ---- Orientation calibration --------------------------------------------
 * Flip these if the crosshair does not land under your finger.
 * The raw values are printed on screen and over Serial, so you can see
 * exactly what the panel reports.                                          */
#define SWAP_XY     0
#define INVERT_X    0
#define INVERT_Y    0
#define RAW_MAX_X   240
#define RAW_MAX_Y   320

#define SPI_WRITE_HZ 40000000UL

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = SPI_WRITE_HZ;
      cfg.freq_read   = 16000000;
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
      cfg.pin_cs          = LCD_CS;
      cfg.pin_rst         = -1;
      cfg.pin_busy        = -1;
      cfg.memory_width    = 240;
      cfg.memory_height   = 320;
      cfg.panel_width     = 240;
      cfg.panel_height    = 320;
      cfg.readable        = true;
      cfg.invert          = false;   /* set true if the image looks negative */
      cfg.rgb_order       = false;   /* set true if red and blue are swapped */
      cfg.dlen_16bit      = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = LCD_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
    }
    _panel.setBus(&_bus);
    _panel.setLight(&_light);
    setPanel(&_panel);
  }

 private:
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
};

LGFX lcd;

struct Pt { bool down; uint16_t rx, ry; int16_t x, y; };

static int16_t mapAxis(uint16_t raw, uint16_t rawMax, uint16_t outMax, bool invert) {
  if (raw >= rawMax) raw = rawMax - 1;
  if (invert) raw = rawMax - 1 - raw;
  return (int16_t)(((uint32_t)raw * (outMax - 1)) / (rawMax - 1));
}

/*** Read FT6336 contact list. Returns number of valid contacts. ***/
static uint8_t readTouch(Pt out[2]) {
  out[0] = Pt{};
  out[1] = Pt{};

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((uint8_t)0x02);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)1) != 1) return 0;
  const uint8_t status = Wire.read() & 0x0F;
  const uint8_t reported = status > 2 ? 2 : status;
  if (!reported) return 0;

  uint8_t buf[12] = {};
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((uint8_t)0x03);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)(reported * 6)) != reported * 6) return 0;
  for (uint8_t i = 0; i < reported * 6; ++i) buf[i] = (uint8_t)Wire.read();

  uint8_t valid = 0;
  for (uint8_t i = 0; i < reported; ++i) {
    const uint8_t* p = buf + i * 6;
    const uint8_t ev = (p[0] >> 6) & 3;
    if (ev == 1) continue;               /* lift */
    const uint16_t rawX = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
    const uint16_t rawY = (uint16_t)(((p[2] & 0x0F) << 8) | p[3]);
    if (rawX >= 0x0FFF || rawY >= 0x0FFF) continue;

    uint16_t cx = rawX, cy = rawY;
    uint16_t mx = RAW_MAX_X, my = RAW_MAX_Y;
#if SWAP_XY
    uint16_t t = cx; cx = cy; cy = t;
    t = mx; mx = my; my = t;
#endif
    out[valid].down = true;
    out[valid].rx = rawX;
    out[valid].ry = rawY;
    out[valid].x = mapAxis(cx, mx, 240, INVERT_X != 0);
    out[valid].y = mapAxis(cy, my, 320, INVERT_Y != 0);
    ++valid;
  }
  return valid;
}

static uint8_t readReg8(uint8_t reg, uint8_t* val) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)1) != 1) return 0;
  *val = (uint8_t)Wire.read();
  return 1;
}

static uint8_t gMaxSeen = 0;
static uint16_t g_rawX = 0, g_rawY = 0;

static void drawChrome() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(TFT_CYAN);
  lcd.drawString("ES3C28P  HARDWARE CHECK", 6, 6);

  /* colour bars */
  const uint16_t bars[7] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN,
                            TFT_MAGENTA, TFT_YELLOW, TFT_WHITE};
  for (uint8_t i = 0; i < 7; ++i) {
    lcd.fillRect((int16_t)(i * 34), 30, 34, 16, bars[i]);
  }
  lcd.drawRect(0, 30, 238, 16, TFT_DARKGREY);

  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(TFT_LIGHTGREY);
  lcd.drawString("240x320 PORTRAIT  ILI9341V  SPI", 6, 50);
  lcd.drawString("COLOUR BARS: R G B C M Y W", 6, 62);

  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("TOUCH: FT6336G @0x38", 6, 76);
  lcd.drawString("TAP ALL FOUR CORNERS + DRAW A LINE", 6, 86);

  /* touch area */
  lcd.drawRect(4, 108, 232, 190, TFT_DARKCYAN);
  lcd.setTextColor(TFT_DARKGREY);
  lcd.drawString("TOUCH AREA", 10, 112);

  lcd.fillRect(0, 302, 240, 18, 0x0841);
  lcd.setTextColor(TFT_CYAN);
  lcd.drawString("BAD MAPPING? EDIT SWAP/INVERT MACROS", 4, 307);
}

static void drawInfo(uint8_t count) {
  lcd.fillRect(0, 95, 240, 12, TFT_BLACK);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(TFT_LIGHTGREY);
  char buf[64];
  snprintf(buf, sizeof(buf), "PTS %u  MAX SEEN %u   RAW %04u %04u",
           count, gMaxSeen, g_rawX, g_rawY);
  lcd.drawString(buf, 6, 96);
}

static void printI2cScan() {
  Serial.println("[i2c] scan:");
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("   found 0x%02X\n", addr);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("=== ES3C28P HARDWARE CHECK ===");

  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(60);

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.setTimeOut(30);

  lcd.init();
  lcd.setRotation(0);
  lcd.setColorDepth(16);
  lcd.setBrightness(220);
  lcd.fillScreen(TFT_BLACK);

  uint8_t id = 0, idAlt = 0;
  const bool haveId = readReg8(0xA3, &id);
  const bool haveAlt = readReg8(0xA8, &idAlt);
  Serial.printf("[display] %ldx%ld\n", (long)lcd.width(), (long)lcd.height());
  Serial.printf("[touch] chip id 0xA3=%s0x%02X  0xA8=%s0x%02X\n",
                haveId ? "" : "?", id, haveAlt ? "" : "?", idAlt);
  printI2cScan();

  drawChrome();
  drawInfo(0);
}

void loop() {
  static uint8_t prevCount = 0;
  static int16_t prevX = -1, prevY = -1;
  static uint32_t lastPrint = 0;

  Pt pts[2];
  const uint8_t count = readTouch(pts);
  if (count > gMaxSeen) gMaxSeen = count;
  if (count) { g_rawX = pts[0].rx; g_rawY = pts[0].ry; }

  if (count != prevCount || (count && (pts[0].x != prevX || pts[0].y != prevY))) {
    /* erase old markers and rebuild the touch area */
    lcd.fillRect(5, 118, 230, 178, TFT_BLACK);
    lcd.drawRect(4, 108, 232, 190, TFT_DARKCYAN);
    lcd.setTextColor(TFT_DARKGREY);
    lcd.setFont(&fonts::Font0);
    lcd.drawString("TOUCH AREA", 10, 112);

    const uint16_t colors[2] = {TFT_CYAN, TFT_MAGENTA};
    for (uint8_t i = 0; i < count; ++i) {
      lcd.drawCircle(pts[i].x, pts[i].y, 22, colors[i]);
      lcd.drawCircle(pts[i].x, pts[i].y, 16, colors[i]);
      lcd.fillCircle(pts[i].x, pts[i].y, 4, TFT_WHITE);
      lcd.drawFastHLine(pts[i].x - 26, pts[i].y, 52, colors[i]);
      lcd.drawFastVLine(pts[i].x, pts[i].y - 26, 52, colors[i]);

      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(colors[i]);
      char line[24];
      snprintf(line, sizeof(line), "X %03d Y %03d", pts[i].x, pts[i].y);
      lcd.drawString(line, 8 + i * 116, 128);
    }

    if (!count) {
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(TFT_DARKGREY);
      lcd.drawString("TOUCH THE PANEL", 62, 190);
    }

    drawInfo(count);

    if (millis() - lastPrint > 80) {
      lastPrint = millis();
      Serial.printf("[touch] n=%u raw=%u,%u map=%d,%d",
                    count, g_rawX, g_rawY, pts[0].x, pts[0].y);
      if (count > 1) Serial.printf("  p2=%d,%d", pts[1].x, pts[1].y);
      Serial.println();
    }
  }

  prevX = count ? pts[0].x : -1;
  prevY = count ? pts[0].y : -1;
  prevCount = count;
  delay(12);
}
