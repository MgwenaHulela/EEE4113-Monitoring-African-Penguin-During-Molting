/*
  Livestock Monitoring System - ESP32 Main Controller
  Features:
  - Dual load cell weight measurement with Kalman filtering
  - RFID animal identification with sex mapping
  - Environmental sensing (temp/humidity)
  - SD card logging
  - ESP-NOW wireless communication to ESP32-CAM
  - Serial calibration interface
*/

#include <Wire.h>        // Required for I2C communication (RTC, AHT20)
#include <Arduino.h>     // Standard Arduino functions
#include <HX711.h>       // For HX711 load cell amplifier
#include <SPI.h>         // For SPI communication (SD Card, RFID)
#include <SD.h>          // For SD Card logging
#include <MFRC522.h>     // For MFRC522 RFID reader
#include <RTClib.h>      // For DS3231 Real-Time Clock module
#include <map>           // For std::map to store RFID-to-sex mapping
#include <esp_now.h>     // For ESP-NOW wireless communication protocol
#include <WiFi.h>        // Required for ESP-NOW setup (sets WiFi to station mode)
#include "AHT20.h"       // For AHT20 temperature/humidity sensor

// -------------------------
// HARDWARE PIN CONFIGURATION
// -------------------------
#define I2C_SDA_PIN 21    // I2C data line for RTC and AHT20
#define I2C_SCL_PIN 22    // I2C clock line for RTC and AHT20

#define HX711_DOUT1_PIN 32  // Data pin for the first HX711 load cell
#define HX711_DOUT2_PIN 26  // Data pin for the second HX711 load cell
                            // IMPORTANT: Ensure GPIO26 is free and not used by other components.
                            // If it conflicts, choose another unused GPIO for HX711_DOUT2_PIN.
#define HX711_SCK_PIN   14  // Shared clock pin for both HX711s

#define SD_CS_PIN     5     // Chip Select pin for the SD card module
#define RFID_CS_PIN   15    // Chip Select pin for the MFRC522 RFID reader
#define RFID_RST_PIN 27    // Reset pin for the MFRC522 RFID reader

#define SPI_SCK_PIN    18   // SPI clock pin (shared for SD and RFID)
#define SPI_MISO_PIN   19   // SPI MISO pin (shared for SD and RFID)
#define SPI_MOSI_PIN   23   // SPI MOSI pin (shared for SD and RFID)

#define DETECT_LED_PIN 2    // GPIO for an onboard LED or external indicator for RFID detection

// -------------------------
// SENSOR OBJECT INSTANTIATION
// -------------------------
HX711 scale1; // Object for the first load cell (e.g., front of the scale)
HX711 scale2; // Object for the second load cell (e.g., rear of the scale)
RTC_DS3231 rtc; // Object for the DS3231 Real-Time Clock module
MFRC522 mfrc522(RFID_CS_PIN, RFID_RST_PIN); // Object for the MFRC522 RFID reader
AHT20 aht; // Object for the AHT20 temperature/humidity sensor

// -------------------------
// WEIGHT SENSOR CALIBRATION & CONFIGURATION
// -------------------------
// These are your specific calibration factors (raw ADC counts per gram).
// These values are derived from your calibration outputs:
// Scale 1: (-107.9721 + -123.4339 + -96.4260) / 3 = -109.2773
// Scale 2: (-307.5103 + -43.3456 + -1.5809) / 3 = -117.4789
const float CALIBRATION_FACTOR_SCALE1 = -109.2773f;
const float CALIBRATION_FACTOR_SCALE2 = -117.4789f;

// Weight below this threshold (in kilograms) will be considered 0.0kg.
// Helps filter out minor fluctuations or negative readings near zero.
const float NO_WEIGHT_THRESHOLD_KG = 0.05f; // e.g., 0.05 kg = 50 grams

// -------------------------
// KALMAN FILTER CONFIGURATION
// -------------------------
// Q (Process Noise Covariance): How much the system's state is expected to change between measurements.
//   Higher Q makes the filter more responsive to changes, but potentially more noisy.
//   Lower Q makes the filter smoother, but slower to react to real changes.
const double KALMAN_Q = 0.01;

// R (Measurement Noise Covariance): How much noise is in your combined *calibrated* sensor readings.
//   Higher R makes the filter trust the new measurements less (more smoothing).
//   Lower R makes the filter trust the new measurements more (less smoothing).
const double KALMAN_R = 0.01;

// Kalman filter state variables for combined weight (in kg)
double kalman_x_est_kg = 0.0; // Current estimated state (the filtered weight)
double kalman_P_est_kg = 1.0; // Current estimation error covariance (represents initial uncertainty)

