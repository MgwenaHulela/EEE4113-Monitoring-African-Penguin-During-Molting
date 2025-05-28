#  Automatic Monitoring of African Penguins During Molt

## Project Overview

This repository contains the design, implementation, and documentation of a modular system for **automated, non-invasive monitoring** of African Penguins (*Spheniscus demersus*) during their molting period. The system was developed as part of the EEE4113F course in the Department of Electrical Engineering at the University of Cape Town.

The system combines:
- **Load cell-based weighing platform**
- **RFID-based penguin identification**
- **Machine learning-based molting stage classification**
- **Environmental data logging (temperature and humidity)**
- **User-friendly web interface**

---

##  Objectives

- Develop a **non-invasive** monitoring platform.
- Track **individual penguin weight** over time.
- Classify **molting stage** using image analysis.
- Associate data with **RFID-tagged individuals**.
- Ensure **robustness** under harsh coastal conditions.
- Operate within a strict **R1500 budget**.
- Present data via a **simple user interface**.

---

## 🧱 Subsystem Breakdown (File Structure)

The project is organized into the following main subsystems:

### Subsystem Mapping

| Subsystem                         | Directory / Key Files                    | Description |
|----------------------------------|------------------------------------------|-------------|
| Mechanical Platform              | `hardware/`                               | CAD, housing, structural design |
| Electronics & Controls           | `software/embedded/` + `hardware/Load_Cell_Integration/` | Embedded code and electrical design |
| Molting Detection & UI          | `software/ml_model/` + `software/web_dashboard/` | ML training code,pipeline, Flask dashboard, database ,ML dataset and Models,validation results|
| Full System Integration & Docs   | `docs/` + `README.md`                    | Documentation and project coordination |

## 🚀 Getting Started

### Prerequisites
- Arduino IDE for embedded development
- Python 3.x with:
  - `Flask`, `OpenCV`, `TensorFlow`, `sqlite3`, `Pandas`
- ESP32-CAM AND ESP32 dev board and RFID reader hardware
- Load cell and HX711 amplifier

### Setup Instructions

1. **Embedded Setup**
   - Flash `embedded/main.ino` onto the ESP32-CAM and ESP32.
   - Calibrate load cells via serial monitor.
   - Ensure RFID reader is functioning (check serial output).

2. **Run Web Dashboard**
   ```bash
   cd software/molting_detection_UI
   pip install -r requirements.txt
   python hi.py

---

## 📊 Performance Testing

| Metric                        | Result                             |
|------------------------------|------------------------------------|
| Weight Measurement Accuracy  | ±0.1 kg                            |
| Molting Stage Classification | 98.45% validation accuracy (VGG16) |
| RFID Detection Range         | ~5 cm                              |
| Full Inference Time          | < 1.5 seconds (on CPU)             |
| Environmental Durability     | Passed waterproof & weatherproof tests |
| Power Efficiency             | Low-power ESP32 operation w/ deep sleep |
| Budget                       | R1498 (out of R1500 limit)         |

All subsystems were tested using Acceptance Test Procedures (ATP-1 to ATP-6) as detailed in the final report.

---

## 📅 Project Timeline

| Phase                  | Timeframe       |
|------------------------|-----------------|
| Design Phase           | Feb–March 2025  |
| Implementation         | March–April 2025|
| Testing & Integration  | April–May 2025  |
| Final Submission       | May 25, 2025    |

---

## 👥 Contributors

This project was completed as part of the EEE4113F Project at the University of Cape Town.

- **Talifhani Nemaangani** (Talifhani-Cloud)
- **Zwivhuya Ndou**
- **Innocent Makhubela** (MgwenaHulela)

Supervised by the Department of Electrical Engineering, UCT.


