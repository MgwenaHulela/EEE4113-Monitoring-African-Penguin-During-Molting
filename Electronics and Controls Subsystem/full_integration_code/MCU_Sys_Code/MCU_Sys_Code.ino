#include <Wire.h>     // Required for I2C communication (RTC)
#include <Arduino.h>  // Standard Arduino functions
#include <HX711.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <RTClib.h>
#include <map>          // Include for std::map
#include <esp_now.h>    // Include for ESP-NOW functionality
#include <WiFi.h>       // Include for WiFi functionality (required by ESP-NOW)
#include "AHT20.h"      // Included: Environmental sensor library (will use your installed library)

// —— PIN CONFIGURATION —————————————————————————————————————
#define I2C_SDA      21
#define I2C_SCL      22
#define DOUT_PIN     32
#define CLK_PIN      14
#define SD_CS        5
#define RFID_CS      15
#define RST_PIN      27
#define SPI_SCK      18
#define SPI_MISO     19
#define SPI_MOSI     23
#define DETECT_LED   2
// ——————————————————————————————————————————————————————————————

// Sensor Objects
HX711 scale;
RTC_DS3231 rtc;
MFRC522 mfrc522(RFID_CS, RST_PIN);
AHT20 aht; // AHT20 sensor object instance

// Calibration and configuration for HX711 (Weight Sensor)
const float HX711_CALIBRATION_A1 = 0.0045558f;
const float HX711_CALIBRATION_A0 = -1862.8f;
const float NO_WEIGHT_THRESHOLD = 10.0f; // Threshold below which weight is considered zero
const int SMA_WINDOW = 5;               // Simple Moving Average window size for weight

long weightBuffer[SMA_WINDOW];
int weightIndex = 0;
bool bufferFilled = false;

// RFID Cooldown and Scan Management
const unsigned long RFID_COOLDOWN_MS = 500; // Milliseconds to wait after a scan before allowing another
unsigned long lastScanTime = 0;             // Stores the timestamp of the last successful RFID scan

// Pre-stored RFID to Sex mapping
// This map stores RFID UIDs as keys and their corresponding sex as values.
// You can expand this map with more RFID UIDs and their associated sex data.
std::map<String, String> rfidSexMap;

// MAC address of the receiving ESP32 board (ESP32-CAM)
// IMPORTANT: This MUST match the actual MAC address of your ESP32-CAM.
uint8_t receiverMac[] = {0xA0, 0xA3, 0xB3, 0x2B, 0xCB, 0x40}; // Example MAC for 'travis cam'

// Buffer for JSON payload (increased size for robustness, adjusted for smaller payload)
// Ensure this is large enough to hold your complete JSON string.
#define JSON_BUFFER_SIZE 200 // Reduced size as light/pressure are removed
char jsonBuffer[JSON_BUFFER_SIZE];

// Global variable to store the unique filename for this session's log
String sessionLogFilename;

// --- Function Prototypes ---
// (Good practice to declare functions before they are used, especially in larger sketches)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
String getTimestamp();
// String getFilenameFromTimestamp(); // No longer needed
bool writeLog(const String &line);
float getSmoothedWeight();
// No longer need prototypes for getTemperature() and getHumidity() here, as they are part of AHT20 class.

// Callback function for ESP-NOW send status
// This function is called after an ESP-NOW packet is sent.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
    // You might want to implement retry logic here for failed deliveries
  }
}