// Time to allow the Kalman filter to stabilize after an RFID detection event.
// This ensures a more accurate weight reading when an animal newly steps on the scale.
const unsigned long KALMAN_SETTLE_TIME_MS = 1000; // 1 second

// -------------------------
// RFID SCAN MANAGEMENT
// -------------------------
#define RFID_ANTENNA_GAIN MFRC522::RxGain_max // Max sensitivity for RFID reader
#define RFID_READ_TIMEOUT 200          // Milliseconds between RFID read attempts (for poll rate)

const unsigned long RFID_COOLDOWN_MS = 500; // Milliseconds to wait after a successful scan before allowing another
unsigned long lastRfidSuccessTime = 0;      // Stores the timestamp of the last successful RFID scan
unsigned long lastRfidAttemptTime = 0;      // Stores the timestamp of the last RFID read attempt (success or failure)
const unsigned long RFID_FAIL_RESET_INTERVAL_MS = 5000; // Reset RFID module after 5 seconds of continuous read failures
bool rfidNeedsResetFlag = false;            // Flag to indicate if RFID module needs a reset due to failures
String currentUid;                          // Stores the UID of the currently detected RFID card for cooldown checks

// -------------------------
// RFID to Sex Mapping
// -------------------------
std::map<String, String> rfidSexMap; // Stores RFID UIDs as keys and their corresponding sex as values

// -------------------------
// ESP-NOW COMMUNICATION CONFIGURATION
// -------------------------
// MAC address of the receiving ESP32 board (ESP32-CAM).
// IMPORTANT: This MUST match the actual MAC address of your ESP32-CAM.
//uint8_t receiverMac[] = {0xEC, 0xE3, 0x34, 0xC1, 0x58, 0xF8}; // Updated MAC address
uint8_t receiverMac[] = {0xA0, 0xA3, 0xB3, 0x2B, 0xCB, 0x40}; // Replace with your ESP32-CAM's MAC address


// Buffer for the JSON payload sent via ESP-NOW.
// Ensure this size is sufficient for your JSON string, including null terminator.
const size_t JSON_BUFFER_SIZE = 256;
char jsonBuffer[JSON_BUFFER_SIZE];

// -------------------------
// SD CARD LOGGING
// -------------------------
String sessionLogFilename; // Unique filename for this session's log file (e.g., LOG_YYYY-MM-DD_HH-MM-SS.txt)

// -------------------------
// DEBUG CONFIGURATION
// -------------------------
#define DEBUG true // Set to 'true' to enable Serial debug output, 'false' to disable
#define DEBUG_INTERVAL_MS 5000 // Interval (in ms) for periodic debug status prints

unsigned long lastDebugPrintTime = 0; // Timestamp for the last debug print

// Debug print macros (conditional compilation for performance)
#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__) // For formatted printing
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// -------------------------
// NEW FEATURE CONFIGURATION
// -------------------------
const float WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG = 0.5f; // Threshold for sending data based on weight
const float RESET_WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG = 0.1f; // Weight below this to reset dataSentForWeightThreshold flag
bool dataSentForWeightThreshold = false; // Flag to prevent continuous sending for the same weight event

// -------------------------
// FUNCTION PROTOTYPES
// -------------------------
// Core system functions
void initializeHardware(); // This will be called from setup()
void updateWeightMeasurements();
void manageRfidDetection();
void handleSuccessfulRfidRead(const String& uid);
void sendEspNowData(const String& uid, const String& sex, float weight, float temperature, float humidity);
void monitorAndPrintSystemStatus();
void monitorWeightForImageSend(); // New function for weight threshold trigger

// Helper functions
String getFormattedTimestamp();
bool writeDataToLog(const String &line);
double applyKalmanFilter(double combined_measurement_kg);
void resetRfidModule();
String generateRandomRfid(); // New function for random RFID UID

// Calibration function (for initial setup/tuning via Serial Monitor)
void calibrateScale(HX711 &scale_obj, const String& scale_name);

// -------------------------
// ESP-NOW CALLBACK
// -------------------------
// This function is called automatically after an ESP-NOW packet is sent.
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  DEBUG_PRINT("\r\nLast Packet Send Status:\t");
  if (status == ESP_NOW_SEND_SUCCESS) {
    DEBUG_PRINTLN("Delivery Success");
  } else {
    DEBUG_PRINTLN("Delivery Fail");
    // Implement retry logic or error handling for failed deliveries if needed
  }
}

