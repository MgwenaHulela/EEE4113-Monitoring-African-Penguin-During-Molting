#include <Arduino.h> // Standard Arduino functions

// --- Simulation Parameters ---
const unsigned long LOG_INTERVAL_MS = 60000; // Display data every 1 minute (60 seconds)
unsigned long lastDisplayTime = 0; // Stores the last time data was displayed

// Simulation start time (for internal time calculation only)
// Corresponds to 2020-05-20 18:26:00
const unsigned long SIM_START_MILLIS_OFFSET = 0; // Assuming millis() starts effectively at 0 for sim
const unsigned long START_HOUR = 18;
const unsigned long START_MINUTE = 26;
const unsigned long START_DAY = 20; // Just for date formatting in simulation
const unsigned long START_MONTH = 5;
const unsigned long START_YEAR = 2025;

// --- Base weather values (Observatory, Cape Town typical for May) ---
// These are dynamic and will change based on time of day in simulation functions
float base_temp_c = 18.0; // degrees Celsius
float base_humidity_rh = 70.0; // %RH

// --- Function Prototypes ---
String getSimulatedTimestamp(unsigned long current_sim_millis);
float simulateTemperature(float current_hour, float base_val, float noise_level, float offset);
float simulateHumidity(float current_hour, float base_val, float noise_level, float offset);

// Generates a formatted simulated timestamp string based on elapsed milliseconds.
String getSimulatedTimestamp(unsigned long current_sim_millis) {
  // Calculate total seconds elapsed from the start of simulation
  unsigned long total_seconds = current_sim_millis / 1000;

  // Calculate simulated time components
  unsigned long sim_second = total_seconds % 60;
  unsigned long total_minutes = total_seconds / 60;
  unsigned long sim_minute = total_minutes % 60;
  unsigned long total_hours = total_minutes / 60;
  unsigned long sim_hour = (START_HOUR + total_hours) % 24;
  unsigned long total_days = total_hours / 24;
  
  // For simplicity, we'll just increment day/month/year for a few days.
  // Full calendar math is complex and not strictly needed if only 24 hours.
  unsigned long sim_day = START_DAY + total_days;
  unsigned long sim_month = START_MONTH;
  unsigned long sim_year = START_YEAR;

  // Handle month/year rollover (basic, sufficient for 24h simulation)
  if (sim_day > 31) { // Assuming average month for this basic simulation
      sim_day -= 31;
      sim_month++;
      if (sim_month > 12) {
          sim_month = 1;
          sim_year++;
      }
  }


  char buf[20]; // YYYY-MM-DD HH:MM:SS\0 (19 chars + null terminator)
  sprintf(buf, "%04lu-%02lu-%02lu %02lu:%02lu:%02lu",
          sim_year, sim_month, sim_day,
          sim_hour, sim_minute, sim_second);
  return String(buf);
}

// Simulates temperature based on hour of day with noise and offset.
float simulateTemperature(float current_hour, float base_val, float noise_level, float offset) {
  float temp_variation_factor = 0.5 * (1 + random(-100, 101) / 1000.0); // +-0.1 random factor
  float diurnal_effect;

  // Peak temperature around 2 PM (14:00), lowest around 5 AM (5:00)
  // This simulation is an approximation of a sine wave-like diurnal cycle.
  if (current_hour >= 2 && current_hour <= 20) {
    diurnal_effect = (1.0 - fabsf((current_hour - 14.0) / 12.0)); // Use fabsf for float abs
  } else {
    // Wrap around for hours like 22-23-0-1-2
    diurnal_effect = (fabsf((fmodf(current_hour + 10.0, 24.0) - 14.0) / 12.0));
  }
  diurnal_effect *= random(900, 1101) / 1000.0; // Additional random scaling (0.9 to 1.1)

  float simulated_temp = base_val + 5.0 * temp_variation_factor * diurnal_effect; // Max temp change of 5 degrees
  simulated_temp += random(-int(noise_level * 100), int(noise_level * 100) + 1) / 100.0; // Add noise
  simulated_temp += offset; // Add sensor specific offset
  return simulated_temp;
}