// Generates a formatted timestamp string from the RTC module.
String getTimestamp() {
  DateTime now = rtc.now();
  // Increased buffer size to prevent stack smashing from day of week string
  // "YYYY-MM-DD HH:MM:SS" is 19 chars + null terminator = 20.
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

// Writes a line of data to the SD card, appending to the session's log file.
bool writeLog(const String &line) {
  // Ensure RFID_CS is high before accessing SD card to prevent conflicts
  digitalWrite(RFID_CS, HIGH);
  // Select SD card
  digitalWrite(SD_CS, LOW);

  // Open the session-specific log file in APPEND mode
  File f = SD.open(sessionLogFilename, FILE_APPEND);
  if (!f) {
    Serial.printf("❌ ERROR: Log file %s open failed for appending.\n", sessionLogFilename.c_str());
    digitalWrite(SD_CS, HIGH); // Deselect SD card on failure
    return false;
  }

  f.println(line);
  f.close();
  Serial.printf("📁 Appended data to %s\n", sessionLogFilename.c_str());
  digitalWrite(SD_CS, HIGH); // Deselect SD card after write
  return true;
}

// Reads raw weight from HX711 and applies a Simple Moving Average filter.
float getSmoothedWeight() {
  long raw = scale.read();
  weightBuffer[weightIndex] = raw;
  weightIndex = (weightIndex + 1) % SMA_WINDOW;
  if (weightIndex == 0) bufferFilled = true; // Buffer is filled after first full cycle

  int count = bufferFilled ? SMA_WINDOW : weightIndex;
  long sum = 0;
  for (int i = 0; i < count; i++) {
    sum += weightBuffer[i];
  }

  float avgRaw = sum / (float)count;
  float weight = HX711_CALIBRATION_A1 * avgRaw + HX711_CALIBRATION_A0;
  return (weight < NO_WEIGHT_THRESHOLD) ? 0.0f : weight; // Return 0 if below threshold
}

void setup() {
  Serial.begin(115200);
  delay(200); // Allow serial to initialize

  // Initialize LED pin
  pinMode(DETECT_LED, OUTPUT);
  digitalWrite(DETECT_LED, LOW); // Ensure LED is off initially

  // Seed the random number generator with an analog read from an unconnected pin
  // This helps ensure different random numbers each time the ESP32 starts.
  randomSeed(analogRead(0));

  // Initialize I2C bus - CRITICAL for RTC and AHT20 communication
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("✅ I2C bus initialized.");

  // Initialize AHT20
  if(!aht.begin()) {
    Serial.println("❌ ERROR: AHT20 initialization failed! Halting.");
    while(1); // Halt if AHT20 is critical for operation
  }
  Serial.println("✅ AHT20 ready.");

  // Initialize HX711 (Weight Sensor)
  scale.begin(DOUT_PIN, CLK_PIN);
  if (!scale.is_ready()) {
    Serial.println("❌ ERROR: HX711 not ready! Continuing without scale data.");
    // Consider adding a while(1) here if scale is absolutely critical
  }
  Serial.println("⚙️ Auto-taring HX711 (5s)...");
  scale.tare(); // Perform initial tare
  delay(5000); // Give time for tare to settle
  Serial.println("✔️ Tare complete.");

  // Initialize RTC (Real-Time Clock)
  if (!rtc.begin()) {
    Serial.println("❌ ERROR: RTC failed to initialize! Halting.");
    while(1); // Halt if RTC is critical for timestamping
  }
  // Adjust RTC time with the compile time (computer's time)
  // This line will set the RTC to the time the sketch was compiled.
  // Remove this line after the first upload if you want the RTC to keep time independently.
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println("✅ RTC ready.");

  // Initialize SD Card and RFID Reader
  // Ensure CS pins are high initially to deselect devices
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(RFID_CS, OUTPUT); digitalWrite(RFID_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // Initialize SPI bus

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ ERROR: SD card initialization failed! Halting.");
    while(1); // Halt if SD card is critical for logging
  }
  Serial.println("✅ SD card ready.");

  // --- Generate unique filename for this session's log file ---
  DateTime now = rtc.now();
  char filename_buf[35]; // Buffer for "/YYYY-MM-DD_HH-MM-SS_SESSION.txt"
  sprintf(filename_buf, "/LOG_%04d-%02d-%02d_%02d-%02d-%02d.txt",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  sessionLogFilename = String(filename_buf);
  Serial.printf("✨ Session log file created: %s\n", sessionLogFilename.c_str());

  mfrc522.PCD_Init(); // Initialize MFRC522 RFID reader
  Serial.println("✅ RFID ready.");
  Serial.println("=== System Operational ===");

  // Populate the RFID to Sex map with known UIDs
  rfidSexMap["2A0D4602"] = "Female";
  rfidSexMap["FAE53502"] = "Male";
  rfidSexMap["7743C001"] = "Male";
  rfidSexMap["C376C001"] = "Female";
  // Add more RFID UIDs and their corresponding sex here as needed

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA); // Set WiFi to station mode for ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW initialization failed! Halting.");
    return; // Halt if ESP-NOW fails to initialize
  }
  Serial.println("✅ ESP-NOW initialized.");

  // Register the ESP-NOW send callback function
  esp_now_register_send_cb(OnDataSent);

  // Add peer (receiver ESP32-CAM)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 0;     // Use channel 0 (default)
  peerInfo.encrypt = false; // No encryption for simplicity

  if (!esp_now_is_peer_exist(receiverMac)) {
    esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
    if (addPeerResult == ESP_OK) {
      Serial.println("✅ ESP-NOW peer added successfully.");
    } else {
      Serial.print("❌ Failed to add ESP-NOW peer: ");
      Serial.println(addPeerResult);
    }
  } else {
    Serial.println("ℹ️ ESP-NOW peer already exists.");
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Implement a cooldown period to prevent rapid re-scanning of the same RFID card
  if (currentMillis - lastScanTime < RFID_COOLDOWN_MS) {
    return; // Return if still in cooldown period
  }

  // Check for new RFID card presence
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return; // No new card, exit loop
  }
  // Read the RFID card serial number (UID)
  if (!mfrc522.PICC_ReadCardSerial()) {
    return; // Failed to read serial, exit loop
  }

  lastScanTime = currentMillis; // Update last scan time
  digitalWrite(DETECT_LED, HIGH); // Turn on LED to indicate RFID detection

  // --- Read RFID UID ---
  String uid;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += '0'; // Add leading zero for single-digit hex
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase(); // Convert UID to uppercase for consistent mapping

  // --- Retrieve Sex data from map ---
  String sex = "Unknown"; // Default value if RFID not found in map
  if (rfidSexMap.count(uid)) { // Check if UID exists in the map
    sex = rfidSexMap[uid];
  }

  // --- Get Timestamp from RTC ---
  String timestamp = getTimestamp();

  // --- Read Sensor Data ---
  // Using actual smoothed weight from HX711
  float weight = getSmoothedWeight()-5.77;

  // Get actual temperature and humidity from AHT20
  float temperature = aht.getTemperature();
  float humidity = aht.getHumidity();

  // Check for valid sensor readings from AHT20
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ WARNING: Failed to read temperature or humidity from AHT20. Skipping ESP-NOW send.");
    digitalWrite(DETECT_LED, LOW);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return; // Skip this cycle if sensor data is invalid
  }

  // --- Prepare Data for SD Card Logging ---
  String logLine = "Timestamp: " + timestamp +
                   " | RFID: " + uid +
                   " | Sex: " + sex +
                   " | Weight: " + String(weight, 1) + "kg" +
                   " | Temp: " + String(temperature, 1) + "°C" +
                   " | Humid: " + String(humidity, 1) + "%";

  // --- Save Data to SD card ---
  Serial.println("\n" + logLine);
  if (!writeLog(logLine)) { // This will now append to the single session file
    Serial.println("❌ Failed to save data to SD card.");
  }

  // --- Construct JSON Payload for ESP-NOW Transmission (using snprintf for robustness) ---
  // Ensure all values are correctly formatted and escaped if necessary.
  // The JSON structure should match what the receiver expects.
  int jsonLen = snprintf(jsonBuffer, JSON_BUFFER_SIZE,
                      "{\"timestamp\":\"%s\","
                      "\"rfid\":\"%s\","
                      "\"sex\":\"%s\","
                      "\"weight\":%.1f,"
                      "\"temperature\":%.1f,"
                      "\"humidity\":%.1f}",
                      timestamp.c_str(),
                      uid.c_str(),
                      sex.c_str(),
                      weight,
                      temperature,
                      humidity);

  // Check for snprintf errors (buffer overflow or encoding error)
  if (jsonLen < 0 || jsonLen >= JSON_BUFFER_SIZE) {
      Serial.println("❌ ERROR: JSON payload too large for buffer or formatting error!");
      digitalWrite(DETECT_LED, LOW); // Turn off LED
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return; // Skip ESP-NOW send for this cycle
  }

  // --- Send Data via ESP-NOW ---
  Serial.println("Attempting to send data via ESP-NOW...");
  Serial.print("JSON Data to be sent (length ");
  Serial.print(jsonLen);
  Serial.print(" bytes): ");
  Serial.println(jsonBuffer); // Print the content of the char array

  // Send the char array. `jsonLen + 1` includes the null terminator.
  esp_err_t result = esp_now_send(receiverMac, (const uint8_t*)jsonBuffer, jsonLen + 1);

  if (result == ESP_OK) {
    Serial.println("✅ Data send initiated via ESP-NOW.");
  } else {
    Serial.print("❌ Failed to initiate data send via ESP-NOW: ");
    Serial.println(result);
  }

  // --- Halt RFID PICC and prepare for next scan ---
  mfrc522.PICC_HaltA();      // Halt PICC (Proximity Integrated Circuit Card)
  mfrc522.PCD_StopCrypto1(); // Stop encryption on PCD (Proximity Coupling Device)

  // Turn off LED immediately after processing the card.
  digitalWrite(DETECT_LED, LOW);
}