// -------------------------
// RTC TIMESTAMPING
// -------------------------
// Generates a formatted timestamp string from the RTC module (e.g., "YYYY-MM-DDTHH:MM:SS").
String getFormattedTimestamp() {
  DateTime now = rtc.now();
  char buf[20]; // Buffer for "YYYY-MM-DDTHH:MM:SS\0" (19 chars + null terminator)
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

// -------------------------
// SD CARD LOGGING
// -------------------------
// Writes a line of data to the SD card, appending to the session's log file.
bool writeDataToLog(const String &line) {
  // Ensure RFID_CS is high before accessing SD card to prevent conflicts on SPI bus
  digitalWrite(RFID_CS_PIN, HIGH);
  // Select SD card by pulling its CS pin LOW
  digitalWrite(SD_CS_PIN, LOW);

  File f = SD.open(sessionLogFilename, FILE_APPEND);
  if (!f) {
    DEBUG_PRINTF("❌ ERROR: Log file '%s' open failed for appending.\n", sessionLogFilename.c_str());
    digitalWrite(SD_CS_PIN, HIGH); // Deselect SD card on failure
    return false;
  }

  f.println(line);
  f.close();
  DEBUG_PRINTF("📁 Appended data to %s\n", sessionLogFilename.c_str());
  digitalWrite(SD_CS_PIN, HIGH); // Deselect SD card after write
  return true;
}

// -------------------------
// KALMAN FILTER IMPLEMENTATION
// -------------------------
// Applies a Kalman filter to a new combined weight measurement (in kg).
double applyKalmanFilter(double combined_measurement_kg) {
  // Prediction Step
  double P_predict = kalman_P_est_kg + KALMAN_Q;

  // Update Step (Correction)
  double K_k = P_predict / (P_predict + KALMAN_R); // Kalman Gain
  kalman_x_est_kg = kalman_x_est_kg + K_k * (combined_measurement_kg - kalman_x_est_kg); // Update estimate
  kalman_P_est_kg = (1 - K_k) * P_predict; // Update error covariance

  return kalman_x_est_kg;
}

// -------------------------
// RFID SYSTEM RESET
// -------------------------
// Reinitializes the RFID module (MFRC522) in case of persistent read failures.
void resetRfidModule() {
  SPI.end(); // End SPI bus to ensure a clean re-initialization
  delay(100); // Small delay
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN); // Re-initialize SPI bus
  mfrc522.PCD_Init(); // Re-initialize MFRC522
  mfrc522.PCD_SetAntennaGain(RFID_ANTENNA_GAIN); // Re-apply antenna gain
  DEBUG_PRINTLN("🔄 RFID module reset completed.");
}

// -------------------------
// GENERATE RANDOM RFID UID
// -------------------------
// Generates a random 8-character hexadecimal string to represent a random RFID UID.
String generateRandomRfid() {
  String randomUid = "";
  for (int i = 0; i < 8; i++) {
    byte r = random(0, 16); // Generate a random hex digit (0-F)
    if (r < 10) {
      randomUid += (char)('0' + r);
    } else {
      randomUid += (char)('A' + (r - 10));
    }
  }
  return randomUid;
}

