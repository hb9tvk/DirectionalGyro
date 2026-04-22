// ============================================================
// Directional Gyro — ESP32-S3 port
// Original: https://github.com/ElectronDon66/Directional-Gyro
//
// Hardware:
//   - ESP32-S3 Zero (or ESP32-S3 DevKit)
//   - Adafruit BNO085 / GY-BNO085 IMU (I2C, address 0x4B)
//   - GC9A01 1.28" round TFT (240x240, SPI)
//   - Quadrature rotary encoder with push button
//
// Pin assignments:
//   TFT  MOSI=11  SCLK=12  CS=10  DC=4   RST=5
//   I2C  SDA=8    SCL=9
//   ENC  A=6      B=7      BTN=3  (BTN→GND when pressed, pull-up internal)
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LovyanGFX.hpp>
#include <Adafruit_BNO08x.h>

// ============================================================
// User configuration
// ============================================================
#define DEFAULT_HEADING   73.0f   // Degrees shown on first boot / double-click (0 = North)
#define HEADING_SCALE     0.5f   // Degrees per encoder detent

// ============================================================
// Pin assignments
// ============================================================
#define I2C_SDA   8
#define I2C_SCL   9
#define ENC_A     6
#define ENC_B     7
#define BTN_PIN   3   // Encoder push-button SW pin — connect to GPIO 3; SW-GND to GND

// ============================================================
// Button timing
// ============================================================
#define DEBOUNCE_MS      30
#define DOUBLE_CLICK_MS  350

// ============================================================
// NVS (persistent storage) keys
// ============================================================
#define NVS_NS       "gyro"
#define NVS_HEADING  "heading"
#define NVS_VALID    "valid"

// ============================================================
// LovyanGFX — GC9A01 on ESP32-S3 SPI2
// ============================================================
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX(void)
  {
    {
      auto cfg        = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = true;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 12;
      cfg.pin_mosi    = 11;
      cfg.pin_miso    = -1;
      cfg.pin_dc      =  4;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg            = _panel_instance.config();
      cfg.pin_cs          = 10;
      cfg.pin_rst         =  5;
      cfg.pin_busy        = -1;
      cfg.panel_width     = 240;
      cfg.panel_height    = 240;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;
      cfg.invert          = true;
      cfg.rgb_order       = false;
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

// ============================================================
// Hardware objects
// ============================================================
LGFX              tft;
lgfx::LGFX_Sprite sprite(&tft);
Adafruit_BNO08x   bno = Adafruit_BNO08x();

// ============================================================
// Encoder state
// ============================================================
volatile long    encoderCount = 0;
volatile uint8_t lastEncoded  = 0;

// ============================================================
// Display
// ============================================================
#define SCREEN_SIZE 240
#define CENTER      120

// ============================================================
// Heading
// ============================================================
float currentHeading = 0;
float lastHeading    = -999;

// ============================================================
// Encoder ISR — must live in IRAM on ESP32
// ============================================================
void IRAM_ATTR updateEncoder()
{
  uint8_t MSB     = digitalRead(ENC_A);
  uint8_t LSB     = digitalRead(ENC_B);
  uint8_t encoded = (MSB << 1) | LSB;
  uint8_t sum     = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCount++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderCount--;

  lastEncoded = encoded;
}

// ============================================================
// Quaternion → Yaw (degrees)
// ============================================================
float quaternionToYaw(float w, float x, float y, float z)
{
  float siny_cosp = 2.0f * (w * z + x * y);
  float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
  return atan2f(siny_cosp, cosy_cosp) * 180.0f / PI;
}

// ============================================================
// Button click detector
// Returns: 0 = none, 1 = single click, 2 = double click
// Simple 4-state machine; call every loop iteration.
// ============================================================
int readButton()
{
  static uint8_t  state      = 0;
  static uint32_t t          = 0;
  static bool     wasPressed = false;

  bool     pressed = (digitalRead(BTN_PIN) == LOW);
  uint32_t now     = millis();

  switch (state) {
    case 0:  // idle — waiting for first press
      if (pressed && !wasPressed) { t = now; state = 1; }
      break;

    case 1:  // button held — wait for release (debounce)
      if (!pressed && (now - t) > DEBOUNCE_MS) { t = now; state = 2; }
      break;

    case 2:  // first release — wait for possible second press
      if (pressed && (now - t) < DOUBLE_CLICK_MS) {
        state = 3;                        // second press within window
      } else if ((now - t) >= DOUBLE_CLICK_MS) {
        state = 0; wasPressed = pressed;
        return 1;                         // single click
      }
      break;

    case 3:  // second press — wait for release
      if (!pressed) { state = 0; wasPressed = false; return 2; }  // double click
      break;
  }

  wasPressed = pressed;
  return 0;
}

// ============================================================
// Heading persistence (NVS)
// ============================================================
void saveHeading(float heading)
{
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putFloat(NVS_HEADING, heading);
  prefs.putBool(NVS_VALID, true);
  prefs.end();
}

// Returns last saved heading, or DEFAULT_HEADING if none stored yet.
float loadHeading()
{
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  bool  valid   = prefs.getBool(NVS_VALID, false);
  float heading = prefs.getFloat(NVS_HEADING, DEFAULT_HEADING);
  prefs.end();
  return valid ? heading : DEFAULT_HEADING;
}

// Apply a heading by setting the encoder offset so that
// currentHeading ≈ target when yaw ≈ 0 (fresh sensor start).
void applyHeading(float target)
{
  noInterrupts();
  encoderCount = (long)roundf(target / HEADING_SCALE);
  interrupts();
  lastHeading = -999;   // force immediate redraw
}

// ============================================================
// Power off — save heading then enter deep sleep.
// Device wakes when BTN_PIN is pulled LOW (button pressed).
// ============================================================
void saveAndSleep()
{
  saveHeading(currentHeading);
  Serial.printf("Heading %.1f saved. Entering deep sleep.\n", currentHeading);
  Serial.flush();

  // Brief "OFF" on display then blank it
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::Font4);
  tft.fillScreen(TFT_BLACK);
  tft.drawString("OFF", CENTER, CENTER);
  delay(600);
  tft.fillScreen(TFT_BLACK);

  // GPIO 3 is an RTC GPIO on ESP32-S3 — safe for ext0 wake
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, 0);  // wake on LOW
  esp_deep_sleep_start();
}

