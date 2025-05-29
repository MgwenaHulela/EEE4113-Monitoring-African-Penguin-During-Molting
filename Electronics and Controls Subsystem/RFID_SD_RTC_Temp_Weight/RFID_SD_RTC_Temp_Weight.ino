#include <Arduino.h>
#include <HX711.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// —— PIN CONFIGURATION —————————————————————————————————————
#define DOUT_PIN     32   // HX711 data
#define CLK_PIN      14   // HX711 clock
#define SD_CS        5
#define RFID_CS      15
#define RST_PIN      27
#define SPI_SCK      18
#define SPI_MISO     19
#define SPI_MOSI     23
#define DETECT_LED   2    // lights on tag detect
// ——————————————————————————————————————————————————————————————

HX711 scale;
Adafruit_BME280 bme;
RTC_DS3231 rtc;
MFRC522 mfrc522(RFID_CS, RST_PIN);

// Calibration constants
const float a1 = 0.0045558f;
const float a0 = -1862.8f;
const float NO_WEIGHT_THRESHOLD = 10.0f;

unsigned long lastScanTime = 0;
const unsigned long COOLDOWN_MS = 500;

// Simple Moving Average buffer
const int SMA_WINDOW = 5;
long weightBuffer[SMA_WINDOW];
int weightIndex = 0;
bool bufferFilled = false;

String getTimestamp() {
  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

bool writeLog(const String &line) {
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  File f = SD.open("/log.txt", FILE_APPEND);
  if (!f) {
    digitalWrite(SD_CS, HIGH);
    Serial.println("❌ ERROR: log.txt open failed");
    return false;
  }
  f.println(line);
  f.close();
  digitalWrite(SD_CS, HIGH);
  return true;
}

void readLastN(int n) {
  digitalWrite(RFID_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  File f = SD.open("/log.txt");
  if (!f) {
    Serial.println("❌ ERROR: log.txt read failed");
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

float getSmoothedWeight() {
  long raw = scale.read();
  weightBuffer[weightIndex] = raw;
  weightIndex = (weightIndex + 1) % SMA_WINDOW;
  if (weightIndex == 0) bufferFilled = true;

  int count = bufferFilled ? SMA_WINDOW : weightIndex;
  long sum = 0;
  for (int i = 0; i < count; i++) sum += weightBuffer[i];

  float avgRaw = sum / (float)count;
  float weight = a1 * avgRaw + a0;
  if (weight < NO_WEIGHT_THRESHOLD) weight = 0;
  return weight;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(DETECT_LED, OUTPUT);
  digitalWrite(DETECT_LED, LOW);

  Serial.println("\n🔧 Initializing modules...");

  // HX711
  scale.begin(DOUT_PIN, CLK_PIN);
  if (!scale.is_ready()) Serial.println("⚠️ HX711 not ready");
  Serial.println("⚙️ Auto-taring (5s)...");
  scale.tare();
  delay(5000);
  Serial.println("✔️ Tare complete.");

  // BME280
  if (!bme.begin(0x76)) { Serial.println("❌ BME280 fail"); while(1); }
  Serial.println("✅ BME280 ready");

  // RTC
  if (!rtc.begin()) { Serial.println("❌ RTC fail"); while(1); }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Reset RTC to current time at upload
  Serial.println("✅ RTC ready");

  // SD + RFID
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(RFID_CS, OUTPUT); digitalWrite(RFID_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  if (!SD.begin(SD_CS)) { Serial.println("❌ SD fail"); while(1); }
  if (!SD.exists("/log.txt")) {
    File f = SD.open("/log.txt", FILE_WRITE);
    if (f) { f.println("🔓 Log created."); f.close(); }
  }
  mfrc522.PCD_Init();
  Serial.println("✅ RFID ready");
  Serial.println("=== Setup complete ===");
}

void loop() {
  unsigned long now = millis();
  if (now - lastScanTime < COOLDOWN_MS) return;

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  lastScanTime = now;
  digitalWrite(DETECT_LED, HIGH);

  // Get UID
  String uid;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += '0';
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  Serial.println("\n📛 RFID UID: " + uid);

  // Read sensors
  String ts   = getTimestamp();
  float temp  = bme.readTemperature();
  float hum   = bme.readHumidity();
  float pres  = bme.readPressure() / 100.0F;
  float weight = getSmoothedWeight();

  // Create log line
  String line = "Timestamp: " + ts +
                " | RFID: " + uid +
                " | Temp: " + String(temp,1) + "C" +
                " | Hum: "  + String(hum,1)  + "%" +
                " | Pres: " + String(pres,1) + "hPa" +
                " | Weight: " + String(weight,1) + "g";

  Serial.println("→ Logging: " + line);
  if (writeLog(line)) {
    Serial.println("✅ Logged successfully");
    readLastN(5);
  } else {
    Serial.println("❌ Log write failed");
  }

  // Wait until card is removed
  while (mfrc522.PICC_IsNewCardPresent()) delay(50);
  digitalWrite(DETECT_LED, LOW);
}