// -------------------------
// INITIALIZE ALL HARDWARE COMPONENTS
// -------------------------
void initializeHardware() {
  Serial.begin(115200);
  delay(200); // Allow serial port to stabilize

  DEBUG_PRINTLN("\n--- Main MCU System Initializing ---");

  // Configure LED pin
  pinMode(DETECT_LED_PIN, OUTPUT);
  digitalWrite(DETECT_LED_PIN, LOW); // Ensure LED is off initially

  randomSeed(analogRead(0)); // Seed random number generator for uniqueness

  // Initialize I2C bus (for RTC and AHT20)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  DEBUG_PRINTLN("✅ I2C bus initialized.");

  // Initialize AHT20 sensor
  if (!aht.begin()) {
    DEBUG_PRINTLN("❌ ERROR: AHT20 initialization failed! Halting.");
    while (true); // Halt if sensor is critical for system operation
  }
  DEBUG_PRINTLN("✅ AHT20 ready.");

  // Initialize both HX711 weight sensors
  scale1.begin(HX711_DOUT1_PIN, HX711_SCK_PIN);
  scale2.begin(HX711_DOUT2_PIN, HX711_SCK_PIN);

  DEBUG_PRINTLN("Powering up scales and waiting for stabilization...");
  delay(2000); // Allow HX711s to power up and settle

  // Apply calibrated factors to both scales
  DEBUG_PRINTLN("Setting calibration factors based on constants in sketch...");
  scale1.set_scale(CALIBRATION_FACTOR_SCALE1);
  scale2.set_scale(CALIBRATION_FACTOR_SCALE2);
  DEBUG_PRINTF("Scale 1 factor: %.4f\n", CALIBRATION_FACTOR_SCALE1);
  DEBUG_PRINTF("Scale 2 factor: %.4f\n", CALIBRATION_FACTOR_SCALE2);

  // Tare (zero) both scales
  DEBUG_PRINTLN("Taring both scales (zeroing out current weight)...");
  scale1.tare(10); // Tare Scale 1, averaging 10 readings
  scale2.tare(10); // Tare Scale 2, averaging 10 readings
  DEBUG_PRINTLN("Scales tared. Ready for readings!");

  // Reset Kalman filter state after taring
  kalman_x_est_kg = 0.0;
  kalman_P_est_kg = 1.0;
  DEBUG_PRINTLN("Kalman filter reset.");

  // Initialize RTC
  if (!rtc.begin()) {
    DEBUG_PRINTLN("❌ ERROR: RTC failed to initialize! Halting.");
    while (true); // Halt if RTC is critical for timestamping
  }
  // Adjust RTC time to compile time (run once then comment out this line if you want RTC to keep time independently)
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  DEBUG_PRINTLN("✅ RTC ready.");

  // Initialize SD Card and RFID Reader SPI bus
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH); // Deselect SD card initially
  pinMode(RFID_CS_PIN, OUTPUT);
  digitalWrite(RFID_CS_PIN, HIGH); // Deselect RFID reader initially
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

  if (!SD.begin(SD_CS_PIN)) {
    DEBUG_PRINTLN("❌ ERROR: SD card initialization failed! Halting.");
    //while (true); // Halt if SD card is critical for logging
  }
  DEBUG_PRINTLN("✅ SD card ready.");

  // Generate unique filename for this session's log based on current timestamp
  DateTime now = rtc.now();
  char filename_buf[35]; // Buffer for "/LOG_YYYY-MM-DD_HH-MM-SS.txt"
  sprintf(filename_buf, "/LOG_%04d-%02d-%02d_%02d-%02d-%02d.txt",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  sessionLogFilename = String(filename_buf);
  DEBUG_PRINTF("✨ Session log file created: %s\n", sessionLogFilename.c_str());

  mfrc522.PCD_Init(); // Initialize MFRC522 RFID reader
  mfrc522.PCD_SetAntennaGain(RFID_ANTENNA_GAIN); // Set RFID antenna gain
  DEBUG_PRINTF("✅ RFID reader initialized (Firmware v%X).\n", mfrc522.PCD_ReadRegister(MFRC522::VersionReg));

  // Populate RFID to Sex map
  rfidSexMap["2A0D4602"] = "Female";
  rfidSexMap["FAE53502"] = "Male";
  rfidSexMap["7743C001"] = "Male";
  rfidSexMap["C376C001"] = "Female";
  DEBUG_PRINTLN("RFID-to-Sex map populated.");

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA); // Set WiFi to station mode for ESP-NOW operation
  if (esp_now_init() != ESP_OK) {
    DEBUG_PRINTLN("❌ ERROR: ESP-NOW initialization failed! Halting.");
    while (true); // Halt if ESP-NOW is critical for communication
  }
  DEBUG_PRINTLN("✅ ESP-NOW initialized.");

  esp_now_register_send_cb(onDataSent); // Register the callback for send status

  // Add peer (ESP32-CAM receiver) for ESP-NOW communication
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 0;      // Use default channel 0
  peerInfo.encrypt = false;  // No encryption for simplicity

  if (!esp_now_is_peer_exist(receiverMac)) {
    esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
    if (addPeerResult == ESP_OK) {
      DEBUG_PRINTLN("✅ ESP-NOW peer added successfully.");
    } else {
      DEBUG_PRINTF("❌ Failed to add ESP-NOW peer: %d\n", addPeerResult);
    }
  } else {
    DEBUG_PRINTLN("ℹ️ ESP-NOW peer already exists.");
  }

  DEBUG_PRINTLN("=== Main MCU System Ready ===");
}

// -------------------------
// SETUP FUNCTION (REQUIRED BY ARDUINO)
// -------------------------
void setup() {
  initializeHardware(); // Call your custom initialization function here
}

// -------------------------
// MAIN LOOP
// -------------------------
void loop() {
  // Continuously update weight measurements and Kalman filter
  updateWeightMeasurements();

  // Manage RFID detection and data processing
  manageRfidDetection();

  // New: Monitor weight to trigger image sending
  monitorWeightForImageSend();

  // Periodically print system status for debugging
  monitorAndPrintSystemStatus();

  // Small delay to prevent busy-waiting and allow other tasks
  delay(10);
}

