const int ldrPin = 34;  // ADC pin connected to LDR

void setup() {
  Serial.begin(115200);
  delay(1000);
}

void loop() {
  int ldrValue = analogRead(ldrPin);
  Serial.printf("LDR: %d\n", ldrValue);
  delay(500);
}
