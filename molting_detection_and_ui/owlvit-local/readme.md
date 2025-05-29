# OwlViT – Open-World Vision Transformer

## Overview

This directory integrates the **OwlViT (Open-World Vision Transformer)** model into our Penguin Molt Detection system. OwlViT enables flexible object detection and zero-shot classification using natural language descriptions.

We use OwlViT for its ability to detect specific body parts or molt-related features in penguins by querying with text prompts like `"partially molted penguin"` or `"bare flipper"`.

---

## What is OwlViT?

OwlViT is a transformer-based model developed by **Meta AI** for **open-vocabulary object detection**. It supports detection of objects described by arbitrary text, without requiring fixed label sets.

> 📖 **Paper:** [OwlViT: Open-Vocabulary Vision Transformer](https://arxiv.org/abs/2205.06230)  
> 🧠 **Authors:** Xinyu Chen, Marcus Rohrbach, et al.  
> 🔗 **Official GitHub:** [https://github.com/google-research/scenic](https://github.com/google-research/scenic)

---

## Credits

This implementation or checkpoint of OwlViT is derived from the official [Google Research Scenic repository](https://github.com/google-research/scenic) and the [OwlViT model release](https://github.com/google-research/scenic/tree/main/scenic/projects/owl_vit).

We do **not claim ownership** of the original model. All credit goes to the authors and maintainers.

---

## Usage

In this project, OwlViT is used for:

- **Region-based prompt detection:** e.g., "molt patch", "missing feathers"
- **Zero-shot classification** of penguin conditions
- Supporting ecological insights without retraining

Example prompt-based usage:

```python
prompt = ["molting penguin", "fully feathered penguin"]
results = owl_vit.detect(image, prompt)
