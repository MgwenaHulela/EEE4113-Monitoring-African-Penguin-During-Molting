#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_camera.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h> // Make sure base64.h and base64.cpp are in your sketch folder

// Camera pin definitions for AI Thinker ESP32-CAM
#define PWDN_GPIO_NUM       32
#define RESET_GPIO_NUM      -1 // -1 for OV2640 on AI-Thinker ESP32-CAM
#define XCLK_GPIO_NUM        0
#define SIOD_GPIO_NUM       26
#define SIOC_GPIO_NUM       27
#define Y9_GPIO_NUM         35
#define Y8_GPIO_NUM         34
#define Y7_GPIO_NUM         39
#define Y6_GPIO_NUM         36
#define Y5_GPIO_NUM         21
#define Y4_GPIO_NUM         19
#define Y3_GPIO_NUM         18
#define Y2_GPIO_NUM          5
#define VSYNC_GPIO_NUM      25
#define HREF_GPIO_NUM       23
#define PCLK_GPIO_NUM       22

// Define onboard flash LED pin
#define LED_FLASH 4 // GPIO 4 on ESP32-CAM for flash LED

// Wi-Fi credentials
const char* ssid = "zwivhuya";      // Your Wi-Fi SSID
const char* password = "12345678"; // Your Wi-Fi Password

// Flask server endpoint
const char* serverURL = "http://192.168.110.124:5000/api/sensor"; // ENSURE THIS IP IS CORRECT FOR YOUR FLASK SERVER

// Global flags and data storage
bool wifiConnected = false;
bool newDataAvailable = false;      // Flag to signal new ESP-NOW data is ready for processing

// ArduinoJson document for incoming data AND for building the combined payload to send to Flask
// DynamicJsonDocument size should be ample for your data + base64 image.
// For SVGA (800x600) JPEG, images can be 30KB - 80KB+. A safe bet is 128KB (131072 bytes) or more.
// Be aware of memory limits on your ESP32-CAM (PSRAM is crucial for large resolutions).
DynamicJsonDocument combinedJsonDoc(131072); // Increased size for robust image handling


// --- Function Prototypes ---
void connectToWiFi();
void initCamera();
camera_fb_t* captureImage();
void sendDataToServer(); // Now uses the global combinedJsonDoc
void printLocalMAC();
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);


// --- Function Implementations ---

void initCamera() {
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
    config.pin_sscb_sda = SIOD_GPIO_NUM; // SCCB (I2C) data line for camera
    config.pin_sscb_scl = SIOC_GPIO_NUM; // SCCB (I2C) clock line for camera
    config.pin_pwdn = PWDN_GPIO_NUM;     // Power down pin
    config.pin_reset = RESET_GPIO_NUM;   // Reset pin
    config.xclk_freq_hz = 20000000;      // 20MHz clock frequency
    config.frame_size = FRAMESIZE_SVGA;  // 800x600 resolution (adjust if memory issues, try VGA or QVGA)
    config.pixel_format = PIXFORMAT_JPEG; // JPEG format for compression
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Grab next frame immediately
    config.fb_location = CAMERA_FB_IN_PSRAM;   // Store frame buffer in PSRAM (required for larger resolutions)
    config.jpeg_quality = 12; // 0-63, lower number means higher quality (larger file size)
    config.fb_count = 1;       // Number of frame buffers

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ ERROR: Camera init failed with error 0x%x\n", err);
        Serial.println("DEBUG: Ensure camera is connected correctly and board type is set (e.g., AI Thinker ESP32-CAM).");
        return; // Do not halt, but return, as the system might still function for non-camera tasks.
    }
    Serial.println("✅ INFO: Camera initialized successfully.");
    // Optional: Adjust camera settings after initialization
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);       // Flip image vertically (common for ESP32-CAM)
        s->set_hmirror(s, 1);     // Mirror image horizontally
        s->set_brightness(s, 0);  // -2 to 2
        s->set_contrast(s, 0);    // -2 to 2
        s->set_saturation(s, 0);  // -2 to 2
        s->set_whitebal(s, 1);    // 0 = disable , 1 = enable
        s->set_awb_gain(s, 1);    // 0 = disable , 1 = enable
        s->set_exposure_ctrl(s, 1); // 0 = disable , 1 = enable
        s->set_aec2(s, 0);        // 0 = disable , 1 = enable
        s->set_gainceiling(s, (gainceiling_t)0); // 0-6
        s->set_quality(s, 12);    // Set JPEG quality again
    }
}

void connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("INFO: Already connected to WiFi.");
        return;
    }

    Serial.println("INFO: Attempting to connect to WiFi...");
    WiFi.mode(WIFI_STA); // Set WiFi to station mode
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    // Try connecting for up to 15 seconds (30 * 500ms)
    while (WiFi.status() != WL_CONNECTED && attempts < 30) { 
        delay(500); 
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("✅ INFO: WiFi connected!");
        Serial.print("INFO: IP address: ");
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        // De-initialize ESP-NOW when WiFi is active to avoid conflicts with WiFi stack
        // This is crucial because ESP-NOW and WiFi station mode share resources.
        esp_now_deinit(); 
        Serial.println("DEBUG: ESP-NOW de-initialized to ensure stable WiFi connection.");
    } else {
        Serial.println();
        Serial.println("❌ WARNING: WiFi connection failed!");
        Serial.println("WARNING: Data will NOT be sent to server as WiFi is not connected.");
        wifiConnected = false;
        // Re-initialize ESP-NOW if WiFi connection fails, so we can still receive data.
        if (esp_now_init() != ESP_OK) {
            Serial.println("ERROR: ESP-NOW Reinit Warning after WiFi fail (might not receive data)");
        } else {
            esp_now_register_recv_cb(onDataRecv); // Re-register callback
            Serial.println("DEBUG: ESP-NOW re-initialized for receiving.");
        }
    }
}

camera_fb_t* captureImage() {
    Serial.println("INFO: Attempting to take picture...");
    
    // Turn on flash LED (assuming it's connected to GPIO4 on ESP32-CAM)
    digitalWrite(LED_FLASH, HIGH);
    delay(100); // Brief delay for flash exposure, adjust as needed

    camera_fb_t* fb = esp_camera_fb_get(); // Get a frame buffer
    
    // Turn off flash LED
    digitalWrite(LED_FLASH, LOW);
    
    if (!fb) {
        Serial.println("❌ ERROR: Camera capture failed! No frame buffer available.");
        return nullptr;
    }
    
    Serial.printf("✅ INFO: Image captured! Size: %d bytes.\n", fb->len);
    return fb;
}

void sendDataToServer() { // No longer takes struct_message& data, uses global combinedJsonDoc
    if (!wifiConnected) {
        Serial.println("❌ WARNING: WiFi not connected, cannot send data to server.");
        return;
    }
    
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");
    
    // --- Add image data to the already parsed incoming JSON in combinedJsonDoc ---
    camera_fb_t* fb = captureImage(); // Capture image here

    if (fb && fb->buf) {
        if (fb->len > 0) {
            String imageBase64 = base64::encode(fb->buf, fb->len);
            combinedJsonDoc["image"] = imageBase64;
            combinedJsonDoc["image_size"] = fb->len; // Optional: include original image size
            Serial.printf("DEBUG: Image encoded to base64. Base64 string length: %d chars.\n", imageBase64.length());
        } else {
            Serial.println("WARNING: Image buffer is empty (len=0), not sending image data.");
        }
    } else {
        Serial.println("INFO: No valid image buffer available (fb is null) to append to JSON.");
    }
    
    // --- REMOVED: 'name' field expectation/addition logic ---
    // The Flask server now handles 'name' as optional (defaults to 'N/A')
    // if (!combinedJsonDoc.containsKey("name")) {
    //     combinedJsonDoc["name"] = "Unknown Animal"; 
    //     Serial.println("DEBUG: 'name' field not found in incoming JSON, set to 'Unknown Animal'.");
    // }


    // Release the camera frame buffer after use to free up memory
    if (fb) {
        esp_camera_fb_return(fb);
        Serial.println("DEBUG: Camera frame buffer returned to pool.");
    }

    String jsonStringToSend;
    // Serialize combined JSON document to a String. Check for serialization success.
    if (serializeJson(combinedJsonDoc, jsonStringToSend) == 0) {
        Serial.println("❌ ERROR: Failed to serialize combined JSON or JSON payload too large for String!");
        http.end();
        return;
    }
    
    // --- NEW: Print JSON about to be sent to server ---
    Serial.println("DEBUG: JSON payload about to be sent to server:");
    serializeJsonPretty(combinedJsonDoc, Serial);
    Serial.println(); // Add a newline for readability
    // --- END NEW ---

    Serial.printf("INFO: Sending JSON payload to server. Payload size: %d bytes.\n", jsonStringToSend.length());
    
    // Use http.POST with c_str() and length() for precise payload transmission
    int httpResponseCode = http.POST((uint8_t*)jsonStringToSend.c_str(), jsonStringToSend.length());
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ HTTP Response Code: %d\n", httpResponseCode);
        Serial.println("DEBUG: Server Response Body: " + response);
    } else {
        Serial.printf("❌ HTTP POST failed. Error Code: %d - Reason: %s\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
        // Do NOT halt here. Allow loop to continue and re-init ESP-NOW.
    }
    
    http.end(); // Close the HTTP connection
}

