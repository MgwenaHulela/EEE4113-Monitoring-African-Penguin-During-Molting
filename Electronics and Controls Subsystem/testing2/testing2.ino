#include <Wire.h>
#include <Arduino.h>
#include <HX711.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include <RTClib.h>
#include <map> // Include for std::map
#include <esp_now.h> // Include for ESP-NOW functionality
#include <WiFi.h>    // Include for WiFi functionality (required by ESP-NOW)
// #include "AHT20.h" // Commented out: Environmental sensor library

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

HX711 scale;
RTC_DS3231 rtc;
MFRC522 mfrc522(RFID_CS, RST_PIN);
// AHT20 aht; // Commented out: Environmental sensor object

// Calibration and configuration
const float a1 = 0.0045558f;
const float a0 = -1862.8f;
const float NO_WEIGHT_THRESHOLD = 10.0f;
const unsigned long COOLDOWN_MS = 500;
const int SMA_WINDOW = 5;

long weightBuffer[SMA_WINDOW];
int weightIndex = 0;
bool bufferFilled = false;
unsigned long lastScanTime = 0;

// Pre-stored RFID to Sex mapping
// This map stores RFID UIDs as keys and their corresponding sex as values.
// You can expand this map with more RFID UIDs and their associated sex data.
std::map<String, String> rfidSexMap;

// Define the structure for data to be sent via ESP-NOW
// This structure must be the same on the receiving ESP32 board
typedef struct struct_message {
  char timestamp[20]; //YYYY-MM-DD HH:MM:SS
  char rfid[20];
  char sex[10];
  float weight;
  float temperature;
  float humidity;
  // int light;    // Removed: Placeholder for light sensor data
  int pressure; // Placeholder, set to 0 as not measured by current sensors
} struct_message;

// Create a struct_message to hold sensor data
struct_message sensorData;

// MAC address of the receiving ESP32 board
// IMPORTANT: This is the MAC address you provided for the receiver (ESP32-CAM)
uint8_t receiverMac[] = {0xA0, 0xA3, 0xB3, 0x2B, 0xCB, 0x40};

// Callback function for ESP-NOW send status
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

String getTimestamp() {
  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

String getFilenameFromTimestamp() {
  DateTime now = rtc.now();
  char filename[30];
  sprintf(filename, "/%04d-%02d-%02d_%02d-%02d-%02d.txt",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(filename);
}

bool writeLog(const String &line) {
  digitalWrite(RFID_CS, HIGH); // Ensure RFID CS is high before accessing SD
  digitalWrite(SD_CS, LOW);    // Select SD card

  String filename = getFilenameFromTimestamp();
  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    digitalWrite(SD_CS, HIGH); // Deselect SD card on failure
    Serial.println("❌ ERROR: log file open failed");
    return false;
  }

  f.println(line);
  f.close();
  digitalWrite(SD_CS, HIGH); // Deselect SD card after write
  return true;
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
  return (weight < NO_WEIGHT_THRESHOLD) ? 0 : weight;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(DETECT_LED, OUTPUT);
  digitalWrite(DETECT_LED, LOW);

  // Seed the random number generator with an analog read from an unconnected pin
  // This helps ensure different random numbers each time the ESP32 starts.
  randomSeed(analogRead(0));

  // Initialize I2C bus - CRITICAL for RTC communication
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("✅ I2C bus initialized."); // Debug line: I2C initialization status

  // Initialize AHT20 - Commented out
  // if(!aht.begin()) {
  //   Serial.println("❌ ERROR: AHT20 initialization failed");
  //   while(1);
  // }
  // Serial.println("✅ AHT20 ready");

  // Initialize HX711
  scale.begin(DOUT_PIN, CLK_PIN);
  if (!scale.is_ready()) {
    Serial.println("❌ ERROR: HX711 not ready");
    //while(1); // Consider if you want to halt here or continue without scale
  }
  Serial.println("⚙️ Auto-taring (5s)...");
  scale.tare();
  delay(5000); // Give time for tare to settle
  Serial.println("✔️ Tare complete");

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("❌ ERROR: RTC failed");
    while(1); // Halt if RTC is critical
  }
  // Adjust RTC time with the compile time (computer's time)
  // This line will set the RTC to the time the sketch was compiled.
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println("✅ RTC ready");

  // Initialize SD and RFID
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH); // Ensure SD_CS is high initially
  pinMode(RFID_CS, OUTPUT); digitalWrite(RFID_CS, HIGH); // Ensure RFID_CS is high initially
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); // Initialize SPI bus

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ ERROR: SD card init failed");
    while(1); // Halt if SD card is critical
  }
  Serial.println("✅ SD card ready");

  mfrc522.PCD_Init(); // Initialize MFRC522
  Serial.println("✅ RFID ready");
  Serial.println("=== System Operational ===");

  // Populate the RFID to Sex map
  // These are example RFID UIDs from your provided log.
  // Add more entries as needed.
  rfidSexMap["2A0D4602"] = "Female";
  rfidSexMap["FAE53502"] = "Male";
  rfidSexMap["7743C001"] = "Male";
  rfidSexMap["C376C001"] = "Female";
  // Add more RFID UIDs and their corresponding sex here
  // Example: rfidSexMap["NEW_RFID_UID"] = "Unknown";

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA); // Set WiFi to station mode
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return; // Halt if ESP-NOW fails to initialize
  }
  Serial.println("✅ ESP-NOW initialized"); // Debug line: ESP-NOW initialization status

  // Register the ESP-NOW send callback function
  esp_now_register_send_cb(OnDataSent);

  // Add peer (receiver)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 0;     // Use channel 0 (default)
  peerInfo.encrypt = false; // No encryption
  
  if (!esp_now_is_peer_exist(receiverMac)) {
    esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
    if (addPeerResult == ESP_OK) {
      Serial.println("✅ ESP-NOW peer added successfully."); // Debug line: Peer added
    } else {
      Serial.print("❌ Failed to add ESP-NOW peer: "); // Debug line: Peer add failure
      Serial.println(addPeerResult);
    }
  } else {
    Serial.println("ℹ️ ESP-NOW peer already exists."); // Debug line: Peer already exists
  }
}

