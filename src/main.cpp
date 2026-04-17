// ============================================================
// Directional Gyro — ESP32-S3 port
// Original: https://github.com/ElectronDon66/Directional-Gyro
//
// Hardware:
//   - ESP32-S3 Zero (or ESP32-S3 DevKit)
//   - Adafruit BNO085 / GY-BNO085 IMU (I2C, address 0x4A)
//     Strap AD0 to GND for 0x4A; leave floating for 0x4B
//   - GC9A01 1.28" round TFT (240x240, SPI)
//   - Optional: quadrature rotary encoder for heading offset
//
// Pin assignments:
//   TFT  MOSI=11  SCLK=12  CS=10  DC=4   RST=5
//   I2C  SDA=8    SCL=9
//   ENC  A=6      B=7
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <Adafruit_BNO08x.h>

// ============================================================
// LovyanGFX display configuration
// GC9A01 round 240x240 display on ESP32-S3 SPI2 (FSPI bus)
// ============================================================
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;

public:
  LGFX(void)
  {
    { // SPI bus
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;      // FSPI / SPI2 on ESP32-S3
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = true;           // no MISO needed
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 12;
      cfg.pin_mosi    = 11;
      cfg.pin_miso    = -1;
      cfg.pin_dc      =  4;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // Display panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs       = 10;
      cfg.pin_rst      =  5;
      cfg.pin_busy     = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 240;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;
      cfg.invert          = true;   // GC9A01 needs colour inversion
      cfg.rgb_order       = false;
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

// =====================
// Hardware Objects
// =====================
LGFX                tft;
lgfx::LGFX_Sprite   sprite(&tft);
Adafruit_BNO08x     bno = Adafruit_BNO08x();

// =====================
// I2C Pins
// =====================
#define I2C_SDA  8
#define I2C_SCL  9

// =====================
// Encoder Pins
// =====================
#define ENC_A  6
#define ENC_B  7

volatile long    encoderCount = 0;
volatile uint8_t lastEncoded  = 0;

// =====================
// Display
// =====================
#define SCREEN_SIZE 240
#define CENTER      120

// =====================
// Heading
// =====================
float currentHeading = 0;
float lastHeading    = -999;

// =====================
// Quadrature ISR — must live in IRAM on ESP32
// =====================
void IRAM_ATTR updateEncoder()
{
  uint8_t MSB = digitalRead(ENC_A);
  uint8_t LSB = digitalRead(ENC_B);

  uint8_t encoded = (MSB << 1) | LSB;
  uint8_t sum     = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderCount++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderCount--;

  lastEncoded = encoded;
}

// =====================
// Quaternion → Yaw
// =====================
float quaternionToYaw(float w, float x, float y, float z)
{
  float siny_cosp = 2.0f * (w * z + x * y);
  float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
  return atan2f(siny_cosp, cosy_cosp) * 180.0f / PI;
}

// =====================
// Draw Compass Rose
// =====================
void drawCompassRose(float heading)
{
  sprite.fillSprite(TFT_BLACK);

  // -------- Ticks --------
  for (int deg = 0; deg < 360; deg += 5)
  {
    float angle  = radians(deg + heading);
    int   rOuter = 110;
    int   rInner = (deg % 30 == 0) ? 88 : (deg % 10 == 0) ? 98 : 105;

    sprite.drawLine(
      CENTER + (int)(sinf(angle) * rInner), CENTER - (int)(cosf(angle) * rInner),
      CENTER + (int)(sinf(angle) * rOuter), CENTER - (int)(cosf(angle) * rOuter),
      TFT_WHITE);
  }

  // -------- 30° numbers (skip cardinals) --------
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextDatum(middle_center);

  for (int deg = 0; deg < 360; deg += 30)
  {
    if (deg == 0 || deg == 90 || deg == 180 || deg == 270) continue;
    float angle = radians(deg + heading);
    int   x = CENTER + (int)(sinf(angle) * 72);
    int   y = CENTER - (int)(cosf(angle) * 72);
    char  buf[4];
    snprintf(buf, sizeof(buf), "%d", deg / 10);
    sprite.setFont(&fonts::Font2);
    sprite.drawString(buf, x, y);
  }

  // -------- Cardinal letters --------
  const struct { char label; int deg; } cardinal[] = {
    {'N',   0}, {'E', 90}, {'S', 180}, {'W', 270}
  };
  sprite.setFont(&fonts::Font4);
  for (int i = 0; i < 4; i++)
  {
    float angle = radians(heading + cardinal[i].deg);
    int   x = CENTER + (int)(sinf(angle) * 70);
    int   y = CENTER - (int)(cosf(angle) * 70);
    char  txt[2] = {cardinal[i].label, '\0'};
    sprite.drawString(txt, x, y);
  }

  // -------- Aircraft symbol --------
  int cx = CENTER, cy = CENTER;
  sprite.fillTriangle(cx, cy - 58, cx - 10, cy - 22, cx + 10, cy - 22, TFT_YELLOW);
  sprite.fillRect(cx - 4,  cy - 22, 8,  55, TFT_YELLOW);   // fuselage
  sprite.fillRect(cx - 45, cy -  5, 90, 10, TFT_YELLOW);   // wings
  sprite.fillRect(cx - 12, cy + 26, 24,  6, TFT_YELLOW);   // tailplane
}

// =====================
// Setup
// =====================
void setup()
{
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 3000)) { delay(10); }
  delay(200);

  Serial.println("== Step 1: Serial OK ==");  Serial.flush();

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("== Step 2: I2C OK ==");  Serial.flush();

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncoded = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);
  Serial.println("== Step 3: Encoder OK ==");  Serial.flush();

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  Serial.println("== Step 4: TFT init OK ==");  Serial.flush();

  sprite.setColorDepth(16);
  if (!sprite.createSprite(SCREEN_SIZE, SCREEN_SIZE))
  {
    Serial.println("ERROR: sprite allocation failed (out of RAM)");
    Serial.flush();
  }
  Serial.println("== Step 5: Sprite OK ==");  Serial.flush();

  while (!bno.begin_I2C(0x4B))
  {
    Serial.println("BNO085 not detected — check wiring. Retrying in 2s...");
    Serial.flush();
    delay(2000);
  }
  Serial.println("== Step 6: BNO085 OK ==");  Serial.flush();

  bno.enableReport(SH2_GAME_ROTATION_VECTOR, 5000);
  Serial.println("== Setup complete. Running. ==");  Serial.flush();
}

// =====================
// Main Loop
// =====================
void loop()
{
  sh2_SensorValue_t sensorValue;

  if (bno.getSensorEvent(&sensorValue))
  {
    if (sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR)
    {
      float yaw = quaternionToYaw(
        sensorValue.un.gameRotationVector.real,
        sensorValue.un.gameRotationVector.i,
        sensorValue.un.gameRotationVector.j,
        sensorValue.un.gameRotationVector.k);

      float headingOffset = encoderCount * 0.5f;
      currentHeading = yaw + headingOffset;
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
}