void printLocalMAC() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    Serial.print("INFO: ESP32-CAM MAC Address: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    Serial.println("DEBUG: Make sure this MAC matches the receiverMAC in your sender code!");
}

// ESP-NOW Data Receive Callback - NOW EXPECTS JSON STRING
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    Serial.println("\nDEBUG: ESP-NOW Data Received Callback Triggered.");
    Serial.printf("DEBUG: Data received from MAC: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", recv_info->src_addr[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.printf(" (Length: %d bytes)\n", len);

    // Prevent processing if previous data is still being handled
    if (newDataAvailable) {
        Serial.println("WARNING: Previous ESP-NOW packet is still being processed. Ignoring current packet.");
        return; 
    }

    // Create a null-terminated string from the received raw data
    char* jsonStrBuffer = new char[len + 1];
    memcpy(jsonStrBuffer, data, len);
    jsonStrBuffer[len] = '\0'; // Null-terminate the string

    String receivedString(jsonStrBuffer);
    delete[] jsonStrBuffer; // Free memory

    // Sanitize JSON (find '{' and '}')
    int jsonStart = receivedString.indexOf('{');
    int jsonEnd = receivedString.lastIndexOf('}') + 1;

    if (jsonStart == -1 || jsonEnd == 0) {
        Serial.println("❌ Invalid JSON structure (no '{' or '}' found).");
        return;
    }

    String validJson = receivedString.substring(jsonStart, jsonEnd);
    Serial.printf("DEBUG: Sanitized JSON string length: %d\n", validJson.length());
    Serial.printf("DEBUG: Sanitized JSON: %s\n", validJson.c_str());

    // --- NEW: Print received JSON from sender ---
    Serial.println("DEBUG: Received JSON payload from ESP-NOW sender:");
    Serial.println(validJson);
    // --- END NEW ---

    // Parse incoming JSON into combinedJsonDoc (this clears previous content)
    // Ensure combinedJsonDoc is cleared before deserializing new data
    combinedJsonDoc.clear(); 
    DeserializationError error = deserializeJson(combinedJsonDoc, validJson);

    if (error) {
        Serial.printf("❌ JSON parse error from ESP-NOW data: %s\n", error.c_str());
        Serial.printf("DEBUG: Invalid JSON: %s\n", validJson.c_str());
        return;
    }

    // Validate required fields from the incoming JSON (NO 'name' field here now)
    // These keys MUST be present in the JSON sent by your sender ESP32
    if (!combinedJsonDoc.containsKey("timestamp") ||
        !combinedJsonDoc.containsKey("rfid") || 
        !combinedJsonDoc.containsKey("weight") ||
        !combinedJsonDoc.containsKey("sex") || 
        !combinedJsonDoc.containsKey("temperature") ||
        !combinedJsonDoc.containsKey("humidity")) {
        Serial.println("❌ Missing one or more required fields (timestamp, rfid, weight, sex, temperature, humidity) in incoming JSON.");
        // Print the content of combinedJsonDoc to see what was actually parsed
        serializeJsonPretty(combinedJsonDoc, Serial);
        Serial.println();
        return;
    }

    newDataAvailable = true; // Set flag to process in loop()
    Serial.println("✅ INFO: Valid JSON packet received via ESP-NOW and parsed into global doc.");
    Serial.println("--- End ESP-NOW Data ---");
}

// --- Setup and Loop ---

void setup() {
    Serial.begin(115200); // Initialize serial communication for debugging
    delay(100);
    
    // Init onboard flash LED pin
    pinMode(LED_FLASH, OUTPUT);
    digitalWrite(LED_FLASH, LOW); // Ensure LED is off initially
    Serial.println("INFO: ESP32-CAM Receiver with Camera Starting...");

    // Initialize camera
    initCamera(); 

    // Set WiFi mode to Station (STA) for ESP-NOW and eventual internet connection
    WiFi.mode(WIFI_STA);
    
    // Print MAC address for ESP-NOW pairing verification
    printLocalMAC();
    
    // Set ESP-NOW channel. Both sender and receiver MUST be on the same channel.
    // Channel 1 is a common default. Adjust if your sender uses a different channel.
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
    Serial.println("DEBUG: WiFi channel set to 1 for ESP-NOW.");
    
    delay(100); // Small delay for WiFi channel to set

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ERROR: Initializing ESP-NOW failed! System halted.");
        while(true); // Critical error, stop execution
    }
    Serial.println("✅ INFO: ESP-NOW initialized successfully.");

    // Register receive callback function to handle incoming ESP-NOW data
    esp_err_t result = esp_now_register_recv_cb(onDataRecv);
    if (result == ESP_OK) {
        Serial.println("✅ INFO: ESP-NOW receive callback registered successfully.");
    } else {
        Serial.printf("❌ ERROR: Failed to register receive callback. Error: %d\n", result);
    }

    Serial.println("INFO: ESP32-CAM ready to receive via ESP-NOW.");
    Serial.println("INFO: Waiting for sensor data from sender (expected as JSON string)...");
}