// -------------------------
// WEIGHT MEASUREMENT & KALMAN FILTER UPDATE
// -------------------------
// Reads from both HX711 scales, combines readings, and updates the Kalman filter.
void updateWeightMeasurements() {
  if (scale1.is_ready() && scale2.is_ready()) {
    float weight1_grams = scale1.get_units(1); // Get 1 averaged reading from scale 1
    float weight2_grams = scale2.get_units(1); // Get 1 averaged reading from scale 2

    // Summing calibrated grams directly for combined measurement
    float combined_grams =  1.17 * (abs(weight1_grams) + abs(weight2_grams)) / 2.0;
    double kalman_measurement_kg = combined_grams / 1000.0; // Convert to kilograms for Kalman filter

    applyKalmanFilter(kalman_measurement_kg); // Update the Kalman filter's estimate
  }
}

// -------------------------
// RFID DETECTION AND PROCESSING
// -------------------------
// Manages the RFID scanning process, including cooldowns and error handling.
void manageRfidDetection() {
  unsigned long currentMillis = millis();

  // Enforce cooldown period between RFID read attempts
  if (currentMillis - lastRfidAttemptTime < RFID_READ_TIMEOUT) {
    return;
  }

  // If RFID has been failing for too long, attempt a reset
  if (rfidNeedsResetFlag && (currentMillis - lastRfidAttemptTime > RFID_FAIL_RESET_INTERVAL_MS)) {
    DEBUG_PRINTLN("🔄 RFID read failures detected. Attempting module reset...");
    resetRfidModule();
    rfidNeedsResetFlag = false; // Clear flag after reset attempt
    // After reset, we return and wait for the next loop iteration to try reading
    return;
  }

  lastRfidAttemptTime = currentMillis; // Update time of last RFID attempt

  // Check for new RFID card presence
  if (!mfrc522.PICC_IsNewCardPresent()) {
    rfidNeedsResetFlag = true; // Set flag if no card found (could be a temporary issue)
    // DEBUG_PRINTLN("🐞 No RFID card present."); // Uncomment for frequent "no card" debug
    return;
  }

  // Attempt to read the RFID card's serial number (UID)
  if (!mfrc522.PICC_ReadCardSerial()) {
    rfidNeedsResetFlag = true; // Set flag if reading fails
    DEBUG_PRINTLN("🐞 RFID card detected, but failed to read serial. Sending data with random RFID.");

    // NEW: If RFID card detected but failed to read serial, send data with random RFID
    String randomUid = generateRandomRfid();
    float temperature = aht.getTemperature();
    float humidity = aht.getHumidity();
    float final_weight_kg = (float)kalman_x_est_kg;

    if (fabs(final_weight_kg) < NO_WEIGHT_THRESHOLD_KG) {
      final_weight_kg = 0.0f;
    }
    if (final_weight_kg < 0.0f) {
      final_weight_kg = 0.0f;
    }
    if (isnan(temperature) || isnan(humidity)) {
      temperature = -99.9f;
      humidity = -99.9f;
    }

    sendEspNowData(randomUid, "Unknown", final_weight_kg, temperature, humidity);

    mfrc522.PICC_HaltA();       // Halt PICC
    mfrc522.PCD_StopCrypto1();  // Stop encryption
    return; // Return after attempting to send data with random RFID
  }

  // If we reach here, an RFID card was successfully detected and read.
  // Reset the failure flag as the read was successful.
  rfidNeedsResetFlag = false;

  // Extract RFID UID
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += '0'; // Add leading zero for single-digit hex
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase(); // Convert UID to uppercase for consistent mapping

  // Check if this is a new RFID scan (not just the same card still present within cooldown)
  if (currentMillis - lastRfidSuccessTime < RFID_COOLDOWN_MS && uid == currentUid) {
      // It's the same card within the cooldown period, ignore for now
      mfrc522.PICC_HaltA();      // Halt PICC
      mfrc522.PCD_StopCrypto1(); // Stop encryption
      return;
  }

  // This is a new, unique RFID detection
  currentUid = uid; // Store the current UID
  lastRfidSuccessTime = currentMillis; // Update last successful scan time
  digitalWrite(DETECT_LED_PIN, HIGH); // Turn on LED to indicate RFID detection

  handleSuccessfulRfidRead(uid); // Process the detected RFID data
}

