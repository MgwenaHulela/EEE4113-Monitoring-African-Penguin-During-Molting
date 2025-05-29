#include <Arduino.h>
#include <HX711.h>

#define DOUT_PIN 27
#define CLK_PIN  4

HX711 scale;

// —— USER CONFIG ——————————————————————————————————————————————
// Place this known weight on the scale during setup (grams):
static constexpr float KNOWN_WEIGHT = 1000.0f;

// How many pre-samples to average at zero and at known weight:
static constexpr uint8_t CAL_SAMPLES = 50;

// Moving-average window:
static constexpr uint8_t MA_N = 5;

// Kalman filter tuning:
//    Increase Q for faster tracking (but more jitter).
//    Decrease R if you trust the sensor more (less smoothing).
static constexpr double Q = 50.0;   
static constexpr double R =  10.0;   

// —— END USER CONFIG ——————————————————————————————————————————

// runtime state
float CALIBRATION_FACTOR = 1.0f;
double P = 1.0, x = NAN;      // Kalman state
float ma_buf[MA_N];           
uint8_t ma_idx = 0;           
bool    ma_full = false;      

// —— SETUP —————————————————————————————————————————————————————
void setup() {
  Serial.begin(115200);
  scale.begin(DOUT_PIN, CLK_PIN);

  // 1) Zero offset
  Serial.println("\n--- Zeroing (no load) ---");
  scale.tare();
  delay(500);

  // 2) Prompt for known weight
  Serial.printf("Place %.0fg on scale...\n", KNOWN_WEIGHT);
  delay(5000);

  // 3) Read raw counts under known weight
  double sum = 0;
  for (uint8_t i = 0; i < CAL_SAMPLES; i++) {
    sum += scale.read();
    delay(50);
  }
  double raw_avg = sum / CAL_SAMPLES;
  CALIBRATION_FACTOR = raw_avg / KNOWN_WEIGHT;
  scale.set_scale(CALIBRATION_FACTOR);

  Serial.printf("Calibrated: factor = %.4f (raw_avg=%.1f)\n\n", CALIBRATION_FACTOR, raw_avg);

  // init moving-average buffer
  for (uint8_t i = 0; i < MA_N; i++) ma_buf[i] = 0.0f;
  Serial.println("=== Ready! ===\n");
}

// simple N-point moving average of scale.get_units()
float movingAverage() {
  float w = scale.get_units(3);
  ma_buf[ma_idx] = w;
  ma_idx = (ma_idx + 1) % MA_N;
  if (ma_idx == 0) ma_full = true;

  float s = 0;
  uint8_t count = ma_full ? MA_N : ma_idx;
  for (uint8_t i = 0; i < count; i++) s += ma_buf[i];
  return s / count;
}

// standard Kalman predict/update
double kalman(double z) {
  if (isnan(x)) {
    x = z;    // seed with first measurement
    return x;
  }
  P += Q;
  double Kgain = P / (P + R);
  x += Kgain * (z - x);
  P *= (1 - Kgain);
  return x;
}

void loop() {
  if (!scale.wait_ready_timeout(200)) {
    Serial.println("HX711 not ready");
    delay(200);
    return;
  }

  float raw_g = movingAverage();
  double filt  = kalman(raw_g);

  // clamp negative
  if (filt < 0) filt = 0;

  Serial.printf("Raw: %.1f g  |  Filtered: %.1f g\n", raw_g, filt);
  delay(300);
}
