Penguin Monitoring System - Electronics and Controls Subsystem
Overview
This subsystem implements the core electronics and control logic for the African Penguin Monitoring System. It features an ESP32-based solution that integrates weight measurement, RFID identification, environmental sensing, and wireless communication to provide comprehensive monitoring capabilities.

Key Features
Dual-load cell weight measurement with Kalman filtering for accurate readings

RFID animal identification with sex mapping

Environmental monitoring (temperature/humidity)

Secure data logging to SD card

Wireless communication via ESP-NOW to ESP32-CAM module

Real-time monitoring through serial interface

Calibration interface for precision tuning

Automated image capture trigger based on weight threshold

Robust error handling for RFID read failures, including random RFID generation for data logging

Hardware Components
Component

Function

Connection

ESP32

Main microcontroller

N/A

HX711 (x2)

Load cell amplifiers

DOUT1: GPIO32, DOUT2: GPIO26, SCK: GPIO14

MFRC522

RFID reader

CS: GPIO15, RST: GPIO27

DS3231

Real-time clock

I²C (SDA: GPIO21, SCL: GPIO22)

AHT20

Temperature/humidity sensor

I²C (SDA: GPIO21, SCL: GPIO22)

SD Card Module

Data logging

CS: GPIO5

ESP32-CAM

Image capture

ESP-NOW communication

Pin Configuration
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

Software Architecture
graph TD
    A[Main Controller - ESP32] --> B[Weight Measurement (HX711, Kalman)]
    A --> C[RFID Detection (MFRC522)]
    A --> D[Environmental Sensing (AHT20)]
    A --> E[Data Logging (SD Card)]
    A --> F[ESP-NOW Communication]
    B --> F
    C --> F
    D --> F
    F --> G[ESP32-CAM Module]
    G --> H[Image Capture]
    G --> I[Server Communication (HTTP POST)]
    H --> I

Installation & Setup
Hardware Assembly

Connect components according to pin configuration

Ensure proper power supply (5V/3.3V as required)

Mount load cells securely in platform

Software Dependencies

Install required Arduino libraries via the Arduino IDE Library Manager:

HX711 by Bogde

MFRC522 by Udo Klein

RTClib by Adafruit

AHT20 (ensure you have a compatible library, e.g., by Adafruit or similar)

ArduinoJson by Benoit Blanchon (for ESP32-CAM side)

arduino-base64 (for ESP32-CAM side)

The esp_now.h and WiFi.h libraries are part of the ESP32 Arduino core and should be available by default after installing the ESP32 board definitions.

Initial Configuration

Update calibration factors in the Main Controller code (after running calibration procedure):

const float CALIBRATION_FACTOR_SCALE1 = -109.2773f;
const float CALIBRATION_FACTOR_SCALE2 = -117.4789f;

Set the ESP32-CAM's MAC address in the Main Controller code:

uint8_t receiverMac[] = {0xA0, 0xA3, 0xB3, 0x2B, 0xCB, 0x40}; // Replace with your ESP32-CAM's actual MAC address

Configure RFID-sex mapping in the Main Controller code:

rfidSexMap["2A0D4602"] = "Female";
rfidSexMap["FAE53502"] = "Male";
// Add more RFID UIDs and their corresponding sex as needed

Update Wi-Fi credentials and Flask server URL in the ESP32-CAM code.

Calibration Procedure
Upload the Main Controller firmware to your ESP32.

Open the Serial Monitor in the Arduino IDE (set to 115200 baud).

Send calibration commands:

calibrate1 for the first load cell

calibrate2 for the second load cell

Follow the on-screen instructions in the Serial Monitor:

Remove all weight from the scale for tare.

Place known weights (e.g., 750g, 2350g, 4450g) when prompted.

The system will calculate and display a calibration factor for each weight.

Crucially: Manually average the calculated factors for each scale.

Update the CALIBRATION_FACTOR_SCALE1 and CALIBRATION_FACTOR_SCALE2 constants at the top of your Main Controller sketch with these new averaged values.

Re-upload the sketch to the ESP32 for the changes to take effect.

Operation Workflow
Penguin Detection

The RFID reader continuously scans for tagged penguins.

Upon successful detection, the system identifies the penguin's UID and sex.

Error Handling: If an RFID card is detected but fails to read its serial, data is still transmitted using a randomly generated RFID UID.

Weight Measurement

Dual load cells capture raw weight data.

A Kalman filter processes these readings for enhanced accuracy and stability.

A 1-second settling period is enforced after an RFID trigger to ensure a stable weight measurement.

Environmental Sensing

The AHT20 sensor captures real-time temperature and humidity data.

Data is validated before transmission to handle potential sensor read errors.

Weight-Triggered Transmission (New Feature)

The system continuously monitors the filtered weight.

If the weight exceeds WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG (default 0.5 kg), and it's a new event, data is transmitted to the ESP32-CAM using a randomly generated RFID UID.

A 2-second settling period is applied after this weight trigger to allow the weight to stabilize.

The system resets this trigger when the weight drops below RESET_WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG (default 0.1 kg) to allow for subsequent detections.

Data Processing

A precise timestamp is generated using the RTC module.

All collected data (timestamp, RFID/random ID, sex, weight, temperature, humidity) is formatted into a JSON payload, for example:

{
  "timestamp": "2025-05-29T14:30:00",
  "rfid": "2A0D4602",
  "sex": "Female",
  "weight": 2.26,
  "temperature": 22.5,
  "humidity": 65.0
}

