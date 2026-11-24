/* ==========================================================================
 * pins.h  -  ES3C28P (ESP32-S3) hardware pin map
 * --------------------------------------------------------------------------
 * These are the confirmed pins for the OceanLabz ES3C28P board.
 * Do not change them unless a hardware conflict has been verified on the
 * actual board.
 * ========================================================================== */
#pragma once

/* ---- Display : ILI9341V 240x320 IPS, SPI -------------------------------- */
#define LCD_CS      10
#define LCD_DC      46
#define LCD_SCK     12
#define LCD_MOSI    11
#define LCD_MISO    13
#define LCD_BL      45           /* backlight, active HIGH */

/* ---- Capacitive touch : FT6336G, I2C ------------------------------------- */
#define TOUCH_SDA   16
#define TOUCH_SCL   15
#define TOUCH_RST   18
#define TOUCH_INT   17
#define TOUCH_I2C_ADDR 0x38

/* ---- Miscellaneous ------------------------------------------------------- */
#define RGB_LED     42
#define BAT_ADC     9
#define BOOT_BTN    0
