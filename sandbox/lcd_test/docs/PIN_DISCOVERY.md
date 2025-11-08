# JC2432W328C Board - Pin Discovery Guide

Quick reference for discovering the actual pin configuration of your JC2432W328C board.

## Method 1: Visual Inspection

Look at the board silkscreen for labeled pins:
- TFT pins: MOSI, MISO, SCK, CS, DC, RST, BL
- Touch pins: T_CS, T_IRQ, T_DO, T_DIN, T_CLK
- Power pins: VCC, GND, VBAT
- Other: EN, BOOT, various GPIO

## Method 2: Continuity Testing

Use a multimeter in continuity mode:

1. **Find ESP32 chip** (square IC, usually largest on board)
2. **Locate display connector** (ribbon cable or direct solder)
3. **Trace pins** from ESP32 to display connector

### Common ESP32 Pins to Check
```
GPIO 2, 4, 5, 12-15, 18, 19, 21, 22, 23, 25-27, 32-33
```

## Method 3: Trial and Error with TFT_eSPI

Create `User_Setup.h` in TFT_eSPI library folder:

```cpp
// Try ST7789 first (common for 2.8" displays)
#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Test these common pin combinations:
// Set 1 (VSPI pins)
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4

// Set 2 (HSPI pins)
// #define TFT_MISO 12
// #define TFT_MOSI 13
// #define TFT_SCLK 14
// #define TFT_CS   5
// #define TFT_DC   4
// #define TFT_RST  2
```

## Method 4: Serial Pin Scanner

Add this to `main.cpp` to scan for I2C/SPI activity:

```cpp
void scanPins() {
  Serial.println("\n=== GPIO Pin Scanner ===");
  
  // Test common SPI pins
  int spi_pins[] = {2, 4, 5, 12, 13, 14, 15, 18, 19, 23};
  
  for (int i = 0; i < 10; i++) {
    pinMode(spi_pins[i], INPUT);
    int value = digitalRead(spi_pins[i]);
    Serial.printf("GPIO %d: %s\n", spi_pins[i], value ? "HIGH" : "LOW");
  }
}
```

## Method 5: Check Manufacturer Documentation

Search for:
- `JC2432W328C schematic`
- `JC2432W328C pinout`
- `JC2432W328C datasheet`

## Common Display Controllers

### ST7789 (Most Likely)
- 240x320 resolution
- SPI interface
- 16-bit color

### ILI9341
- 240x320 resolution
- SPI interface
- 16-bit color

### ST7735
- 128x160 or 160x128
- SPI interface
- 16-bit color

## Battery Voltage Sensing

To find battery voltage pin:

```cpp
// Test all ADC-capable pins
int adc_pins[] = {32, 33, 34, 35, 36, 39};

void scanBatteryPin() {
  Serial.println("\n=== ADC Pin Scanner (Battery Voltage) ===");
  
  for (int i = 0; i < 6; i++) {
    pinMode(adc_pins[i], INPUT);
    int raw = analogRead(adc_pins[i]);
    float voltage = (raw / 4095.0) * 3.3 * 2;  // Assume voltage divider
    
    Serial.printf("GPIO %d: Raw=%d, Voltage=%.2fV\n", 
                  adc_pins[i], raw, voltage);
  }
}
```

Expected battery voltage: 3.0-4.2V (for Li-ion/LiPo)

## Quick Test Program

Update `main.cpp` with discovery code:

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== JC2432W328C Pin Discovery ===\n");
  
  // Scan all GPIO states
  for (int pin = 0; pin <= 39; pin++) {
    if (pin == 1 || pin == 3) continue;  // Skip TX/RX
    
    pinMode(pin, INPUT);
    delay(1);
    int value = digitalRead(pin);
    Serial.printf("GPIO %2d: %s\n", pin, value ? "HIGH" : "LOW");
  }
}

void loop() {
  delay(5000);
}
```

## Expected Results

Once you identify the pins, update `platformio.ini`:

```ini
build_flags = 
    ; Display pins (example - replace with actual)
    -DUSER_SETUP_LOADED=1
    -DST7789_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_MISO=19
    -DTFT_MOSI=23
    -DTFT_SCLK=18
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=4
    -DTFT_BL=27
```

## Next Steps

1. Run basic test to verify board communication
2. Use pin scanner to identify connected pins
3. Test display with TFT_eSPI library
4. Document findings in README.md
5. Create working display test

## Need Help?

If you find the actual pinout, please update:
- `lcd_test/README.md` with correct pins
- `lcd_test/platformio.ini` with build flags
- `lcd_test/docs/PINOUT.md` (create this file)