Wireless Transmission

The JSON data payload is sent wirelessly via ESP-NOW to the paired ESP32-CAM module.

The system includes success/failure handling for ESP-NOW packet delivery.

Data Logging

All measurements and associated metadata are securely stored on an SD card for local backup.

Each session's data is logged to a unique filename: /LOG_YYYY-MM-DD_HH-MM-SS.txt.

Troubleshooting
Issue

Solution

RFID not detecting / "failed to read serial"

Check antenna connection, ensure RFID_RST_PIN is correct, try resetRfidModule() (automatic in code), verify card placement. If persistent, data is sent with random RFID.

Inaccurate weight readings

Recalibrate load cells (calibrate1/calibrate2), ensure load cells are mounted securely and not touching the platform's edges, check wiring to HX711.

SD card not logging data

Verify card format (FAT32), check SD_CS_PIN connection, ensure SD card is properly inserted and functional.

ESP-NOW transmission failures

Confirm receiverMac address matches ESP32-CAM, ensure both ESP32s are within range, check ESP-NOW initialization on both devices.

Temperature/Humidity sensor read errors

Verify I²C connections (SDA/SCL), ensure AHT20 is properly powered, check for loose wires.

No image capture on ESP32-CAM

Check ESP32-CAM's Wi-Fi connection to the server, ensure camera is initialized correctly on CAM side, verify flash LED operation.

Configuration Options
Weight Thresholds:

const float NO_WEIGHT_THRESHOLD_KG = 0.05f; // Weight below this is considered zero
const float WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG = 0.5f; // Weight to trigger data send
const float RESET_WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG = 0.1f; // Weight to reset trigger

Kalman Filter Tuning:

const double KALMAN_Q = 0.01;  // Process noise covariance (higher = more responsive, more noisy)
const double KALMAN_R = 0.01;  // Measurement noise covariance (higher = more smoothing, slower reaction)

RFID Settings:

#define RFID_ANTENNA_GAIN MFRC522::RxGain_max // Max sensitivity
#define RFID_READ_TIMEOUT 200                 // Milliseconds between read attempts
const unsigned long RFID_COOLDOWN_MS = 500;   // Cooldown after successful read
const unsigned long RFID_FAIL_RESET_INTERVAL_MS = 5000; // Reset RFID module after 5s of failure

Data Settling Time:

const unsigned long KALMAN_SETTLE_TIME_MS = 1000; // Time to settle after RFID trigger
// Note: Weight-triggered send has its own 2-second hardcoded delay for settling.

Data Flow Diagram
sequenceDiagram
    participant Penguin
    participant Scale
    participant MainMCU_ESP32 as ESP32 (Main Controller)
    participant ESP32_CAM as ESP32-CAM Module
    participant FlaskServer as Server

    Penguin->>Scale: Steps on platform
    Scale->>MainMCU_ESP32: Raw Weight Data (HX711)
    MainMCU_ESP32->>MainMCU_ESP32: Kalman Filtering
    MainMCU_ESP32->>RFID: Scan for Tag
    RFID-->>MainMCU_ESP32: Penguin UID (or fail)

    alt RFID Detected & Read
        MainMCU_ESP32->>MainMCU_ESP32: RFID Data Processed
        MainMCU_ESP32->>MainMCU_ESP32: Wait for Kalman Settle (1s)
    else RFID Failed Read
        MainMCU_ESP32->>MainMCU_ESP32: Generate Random RFID
    else Weight Threshold Exceeded
        MainMCU_ESP32->>MainMCU_ESP32: Generate Random RFID
        MainMCU_ESP32->>MainMCU_ESP32: Wait for Kalman Settle (2s)
    end

    MainMCU_ESP32->>AHT20: Read Temp/Humidity
    AHT20-->>MainMCU_ESP32: Environmental Data
    MainMCU_ESP32->>RTC: Get Timestamp
    RTC-->>MainMCU_ESP32: Timestamp

    MainMCU_ESP32->>SD: Log Data (JSON)
    MainMCU_ESP32->>ESP32_CAM: Send Data (JSON via ESP-NOW)

    ESP32_CAM->>ESP32_CAM: Receive ESP-NOW Data
    ESP32_CAM->>ESP32_CAM: Connect to Wi-Fi
    ESP32_CAM->>Camera: Capture Image
    Camera-->>ESP32_CAM: Image Data
    ESP32_CAM->>FlaskServer: POST Data + Base64 Image (HTTP)
    FlaskServer-->>ESP32_CAM: Acknowledgment
    ESP32_CAM->>ESP32_CAM: Disconnect from Wi-Fi

Maintenance
Regular Calibration: Periodically check and recalibrate load cells using the serial calibration procedure to maintain weight measurement accuracy.

RFID Antenna Cleaning: Ensure the RFID reader antenna surface is clean and free from obstructions for optimal tag detection.

RTC Battery Backup: Verify the RTC module's battery backup to ensure accurate timekeeping even during power loss.

SD Card Management: Format the SD card monthly or as needed to prevent data corruption and ensure sufficient storage space.

Firmware Updates: Keep the ESP32 Main Controller and ESP32-CAM firmware updated to benefit from bug fixes, performance improvements, and new features.

Contributors
Zwivhuya Ndou (System Architecture, Firmware Development)

Talifhani Nemanggani (Hardware Integration)

Innocent Makhubela (Testing & Validation)