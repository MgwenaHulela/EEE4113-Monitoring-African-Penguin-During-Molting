#include <SPI.h>
#include <MFRC522.h>

// ── RFID connections ──
#define SS_PIN   15   // SDA pin, acts as CS
#define RST_PIN  27   // Reset pin
#define LED_PIN   2   // Status LED

MFRC522 mfrc522(SS_PIN, RST_PIN);

// ── Define known penguin UIDs ──
constexpr size_t NUM_PENGUINS = 2;
const char* penguins[NUM_PENGUINS][2] = {
  { "FAE53502", "Penguin A" },
  { "C376C001", "Penguin B" }
};

// ── Lookup function ──
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

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n🔧 Initializing SPI and RFID...");

  // Initialize SPI bus with explicit pins
  // SCK = 18, MISO = 19, MOSI = 23, SS = SS_PIN
  SPI.begin(18, 19, 23, SS_PIN);
  delay(100);

  mfrc522.PCD_Init();
  delay(100);

  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("❌ RFID module not detected. Check wiring:");
    Serial.println("   • SS_PIN must go to SDA on MFRC522");
    Serial.println("   • RST_PIN must go to RST on MFRC522");
    Serial.println("   • SCK/MISO/MOSI must match 18/19/23 on ESP32");
  } else {
    Serial.print("✅ RFID module detected. Version: 0x");
    Serial.println(version, HEX);
    Serial.println("Ready – tap an RFID tag.");
  }
}

void loop() {
  // Debug: show PICC_IsNewCardPresent() status
  bool present = mfrc522.PICC_IsNewCardPresent();
  Serial.print("DBG: NewCardPresent? ");
  Serial.println(present ? "YES" : "no");

  if (!present) {
    delay(500);
    return;
  }

  // Debug: show PICC_ReadCardSerial() result
  bool readOk = mfrc522.PICC_ReadCardSerial();
  Serial.print("DBG: ReadCardSerial? ");
  Serial.println(readOk ? "OK" : "FAIL");

  if (!readOk) {
    delay(500);
    return;
  }

  // Toggle LED to show detection
  digitalWrite(LED_PIN, HIGH);

  // Format UID as uppercase hex string
  String uidStr;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += '0';
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  String name = getPenguinName(uidStr);
  Serial.print("🕹️  Detected: ");
  Serial.print(name);
  Serial.print(" (");
  Serial.print(uidStr);
  Serial.println(")");

  // Cleanup
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1000);
  digitalWrite(LED_PIN, LOW);
}
