#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h> // Include the simple pin driver

#define SD_CS_PIN 5     // VSPI CS pin for SD card
#define RFID_CS_PIN 15  // HSPI CS pin for RFID
#define RFID_RST_PIN 4  // RFID RST pin

// Custom SPI buses
SPIClass hspi(HSPI);  // HSPI for RFID
SPIClass vspi(VSPI);  // VSPI for SD card

// --- MFRC522v2 Instantiation ---
// 1. Create a simple pin object for the RFID Chip Select (SS) pin
MFRC522DriverPinSimple rfid_cs_pin(RFID_CS_PIN);

// 2. Create the MFRC522 SPI driver instance, linking it to the CS pin and the HSPI bus
// The constructor expects a reference to the pin object and a reference to the SPIClass object
MFRC522DriverSPI driver(rfid_cs_pin, hspi); // Corrected constructor call

// 3. Create the MFRC522 object, passing the driver instance
// The constructor expects only a reference to the driver
MFRC522 mfrc522(driver); // Corrected constructor call
// -------------------------------

File dataFile;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("🔧 Initializing system...");

  // Initialize RFID (HSPI)
  Serial.println("🔌 Starting HSPI for RFID...");
  // hspi.begin() configures the HSPI pins (SCLK, MISO, MOSI, SS)
  // The SS pin (RFID_CS_PIN) is handled by the driver now, but it's good practice
  // to include it in the begin call if your board requires it for setup.
  // Ensure these pins (14, 12, 13) are correct for HSPI on your ESP32 board (SCLK, MISO, MOSI)
  hspi.begin(14, 12, 13, RFID_CS_PIN);
  Serial.println("DEBUG: HSPI begin called for RFID.");

  // --- Handle RFID Reset Pin ---
  // The MFRC522v2 PCD_Init() does not take the RST pin.
  // We need to manually toggle the RST pin to ensure the module is reset.
  Serial.println("DEBUG: Toggling RFID RST pin...");
  pinMode(RFID_RST_PIN, OUTPUT); // Set the RST pin as an output
  digitalWrite(RFID_RST_PIN, LOW); // Pulse the RST pin low
  delay(50); // Small delay
  digitalWrite(RFID_RST_PIN, HIGH); // Bring the RST pin high
  delay(50); // Wait for the module to come out of reset
  Serial.println("DEBUG: RFID RST pin toggled.");
  // -----------------------------

  // Initialize the MFRC522 PCD (Proximity Coupling Device - the reader itself)
  // Call PCD_Init() without any arguments for this library version.
  Serial.println("DEBUG: Calling mfrc522.PCD_Init()...");
  bool rfid_initialized = mfrc522.PCD_Init(); // Capture the return value

  // Check if RFID initialization was successful
  if (rfid_initialized) {
    Serial.println("✅ RFID reader initialised successfully.");

    // Attempt to read the firmware version for more specific debugging
    byte version = mfrc522.PCD_GetVersion(); // Get the firmware version
    if (version == 0x00 || version == 0xFF) {
      Serial.println("❌ RFID not detected (version check failed)!");
      Serial.println("   Possible causes: Wiring issue, incorrect SPI pins, or faulty module.");
    } else {
      Serial.print("✅ RFID firmware version: 0x");
      Serial.println(version, HEX);
    }

  } else {
    Serial.println("❌ RFID reader initialisation failed!");
    Serial.println("   Possible causes: Wiring issue, incorrect SPI pins, or faulty module.");
    // Consider adding a loop here to halt or indicate a critical error
    // while(true);
    // Depending on your application, you might continue without RFID or exit setup
  }


  // Initialize SD card (VSPI)
  Serial.println("🔌 Starting VSPI for SD card...");
  // vspi.begin() configures the VSPI pins (SCLK, MISO, MOSI, SS)
  // Ensure these pins (18, 19, 23) are correct for VSPI on your ESP32 board (SCLK, MISO, MOSI)
  vspi.begin(18, 19, 23, SD_CS_PIN);
  Serial.println("DEBUG: VSPI begin called for SD card.");

  // SD.begin() for ESP32 takes the CS pin and the SPIClass instance
  Serial.println("DEBUG: Calling SD.begin()...");
  if (!SD.begin(SD_CS_PIN, vspi)) {
    Serial.println("❌ SD card initialisation failed!");
    // You might want to loop here or indicate a fatal error
    // while (true); // Uncomment this line to halt execution on SD failure
    return; // Exit setup if SD card fails
  }
  Serial.println("✅ SD card initialized successfully.");

  // Open log file
  Serial.println("📂 Opening log file...");
  // Check if SD is actually initialised before trying to open the file
  // SD.cardAttached() is a useful check if available in your SD library version
  // Alternatively, you can rely on the success of SD.begin()
  dataFile = SD.open("/log.txt", FILE_WRITE);
  if (dataFile) {
    Serial.println("DEBUG: Log file opened successfully.");
    dataFile.println("=== Log Start ===");
    dataFile.close(); // Close the file after writing the header
    Serial.println("✅ Log file ready.");
  } else {
    Serial.println("❌ Failed to open log.txt!");
    // Handle this error - maybe set a flag to disable logging to SD?
  }

  Serial.println("🔧 System initialization complete.");
  Serial.println("Waiting for RFID tags...");
}

void loop() {
  // Check for new RFID tag
  // PICC_IsNewCardPresent() checks if a new card is physically near the reader.
  // PICC_ReadCardSerial() reads the UID of the card if one is present.
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

    // Construct UID string from the byte array
    String uidStr = "";
    // mfrc522.uid.size and mfrc522.uid.uidByte are standard
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      // Add a leading zero if the byte is less than 16 (0x10) for consistent formatting
      if (mfrc522.uid.uidByte[i] < 0x10) {
        uidStr += "0";
      }
      // Convert the byte to its hexadecimal string representation
      uidStr += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase(); // Format the UID string as uppercase HEX

    // Get timestamp (using millis() as a simple example)
    unsigned long timestamp = millis(); // Consider using an RTC module for actual time

    // Create the log entry string
    String logEntry = String(timestamp) + ", UID: " + uidStr;

    // Log to serial monitor
    Serial.println("📛 Detected: " + logEntry);

    // Re-open the file in append mode to add new entries
    // It's better to open, write, and close for each entry to ensure data is saved
    // in case of power loss.
    dataFile = SD.open("/log.txt", FILE_APPEND); // Use FILE_APPEND to add to the end
    if (dataFile) {
      dataFile.println(logEntry);
      dataFile.close(); // Close the file after writing
    } else {
      Serial.println("❌ Failed to write to SD!");
    }

    // Halt PICC (Proximity Integrated Circuit Card) to stop it from re-presenting
    // immediately and allow other cards to be read.
    mfrc522.PICC_HaltA();

    // Stop encryption if it was started. This is often done after reading.
    // Depending on the library version and tag type, this might not be strictly
    // necessary after PICC_HaltA(), but it's good practice.
    mfrc522.PCD_StopCrypto1();
  }
  // Add a small delay in the loop to prevent it from running too fast
  // and potentially missing tags or consuming excessive power.
  // delay(50); // Optional: Add a small delay if needed
}