// ============================================================
// Draw compass rose into sprite (does NOT push to screen)
// ============================================================
void drawCompassRose(float heading)
{
  sprite.fillSprite(TFT_BLACK);

  // Ticks
  for (int deg = 0; deg < 360; deg += 5)
  {
    float angle  = radians(deg - heading);
    int   rOuter = 110;
    int   rInner = (deg % 30 == 0) ? 88 : (deg % 10 == 0) ? 98 : 105;
    sprite.drawLine(
      CENTER + (int)(sinf(angle) * rInner), CENTER - (int)(cosf(angle) * rInner),
      CENTER + (int)(sinf(angle) * rOuter), CENTER - (int)(cosf(angle) * rOuter),
      TFT_WHITE);
  }

  // 30° numbers (skip cardinal positions)
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextDatum(middle_center);
  sprite.setFont(&fonts::Font2);

  for (int deg = 0; deg < 360; deg += 30)
  {
    if (deg == 0 || deg == 90 || deg == 180 || deg == 270) continue;
    float angle = radians(deg - heading);
    char  buf[4];
    snprintf(buf, sizeof(buf), "%d", deg / 10);
    sprite.drawString(buf,
      CENTER + (int)(sinf(angle) * 72),
      CENTER - (int)(cosf(angle) * 72));
  }

  // Cardinal letters
  const struct { char label; int deg; } cardinal[] = {
    {'N', 0}, {'E', 90}, {'S', 180}, {'W', 270}
  };
  sprite.setFont(&fonts::Font4);
  for (int i = 0; i < 4; i++)
  {
    float angle = radians(cardinal[i].deg - heading);
    char  txt[2] = {cardinal[i].label, '\0'};
    sprite.drawString(txt,
      CENTER + (int)(sinf(angle) * 70),
      CENTER - (int)(cosf(angle) * 70));
  }

  // Aircraft symbol (fixed at centre)
  int cx = CENTER, cy = CENTER;
  sprite.fillTriangle(cx, cy - 58, cx - 10, cy - 22, cx + 10, cy - 22, TFT_YELLOW);
  sprite.fillRect(cx - 4,  cy - 22,  8, 55, TFT_YELLOW);   // fuselage
  sprite.fillRect(cx - 45, cy -  5, 90, 10, TFT_YELLOW);   // wings
  sprite.fillRect(cx - 12, cy + 26, 24,  6, TFT_YELLOW);   // tailplane
}

// ============================================================
// Setup
// ============================================================
void setup()
{
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 3000)) { delay(10); }

  // Power saving — disable RF hardware (WiFi + BT share the same RF block)
  WiFi.mode(WIFI_OFF);
  btStop();

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncoded = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);

  // Button — internal pull-up, no external resistor needed
  pinMode(BTN_PIN, INPUT_PULLUP);
  // Wait for button release in case we just woke from deep sleep via this pin
  delay(50);
  while (digitalRead(BTN_PIN) == LOW) { delay(10); }

  // Restore last heading (or DEFAULT_HEADING on first boot)
  float restored = loadHeading();
  applyHeading(restored);
  Serial.printf("Heading restored: %.1f deg\n", restored);

  // Display
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Sprite
  sprite.setColorDepth(16);
  sprite.createSprite(SCREEN_SIZE, SCREEN_SIZE);

  // BNO085
  while (!bno.begin_I2C(0x4B))
  {
    Serial.println("BNO085 not detected — retrying...");
    delay(2000);
  }
  bno.enableReport(SH2_GAME_ROTATION_VECTOR, 5000);   // 200 Hz
  Serial.println("Ready.");
}

// ============================================================
// Main loop
// ============================================================
void loop()
{
  // --- Button events ---
  switch (readButton()) {
    case 1:  // single click → save heading and sleep
      saveAndSleep();
      break;
    case 2:  // double click → load default heading
      applyHeading(DEFAULT_HEADING);
      Serial.printf("Default heading loaded: %.1f deg\n", DEFAULT_HEADING);
      break;
  }

  // --- IMU ---
  sh2_SensorValue_t sensorValue;
  if (bno.getSensorEvent(&sensorValue) &&
      sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR)
  {
    float yaw = quaternionToYaw(
      sensorValue.un.gameRotationVector.real,
      sensorValue.un.gameRotationVector.i,
      sensorValue.un.gameRotationVector.j,
      sensorValue.un.gameRotationVector.k);

    currentHeading = -yaw + encoderCount * HEADING_SCALE;
    while (currentHeading <   0) currentHeading += 360;
    while (currentHeading >= 360) currentHeading -= 360;

    if (fabsf(currentHeading - lastHeading) > 0.2f)
    {
      drawCompassRose(currentHeading);
      sprite.pushSprite(0, 0);
      lastHeading = currentHeading;
    }
  }
}
