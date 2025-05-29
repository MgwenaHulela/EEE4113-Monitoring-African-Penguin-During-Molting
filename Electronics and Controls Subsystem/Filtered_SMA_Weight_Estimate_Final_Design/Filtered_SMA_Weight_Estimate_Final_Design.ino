#include <Arduino.h>
#include <HX711.h>

// HX711 wiring
#define DOUT_PIN 32
#define CLK_PIN 14

HX711 scale;

// Optional: store calibration factor here if known
float calibration_factor = -1.0;

// For basic outlier filtering
float filtered_average(int samples) {
  float values[samples];
  float sum = 0;
  for (int i = 0; i < samples; i++) {
    values[i] = scale.get_units(1); // One reading with calibration
    delay(10); // Small delay between samples
  }

  // Discard max and min to reduce impact of outliers
  float maxVal = values[0];
  float minVal = values[0];
  for (int i = 1; i < samples; i++) {
    if (values[i] > maxVal) maxVal = values[i];
    if (values[i] < minVal) minVal = values[i];
  }

  // Compute average excluding min and max
  for (int i = 0; i < samples; i++) {
    if (values[i] != maxVal && values[i] != minVal)
      sum += values[i];
  }

  return sum / (samples - 2); // Exclude 2 outliers
}

void calibrate() {
  Serial.println("⚙️ Remove all weight for tare...");
  delay(3000);
  scale.tare();
  Serial.println("✔️ Tare complete. Now place a known weight (e.g. 800g).");
  delay(5000);

  float known_weight_grams = 800.0;

  Serial.println("Reading average raw value with weight:");
  long raw_reading = scale.read_average(50); // More samples for stability
  Serial.print("Raw reading with known weight: ");
  Serial.println(raw_reading);

  if (raw_reading != 0 && known_weight_grams > 0) {
    calibration_factor = raw_reading / known_weight_grams;
    scale.set_scale(calibration_factor);
    Serial.print("✅ Calibration factor set: ");
    Serial.println(calibration_factor, 4);
  } else {
    Serial.println("❌ Calibration failed. Using fallback factor 1.0");
    scale.set_scale(1.0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- HX711 Stable SMA Weight Estimation ---");
  scale.begin(DOUT_PIN, CLK_PIN);

  calibrate();

  Serial.println("\n--- System Ready ---");
  Serial.println("Type 'recalibrate' to redo calibration.");
  Serial.println("Format: Raw ADC\tFiltered Weight (g)");
}

void loop() {
  static uint32_t lastPrint = 0;

  if (millis() - lastPrint >= 500) { // Slower update: every 0.5s
    if (scale.is_ready()) {
      long raw_adc = scale.read();
      float avg_weight = filtered_average(10); // SMA with outlier filtering
       avg_weight = (-avg_weight*2)/1000;
      Serial.printf("Raw ADC: %ld\tFiltered SMA Weight: %.2fkg\n", raw_adc, avg_weight);
    } else {
      Serial.println("⚠️ Scale not ready.");
    }

    lastPrint = millis();
  }

  // Recalibration trigger via Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "recalibrate") {
      Serial.println("\n🔁 Recalibration initiated...");
      calibrate();
    } else {
      Serial.print("Unknown command: ");
      Serial.println(command);
    }
    while (Serial.available()) Serial.read(); // Flush buffer
  }
}
