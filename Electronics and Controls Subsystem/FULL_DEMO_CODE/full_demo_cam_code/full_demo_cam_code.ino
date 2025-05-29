#include <esp_now.h>      // ESP-NOW communication library
#include <WiFi.h>         // WiFi connectivity
#include <WiFiClient.h>   // HTTP client
#include <HTTPClient.h>   // HTTP POST requests
#include "esp_camera.h"   // Camera functions
#include <ArduinoJson.h>  // JSON parsing and generation
#include "Base64.h"       // Base64 encoding for image

// --- Wi-Fi Credentials ---
const char* WIFI_SSID = "Inno";
const char* WIFI_PASSWORD = "11112211";

// --- Flask Server URL ---
const char* FLASK_SERVER_URL = "http://172.20.10.3:5000/api/esp32-detection";

// --- Pin Definitions ---
#define LED_FLASH_PIN 4      // Flash LED on AI Thinker ESP32-CAM
#define STATUS_LED_PIN 33    // Status LED pin
#define LIGHT_SENSOR_PIN 34  // Analog light sensor input

// --- Camera Pin Definitions (AI Thinker ESP32-CAM) ---
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// --- Global Variables ---
String g_receivedTimestamp = "";
String g_receivedRfid = "";
String g_receivedSex = "";
float g_receivedWeight = 0.0f;
float g_receivedTemperature = 0.0f;
float g_receivedHumidity = 0.0f;
int g_lightSensorValue = 0;
float g_dummyPressure = 1013.25f; // Example pressure placeholder

bool g_newDataReceived = false; // Flag set by ESP-NOW callback

// --- ESP-NOW Receive Callback ---
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, (const char*)data, len);

  if (error) {
    Serial.print("❌ ERROR: JSON deserialization failed: ");
    Serial.println(error.f_str());
    return;
  }

  Serial.println("\n--- ESP-NOW Data Received ---");
  g_receivedTimestamp   = doc["timestamp"].as<String>();
  g_receivedRfid        = doc["rfid"].as<String>();
  g_receivedSex         = doc["sex"].as<String>();
  g_receivedWeight      = doc["weight"].as<float>();
  g_receivedTemperature = doc["temperature"].as<float>();
  g_receivedHumidity    = doc["humidity"].as<float>();

  Serial.printf("[INFO] Timestamp: %s\n", g_receivedTimestamp.c_str());
  Serial.printf("[INFO] RFID: %s\n", g_receivedRfid.c_str());
  Serial.printf("[INFO] Sex: %s\n", g_receivedSex.c_str());
  Serial.printf("[INFO] Weight: %.2f kg\n", g_receivedWeight);
  Serial.printf("[INFO] Temperature: %.1f C\n", g_receivedTemperature);
  Serial.printf("[INFO] Humidity: %.1f %%\n", g_receivedHumidity);

  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(100);
  digitalWrite(STATUS_LED_PIN, LOW);

  g_newDataReceived = true;
}

// --- Initialize Camera ---
bool initializeCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println("[INFO] PSRAM detected, using SVGA resolution.");
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    Serial.println("[INFO] No PSRAM detected, using QVGA resolution.");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ ERROR: Camera init failed with error 0x%x\n", err);
    return false;
  }
  Serial.println("✅ Camera initialized.");
  return true;
}

// --- Connect to WiFi ---
bool connectToWiFi() {
  Serial.printf("[INFO] Connecting to WiFi SSID: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.printf("[INFO] IP Address: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    Serial.println("\n❌ WiFi connection failed.");
    return false;
  }
}

// --- Disconnect WiFi ---
void disconnectFromWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[INFO] WiFi disconnected.");
}

// --- Setup ESP-NOW Receiver ---
void setupEspNowReceiver() {
  WiFi.mode(WIFI_STA);
  Serial.println("[INFO] Initializing ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ERROR: ESP-NOW init failed! Restarting...");
    delay(5000);
    ESP.restart();
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("✅ ESP-NOW initialized.");
}

// --- Send Data + Image to Flask Server ---
void sendToServer(camera_fb_t* fb) {
  if (!fb || fb->len == 0) {
    Serial.println("[ERROR] Invalid camera frame.");
    return;
  }

  g_lightSensorValue = analogRead(LIGHT_SENSOR_PIN);
  Serial.printf("[INFO] Light sensor reading: %d\n", g_lightSensorValue);

  // Base64 encode image (without the "data:image/jpeg;base64," prefix if your server expects just raw base64)
  String imageBase64 = base64::encode(fb->buf, fb->len);
  // If your server requires "image_base64" key, use that key name exactly.
  // Here, we're using "image" as the key — adjust on the server side accordingly.
  imageBase64 = "data:image/jpeg;base64," + imageBase64;

  // Construct JSON payload string manually
  String jsonPayload = "{";
  jsonPayload += "\"timestamp\":\"" + g_receivedTimestamp + "\",";
  jsonPayload += "\"rfid\":\"" + g_receivedRfid + "\",";
  jsonPayload += "\"sex\":\"" + g_receivedSex + "\",";
  jsonPayload += "\"weight\":" + String(g_receivedWeight, 2) + ",";
  jsonPayload += "\"temperature\":" + String(g_receivedTemperature, 1) + ",";
  jsonPayload += "\"humidity\":" + String(g_receivedHumidity, 1) + ",";
  jsonPayload += "\"light\":" + String(g_lightSensorValue) + ",";
  jsonPayload += "\"pressure\":" + String(g_dummyPressure, 2) + ",";
  jsonPayload += "\"image_base64\":\"" + imageBase64 + "\"";  // Use key image_base64 to match your Flask error
  jsonPayload += "}";

  Serial.println("[INFO] JSON payload:");
  Serial.println(jsonPayload);

  WiFiClient client;
  HTTPClient http;

  http.begin(client, FLASK_SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  Serial.println("[INFO] Sending HTTP POST...");
  int httpCode = http.POST(jsonPayload);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("[INFO] Server response (code %d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("❌ ERROR: HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(LED_FLASH_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(LED_FLASH_PIN, LOW);

  Serial.println("\n--- ESP32-CAM Receiver Starting ---");

  if (!initializeCamera()) {
    Serial.println("❌ Camera initialization failed, halting.");
    while (true) delay(1000);
  }

  setupEspNowReceiver();
}

// --- Arduino Loop ---
void loop() {
  if (g_newDataReceived) {
    g_newDataReceived = false;

    Serial.println("\n--- New ESP-NOW data ready ---");

    if (connectToWiFi()) {
      digitalWrite(LED_FLASH_PIN, HIGH);
      delay(100);
      camera_fb_t* fb = esp_camera_fb_get();
      digitalWrite(LED_FLASH_PIN, LOW);

      if (fb) {
        sendToServer(fb);
        esp_camera_fb_return(fb);
      } else {
        Serial.println("❌ ERROR: Camera capture failed.");
      }

      disconnectFromWiFi();
    } else {
      Serial.println("❌ WiFi connection failed. Skipping data send.");
    }

    // Re-enable ESP-NOW receiver after WiFi changes
    setupEspNowReceiver();
  }
  delay(10);
}