#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Set Wi-Fi to Station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Print the MAC address
  Serial.println("ESP32-CAM MAC Address:");
  Serial.println(WiFi.macAddress());
}

void loop() {
  // Nothing to do in loop
}
