

#include "HX711.h"

 

// Shared Clock Pin

const int SHARED_SCK_PIN = 3;

 

// Individual Data Pins for Load Cells

const int LOADCELL1_DOUT_PIN = 2;

const int LOADCELL2_DOUT_PIN = 4;

const int LOADCELL3_DOUT_PIN = 16;

const int LOADCELL4_DOUT_PIN = 15;

 

// Create 4 HX711 scale objects

HX711 scale1;

HX711 scale2;

HX711 scale3;

HX711 scale4;

 

void setup() {

  Serial.begin(921600);

 

  // Initialize all 4 scales using shared SCK pin

  scale1.begin(LOADCELL1_DOUT_PIN, SHARED_SCK_PIN);

  scale2.begin(LOADCELL2_DOUT_PIN, SHARED_SCK_PIN);

  scale3.begin(LOADCELL3_DOUT_PIN, SHARED_SCK_PIN);

  scale4.begin(LOADCELL4_DOUT_PIN, SHARED_SCK_PIN);

 

  Serial.println("Four Load Cell Calibration System Initialized with Shared Clock Line");

}

 

void loop() {

  if (scale1.is_ready() && scale2.is_ready() && scale3.is_ready() && scale4.is_ready()) {

 

    // Reset calibration factor

    scale1.set_scale();

    scale2.set_scale();

    scale3.set_scale();

    scale4.set_scale();

 

    Serial.println("Tare... remove any weights from ALL load cells.");

    delay(5000);

 

    // Tare each scale

    scale1.tare();

    scale2.tare();

    scale3.tare();

    scale4.tare();

 

    Serial.println("Tare done for all load cells...");

    Serial.println("Place a known weight distributed across all load cells...");

    delay(5000);

 

    // Read from each scale

    long reading1 = scale1.get_units(10);

    long reading2 = scale2.get_units(10);

    long reading3 = scale3.get_units(10);

    long reading4 = scale4.get_units(10);

 

    long averageReading = (reading1 + reading2 + reading3 + reading4) / 4;

 

    Serial.println("Individual Load Cell Readings:");

    Serial.print("Load Cell 1: "); Serial.println(reading1);

    Serial.print("Load Cell 2: "); Serial.println(reading2);

    Serial.print("Load Cell 3: "); Serial.println(reading3);

    Serial.print("Load Cell 4: "); Serial.println(reading4);

 

    Serial.print("Average Reading: "); Serial.println(averageReading);

    Serial.println("Calibration factor will be (average reading)/(known weight)");

    Serial.println("----------------------------------------");

 

  } else {

    Serial.println("Checking HX711 connections:");

    if (!scale1.is_ready()) Serial.println("HX711 #1 not found.");

    if (!scale2.is_ready()) Serial.println("HX711 #2 not found.");

    if (!scale3.is_ready()) Serial.println("HX711 #3 not found.");

    if (!scale4.is_ready()) Serial.println("HX711 #4 not found.");

  }

 

  delay(1000);

}

 