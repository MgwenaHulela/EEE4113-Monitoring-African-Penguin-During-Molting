#include <Arduino.h>
#include <HX711.h>

#define DOUT 4
#define CLK 13


HX711 scale;

// Kalman Filter Parameters (tuned for 1000g)
double Q = 0.1;        // Process noise (lower = more trust in model)
double R = 10.0;       // Measurement noise (higher = smoother but slower)
double P = 1.0;        // Initial estimation error
double K = 0.0;        // Kalman gain
double x = 1000.0;     // Initial estimate (set to expected 1000g)

// Moving Average with Median Pre-Filter
const uint8_t MA_SIZE = 7;
const uint8_t MEDIAN_SIZE = 5;
double maBuffer[MA_SIZE];
double medianBuffer[MEDIAN_SIZE];
uint8_t idx = 0;

// Flag to track HX711 initialization status
bool hx711_initialized = false;

void setup() {
  Serial.begin(115200);
  delay(200); // Give serial time to initialize

  Serial.println("\n--- HX711 Weight Sensor Test ---");
  Serial.print("Attempting to initialize HX711 on DOUT ");
  Serial.print(DOUT);
  Serial.print(", CLK ");
  Serial.println(CLK);

  scale.begin(DOUT, CLK);

  if (!scale.is_ready()) {
    Serial.println("❌ ERROR: HX711 not ready! Please check wiring and power supply. Continuing without scale data.");
    hx711_initialized = false; // Mark as not initialized
    // Do NOT halt, as requested.
  } else {
    Serial.println("✅ HX711 initialized successfully.");
    hx711_initialized = true; // Mark as initialized

    Serial.println("⚙️ Performing tare. Ensure no weight is on the scale...");
    delay(3000); // Give time for user to remove weight
    scale.tare();
    Serial.println("✔️ Tare complete. Place a known 1000g weight on the scale...");
    delay(5000); // Give time for user to place weight

    // Auto-calibration
    Serial.println("⚙️ Performing auto-calibration with 1000g reference...");
    float raw_for_calib = scale.read_average(20); // Average more readings for calibration
    if (raw_for_calib != 0) { // Avoid division by zero
        float calib_factor = raw_for_calib / 1000.0; // Assuming 1000g reference (adjust if using different weight)
        scale.set_scale(calib_factor);
        Serial.print("✅ Calibration factor calculated and set: ");
        Serial.println(calib_factor, 4);
    } else {
        Serial.println("❌ ERROR: Raw reading for calibration was 0. Cannot set calibration factor. Check load cell wiring/load.");
        // We'll still continue, but data might be garbage.
    }
    
    // Initialize filters with a plausible starting value (e.g., 0.0 or reference weight)
    Serial.println("⚙️ Initializing filter buffers...");
    for(uint8_t i=0; i<MA_SIZE; i++) maBuffer[i] = 0.0; // Start with 0 or a reasonable default
    for(uint8_t i=0; i<MEDIAN_SIZE; i++) medianBuffer[i] = 0.0; // Start with 0
    x = 0.0; // Initialize Kalman estimate to 0
    Serial.println("✅ Filter buffers initialized.");
  }
  Serial.println("\n--- HX711 Test Operational ---");
  Serial.println("Monitoring weight. Data will be printed every 100ms.");
  if (!hx711_initialized) {
    Serial.println("❗ WARNING: Scale is not operational. Output will show 0.0g.");
  }
}

double getMedian() {
  if (!hx711_initialized) return 0.0; // Return 0 if scale not initialized

  // Get MEDIAN_SIZE quick samples
  for(uint8_t i=0; i<MEDIAN_SIZE; i++) {
    medianBuffer[i] = scale.get_units(1); // Get 1 unit reading per sample
  }
  
  // Simple bubble sort for median
  for(uint8_t i=0; i<MEDIAN_SIZE-1; i++) {
    for(uint8_t j=i+1; j<MEDIAN_SIZE; j++) {
      if(medianBuffer[j] < medianBuffer[i]) {
        double temp = medianBuffer[i];
        medianBuffer[i] = medianBuffer[j];
        medianBuffer[j] = temp;
      }
    }
  }
  Serial.print("DEBUG: Median buffer sorted: ");
  for(uint8_t i=0; i<MEDIAN_SIZE; i++) {
      Serial.print(medianBuffer[i], 1);
      Serial.print(" ");
  }
  Serial.println();
  Serial.print("DEBUG: Median value: ");
  Serial.println(medianBuffer[MEDIAN_SIZE/2], 1);
  return medianBuffer[MEDIAN_SIZE/2];
}

double getFilteredWeight() {
  if (!hx711_initialized) return 0.0; // Return 0 if scale not initialized

  // 1. Get median-filtered reading
  double median_filtered_reading = getMedian();
  
  // 2. Update moving average buffer
  maBuffer[idx] = median_filtered_reading;
  idx = (idx + 1) % MA_SIZE;
  
  double sum_ma = 0;
  for(uint8_t i=0; i<MA_SIZE; i++) {
    sum_ma += maBuffer[i];
  }
  double ma_average = sum_ma / MA_SIZE;
  Serial.print("DEBUG: MA buffer content: ");
  for(uint8_t i=0; i<MA_SIZE; i++) {
      Serial.print(maBuffer[i], 1);
      Serial.print(" ");
  }
  Serial.println();
  Serial.print("DEBUG: MA average: ");
  Serial.println(ma_average, 1);
  
  // 3. Apply Kalman filter to MA result
  P += Q; // Predict: Project the error covariance ahead (state update)
  K = P / (P + R); // Update: Calculate the Kalman gain
  x += K * (ma_average - x); // Update: Incorporate the new measurement (measurement update)
  P *= (1 - K); // Update: Update the error covariance

  Serial.printf("DEBUG: Kalman P=%.3f, K=%.3f, x=%.1f\n", P, K, x);
  
  return x;
}

void loop() {
  static uint32_t lastPrint = 0;
  if(millis() - lastPrint >= 100) { // 10Hz update rate (100ms interval)
    
    // Check if HX711 is initialized before attempting to read
    if (hx711_initialized) {
        long raw_adc = scale.read(); // Get raw ADC value directly
        double raw_units = scale.get_units(1); // Get raw converted units (before MA/Kalman)
        double filtered_weight = getFilteredWeight();

        Serial.print("Raw ADC: ");
        Serial.print(raw_adc);
        Serial.print("\tRaw Units: ");
        Serial.print(raw_units, 1);
        Serial.print("g\tFiltered: ");
        Serial.print(filtered_weight, 1);
        Serial.println("g");
    } else {
        // Print message if scale is not operational
        Serial.println("HX711 not operational. Please check wiring. Waiting for 'recalibrate' command...");
    }
    
    lastPrint = millis();
  }
  
  // Recalibration trigger via Serial
  if(Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Remove any leading/trailing whitespace
    if (command == "recalibrate") {
        Serial.println("\n--- Recalibration Triggered by User ---");
        setup(); // Re-run setup to re-initialize and recalibrate
    } else {
        Serial.print("Unknown command: ");
        Serial.println(command);
    }
    while(Serial.available()) Serial.read(); // Clear any remaining characters in the serial buffer
  }
}