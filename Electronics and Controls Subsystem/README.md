# Penguin Monitoring System - Electronics and Controls Subsystem

### Overview

This subsystem implements the core electronics and control logic for the African Penguin Monitoring System. It features an ESP32-based solution that integrates weight measurement, RFID identification, environmental sensing, and wireless communication to provide comprehensive monitoring capabilities.

### Key Features

* Dual-load cell weight measurement with Kalman filtering for accurate readings

* RFID animal identification with sex mapping

* Environmental monitoring (temperature/humidity)

* Secure data logging to SD card

* Wireless communication via ESP-NOW to ESP32-CAM module

* Real-time monitoring through serial interface

* Calibration interface for precision tuning

* Automated image capture trigger based on weight threshold

* Robust error handling for RFID read failures, including random RFID generation for data logging

### Hardware Components

| Component | Function | Connection | 
 | ----- | ----- | ----- | 
| ESP32 | Main microcontroller | N/A | 
| HX711 (x2) | Load cell amplifiers | DOUT1: GPIO32, DOUT2: GPIO26, SCK: GPIO14 | 
| MFRC522 | RFID reader | CS: GPIO15, RST: GPIO27 | 
| DS3231 | Real-time clock | I²C (SDA: GPIO21, SCL: GPIO22) | 
| AHT20 | Temp/humidity sensor | I²C (SDA: GPIO21, SCL: GPIO22) | 
| SD Card Module | Data logging | CS: GPIO5 | 
| ESP32-CAM | Image capture | ESP-NOW communication | 

### Pin Configuration

```cpp
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define HX711_DOUT1_PIN 32
#define HX711_DOUT2_PIN 26
#define HX711_SCK_PIN 14
#define SD_CS_PIN 5
#define RFID_CS_PIN 15
#define RFID_RST_PIN 27
#define SPI_SCK_PIN 18
#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23
#define DETECT_LED_PIN 2
