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

A standard bare quadrature encoder has **3 rotation pins** (GND, A, B) and **2 button pins** (SW, SW-GND).

| Encoder Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| GND (middle / C) | GND    | Common — encoder shorts A/B to this on each detent |
| A (CLK)     | GPIO 6        | Quadrature channel A |
| B (DT)      | GPIO 7        | Quadrature channel B |
| VCC / 3V3   | —             | **Leave unconnected** — not needed |
| SW (button) | —             | Not used in firmware |
| SW GND      | —             | Not used in firmware |

> **No external pull-up resistors needed.** The firmware configures GPIO 6 and GPIO 7 as
> `INPUT_PULLUP`, so the lines are held at 3.3 V internally. The encoder's common (middle) pin
> must be connected to GND — when the shaft turns, it briefly shorts A or B to GND,
> which the microcontroller detects as a falling edge.

> The middle pin is almost always the **GND / common**. On modules with PCB labels,
> look for `CLK` → A, `DT` → B, and the remaining signal pin (`–` or `C`) → GND.

```
Encoder viewed from shaft side:

    ┌────────────────┐
    │   shaft/knob   │
    └────────────────┘
         │    │    │
         A   GND   B
       GPIO6  GND  GPIO7
```

> The encoder sets a heading offset (0.5° per detent).  
> Turning clockwise increases the displayed heading; counter-clockwise decreases it.  
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
3V3     ──── GC9A01 VCC, GC9A01 BL, BNO085 VIN
GND     ──── GC9A01 GND, BNO085 GND, BNO085 AD0, Encoder GND (middle pin)
```

---

## Software

### PlatformIO setup

1. Open the project folder in VS Code with the PlatformIO extension installed.
2. PlatformIO will automatically download all library dependencies on first build:
   - [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — display driver (ESP32-S3 native)
   - [Adafruit BNO08x](https://github.com/adafruit/Adafruit_BNO08x) — IMU driver
   - [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) — I2C/SPI abstraction
3. Build and upload with the PlatformIO toolbar or `pio run -t upload`.

### Display pin configuration

Display pins are configured directly in the `LGFX` class at the top of `src/main.cpp` — no build flags or separate header needed. To change a pin, edit the `cfg.pin_*` lines in the constructor.

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
