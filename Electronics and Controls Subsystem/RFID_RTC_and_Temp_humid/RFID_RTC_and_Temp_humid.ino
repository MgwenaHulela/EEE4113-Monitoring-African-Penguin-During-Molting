#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "RTClib.h"
#include <SPI.h>
#include <MFRC522.h>

#define SEALEVELPRESSURE_HPA (1013.25)

// ─── Hardware pins ────────────────────────────────────────────────────────────
// BME280 & RTC  → I2C on default Wire pins
// MFRC522 on VSPI:
//   SCK  → GPIO18
//   MOSI → GPIO23
//   MISO → GPIO19
//   SDA  → GPIO5  (CS / SS)
//   RST  → GPIO26 (RESET)
#define SS_PIN   5
#define RST_PIN  26

// ─── Penguins mapping ─────────────────────────────────────────────────────────
constexpr size_t NUM_PENGUINS = 2;
const char* penguins[NUM_PENGUINS][2] = {
  { "FAE53502", "Penguin A" },
  { "C376C001", "Penguin B" }
};

// Return the penguin name for a given UID, or "Unknown"
String getPenguinName(const String &uid) {
  for (size_t i = 0; i < NUM_PENGUINS; i++) {
    if (uid == penguins[i][0]) {
      return String(penguins[i][1]);
    }
  }
  return "Unknown";
}

// ─── Sensor & peripheral objects ─────────────────────────────────────────────
Adafruit_BME280 bme;
RTC_DS3231       rtc;
MFRC522          mfrc522(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // I²C init for BME280 & RTC
  Wire.begin();

  // BME280 init
  if (!bme.begin(0x76)) {
    Serial.println("BME280 not found!");
    while (1) delay(10);
  }

  // RTC init
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1) delay(10);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(2025, 4, 28,  4, 10,  0));
  }

  // VSPI init for RFID
  SPI.begin(18, 19, 23, SS_PIN);
  mfrc522.PCD_Init();

  Serial.println("System ready. Tap a tag to log:");
}

void loop() {
  // Wait for a new RFID card
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // Build UID string (uppercase hex)
  String uidStr;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += '0';
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  // Read timestamp, temp, humidity
  DateTime now = rtc.now();
  char ts[20];
  snprintf(ts, sizeof(ts),
    "%04d-%02d-%02d %02d:%02d:%02d",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second()
  );

  float temp  = bme.readTemperature();
  float hum   = bme.readHumidity();

  // Lookup penguin name
  String name = getPenguinName(uidStr);

  // Print in one line:
  // Timestamp, PenguinName (UID), Temp°C, Humidity%
  Serial.print(ts);
  Serial.print(", ");
  Serial.print(name);
  Serial.print(" (");
  Serial.print(uidStr);
  Serial.print("), ");
  Serial.print(temp, 1);
  Serial.print("°C, ");
  Serial.print(hum, 1);
  Serial.println("%");

  // Cleanup
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
