#include <Arduino.h>
#include <HX711.h> // Ensure you have this library installed

// --- Pin Definitions for Both HX711 Scales ---
#define DOUT1_PIN 32        // Data pin for Scale 1
#define DOUT2_PIN 27        // Data pin for Scale 2
#define SCK_SHARED_PIN 14 // Shared clock pin for both HX711s

// --- HX711 Scale Objects ---
HX711 scale1; // For the first load cell
HX711 scale2; // For the second load cell

// --- Calibration Factors (UPDATED WITH YOUR AVERAGED CALIBRATED VALUES!) ---
// These values convert raw ADC counts into grams.
// If your readings go negative when adding weight, your factor should be negative.
// These values are derived from your calibration outputs:
// Scale 1: (-107.9721 + -123.4339 + -96.4260) / 3 = -109.2773
// Scale 2: (-307.5103 + -43.3456 + -1.5809) / 3 = -117.4789
const float CALIBRATION_FACTOR_SCALE1 = -109.2773; // <<< UPDATED VALUE from your Scale 1 calibration >>>
const float CALIBRATION_FACTOR_SCALE2 = -117.4789; // <<< UPDATED VALUE from your Scale 2 calibration >>>

// --- Kalman Filter Parameters for Combined Weight Estimation ---
// Tune these values carefully for optimal smoothing vs. responsiveness for your setup.
// These are good starting points AFTER you have correct calibration factors.
// Q (Process Noise Covariance): How much the system's state is expected to change between measurements.
//   Higher Q makes the filter more responsive to changes, but potentially more noisy.
//   Lower Q makes the filter smoother, but slower to react to real changes.
double Q = 0.001; // Adjusted for better stability on kg scale (e.g., 1g variance)

// R (Measurement Noise Covariance): How much noise is in your combined *calibrated* sensor readings.
//   Higher R makes the filter trust the new measurements less (more smoothing).
//   Lower R makes the filter trust the new measurements more (less smoothing).
double R = 0.01; // Adjusted for better smoothing on kg scale (e.g., 10g variance)

// Kalman filter state variables for combined weight
double x_est_combined_kg = 0.0; // Initial state estimate (combined weight in kg).
double P_est_combined_kg = 1.0; // Initial estimation error covariance (represents initial uncertainty).

// --- Function to apply Kalman filter to a new combined measurement ---
// This function takes the combined scale readings as the measurement.
double getKalmanFilteredWeight(double combined_measurement_kg) {
    double z_k = combined_measurement_kg; // The current combined measurement (e.g., 2.5 kg)

    // 1. Prediction Step
    double P_predict = P_est_combined_kg + Q;

    // 2. Update Step (Correction)
    double K_k = P_predict / (P_predict + R);
    x_est_combined_kg = x_est_combined_kg + K_k * (z_k - x_est_combined_kg);
    P_est_combined_kg = (1 - K_k) * P_predict;

    return x_est_combined_kg;
}

