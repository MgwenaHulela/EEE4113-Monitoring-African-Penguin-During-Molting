#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;
DateTime now;  // Declare 'now' globally so it can be accessed in both setup() and loop()

void setup() {
  Serial.begin(115200);
  if (!rtc.begin()) {
    Serial.println("❌ ERROR: Could not find RTC");
    while (1);
  }

  // Manually set the date and time here
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // Set to compile time

  // Print the current time after setting the RTC
  now = rtc.now();  // Get the current time from the RTC
  Serial.print("Current time after setting RTC: ");
  Serial.print(now.year(), DEC);
  Serial.print("-");
  Serial.print(now.month(), DEC);
  Serial.print("-");
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.println(now.second(), DEC);

  Serial.println("RTC set to compile time!");
}

void loop() {
  // Print the current time every second
  now = rtc.now();  // Get the current time from the RTC
  Serial.print("Current time: ");
  Serial.print(now.year(), DEC);
  Serial.print("-");
  Serial.print(now.month(), DEC);
  Serial.print("-");
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.println(now.second(), DEC);

  delay(1000);  // Delay for 1 second
}
