#include <Arduino.h> // Standard Arduino functions
#include <SPI.h>     // Required for MFRC522 communication
#include <MFRC522.h> // MFRC522 RFID Reader library
#include <map>       // For std::map to store RFID-to-sex mappings

// --- PIN CONFIGURATION ---
#define RFID_CS    15 // Chip Select pin for MFRC522
#define RST_PIN    27 // Reset pin for MFRC522
#define DETECT_LED 2  // GPIO pin for the detection LED

// --- MFRC522 RFID Reader Object ---
MFRC522 mfrc522(RFID_CS, RST_PIN);

// --- RFID to Sex Mapping ---
// This map stores RFID UIDs as keys and their corresponding sex as values.
// You can expand this map with more RFID UIDs and their associated sex data.
std::map<String, String> rfidSexMap;

void setup() {
  Serial.begin(115200); // Initialize serial communication
  delay(100); // Small delay to allow serial monitor to open

  Serial.println("\n🚀 Booting up RFID Detection System...");

  // Initialize LED pin
  pinMode(DETECT_LED, OUTPUT);
  digitalWrite(DETECT_LED, LOW); // Ensure LED is off initially
  Serial.println("💡 LED pin initialized (GPIO 2).");

  // Initialize SPI bus
  SPI.begin();
  Serial.println("🌐 SPI bus initialized.");

  // Initialize MFRC522 RFID reader
  mfrc522.PCD_Init();
  Serial.println("✅ RFID Reader Initialized. Waiting for tags...");

  // Populate the RFID to Sex map with known UIDs
  // IMPORTANT: Replace these with the actual UIDs of your RFID tags
  //rfidSexMap["2A0D4602"] = "Female"; // Example UID for a female penguin
  rfidSexMap["FAE53502"] = "Male";   // Example UID for a male penguin
  rfidSexMap["7743C001"] = "Male";   // Another example UID
  rfidSexMap["C376C001"] = "Female"; // Another example UID
  Serial.println("📚 RFID-to-Sex map populated.");
  Serial.println("\nReady for RFID scans. Swipe a tag!");
}

void loop() {
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) {
    // Serial.println("🔍 No new card present. Looping..."); // Uncomment for verbose "no card" debug
    return; // No new card, return and try again
  }

  Serial.println("\n--- 🐧 Potential RFID Tag Detected! ---"); // Debug message

  // Select one of the cards AND ensure we can read its serial
  if (mfrc522.PICC_ReadCardSerial()) { // ONLY proceed if read is successful
    Serial.println("✅ UID read successfully!"); // Debug message
    
    // --- RFID Tag Detected AND UID Read Successfully ---
    digitalWrite(DETECT_LED, HIGH); // Turn on LED

    // --- Read RFID UID ---
    String uid;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) {
        uid += '0'; // Add leading zero for single-digit hex values
      }
      uid += String(mfrc522.uid.uidByte[i], HEX); // Convert byte to hex string
    }
    uid.toUpperCase(); // Convert UID to uppercase for consistent mapping

    // --- Retrieve Sex data from map ---
    String sex = "Unknown"; // Default value if RFID not found in map
    if (rfidSexMap.count(uid)) { // Check if UID exists in the map
      sex = rfidSexMap[uid];
      Serial.print("✨ Mapped UID to sex: "); // Debug message
    } else {
      Serial.print("❓ UID not found in map: "); // Debug message
    }

    // --- Print Results to Serial Monitor ---
    Serial.print("UID: ");
    Serial.println(uid);
    Serial.print("Penguin Sex: ");
    Serial.println(sex);
    Serial.println("--- Processing Complete ---"); // Debug message

    // --- Halt RFID PICC and prepare for next scan ---
    mfrc522.PICC_HaltA();      // Halt PICC (Proximity Integrated Circuit Card)
    mfrc522.PCD_StopCrypto1(); // Stop encryption on PCD (Proximity Coupling Device)

    // Keep LED on for a short visual confirmation, then turn off
    delay(500); // LED stays on for 0.5 seconds
    digitalWrite(DETECT_LED, LOW);
    Serial.println("💡 LED off. Ready for next scan."); // Debug message

  } else { // If PICC_ReadCardSerial() failed
    Serial.println("❌ Failed to read card serial. Tag detected but could not be processed."); // Debug message for failure
    // It's good practice to halt/stop crypto even on failed read attempts if possible,
    // to reset the reader state.
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    digitalWrite(DETECT_LED, LOW); // Ensure LED is off if read failed.
    Serial.println("🚨 Reader reset after failed read."); // Debug message
  }
}