void loop() {
  unsigned long now = millis();
  if (now - lastScanTime < COOLDOWN_MS) {
    // Return if still in cooldown period after a previous scan
    return;
  }

  // Check for new RFID card
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Read the RFID card serial number
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  lastScanTime = now; // Update last scan time
  digitalWrite(DETECT_LED, HIGH); // Turn on LED to indicate detection

  // Read RFID UID
  String uid;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += '0'; // Add leading zero for single-digit hex
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase(); // Convert UID to uppercase

  // Retrieve sex data from map
  String sex = "Unknown"; // Default value if RFID not found
  if (rfidSexMap.count(uid)) { // Check if UID exists in the map
    sex = rfidSexMap[uid];
  }

  // Read timestamp
  String ts = getTimestamp();

  // Generate random sensor data for ESP-NOW transmission and SD card logging
  // These ranges are based on the new snippet provided.
  float random_weight = 3.0 + (float)random(0, 20) / 10.0; // Random weight between 3.0 and 4.9
  float random_temperature = 20.0 + (float)random(0, 100) / 10.0; // Random temp between 20.0 and 29.9
  float random_humidity = 40.0 + (float)random(0, 300) / 10.0; // Random humid between 40.0 and 69.9

  // Get actual weight from scale for local logging (optional, if you want to keep scale data in log)
  // If you want the SD log to also use the random_weight, comment out the line below
  // and use random_weight for the 'line' string.
  float actual_smoothed_weight = getSmoothedWeight(); 

  // Generate filename for image (if an image capture system were present)
  String imageName = ts;
  imageName.replace(" ", "_"); // Replace spaces with underscores
  imageName.replace(":", "-"); // Replace colons with hyphens
  String imageFile = imageName + ".jpg";

  // Create log entry string for SD card, using actual RFID/Sex/Timestamp
  // and the newly generated random sensor data (including random weight for consistency with ESP-NOW)
  String line = "Timestamp: " + ts +
                " | RFID: " + uid +
                " | Sex: " + sex +
                " | Weight: " + String(random_weight, 1) + "g" + // Using random weight for log
                " | Temp: " + String(random_temperature, 1) + "°C" +
                " | Humid: " + String(random_humidity, 1) + "%" +
                " | Image: " + imageFile;

  // Save data to SD card
  Serial.println("\n" + line);
  if (writeLog(line)) {
    Serial.println("📁 Saved to SD card");
  }

  // Populate the ESP-NOW struct with real data (RFID, Sex, Timestamp)
  // and the newly generated random sensor data (Weight, Temp, Humidity)
  strncpy(sensorData.timestamp, ts.c_str(), sizeof(sensorData.timestamp) - 1);
  sensorData.timestamp[sizeof(sensorData.timestamp) - 1] = '\0'; // Ensure null termination

  strncpy(sensorData.rfid, uid.c_str(), sizeof(sensorData.rfid) - 1);
  sensorData.rfid[sizeof(sensorData.rfid) - 1] = '\0'; // Ensure null termination

  strncpy(sensorData.sex, sex.c_str(), sizeof(sensorData.sex) - 1);
  sensorData.sex[sizeof(sensorData.sex) - 1] = '\0'; // Ensure null termination

  sensorData.weight = random_weight;
  sensorData.temperature = random_temperature;
  sensorData.humidity = random_humidity;
  // sensorData.light = 0;    // Removed: Light sensor data
  sensorData.pressure = 0; // Set to 0 as not measured

  // Send data via ESP-NOW
  Serial.println("Attempting to send data via ESP-NOW..."); // Debug line: Before sending
  esp_err_t result = esp_now_send(receiverMac, (uint8_t *)&sensorData, sizeof(sensorData));
  if (result == ESP_OK) {
    Serial.println("✅ Data send initiated via ESP-NOW."); // Debug line: Send initiated
    Serial.print("Data sent via ESP-NOW - Temp: ");
    Serial.print(sensorData.temperature);
    Serial.print("°C, Humidity: ");
    Serial.print(sensorData.humidity);
    Serial.print("%, Weight: ");
    Serial.print(sensorData.weight);
    Serial.println("g");
  } else {
    Serial.print("❌ Failed to initiate data send via ESP-NOW: ");
    Serial.println(result);
  }

  // Halt PICC and prepare for next scan
  mfrc522.PICC_HaltA();
  // Stop encryption on PCD
  mfrc522.PCD_StopCrypto1();

  // Turn off LED immediately after processing the card.
  // The COOLDOWN_MS will prevent immediate re-scanning.
  digitalWrite(DETECT_LED, LOW); // Turn off LED
}