// --- Calibration Function for Individual Scales (Now prompts for multiple known weights) ---
void calibrateScale(HX711 &scale_obj, const String& scale_name) {
    Serial.print("\n--- Calibrating ");
    Serial.print(scale_name);
    Serial.println(" ---");
    Serial.println("⚙️ Remove all weight from this scale for tare...");
    delay(3000);

    // Perform tare (zero out the current reading)
    scale_obj.tare(20); // Tare (zero) the scale, averaging 20 readings
    Serial.print("✔️ ");
    Serial.print(scale_name);
    Serial.println(" tared. The zero offset is now set.");
    Serial.print("Initial (tared) raw reading: ");
    Serial.println(scale_obj.get_value()); // Show the raw value after tare (should be close to 0)

    // Define the known weights for multi-point calibration
    float known_weights_grams[] = {750.0, 2350.0, 4450.0}; // 0.75kg, 2.35kg, 4.45kg in grams
    int num_weights = sizeof(known_weights_grams) / sizeof(known_weights_grams[0]);

    Serial.println("\n--- Performing Multi-Point Calibration ---");
    Serial.println("Follow the prompts carefully. DO NOT move the scale during this process.");
    Serial.println("Record each 'Calculated Factor for THIS weight' to average later.");

    for (int i = 0; i < num_weights; ++i) {
        float current_known_weight_g = known_weights_grams[i];
        Serial.print("\nSTEP ");
        Serial.print(i + 1);
        Serial.print("/3: Place EXACTLY ");
        Serial.print(current_known_weight_g);
        Serial.println(" grams on the scale. Press Enter in Serial Monitor when stable.");

        // Wait for user to place weight and press Enter
        while (Serial.available() == 0) {
            Serial.print(".");
            delay(500);
        }
        String dummy = Serial.readStringUntil('\n'); // Consume the Enter
        while (Serial.available()) Serial.read(); // Clear any remaining characters in the input buffer

        Serial.print("Reading average value with ");
        Serial.print(current_known_weight_g);
        Serial.println(" grams... (Please wait)");
        delay(2000); // Give physical weight time to settle on the scale

        // Get an average reading of the 'units' (raw value minus tare offset) with the known weight
        // This 'units_with_weight' is the raw difference caused by the known weight.
        float units_with_weight = scale_obj.get_value(50); // Average 50 readings for stability
        Serial.print("Units (raw - offset) with ");
        Serial.print(current_known_weight_g);
        Serial.print("g: ");
        Serial.println(units_with_weight);

        if (units_with_weight != 0 && current_known_weight_g > 0) {
            float calculated_cal_factor = units_with_weight / current_known_weight_g;
            Serial.print("✅ Calculated Factor for THIS weight (");
            Serial.print(current_known_weight_g);
            Serial.print("g): ");
            Serial.println(calculated_cal_factor, 4); // Print with 4 decimal places for precision
        } else {
            Serial.println("❌ Calculation failed for this weight. Ensure weight is positive and reading is non-zero after tare.");
        }
        Serial.println("--- Remove weight to prepare for next step ---");
        delay(2000); // Pause to allow weight removal
    }

    Serial.println("\n--- Multi-Point Calibration Data Collected ---");
    Serial.println("⚠️ IMPORTANT: Manually AVERAGE the 3 'Calculated Factor for THIS weight' values you recorded for this scale.");
    Serial.println("   (Example: If factors were -104500, -104600, -104450, your average would be -104516.67)");
    Serial.println("   Use this averaged value to update CALIBRATION_FACTOR_SCALE1 (or 2) at the top of your sketch!");
    Serial.println("   Then RE-UPLOAD the sketch with the updated factors for proper operation.");

    // For immediate testing after calibration, set the scale using the factor from the last known weight.
    // This is temporary and will be overridden by your manually updated const factor on next upload.
    float last_units_with_weight = scale_obj.get_value(20);
    if(last_units_with_weight != 0 && known_weights_grams[num_weights - 1] > 0){
        scale_obj.set_scale(last_units_with_weight / known_weights_grams[num_weights - 1]);
        Serial.print("\nTemporary set_scale for testing: ");
        Serial.println(last_units_with_weight / known_weights_grams[num_weights - 1], 4);
    } else {
        scale_obj.set_scale(1.0); // Fallback
    }

    Serial.println("--- Calibration Process Complete for " + scale_name + " ---");
}

void setup() {
    Serial.begin(115200); // Set baud rate
    delay(300);
    Serial.println("\n--- HX711 Dual Scale Combined Weight with Kalman Filter ---");
    Serial.println("    Remember to CALIBRATE both scales first for accurate readings.");

    // Initialize both HX711 scales
    scale1.begin(DOUT1_PIN, SCK_SHARED_PIN);
    scale2.begin(DOUT2_PIN, SCK_SHARED_PIN);

    // Power up and wait for HX711s to stabilize
    Serial.println("Powering up scales and waiting for stabilization...");
    delay(2000); // Give HX711s time to power up and stabilize

    // Set individual calibration factors based on your prior findings
    // IMPORTANT: Ensure these are set correctly for accurate readings.
    Serial.println("Setting calibration factors based on constants in sketch...");
    scale1.set_scale(CALIBRATION_FACTOR_SCALE1);
    scale2.set_scale(CALIBRATION_FACTOR_SCALE2);
    Serial.print("Scale 1 factor: "); Serial.println(CALIBRATION_FACTOR_SCALE1, 4);
    Serial.print("Scale 2 factor: "); Serial.println(CALIBRATION_FACTOR_SCALE2, 4);


    // Tare both scales (zero them out)
    Serial.println("Taring both scales (zeroing out current weight)...");
    scale1.tare(10); // Tare Scale 1, averaging 10 readings
    scale2.tare(10); // Tare Scale 2, averaging 10 readings
    Serial.println("Scales tared. Ready for readings!");

    // Reset Kalman filter for combined weight after initial tare
    x_est_combined_kg = 0.0;
    P_est_combined_kg = 1.0;

    Serial.println("\n--- System Ready ---");
    Serial.println("Type 'calibrate1' to calibrate Scale 1.");
    Serial.println("Type 'calibrate2' to calibrate Scale 2.");
    Serial.println("Output Format: Raw1\tW1(g)\tRaw2\tW2(g)\tFiltered: (kg)");
}

