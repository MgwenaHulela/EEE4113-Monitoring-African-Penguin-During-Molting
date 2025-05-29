#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "RTClib.h"
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <SD.h>

// ─── Constants ──────────────────────────────────────────────────────────────
#define SEALEVELPRESSURE_HPA (1013.25)

// SD card
#define SD_CS_PIN 15  // SD card CS pin
File logFile;

// RFID
#define SS_PIN   14    // RFID SS (CS) pin
#define RST_PIN  26   // RFID reset pin

// Penguins
constexpr size_t NUM_PENGUINS = 2;
const char* penguins[NUM_PENGUINS][2] = {
  { "FAE53502", "Penguin A" },
  { "C376C001", "Penguin B" }
};

// ─── Objects ────────────────────────────────────────────────────────────────
Adafruit_BME280 bme;
RTC_DS3231 rtc;
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ─── Functions ──────────────────────────────────────────────────────────────
String getPenguinName(const String &uid) {
  for (size_t i = 0; i < NUM_PENGUINS; i++) {
    if (uid == penguins[i][0]) {
      return String(penguins[i][1]);
    }
  }
  return "Unknown";
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize I2C
  Wire.begin();

  // Initialize BME280
  if (!bme.begin(0x76)) {
    Serial.println("❌ BME280 not found!");
    while (1) delay(10);
  }

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("❌ RTC not found!");
    while (1) delay(10);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(2025, 4, 28, 4, 10, 0)); // Set a default time
  }

  // Start SPI for both devices
  SPI.begin(18, 19, 23, SS_PIN); // VSPI

  // Ensure SD card CS is high (not selected)
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  // Initialize RFID reader
  mfrc522.PCD_Init();
  delay(10);
  mfrc522.PCD_DumpVersionToSerial(); // Debug check for reader

  // Initialize SD card
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("❌ SD card initialization failed!");
    while (1) delay(10);
  }
  Serial.println("✅ SD card initialized.");

  // Create/Open JSON log file
  logFile = SD.open("/log.json", FILE_APPEND);
  if (!logFile) {
    Serial.println("❌ Failed to open log.json");
    while (1) delay(10);
  }
  logFile.close();

  Serial.println("✅ System ready. Tap a tag to log:");
}

void loop() {
  // Deselect SD before reading RFID
  digitalWrite(SD_CS_PIN, HIGH);

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    delay(50); // prevent hammering
    return;
  }

  // Build UID string (uppercase hex)
  String uidStr;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += '0';
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  Serial.print("📡 Detected UID: ");
  Serial.println(uidStr);

  // Get Penguin Name
  String penguinName = getPenguinName(uidStr);

  // Get Timestamp
  DateTime now = rtc.now();
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp),
    "%04d-%02d-%02d %02d:%02d:%02d",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second()
  );

  // Read Temp and Humidity
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();

  // Log to SD Card
  digitalWrite(SD_CS_PIN, LOW); // select SD
  logFile = SD.open("/log.json", FILE_APPEND);
  if (logFile) {
    logFile.print("{");
    logFile.print("\"timestamp\":\""); logFile.print(timestamp); logFile.print("\",");
    logFile.print("\"penguin\":\"");   logFile.print(penguinName); logFile.print("\",");
    logFile.print("\"uid\":\"");       logFile.print(uidStr);      logFile.print("\",");
    logFile.print("\"temperature\":"); logFile.print(temp, 1);     logFile.print(",");
    logFile.print("\"humidity\":");    logFile.print(hum, 1);
    logFile.println("}");

    logFile.close();

    // Serial feedback
    Serial.print("📝 Logged: ");
    Serial.print(timestamp);
    Serial.print(" | ");
    Serial.print(penguinName);
    Serial.print(" (");
    Serial.print(uidStr);
    Serial.print(") | Temp: ");
    Serial.print(temp, 1);
    Serial.print("°C | Hum: ");
    Serial.print(hum, 1);
    Serial.println("%");

    File checkFile = SD.open("/log.json");
    if (checkFile) {
      Serial.print("📁 log.json size: ");
      Serial.print(checkFile.size());
      Serial.println(" bytes");
      checkFile.close();
    }
  } else {
    Serial.println("❌ Failed to write to log.json");
  }

  // RFID Cleanup
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
