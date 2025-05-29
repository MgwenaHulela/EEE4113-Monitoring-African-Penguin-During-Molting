#include <Wire.h>
#include "AHT20.h"

// I2C Pins
#define I2C_SDA 21
#define I2C_SCL 22

AHT20 aht;

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Initialize AHT20
  if(!aht.begin()) {
    Serial.println("Failed to initialize AHT20!");
    while(1);
  }
  Serial.println("AHT20 sensor ready!");
}

void loop() {
  // Read sensor data
  float temperature = aht.getTemperature();
  float humidity = aht.getHumidity();

  // Print readings
  Serial.printf("Temperature: %.2f°C\tHumidity: %.2f%%\n", temperature, humidity);
  
  delay(2000); // 2-second delay between readings
}