# Directional Gyro — ESP32-S3

An aircraft-style directional gyro indicator running on an ESP32-S3.  
Displays a rotating compass rose on a 1.28" round TFT, driven by a BNO085 IMU using only gyro and accelerometer (no magnetometer — immune to magnetic interference).  
A rotary encoder lets you manually offset the displayed heading.

Based on the original Teensy 4.0 project by ElectronDon66:  
https://github.com/ElectronDon66/Directional-Gyro

---

## Bill of Materials

| # | Component | Notes |
|---|-----------|-------|
| 1 | ESP32-S3 Zero (or DevKit) | Waveshare ESP32-S3-Zero or equivalent |
| 2 | BNO085 IMU | Adafruit BNO085 or GY-BNO085 breakout |
| 3 | GC9A01 1.28" round TFT | 240×240 SPI display |
| 4 | Rotary encoder | Quadrature type with push button (optional) |
| 5 | Breadboard / proto board | |
| 6 | Jumper wires | |

---

## Wiring

### GC9A01 Round TFT Display (SPI)

| Display Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| VCC         | 3V3           | 3.3 V power |
| GND         | GND           | Ground |
| MOSI / SDA  | GPIO 11       | SPI data |
| SCK / SCL   | GPIO 12       | SPI clock |
| CS          | GPIO 10       | Chip select |
| DC / RS     | GPIO 4        | Data / Command select |
| RST         | GPIO 5        | Reset |
| BL          | 3V3           | Backlight — tie to 3.3 V or add PWM pin |

### BNO085 IMU (I2C)

| BNO085 Pin | ESP32-S3 GPIO | Notes |
|------------|---------------|-------|
| VIN / VCC  | 3V3           | 3.3 V power |
| GND        | GND           | Ground |
| SDA        | GPIO 8        | I2C data |
| SCL        | GPIO 9        | I2C clock |
| AD0 / ADDR | GND           | Selects I2C address 0x4A (leave floating for 0x4B) |
| INT        | —             | Not used |
| RST        | —             | Not used |

> **I2C address:** Ground the AD0 pin for address `0x4A` (default in firmware).  
> If your module uses `0x4B`, change `bno.begin_I2C()` to `bno.begin_I2C(0x4B)` in `src/main.cpp`.

### Rotary Encoder (Quadrature)

| Encoder Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| VCC         | 3V3           | 3.3 V power |
| GND         | GND           | Ground |
| A (CLK)     | GPIO 6        | Quadrature channel A |
| B (DT)      | GPIO 7        | Quadrature channel B |
| SW (button) | —             | Not used in firmware |

> The encoder sets a heading offset (0.5° per detent).  
> Turning it clockwise increases the displayed heading; counter-clockwise decreases it.  
> The scaling factor can be adjusted in `src/main.cpp` — look for `encoderCount * 0.5f`.

---

## Wiring Diagram (text)

```
ESP32-S3
─────────────────────────────────────────────
GPIO 4  ──── GC9A01 DC
GPIO 5  ──── GC9A01 RST
GPIO 6  ──── Encoder A
GPIO 7  ──── Encoder B
GPIO 8  ──── BNO085 SDA
GPIO 9  ──── BNO085 SCL
GPIO 10 ──── GC9A01 CS
GPIO 11 ──── GC9A01 MOSI
GPIO 12 ──── GC9A01 SCK
3V3     ──── GC9A01 VCC, GC9A01 BL, BNO085 VIN, Encoder VCC
GND     ──── GC9A01 GND, BNO085 GND, BNO085 AD0, Encoder GND
```

---

## Software

### PlatformIO setup

1. Open the project folder in VS Code with the PlatformIO extension installed.
2. PlatformIO will automatically download all library dependencies on first build:
   - [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — display driver
   - [Adafruit BNO08x](https://github.com/adafruit/Adafruit_BNO08x) — IMU driver
   - [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) — I2C/SPI abstraction
3. Build and upload with the PlatformIO toolbar or `pio run -t upload`.

### TFT_eSPI pin configuration

Display pins are configured via `-D` build flags in `platformio.ini` (no manual library editing needed).  
To change a pin, update the corresponding `-DTFT_*` line in `platformio.ini`.

### Adjusting the heading offset scaling

In `src/main.cpp`, the line:

```cpp
float headingOffset = encoderCount * 0.5f;
```

maps each encoder detent to 0.5°. Change the multiplier if your encoder has a different detent resolution.

---

## How it works

1. The BNO085 runs in **Game Rotation Vector** mode at 200 Hz — fusing gyro and accelerometer only (no compass).
2. The quaternion output is converted to a yaw angle (heading).
3. The rotary encoder adds an offset so you can synchronize the indicator to a known heading at startup.
4. A 240×240 sprite is rendered each time the heading changes by more than 0.2° and pushed to the display.
5. The aircraft symbol is fixed at the center; the compass card rotates behind it.

---

## License

Ported from the original work by ElectronDon66, shared for educational and personal use.