void loop() {
    // Only process new data when the 'newDataAvailable' flag is set
    if (newDataAvailable) {
        // Reset the flag immediately to prevent re-processing the same data
        // before the current processing is complete
        newDataAvailable = false; 

        Serial.println("INFO: New sensor data received via ESP-NOW. Initiating data processing and server upload...");
        
        // Attempt to connect to WiFi to send data to the internet server
        // connectToWiFi() will de-initialize ESP-NOW if successful to prevent conflicts.
        connectToWiFi();
        
        if (wifiConnected) {
            Serial.println("INFO: WiFi is connected. Proceeding with image capture and data upload.");
            
            // Call send function; it will now capture image and add it to combinedJsonDoc
            sendDataToServer();
            
            // After sending data, disconnect from WiFi to allow ESP-NOW to re-initialize cleanly.
            WiFi.disconnect(true); 
            Serial.println("DEBUG: Disconnected from WiFi after sending data.");
        } else {
            Serial.println("WARNING: WiFi was not connected, data not sent to server. ESP-NOW remains active.");
        }
        
        // Reinitialize ESP-NOW. This is crucial if WiFi connected successfully (which de-initializes ESP-NOW).
        // Also ensures it's active if WiFi failed to connect.
        delay(500); // Small delay for clean WiFi disconnection/reinitialization state
        WiFi.mode(WIFI_STA); // Ensure WiFi is in Station mode for ESP-NOW
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Re-set ESP-NOW channel for robustness

        // Re-initialize ESP-NOW and register callback
        if (esp_now_init() == ESP_OK) {
            esp_now_register_recv_cb(onDataRecv);
            Serial.println("INFO: ESP-NOW reinitialized, ready for next packet.");
        } else {
            Serial.println("ERROR: Failed to reinitialize ESP-NOW. This device may miss incoming packets.");
        }
    }
    
    delay(10); // Small delay to prevent the watchdog timer from resetting the ESP32
}