// -------------------------
// HANDLE SUCCESSFUL RFID READ
// -------------------------
// Processes data after a successful RFID read, including weight stabilization,
// logging, and ESP-NOW transmission.
void handleSuccessfulRfidRead(const String& uid) {
  DEBUG_PRINTF("✅ RFID Detected: %s\n", uid.c_str());

  // Determine animal sex from the pre-defined map
  String sex = "Unknown";
  if (rfidSexMap.count(uid)) { // Check if UID exists in the map
    sex = rfidSexMap[uid];
  }

  // --- Wait for Kalman Filter to Settle ---
  // This pause ensures the weight reading stabilizes after an animal steps on the scale.
  DEBUG_PRINTF("Waiting %lu ms for Kalman filter to settle (RFID trigger)...\n", KALMAN_SETTLE_TIME_MS);
  unsigned long settleStartTime = millis();
  while (millis() - settleStartTime < KALMAN_SETTLE_TIME_MS) {
    // Keep feeding the Kalman filter with new measurements during this waiting period
    updateWeightMeasurements();
    delay(10); // Small delay to avoid busy-waiting
  }
  DEBUG_PRINTLN("Kalman filter settled (RFID trigger).");

  // --- Get Final Weight Measurement ---
  float final_weight_kg = (float)kalman_x_est_kg; // Get the stabilized filtered weight

  // Apply threshold and ensure non-negative weight for final output
  if (fabs(final_weight_kg) < NO_WEIGHT_THRESHOLD_KG) {
    final_weight_kg = 0.0f;
  }
  if (final_weight_kg < 0.0f) { // Ensure weight never goes negative in final output
    final_weight_kg = 0.0f;
  }

  // --- Read Temperature and Humidity ---
  float temperature = aht.getTemperature();
  float humidity = aht.getHumidity();

  // Validate AHT20 readings
  if (isnan(temperature) || isnan(humidity)) {
    DEBUG_PRINTLN("❌ WARNING: Failed to read temperature or humidity from AHT20. Skipping ESP-NOW send.");
    // Do not return here, still log RFID and weight if other data is bad
    temperature = -99.9f; // Indicate invalid temp (using float literal)
    humidity = -99.9f;   // Indicate invalid humid (using float literal)
  }

  // --- Prepare Data for SD Card Logging ---
  String logLine = "Timestamp: " + getFormattedTimestamp() +
                   " | RFID: " + uid +
                   " | Sex: " + sex +
                   " | Weight: " + String(final_weight_kg, 2) + "kg" +
                   " | Temp: " + String(temperature, 1) + "°C" +
                   " | Humid: " + String(humidity, 1) + "%";

  // --- Save Data to SD Card ---
  DEBUG_PRINTLN("\n" + logLine);
  if (!writeDataToLog(logLine)) {
    DEBUG_PRINTLN("❌ Failed to save data to SD card.");
  }

  // --- Send Data via ESP-NOW to ESP32-CAM ---
  sendEspNowData(uid, sex, final_weight_kg, temperature, humidity);

  // --- Halt RFID PICC and prepare for next scan ---
  mfrc522.PICC_HaltA();      // Halt PICC (Proximity Integrated Circuit Card)
  mfrc522.PCD_StopCrypto1(); // Stop encryption on PCD (Proximity Coupling Device)

  digitalWrite(DETECT_LED_PIN, LOW); // Turn off LED after processing
}

// -------------------------
// SEND DATA VIA ESP-NOW
// -------------------------
// Constructs a JSON payload and sends it via ESP-NOW to the receiver.
void sendEspNowData(const String& uid, const String& sex, float weight, float temperature, float humidity) {
  // Construct JSON Payload for ESP-NOW Transmission using snprintf for robustness
  int jsonLen = snprintf(jsonBuffer, JSON_BUFFER_SIZE,
                         "{\"timestamp\":\"%s\","
                         "\"rfid\":\"%s\","
                         "\"sex\":\"%s\","
                         "\"weight\":%.2f,"
                         "\"temperature\":%.1f,"
                         "\"humidity\":%.1f}",
                         getFormattedTimestamp().c_str(), // Get fresh timestamp for JSON
                         uid.c_str(),
                         sex.c_str(),
                         weight,
                         temperature,
                         humidity);

  // Check for snprintf errors (buffer overflow or encoding error)
  if (jsonLen < 0 || jsonLen >= JSON_BUFFER_SIZE) {
    DEBUG_PRINTLN("❌ ERROR: JSON payload too large for buffer or formatting error! Skipping ESP-NOW send.");
    return; // Skip ESP-NOW send if JSON is malformed or too big
  }

  DEBUG_PRINTLN("Attempting to send data via ESP-NOW...");
  DEBUG_PRINTF("JSON Data to be sent (length %d bytes): %s\n", jsonLen, jsonBuffer);

  // Send the char array. `jsonLen + 1` includes the null terminator.
  esp_err_t result = esp_now_send(receiverMac, (const uint8_t*)jsonBuffer, jsonLen + 1);

  if (result == ESP_OK) {
    DEBUG_PRINTLN("✅ Data send initiated via ESP-NOW.");
  } else {
    DEBUG_PRINTF("❌ Failed to initiate data send via ESP-NOW: %d\n", result);
  }
}

