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
| AD0 / ADDR | —             | Leave floating — selects I2C address 0x4B |
| INT        | —             | Not used |
| RST        | —             | Not used |

> **I2C address:** The firmware uses address `0x4B` (AD0 left unconnected / floating).  
> If your module has AD0 grounded it uses `0x4A` — change `bno.begin_I2C(0x4B)` to `bno.begin_I2C(0x4A)` in `src/main.cpp`.

### Rotary Encoder (Quadrature)

A standard bare quadrature encoder has **3 rotation pins** (GND, A, B) and **2 button pins** (SW, SW-GND).

| Encoder Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| GND (middle / C) | GND    | Common — encoder shorts A/B to this on each detent |
| A (CLK)     | GPIO 6        | Quadrature channel A |
| B (DT)      | GPIO 7        | Quadrature channel B |
| VCC / 3V3   | —             | **Leave unconnected** — not needed |
| SW (button) | GPIO 3        | Push button — single click = sleep, double click = default heading |
| SW GND      | GND           | Button common |

> **No external pull-up resistors needed.** The firmware configures GPIO 3, 6 and 7 as
> `INPUT_PULLUP`, so all lines are held at 3.3 V internally. The encoder's common (middle) pin
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

Button pins (separate pair):
    SW ──── GPIO 3
    SW-GND ──── GND
```

> The encoder sets a heading offset (0.5° per detent).  
> Turning clockwise increases the displayed heading; counter-clockwise decreases it.  
> The scaling factor can be adjusted in `src/main.cpp` — look for `HEADING_SCALE`.

---

## Wiring Diagram (text)

```
ESP32-S3
─────────────────────────────────────────────
GPIO 3  ──── Encoder SW (button)
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
GND     ──── GC9A01 GND, BNO085 GND, Encoder GND (middle pin), Encoder SW-GND
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

In `src/main.cpp`, change the `HEADING_SCALE` define:

```cpp
#define HEADING_SCALE  0.5f   // degrees per encoder detent
```

The default of 0.5° per detent suits most 20-detent encoders. Increase it for coarser adjustment.

---

## How it works

1. The BNO085 runs in **Game Rotation Vector** mode at 200 Hz — fusing gyro and accelerometer only (no compass).
2. The quaternion is converted to yaw. The sign is inverted to match aviation convention (clockwise turn = increasing heading).
3. The rotary encoder adds an offset to synchronize the indicator to a known heading.
4. A 240×240 sprite is rendered each time the heading changes by more than 0.2° and pushed to the display in one DMA transfer.
5. The aircraft symbol is fixed at the centre; the compass card rotates behind it so the current heading is always at the top.

## Power and button behaviour

| Action | Effect |
|--------|--------|
| **Single click** | Saves current heading to flash, shows "OFF", enters deep sleep |
| **Deep sleep** | All peripherals off; wakes instantly when button is pressed again |
| **Double click** | Loads the `DEFAULT_HEADING` value defined in `src/main.cpp` |
| **Power on / wake** | Restores last saved heading automatically |
| **First boot** | Uses `DEFAULT_HEADING` (default: 0° / North) |

WiFi and Bluetooth are disabled at startup to reduce current consumption.

### Changing the default heading

Edit the `DEFAULT_HEADING` define near the top of `src/main.cpp`:

```cpp
#define DEFAULT_HEADING  270.0f   // e.g. West
```

---

## License

Ported from the original work by ElectronDon66, shared for educational and personal use.
