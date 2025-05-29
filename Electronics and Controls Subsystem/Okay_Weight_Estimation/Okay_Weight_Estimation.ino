#include <HX711.h>

// HX711 Pin configuration
#define DT 32  // Change this
#define SCK  14  // …and this


HX711 scale;

// Polynomial regression coefficients (degree 1 - Linear Model)
// Estimated weight = a1 * Raw Reading + a0
// These coefficients are derived from the provided raw data for 1000g and 2000g.
const float a1 = 0.0045558; // Coefficient for the Raw Reading term
const float a0 = -1862.8;  // Constant term (intercept)

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("🔧 Initializing HX711...");
  // Initialize the scale object with the data and clock pins
  scale.begin(DT, SCK);
  delay(500); // Give the sensor a moment to power up and stabilize

  // Check if the HX711 is communicating
  if (!scale.is_ready()) {
    Serial.println("❌ HX711 not found or not ready. Check wiring and power.");
    // Halt execution if the sensor is not detected
  }

 // Serial.println("✅ HX711 ready.");

  // Note: When using a polynomial model derived from data points,
  // the 'tare' function is typically not used in the loop as the
  // 'a0' coefficient implicitly accounts for the zero offset based on the data.
  // However, if you want to zero the scale at the start, you could
  // perform a tare here and adjust the model or subtract the tare reading
  // from subsequent raw readings before applying the polynomial.
  // For this implementation using the derived polynomial directly,
  // we rely on the 'a0' term for the zero point based on the calibration data.

  Serial.println("Using polynomial model for weight estimation.");
  Serial.println("Place weight on the scale...");
}

void loop() {
  // Check if the HX711 is ready to provide a new reading
  if (scale.is_ready()) {
    // Read the raw value from the HX711
    long rawReading = scale.read();

    // Apply the linear polynomial model
    // Estimated Weight = a1 * Raw Reading + a0
    float estimatedWeight = a1 * (float)rawReading + a0;

    // Print the raw reading and the estimated weight to the Serial Monitor
    Serial.print("📟 Raw HX711 Reading: ");
    Serial.print(rawReading);
    Serial.print(" | ⚖️ Est Weight: ");
    Serial.print(estimatedWeight, 2); // Print estimated weight with 2 decimal places
    Serial.println(" g");
  } else {
    // Indicate if the HX711 is not ready
    Serial.println("⚠️ HX711 not ready to read.");
  }

  // Delay before taking the next reading
  delay(500);
}
