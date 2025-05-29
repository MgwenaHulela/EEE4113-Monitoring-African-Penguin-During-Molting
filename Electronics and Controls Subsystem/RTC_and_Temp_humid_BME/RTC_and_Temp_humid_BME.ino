#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "RTClib.h"

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;
RTC_DS3231 rtc;
unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000; // 1 second exact

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Initialize BME280
  if (!bme.begin(0x76)) {
    Serial.println("BME280 not found!");
    while(1);
  }
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while(1);
  }
  
  // Set precise RTC time if needed
  if (rtc.lostPower()) {
    Serial.println("Setting precise RTC time...");
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // For even more precision, manually set with:
    rtc.adjust(DateTime(2025, 4, 28, 4, 10, 0));
  }
  
  // Wait for sensors to stabilize
  delay(100);
}

void loop() {
  DateTime now = rtc.now();
  
  // Precise 1-second interval using RTC
  if (now.unixtime() > lastReadTime) {
    lastReadTime = now.unixtime();
    
    // Single line output format:
    // Timestamp, Temp(C), Humidity(%), Pressure(hPa)
    Serial.print(now.timestamp(DateTime::TIMESTAMP_FULL));
    Serial.print(", ");
    Serial.print(bme.readTemperature(), 1);
    Serial.print("°C, ");
    Serial.print(bme.readHumidity(), 1);
    Serial.print("%, ");
    Serial.print(bme.readPressure() / 100.0, 1);
    Serial.println("hPa");
  }
  
  // Small delay to prevent bus contention
  delay(10);
}