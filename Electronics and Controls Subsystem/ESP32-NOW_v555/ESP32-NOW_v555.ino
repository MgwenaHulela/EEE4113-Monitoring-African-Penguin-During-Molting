#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "zwivhuya";


const char* password = "12345678";
const char* serverUrl = "http://192.168.110.124:5000/api/sensor";

// LED pin
#define LED_PIN 33

// Data management
bool dataReceived = false;
String receivedJsonData = "";

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    Serial.println("\nDEBUG: ESP-NOW Data Received");
    Serial.printf("Raw length: %d bytes\n", len);

    // Create null-terminated string
    char* jsonStr = new char[len + 1];
    memcpy(jsonStr, data, len);
    jsonStr[len] = '\0';
    
    String receivedString(jsonStr);
    delete[] jsonStr;

    // Sanitize JSON
    int jsonStart = receivedString.indexOf('{');
    int jsonEnd = receivedString.lastIndexOf('}') + 1;
    
    if (jsonStart == -1 || jsonEnd == 0) {
        Serial.println("❌ Invalid JSON structure");
        return;
    }

    String validJson = receivedString.substring(jsonStart, jsonEnd);
    Serial.printf("Sanitized JSON length: %d\n", validJson.length());

    // Validate JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, validJson);
    
    if (error) {
        Serial.printf("❌ JSON parse error: %s\n", error.c_str());
        return;
    }

    // Validate required fields
    if (!doc.containsKey("timestamp") || 
        !doc.containsKey("rfid") ||
        !doc.containsKey("weight")) {
        Serial.println("❌ Missing required fields");
        return;
    }

    receivedJsonData = validJson;
    dataReceived = true;
    
    // Visual feedback
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    
    Serial.println("✅ Valid JSON received");
}

void setup() {
    Serial.begin(115200);
    delay(100);
    pinMode(LED_PIN, OUTPUT);
    
    WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed");
        while(1);
    }
    
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("✅ System Ready");
}

void sendToServer() {
    if (!receivedJsonData.length()) return;

    WiFi.begin(ssid, password);
    unsigned long start = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");
        
        Serial.printf("Sending JSON (%d bytes): %s\n", 
                     receivedJsonData.length(),
                     receivedJsonData.c_str());
        
        int httpCode = http.POST(receivedJsonData);
        String response = http.getString();
        
        Serial.printf("Server response: %d\n", httpCode);
        Serial.println(response);
        
        http.end();
        WiFi.disconnect(true);
    }
    
    // Re-init ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) Serial.println("ESP-NOW Reinit Warning");
    dataReceived = false;
    receivedJsonData = "";
}

void loop() {
    if (dataReceived) {
        sendToServer();
    }
    delay(10);
}