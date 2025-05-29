#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Wi-Fi credentials
const char* ssid = "zwivhuya"; //network name
const char* password = "12345678";  // Replace with actual password

// Flask server endpoint
const char* serverUrl = "http://192.168.51.124:5000/api/sensor";

// Flash LED pin (GPIO 33 on ESP32-CAM)
#define LED_PIN 33

// Struct for received data - MUST MATCH SENDER'S STRUCT EXACTLY
typedef struct struct_message {
  char timestamp[20]; //YYYY-MM-DD HH:MM:SS
  char rfid[20];
  char sex[10];
  float weight;
  float temperature;
  float humidity;
  int light;
  int pressure;
} struct_message;

struct_message incomingData;
bool dataReceived = false; // Flag for actual ESP-NOW reception

// Dummy data for testing server communication
struct_message dummySensorData = {
  "2025-05-21 18:00:00", // Dummy Timestamp
  "DUMMYRFID1234567",    // Dummy RFID
  "Test",                // Dummy Sex
  500.0,                 // Dummy Weight
  25.5,                  // Dummy Temperature
  60.0,                  // Dummy Humidity
  500,                   // Dummy Light
  1012                   // Dummy Pressure
};

// Interval for sending dummy data (in milliseconds)
const unsigned long DUMMY_SEND_INTERVAL_MS = 10000; // Send every 10 seconds
unsigned long lastDummySendTime = 0;

// Callback function for received ESP-NOW data
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.println("DEBUG: OnDataRecv entered.");
  // Ensure the received data length matches the expected struct size
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, data, sizeof(incomingData));
    Serial.println("✅ ESP-NOW data received:");
    Serial.printf("Timestamp: %s\n", incomingData.timestamp);
    Serial.printf("RFID: %s\n", incomingData.rfid);
    Serial.printf("Sex: %s\n", incomingData.sex);
    Serial.printf("Weight: %.1fg\n", incomingData.weight);
    Serial.printf("Temperature: %.1f°C\n", incomingData.temperature);
    Serial.printf("Humidity: %.1f%%\n", incomingData.humidity);
    Serial.printf("Light: %d\n", incomingData.light);
    Serial.printf("Pressure: %d\n", incomingData.pressure);

    // Flash the LED
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    dataReceived = true; // Set flag to process data in loop()
  } else {
    Serial.printf("❌ ESP-NOW data length mismatch! Expected %d, got %d\n", sizeof(struct_message), len);
  }
  Serial.println("DEBUG: OnDataRecv exited.");
}

void setup() {
  Serial.begin(115200);
  delay(100); // Give serial a moment to initialize
  Serial.println("DEBUG: Starting setup()...");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("DEBUG: LED pin initialized.");

  WiFi.mode(WIFI_STA);  // Important: must be in station mode for ESP-NOW
  Serial.println("DEBUG: WiFi mode set to STATION.");

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    // Consider adding a delay or loop here if initialization failure is critical
    return; // Halt setup if ESP-NOW fails
  }
  Serial.println("✅ ESP-NOW initialized.");

  // Register the ESP-NOW receive callback
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("✅ ESP-NOW receive callback registered.");
  Serial.println("✅ ESP32-CAM Ready to receive ESP-NOW data.");
  Serial.println("DEBUG: Setup() complete.");
}

void loop() {
  unsigned long currentMillis = millis();

  // Periodically send dummy data to the server for testing
  if (currentMillis - lastDummySendTime >= DUMMY_SEND_INTERVAL_MS) {
    lastDummySendTime = currentMillis;
    Serial.println("\nDEBUG: Sending dummy data to server (simulating ESP-NOW reception).");

    // Copy dummy data into incomingData to simulate reception
    memcpy(&incomingData, &dummySensorData, sizeof(dummySensorData));
    dataReceived = true; // Set flag to trigger the send logic
  }

  // Check if data has been received via ESP-NOW OR if dummy data needs to be sent
  if (dataReceived) {
    Serial.println("DEBUG: Data ready for processing (either real ESP-NOW or dummy data).");
    dataReceived = false; // Reset flag immediately after deciding to process

    // Connect to Wi-Fi to send data to Flask server
    Serial.print("DEBUG: Attempting to connect to WiFi... SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();

    // Wait for WiFi connection with a timeout
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
      Serial.print(".");
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi connected!");
      Serial.print("DEBUG: Connected to WiFi: ");
      Serial.println(WiFi.SSID()); // Display the connected SSID
      Serial.print("📶 IP address: ");
      Serial.println(WiFi.localIP());

      // Send the data to Flask server
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");

      String payload = "{";
      payload += "\"timestamp\":\"" + String(incomingData.timestamp) + "\",";
      payload += "\"rfid\":\"" + String(incomingData.rfid) + "\",";
      payload += "\"sex\":\"" + String(incomingData.sex) + "\",";
      payload += "\"weight\":" + String(incomingData.weight) + ",";
      payload += "\"temperature\":" + String(incomingData.temperature) + ",";
      payload += "\"humidity\":" + String(incomingData.humidity) + ",";
      payload += "\"light\":" + String(incomingData.light) + ",";
      payload += "\"pressure\":" + String(incomingData.pressure);
      payload += "}";

      Serial.println("DEBUG: JSON Payload: " + payload); // Debug print the payload

      int httpResponseCode = http.POST(payload);

      if (httpResponseCode > 0) {
        Serial.printf("✅ Data sent! Server responded with code: %d\n", httpResponseCode);
        String response = http.getString();
        Serial.println("📥 Response: " + response);
      } else {
        Serial.printf("❌ Failed to send data. HTTP error: %s\n", http.errorToString(httpResponseCode).c_str());
      }

      http.end(); // Close HTTP connection

      // Disconnect from Wi-Fi to save power and prepare for ESP-NOW again
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF); // Turn off WiFi radio completely
      Serial.println("📴 WiFi disconnected\n");

      // Reinitialize ESP-NOW after Wi-Fi shutdown
      // This is crucial because WiFi.mode(WIFI_OFF) also disables ESP-NOW
      WiFi.mode(WIFI_STA); // Set back to station mode for ESP-NOW
      if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error re-initializing ESP-NOW after WiFi disconnect.");
      } else {
        Serial.println("✅ ESP-NOW re-initialized after WiFi disconnect.");
        esp_now_register_recv_cb(OnDataRecv); // Re-register the callback
        Serial.println("✅ ESP-NOW receive callback re-registered.");
      }

    } else {
      Serial.println("\n❌ WiFi connection failed");
      // If WiFi fails, ensure ESP-NOW is still active
      WiFi.mode(WIFI_STA); // Set back to station mode for ESP-NOW
      if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error re-initializing ESP-NOW after WiFi connect fail.");
      } else {
        Serial.println("✅ ESP-NOW re-initialized after WiFi connect fail.");
        esp_now_register_recv_cb(OnDataRecv); // Re-register the callback
        Serial.println("✅ ESP-NOW receive callback re-registered.");
      }
    }
  }
  // Small delay to prevent watchdog timer issues if loop runs too fast without processing data
  delay(10);
}