// Simulates humidity based on hour of day with noise and offset.
float simulateHumidity(float current_hour, float base_val, float noise_level, float offset) {
  float humid_variation_factor = 0.5 * (1 + random(-100, 101) / 1000.0); // +-0.1 random factor
  float diurnal_effect;

  // Higher humidity at night/early morning, lower during day (peak around 8 AM, lowest around 8 PM)
  // This simulation is an approximation of a sine wave-like diurnal cycle.
  if (current_hour >= 0 && current_hour <= 20) {
    diurnal_effect = (1.0 - fabsf((current_hour - 8.0) / 12.0));
  } else {
    diurnal_effect = (fabsf((fmodf(current_hour + 4.0, 24.0) - 8.0) / 12.0));
  }
  diurnal_effect *= random(900, 1101) / 1000.0; // Additional random scaling (0.9 to 1.1)

  float simulated_humid = base_val + 15.0 * humid_variation_factor * diurnal_effect; // Max humidity change of 15%
  simulated_humid += random(-int(noise_level * 100), int(noise_level * 100) + 1) / 100.0; // Add noise
  simulated_humid += offset; // Add sensor specific offset

  // Clamp humidity to realistic range
  if (simulated_humid > 100.0) simulated_humid = 100.0;
  if (simulated_humid < 0.0) simulated_humid = 0.0;

  return simulated_humid;
}


void setup() {
  Serial.begin(115200);
  delay(100); // Allow serial to initialize

  Serial.println("\n--- ESP32 Weather Data Simulation (Serial Output) ---");
  Serial.println("Assuming start time: 2020-05-20 18:26:00");
  Serial.println("Data will be displayed every 1 minute for 24 hours.");

  // Seed the random number generator with an analog read from an unconnected pin
  randomSeed(analogRead(0));

  // Print header row for the data
  Serial.println("Timestamp,AHT20+BMP280 Temperature,BME280 Temperature (Reference),AHT20+BMP280 Humidity,BME280 Humidity (Reference)");

  lastDisplayTime = millis(); // Initialize lastDisplayTime
}

void loop() {
  unsigned long currentMillis = millis();

  // Display data every minute
  if (currentMillis - lastDisplayTime >= LOG_INTERVAL_MS) {
    lastDisplayTime = currentMillis; // Update last display time

    // Calculate simulated time from millis()
    // This is the total milliseconds from the start of the simulation run on the ESP32
    unsigned long simulated_elapsed_millis = currentMillis; 
    
    // Calculate simulated hour for diurnal cycle, adjusting for initial start hour
    float current_sim_hour_total = (float)simulated_elapsed_millis / 3600000.0; // Total hours elapsed
    float simulated_hour_of_day = fmodf((START_HOUR + START_MINUTE / 60.0 + current_sim_hour_total), 24.0);

    // --- Simulate AHT20+BMP280 Readings ---
    // Higher noise, slight temp offset, slight humid offset
    float aht_bmp_temp = simulateTemperature(simulated_hour_of_day, base_temp_c, 0.5, 0.2); // +-0.5 C noise, +0.2 C offset
    float aht_bmp_humid = simulateHumidity(simulated_hour_of_day, base_humidity_rh, 2.0, -1.0); // +-2.0 %RH noise, -1.0 %RH offset

    // --- Simulate BME280 (Reference) Readings ---
    // Lower noise, no significant offsets (closer to 'true' value)
    float bme_temp = simulateTemperature(simulated_hour_of_day, base_temp_c, 0.1, 0.0); // +-0.1 C noise, no offset
    float bme_humid = simulateHumidity(simulated_hour_of_day, base_humidity_rh, 0.5, 0.0); // +-0.5 %RH noise, no offset

    // Get formatted simulated timestamp
    String timestamp_str = getSimulatedTimestamp(simulated_elapsed_millis);

    // Print data to Serial Monitor
    Serial.printf("%s|%.2f|%.2f|%.2f|%.2f\n",
                  timestamp_str.c_str(),
                  aht_bmp_temp,
                  bme_temp,
                  aht_bmp_humid,
                  bme_humid);
  }

  // Check if 24 hours of simulation has passed
  // 24 hours = 24 * 60 minutes * 60 seconds * 1000 milliseconds
  if (currentMillis >= (24UL * 3600UL * 1000UL)) {
    Serial.println("\nSimulation complete (24 hours reached). Halting display.");
    while(true); // Stop display after 24 hours
  }

  // Small delay to prevent too rapid looping when not logging, saves CPU cycles
  delay(10);
}