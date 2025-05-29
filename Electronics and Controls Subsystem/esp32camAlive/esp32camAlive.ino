#include <Arduino.h>

// Create a Serial object for UART1
HardwareSerial SerialHost(1); // UART1

void setup() {
  Serial.begin(115200); // Debug output
  delay(1000);

  // Start UART1 on GPIO14 (RX), GPIO15 (TX)
  SerialHost.begin(9600, SERIAL_8N1, 14, 15);

  Serial.println("ESP32-CAM booted");

  // Send alive signal to main microcontroller
  SerialHost.println("I_AM_ALIVE");
  Serial.println("Sent: I_AM_ALIVE");
}

void loop() {
  // You can extend this loop to handle commands later
}
