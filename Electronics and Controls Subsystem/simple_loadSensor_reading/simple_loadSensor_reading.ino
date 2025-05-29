#include <HX711.h>
#include <SPI.h>
#include <SD.h>

// HX711 Pin configuration
#define DT 32     // Data pin (DT)
#define SCK 14    // Clock pin (SCK)

// SD card configuration
#define SD_CS 5  // Chip select pin for SD card (adjust to your setup)

HX711 scale;
    
// Parameters
const float true_weight = 800.0;  // True weight in grams (adjust as needed)
const int sample_count = 200;

File dataFile;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize HX711
  Serial.println("Initializing HX711...");
  scale.begin(DT, SCK);
  delay(500);

  // Added a loop for initial readiness check
  int retries = 0;
  while (!scale.is_ready()) {
    Serial.println("❌ HX711 not ready during setup. Check wiring and power. Retrying...");
    delay(500); // Give it some time before retrying
    retries++;
    if (retries > 10) { // Limit setup retries to prevent infinite loop
      Serial.println("❌ HX711 failed to become ready after multiple attempts. Halting.");
      while (1);
    }
  }
  Serial.println("✅ HX711 ready.");

  // Tare scale
  Serial.println("Taring scale. Remove any weights...");
  scale.tare(); // Ensure nothing is on the load cell during tare
  delay(3000); // Give time for tare to settle

  // Prompt to place weight
  Serial.println("Place known weight...");
  delay(5000); // Give user time to place weight

  // Initialize SD card
  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD card initialization failed!");
    while (1); // Halt if SD card is critical
  }
  Serial.println("✅ SD card initialized.");

  // Build filename: weight_<true_weight>.csv (e.g., weight_2000.csv)
  char filename[32];
  snprintf(filename, sizeof(filename), "/weight_%d.csv", (int)true_weight);

  // Open file
  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile) {
    Serial.println("❌ Failed to open file on SD card.");
    while (1); // Halt if file cannot be opened
  }

  // Write header
  dataFile.println("raw_reading,true_weight_grams");

  // Collect and store samples
  Serial.print("Collecting ");
  Serial.print(sample_count);
  Serial.println(" samples...");

  for (int i = 0; i < sample_count; i++) {
    // Before reading, explicitly wait for the HX711 to be ready
    while (!scale.is_ready()) {
      Serial.println("⚠️ HX711 not ready for read. Waiting...");
      delay(200); // Wait a bit longer before retrying to read
    }

    long reading = scale.read(); // Read the raw value
    
    // Check for "bad" readings immediately and optionally skip/retry
    if (reading == -1 || reading == 0x7FFFFF || reading == 0x800000) { // Common bad values: timeout, max pos, max neg
        Serial.printf("❌ Abnormal reading detected (0x%X). Retrying sample %d.\n", reading, i + 1);
        i--; // Decrement counter to retry this sample
        delay(500); // Add a longer delay to prevent rapid retries on persistent issues
        continue; // Skip to next iteration of loop
    }

    dataFile.print(reading);
    dataFile.print(",");
    dataFile.println(true_weight);

    Serial.print("Sample ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(reading);
    
    delay(50); // Small delay between samples to allow HX711 to settle, especially if issues persist
  }

  dataFile.close();
  Serial.print("✅ Done. File saved as ");
  Serial.println(filename);
}

void loop() {
  // No repeat needed for this calibration sketch
}