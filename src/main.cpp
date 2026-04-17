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
// Pin assignments (adjust in User_Setup.h for display pins):
//   TFT  MOSI=11  SCLK=12  CS=10  DC=4   RST=5
//   I2C  SDA=8    SCL=9
//   ENC  A=6      B=7
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Adafruit_BNO08x.h>

// =====================
// Hardware Objects
// =====================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
Adafruit_BNO08x bno = Adafruit_BNO08x();

// =====================
// I2C Pins
// =====================
#define I2C_SDA  8
#define I2C_SCL  9

// =====================
// Encoder Pins
// (All GPIO pins support interrupts on ESP32-S3)
// =====================
#define ENC_A  6
#define ENC_B  7

volatile long encoderCount = 0;
volatile uint8_t lastEncoded = 0;

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
// Quadrature ISR
// ESP32 ISRs must be placed in IRAM
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
  float yaw = atan2f(siny_cosp, cosy_cosp);
  return yaw * 180.0f / PI;
}

// =====================
// Draw Compass Rose
// =====================
void drawCompassRose(float heading)
{
  sprite.fillSprite(TFT_BLACK);

  // -------- Compass Card Ticks --------
  for (int deg = 0; deg < 360; deg += 5)
  {
    float angle = radians(deg + heading);

    int rOuter = 110;
    int rInner = 105;

    if (deg % 30 == 0) {
      rInner = 88;    // Long tick (30°)
    } else if (deg % 10 == 0) {
      rInner = 98;    // Medium tick (10°)
    }

    int x0 = CENTER + (int)(sinf(angle) * rInner);
    int y0 = CENTER - (int)(cosf(angle) * rInner);
    int x1 = CENTER + (int)(sinf(angle) * rOuter);
    int y1 = CENTER - (int)(cosf(angle) * rOuter);

    sprite.drawLine(x0, y0, x1, y1, TFT_WHITE);
  }

  // -------- 30° Numbers (Except N/E/S/W) --------
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.setTextDatum(MC_DATUM);

  for (int deg = 0; deg < 360; deg += 30)
  {
    if (deg == 0 || deg == 90 || deg == 180 || deg == 270)
      continue;

    float angle = radians(deg + heading);
    int x = CENTER + (int)(sinf(angle) * 72);
    int y = CENTER - (int)(cosf(angle) * 72);

    int displayNum = deg / 10;   // 30→3, 60→6, etc.
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", displayNum);
    sprite.drawString(buf, x, y, 2);
  }

  // -------- Cardinal Letters (N E S W) --------
  struct { char label; int deg; } cardinal[] = {
    {'N',   0},
    {'E',  90},
    {'S', 180},
    {'W', 270}
  };

  for (int i = 0; i < 4; i++)
  {
    float angle = radians(heading + cardinal[i].deg);
    int x = CENTER + (int)(sinf(angle) * 70);
    int y = CENTER - (int)(cosf(angle) * 70);

    char txt[2] = {cardinal[i].label, '\0'};
    sprite.drawString(txt, x, y, 4);
  }

  // -------- Aircraft Symbol --------
  int cx = CENTER;
  int cy = CENTER;

  // Nose (extended upward)
  sprite.fillTriangle(
    cx,      cy - 58,
    cx - 10, cy - 22,
    cx + 10, cy - 22,
    TFT_YELLOW
  );

  // Fuselage
  sprite.fillRect(cx - 4, cy - 22, 8, 55, TFT_YELLOW);

  // Wings
  sprite.fillRect(cx - 45, cy - 5, 90, 10, TFT_YELLOW);

  // Tailplane
  sprite.fillRect(cx - 12, cy + 26, 24, 6, TFT_YELLOW);
}

// =====================
// Setup
// =====================
void setup()
{
  Serial.begin(115200);

  // I2C with explicit pin assignment (required on ESP32)
  Wire.begin(I2C_SDA, I2C_SCL);

  // Encoder setup — all GPIO pins support interrupts on ESP32-S3
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncoded = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);

  // Display init
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  sprite.createSprite(SCREEN_SIZE, SCREEN_SIZE);
  sprite.setSwapBytes(true);

  // BNO085 init
  if (!bno.begin_I2C())
  {
    Serial.println("BNO085 not detected. Check wiring and I2C address (0x4A).");
    while (1) { delay(100); }
  }

  // Game Rotation Vector — gyro+accel only, no magnetometer
  bno.enableReport(SH2_GAME_ROTATION_VECTOR, 5000);   // 5000 µs = 200 Hz

  Serial.println("Directional Gyro running.");
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
      float q_w = sensorValue.un.gameRotationVector.real;
      float q_x = sensorValue.un.gameRotationVector.i;
      float q_y = sensorValue.un.gameRotationVector.j;
      float q_z = sensorValue.un.gameRotationVector.k;

      float yaw = quaternionToYaw(q_w, q_x, q_y, q_z);

      // Convert encoder counts to degrees (0.5° per detent — adjust scaling if needed)
      float headingOffset = encoderCount * 0.5f;

      currentHeading = yaw + headingOffset;

      // Normalize to 0–360
      while (currentHeading <   0) currentHeading += 360;
      while (currentHeading >= 360) currentHeading -= 360;

      // Only redraw if changed enough to matter
      if (fabsf(currentHeading - lastHeading) > 0.2f)
      {
        drawCompassRose(currentHeading);
        sprite.pushSprite(0, 0);
        lastHeading = currentHeading;
      }
    }
  }
}