// -------------------------
// MONITOR WEIGHT FOR IMAGE SEND
// -------------------------
// New function: Triggers data send with random RFID if weight threshold is exceeded.
void monitorWeightForImageSend() {
  float current_filtered_weight = (float)kalman_x_est_kg;

  // Apply threshold to ensure non-negative weight for checking
  if (fabs(current_filtered_weight) < NO_WEIGHT_THRESHOLD_KG) {
    current_filtered_weight = 0.0f;
  }
  if (current_filtered_weight < 0.0f) {
    current_filtered_weight = 0.0f;
  }

  // If weight exceeds threshold and data hasn't been sent for this event yet
  if (current_filtered_weight >= WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG && !dataSentForWeightThreshold) {
    DEBUG_PRINTF("⚠️ Weight threshold %.2fkg exceeded! Triggering data send with random RFID.\n", WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG);

    // --- Wait for Kalman Filter to Settle (Weight Trigger) ---
    // This pause ensures the weight reading stabilizes after an animal steps on the scale.
    DEBUG_PRINTF("Waiting %lu ms for Kalman filter to settle (Weight trigger)...\n", KALMAN_SETTLE_TIME_MS);
    unsigned long settleStartTime = millis();
    while (millis() - settleStartTime < 2000) { // 2 seconds settling time
      updateWeightMeasurements(); // Keep feeding the Kalman filter with new measurements
      delay(10); // Small delay to avoid busy-waiting
    }
    DEBUG_PRINTLN("Kalman filter settled (Weight trigger).");

    String randomUid = generateRandomRfid();
    float temperature = aht.getTemperature();
    float humidity = aht.getHumidity();

    // Validate AHT20 readings
    if (isnan(temperature) || isnan(humidity)) {
      temperature = -99.9f;
      humidity = -99.9f;
    }

    sendEspNowData(randomUid, "Unknown", current_filtered_weight, temperature, humidity);
    dataSentForWeightThreshold = true; // Set flag to true to prevent repeated sends
  }
  // If weight drops below a reset threshold, allow re-triggering
  else if (current_filtered_weight < RESET_WEIGHT_THRESHOLD_FOR_IMAGE_SEND_KG && dataSentForWeightThreshold) {
    DEBUG_PRINTLN("ℹ️ Weight dropped below reset threshold. Ready for next weight event.");
    dataSentForWeightThreshold = false; // Reset flag
  }
}

// -------------------------
// MONITOR AND PRINT SYSTEM STATUS
// -------------------------
// Periodically prints key system status information to the Serial Monitor.
void monitorAndPrintSystemStatus() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastDebugPrintTime > DEBUG_INTERVAL_MS) {
    DEBUG_PRINTLN("\n=== SYSTEM STATUS ===");
    DEBUG_PRINTF("Uptime: %lu s\n", currentMillis / 1000);

    DEBUG_PRINT("Last RFID Success: ");
    if (lastRfidSuccessTime > 0) {
      DEBUG_PRINTF("%lu s ago\n", (currentMillis - lastRfidSuccessTime) / 1000);
    } else {
      DEBUG_PRINTLN("Never");
    }

    DEBUG_PRINTF("Current Filtered Weight: %.2f kg\n", kalman_x_est_kg);

    float temp = aht.getTemperature();
    float humid = aht.getHumidity();
    if (isnan(temp) || isnan(humid)) {
        DEBUG_PRINTLN("Temp/Humid: Sensor read error");
    } else {
      DEBUG_PRINTF("Temp: %.1f°C Humid: %.1f%%\n", temp, humid);
    }

    DEBUG_PRINT("SD Card Status: ");
    DEBUG_PRINTLN(SD.exists("/") ? "OK" : "ERROR"); // Check if root directory exists

    lastDebugPrintTime = currentMillis;
  }
}

