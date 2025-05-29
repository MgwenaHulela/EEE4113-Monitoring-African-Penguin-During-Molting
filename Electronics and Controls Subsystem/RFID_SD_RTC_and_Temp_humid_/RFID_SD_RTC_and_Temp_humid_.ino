#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// Pin definitions
#define SD_CS     5
#define RFID_CS   15
#define RST_PIN   27
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23

// I2C for BME280 + RTC
Adafruit_BME280 bme;
RTC_DS3231 rtc;
MFRC522 mfrc522(RFID_CS, RST_PIN);

void setup() {
  Serial.begin(115200);
  delay(500);

  // SPI bus for RFID + SD
  pinMode(SD_CS, OUTPUT);
  pinMode(RFID_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(RFID_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // Init RFID
  mfrc522.PCD_Init();

  // Init SD
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD init failed");
    while (1);
  }
  if (!SD.exists("/log.txt")) {
    File f = SD.open("/log.txt", FILE_WRITE);
    if (f) {
      f.println("🔓 Log created.");
      f.close();
    }
  }

  // Init RTC
  if (!rtc.begin()) {
    Serial.println("❌ RTC not found");
    while (1);
  }
  if (rtc.lostPower()) {
    // set to compile time if power lost
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Init BME280
  if (!bme.begin(0x76)) {
    Serial.println("❌ BME280 not found");
    while (1);
  }

  Serial.println("🔧 Setup complete ▶ awaiting RFID");
}

String getTimestamp() {
  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

// Write a single line to log.txt
bool writeLog(const String &line) {
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  File f = SD.open("/log.txt", FILE_APPEND);
  if (!f) {
    digitalWrite(SD_CS, HIGH);
    return false;
  }
  f.println(line);
  f.flush();
  f.close();
  digitalWrite(SD_CS, HIGH);
  return true;
}

void readLastN(int n) {
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  File f = SD.open("/log.txt");
  if (!f) {
    Serial.println("❌ Cannot read log");
    digitalWrite(SD_CS, HIGH);
    return;
  }
  const int M = 100;
  String buf[M];
  int cnt = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length()) {
      buf[cnt % M] = line;
      cnt++;
    }
  }
  f.close();
  digitalWrite(SD_CS, HIGH);
  int start = max(0, cnt - n);
  for (int i = start; i < cnt; i++) {
    Serial.println("▶ " + buf[i % M]);
  }
}

void loop() {
  // wait up to 5s for a tag
  unsigned long t0 = millis();
  while (millis() - t0 < 5000) {
    if (mfrc522.PICC_IsNewCardPresent() &&
        mfrc522.PICC_ReadCardSerial()) {

      // build UID string
      String uid;
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uid += '0';
        uid += String(mfrc522.uid.uidByte[i], HEX);
        if (i < mfrc522.uid.size - 1) uid += ':';
      }
      uid.toUpperCase();
      mfrc522.PICC_HaltA();

      // read sensors
      String timestamp = getTimestamp();
      float temp = bme.readTemperature();
      float hum  = bme.readHumidity();
      float pres = bme.readPressure() / 100.0F;

      // assemble log line
      String line = "Timestamp: " + timestamp +
                    " | RFID: " + uid +
                    " | Temp: " + String(temp,1) + "C" +
                    " | Hum: "  + String(hum,1)  + "%" +
                    " | Pres: " + String(pres,1) + "hPa" +
                    " | Weight: 0g" +  // placeholder
                    " | Image: no_image.jpg";

      // write and read back last 5
      if (writeLog(line)) {
        Serial.println("✅ Logged:");
        Serial.println(line);
        delay(200);
        Serial.println("📖 Last 5:");
        readLastN(5);
      } else {
        Serial.println("❌ Log write failed");
      }

      break;
    }
  }

  delay(2000);
}