void loop() {
    static uint32_t lastPrint = 0;

    // Read and print every 100ms
    if (millis() - lastPrint >= 100) { // Adjust this delay for faster/slower updates
        // Check if HX711s are ready to read
        if (scale1.is_ready() && scale2.is_ready()) {
            // --- Get raw ADC values from each scale ---
            long raw_reading1 = scale1.read();
            long raw_reading2 = scale2.read();

            // --- Get calibrated weight in grams from each scale ---
            // get_units() returns the weight after tare and calibration factor are applied.
            float weight1_grams = scale1.get_units(1); // Average 1 reading for speed in loop
            float weight2_grams = scale2.get_units(1); // Average 1 reading for speed in loop

            // --- Combine readings for Kalman Filter ---
            // Use absolute values to ensure positive contributions,
            // which helps if one scale's calibration factor sign is incorrect
            // or if there's a negative offset that needs to be effectively ignored for summation.
            float combined_raw_grams = 1.03 * (abs(weight1_grams) + abs(weight2_grams)) / 2.0;

            // Convert to kilograms for the Kalman filter input
            // CRITICAL CORRECTION: Ensure Kalman filter input is in KG if its state variables are in KG.
            double kalman_measurement_kg = combined_raw_grams / 1000.0; // Divide by 1000 to convert grams to kilograms

            // Apply the Kalman filter to the combined measurement
            double kalman_filtered_weight_kg = getKalmanFilteredWeight(kalman_measurement_kg);

            // Print the results, including raw ADC values for tuning help
            // Note: weight1_grams and weight2_grams are still in grams here for display consistency.
            // The filtered output is correctly in kg.
            Serial.printf("Raw1: %ld\tW1: %.2fg\tRaw2: %ld\tW2: %.2fg\tFiltered: %.2fkg\n",
                          raw_reading1, weight1_grams, raw_reading2, weight2_grams, kalman_filtered_weight_kg);

            lastPrint = millis();
        } else {
            // Optionally, print a message if scales are not ready
            // Serial.println("Scales not ready...");
            delay(10); // Small delay to prevent busy-waiting
        }
    }

    // --- Serial Commands for Individual Scale Calibration ---
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "calibrate1") {
            calibrateScale(scale1, "Scale 1");
            // After re-calibration, re-tare both scales for consistent zeroing
            Serial.println("\nRe-taring both scales after calibration...");
            scale1.tare(10);
            scale2.tare(10);
            // Also reset Kalman filter to start fresh with new calibration
            x_est_combined_kg = 0.0;
            P_est_combined_kg = 1.0;
            Serial.println("Scales re-tared and Kalman filter reset.");
        } else if (command == "calibrate2") {
            calibrateScale(scale2, "Scale 2");
            // After re-calibration, re-tare both scales for consistent zeroing
            Serial.println("\nRe-taring both scales after calibration...");
            scale1.tare(10);
            scale2.tare(10);
            // Also reset Kalman filter to start fresh with new calibration
            x_est_combined_kg = 0.0;
            P_est_combined_kg = 1.0;
            Serial.println("Scales re-tared and Kalman filter reset.");
        } else {
            Serial.print("Unknown command: ");
            Serial.println(command);
        }
        // Clear any remaining characters in the serial buffer
        while (Serial.available()) Serial.read();
    }
}