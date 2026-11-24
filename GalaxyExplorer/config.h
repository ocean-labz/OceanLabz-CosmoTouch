/* ==========================================================================
 * config.h  -  Compile-time configuration / calibration
 * ========================================================================== */
#pragma once
#include <stdint.h>
#include "pins.h"

/* ---- Display ------------------------------------------------------------- */
#define SCREEN_W                 240
#define SCREEN_H                 320
#define SPACE_BG                 0x0000       /* RGB565 pure black */
#define DISPLAY_SPI_FREQ         60000000UL   /* lower to 40000000 if artifacts */
#define DISPLAY_SPI_READ_FREQ    16000000UL
#define DISPLAY_ROTATION         0            /* 0..3, portrait at 0 */
/* The ES3C28P uses an IPS panel whose native colours are inverted. With
 * DISPLAY_INVERT clear, 0x0000 is displayed as off-white. The panel also
 * needs BGR order; RGB_ORDER 0 swaps red and blue. */
#define DISPLAY_INVERT           1
#define DISPLAY_RGB_ORDER        1
#define BACKLIGHT_DEFAULT        210
#define BACKLIGHT_DIM            40

/* ---- Touch --------------------------------------------------------------- */
#define TOUCH_I2C_FREQ           400000UL
#define TOUCH_POLL_MS            8            /* ~125 Hz sampling */
#define TOUCH_ASYNC_TASK         1            /* 1 = dedicated core-0 polling task */
#define TOUCH_TASK_CORE          0
#define TOUCH_DRIVER_MAX_POINTS  2            /* FT6336G reports up to 2 points */

/* Raw panel orientation. The FT6336G on this board reports Y with its origin
 * at the physical bottom, while the LCD is addressed from the top. That made
 * every touch land on the vertically mirrored row. Invert Y, leave X alone;
 * the five-target calibration screen prints raw + mapped values so the axes
 * can be confirmed on the actual panel. */
#define TOUCH_SWAP_XY            0
#define TOUCH_INVERT_X           0
#define TOUCH_INVERT_Y           1
#define TOUCH_RAW_MAX_X          240          /* native full-scale of X reports */
#define TOUCH_RAW_MAX_Y          320          /* native full-scale of Y reports */

/* Gesture tuning */
#define GESTURE_TAP_MAX_MS       380
#define GESTURE_TAP_MOVE_PX      14
#define GESTURE_DOUBLE_MS        330
#define GESTURE_DOUBLE_PX        30
#define GESTURE_LONG_MS          620
#define GESTURE_LONG_MOVE_PX     18
#define GESTURE_DRAG_START_PX    12
#define GESTURE_SWIPE_MIN_PX     46
#define GESTURE_SWIPE_MAX_MS     600

/* ---- Animation / content ------------------------------------------------- */
#define FRAME_TARGET_MS          30           /* ~33 fps ceiling for the galaxy */
#define GALAXY_PARTICLE_COUNT    96
#define STAR_COUNT               250
#define PLANET_COUNT             8
#define EFFECT_PARTICLE_MAX      360
#define RIPPLE_MAX               8
#define SHOOTING_STAR_MAX        4
#define CREATED_STAR_MAX         20

/* ---- UI ------------------------------------------------------------------ */
#define HUD_BAR_H                48
#define UI_ACCENT_CYAN           0x2DFF       /* rgb565 */
#define UI_ACCENT_PURPLE         0x8B3F
#define UI_ACCENT_YELLOW         0xFDC6

/* ---- Optional hardware features ----------------------------------------- */
#define ENABLE_RGB_LED           1            /* WS2812 on GPIO42 */
#define SERIAL_DEBUG             1

#if ENABLE_RGB_LED
  #define RGB_LED_COUNT          1
#endif
