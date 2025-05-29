#include "esp_camera.h"
#include <WiFi.h>

// Camera pins for AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32  // Changed from -1 to 32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const char* ssid = "Penguin_AP";
const char* password = "penguin123";

WiFiServer server(9002);

void startCamera() {
  // Reset camera by toggling PWDN
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(10);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(100);
  
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
  
  // Start with lower resolution to improve initialization success rate
  config.frame_size = FRAMESIZE_QVGA;  // Lower than VGA for initial testing
  config.jpeg_quality = 15;  // Lower quality (higher number = lower quality)
  config.fb_count = 2;  // Increased from 1 to 2 for better stability
  
  // Try multiple times to initialize the camera
  int retries = 3;
  esp_err_t err = ESP_FAIL;
  
  while (retries > 0 && err != ESP_OK) {
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Camera init failed with error 0x%x, retrying... (%d attempts left)\n", err, retries-1);
      delay(500);
      retries--;
    }
  }
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed after multiple attempts: 0x%x\n", err);
    Serial.println("Possible issues:");
    Serial.println("1. Check power supply - try a different USB port or external power");
    Serial.println("2. Check camera module connections");
    Serial.println("3. Camera module might be damaged");
    while (true) delay(1000);  // Halt program if camera init failed
  }
  
  Serial.println("Camera initialized successfully!");
  
  // Now that camera is working, can switch to higher resolution if desired
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_VGA);  // Switch to VGA after successful init
    s->set_quality(s, 12);  // Adjust to original quality
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32-CAM Initialization");
  delay(500);

  // Try to initialize camera
  Serial.println("Initializing camera...");
  startCamera();

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  // Wait for connection with timeout
  int timeout = 20;  // 10 second timeout
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed! Check credentials or network availability.");
    // Continue anyway, as camera might still work locally
  } else {
    Serial.println("\n✅ Connected to WiFi as server.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }

  server.begin();
  Serial.println("Server started on port 9002. Waiting for clients...");
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("📥 Client connected, waiting for trigger...");
    unsigned long lastActivity = millis();
    bool clientActive = true;

    while (client.connected() && clientActive) {
      // Check for client timeout
      if (millis() - lastActivity > 30000) {  // 30 second timeout
        Serial.println("Client timeout, disconnecting");
        clientActive = false;
        break;
      }
      
      if (client.available()) {
        lastActivity = millis();
        char trigger = client.read();
        
        if (trigger == 'T') {
          Serial.println("📸 Trigger received! Capturing image...");

          camera_fb_t* fb = esp_camera_fb_get();
          if (!fb) {
            Serial.println("❌ Camera capture failed!");
            client.write("FAIL", 4);
            break;
          }

          // Acknowledge capture success
          client.write("OKAY", 4);
          delay(50);

          // Send image size as header
          uint32_t len = fb->len;
          uint8_t hdr[4] = {
            (len >> 24) & 0xFF,
            (len >> 16) & 0xFF,
            (len >> 8) & 0xFF,
            len & 0xFF
          };
          
          // Send data in chunks to avoid buffer issues
          client.write(hdr, 4);
          
          // Send image data in chunks of 1024 bytes
          const size_t bufSize = 1024;
          size_t sentBytes = 0;
          
          while (sentBytes < len) {
            size_t remainingBytes = len - sentBytes;
            size_t bytesToSend = (remainingBytes > bufSize) ? bufSize : remainingBytes;
            
            client.write(fb->buf + sentBytes, bytesToSend);
            sentBytes += bytesToSend;
            
            // Small delay to allow WiFi stack to process
            if (bytesToSend == bufSize) {
              delay(1);
            }
          }
          
          client.flush();
          Serial.printf("✅ Image sent (%u bytes)\n", len);
          esp_camera_fb_return(fb);
        } else if (trigger == 'R') {
          // Added ability to reset camera if needed
          Serial.println("🔄 Reset request received, resetting camera...");
          client.write("RSOK", 4);
          startCamera();
        }
      }
      
      // Small delay to prevent tight loop
      delay(10);
    }
    
    client.stop();
    Serial.println("❌ Client disconnected.");
  }
  
  // Check WiFi connection periodically and attempt reconnection if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    WiFi.reconnect();
    delay(5000);
  }
  
  delay(100);
  }  // Small delay in main loop to prevent CPU overload