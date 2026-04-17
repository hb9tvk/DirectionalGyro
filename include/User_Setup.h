// ============================================================
// TFT_eSPI User Setup for GC9A01 round display on ESP32-S3
// ============================================================
// This file is auto-selected by the -DUSER_SETUP_LOADED build flag.
// Adjust the pin numbers below to match your wiring.
// ============================================================

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ---- SPI Pins (uses ESP32-S3 SPI2/FSPI hardware bus) ----
#define TFT_MOSI  11   // SPI Data Out
#define TFT_SCLK  12   // SPI Clock
#define TFT_CS    10   // Chip Select
#define TFT_DC     4   // Data/Command
#define TFT_RST    5   // Reset  (set to -1 if tied to 3.3V)
// TFT_BL not defined — backlight pin not used (tie to 3.3V)

// ---- Fonts to include ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

// ---- SPI speed ----
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
