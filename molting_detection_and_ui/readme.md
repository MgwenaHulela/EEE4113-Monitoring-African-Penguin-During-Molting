# Penguin Molt Detection Subsystem

## Overview

The Penguin Molt Detection Subsystem is a core component of the **Automated Penguin Molt Detection** project. This subsystem processes input images of penguins and automatically detects and classifies their molting stages using image processing and machine learning techniques.

It is designed to:

- Identify and isolate penguin images from background scenes.
- Extract relevant visual features related to molting (e.g., feather loss patterns).
- Classify the molt stage using a trained machine learning model.
- Output molt stage predictions with confidence scores for ecological monitoring.

---

## Features

- **Image Preprocessing:** Resizing, normalization, and noise reduction.
- **Penguin Segmentation:** Extracts penguin regions using computer vision algorithms.
- **Feature Extraction:** Identifies key molt-related visual cues.
- **Molting Stage Classification:** Uses a supervised ML model (e.g., CNN, SVM) trained on labeled molt stage images.
- **Batch Processing:** Supports processing of multiple images over time for tracking changes.



## Requirements

- Python 3.8+
- OpenCV
- scikit-learn / TensorFlow / PyTorch (depending on ML model)
- NumPy
- pandas


---


