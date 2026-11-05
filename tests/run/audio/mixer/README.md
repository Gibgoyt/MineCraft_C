# tests/run/audio/mixer

**Purpose:** ctest `run_audio_mixer`: writes the mixer's output to memory and checks the samples: one voice exactly, each ear's gain, the volume's square, voices adding up and held to 16 bits, the ninth voice, no sound twice running, every real sound going through whole, the same samples from the same seed, no heap, and what a place comes to for each ear, against what the camera calls right. It also prints what 8 voices cost.
**Tier:** test. Nothing connects to anything, so it can never make a sound.