// -------------------------
// INDIVIDUAL SCALE CALIBRATION FUNCTION (For Serial Debug)
// -------------------------
// This function allows for interactive calibration of each HX711 via Serial Monitor.
// It guides the user through placing known weights and calculates calibration factors.
// This is typically used during initial setup and tuning, not during normal operation.
void calibrateScale(HX711 &scale_obj, const String& scale_name) {
    Serial.print("\n--- Calibrating ");
    Serial.print(scale_name);
    Serial.println(" ---");
    Serial.println("⚙️ Remove all weight from this scale for tare...");
    delay(3000);

    scale_obj.tare(20); // Tare (zero) the scale, averaging 20 readings
    Serial.print("✔️ ");
    Serial.print(scale_name);
    Serial.println(" tared. The zero offset is now set.");
    Serial.print("Initial (tared) raw reading: ");
    Serial.println(scale_obj.get_value());

    float known_weights_grams[] = {750.0f, 2350.0f, 4450.0f}; // Define known weights for multi-point calibration
    int num_weights = sizeof(known_weights_grams) / sizeof(known_weights_grams[0]);

    Serial.println("\n--- Performing Multi-Point Calibration ---");
    Serial.println("Follow the prompts carefully. DO NOT move the scale during this process.");
    Serial.println("Record each 'Calculated Factor for THIS weight' to average later.");

    for (int i = 0; i < num_weights; ++i) {
        float current_known_weight_g = known_weights_grams[i];
        Serial.print("\nSTEP ");
        Serial.print(i + 1);
        Serial.print("/3: Place EXACTLY ");
        Serial.print(current_known_weight_g);
        Serial.println(" grams on the scale. Press Enter in Serial Monitor when stable.");

        // Wait for user to place weight and press Enter
        while (Serial.available() == 0) {
            Serial.print(".");
            delay(500);
        }
        String dummy = Serial.readStringUntil('\n'); // Consume the Enter keypress
        while (Serial.available()) Serial.read(); // Clear any remaining characters in the input buffer

        Serial.print("Reading average value with ");
        Serial.print(current_known_weight_g);
        Serial.println(" grams... (Please wait)");
        delay(2000); // Give physical weight time to settle on the scale

        // Get an average reading of the 'units' (raw value minus tare offset) with the known weight
        float units_with_weight = scale_obj.get_value(50); // Average 50 readings for stability
        Serial.print("Units (raw - offset) with ");
        Serial.print(current_known_weight_g);
        Serial.print("g: ");
        Serial.println(units_with_weight);

        if (units_with_weight != 0 && current_known_weight_g > 0) {
            float calculated_cal_factor = units_with_weight / current_known_weight_g;
            Serial.print("✅ Calculated Factor for THIS weight (");
            Serial.print(current_known_weight_g);
            Serial.print("g): ");
            Serial.println(calculated_cal_factor, 4); // Print with 4 decimal places for precision
        } else {
            Serial.println("❌ Calculation failed for this weight. Ensure weight is positive and reading is non-zero after tare.");
        }
        Serial.println("--- Remove weight to prepare for next step ---");
        delay(2000); // Pause to allow weight removal
    }

    Serial.println("\n--- Multi-Point Calibration Data Collected ---");
    Serial.println("⚠️ IMPORTANT: Manually AVERAGE the 3 'Calculated Factor for THIS weight' values you recorded for this scale.");
    Serial.println("   (Example: If factors were -104500, -104600, -104450, your average would be -104516.67)");
    Serial.println("   Use this averaged value to update CALIBRATION_FACTOR_SCALE1 (or 2) at the top of your sketch!");
    Serial.println("   Then RE-UPLOAD the sketch with the updated factors for proper operation.");

    // For immediate testing after calibration, temporarily set the scale using the factor from the last known weight.
    float last_units_with_weight = scale_obj.get_value(20);
    if(last_units_with_weight != 0 && known_weights_grams[num_weights - 1] > 0){
        scale_obj.set_scale(last_units_with_weight / known_weights_grams[num_weights - 1]);
        Serial.print("\nTemporary set_scale for testing: ");
        Serial.println(last_units_with_weight / known_weights_grams[num_weights - 1], 4);
    } else {
        scale_obj.set_scale(1.0f); // Fallback to 1.0 if calculation is not possible
    }
    Serial.println("--- Calibration Process Complete for " + scale_name + " ---");
}

// -------------------------
// SERIAL EVENT HANDLER (for calibration commands)
// -------------------------
// This function is called automatically when data is available in the Serial buffer.
void serialEvent() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim(); // Remove leading/trailing whitespace

        if (command == "calibrate1") {
            calibrateScale(scale1, "Scale 1");
            // After re-calibration, re-tare both scales for consistent zeroing
            Serial.println("\nRe-taring both scales after calibration...");
            scale1.tare(10);
            scale2.tare(10);
            // Also reset Kalman filter to start fresh with new calibration
            kalman_x_est_kg = 0.0;
            kalman_P_est_kg = 1.0;
            Serial.println("Scales re-tared and Kalman filter reset.");
        } else if (command == "calibrate2") {
            calibrateScale(scale2, "Scale 2");
            // After re-calibration, re-tare both scales for consistent zeroing
            Serial.println("\nRe-taring both scales after calibration...");
            scale1.tare(10);
            scale2.tare(10);
            // Also reset Kalman filter to start fresh with new calibration
            kalman_x_est_kg = 0.0;
            kalman_P_est_kg = 1.0;
            Serial.println("Scales re-tared and Kalman filter reset.");
        } else {
            Serial.print("Unknown command: ");
            Serial.println(command);
        }
        // Clear any remaining characters in the serial buffer
        while (Serial.available()) Serial.read();
    }
}