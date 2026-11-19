# MSLA bench performance simulation

Three parts, run by `bash tests/perf/run.sh`:
1. **Real-time budget** of the acquisition chain (`budget.py`, analytical);
2. **Compute cost** of the detection (`bench.cpp`, real measurement on a PC);
3. **Noise robustness** of the signal (`noise_sweep.js`, sweep).

These are analysis tools (not pass/fail tests); the numbers below are the ones obtained
on this code base. Re-run the scripts to reproduce them.

## 1. Real-time budget (sensor -> electronics -> links)

| Item | Value | Comment |
|---|---|---|
| Read of one sample, 1 cell | ~110 us | bit-bang 25 pulses, common clock |
| Read of one sample, 5 cells | ~170 us | the 5 HX711 read **together** (common SCK) |
| Rate 10 SPS | period 100 ms | RATE to ground |
| Rate 80 SPS | period 12.5 ms | RATE to +3.3 V (recommended) |
| CPU busy reading (80 SPS, 5 cells) | ~1.4 % | the rest is the HX711 conversion wait |
| USB CSV (921600 baud), 5 cells @ 80 SPS | ~5 % of the rate | sending one line ~0.63 ms |
| Serial2 motherboard (115200 baud) | < 0.1 % | a few bytes per layer |

**Conclusion.** The rate is limited by the HX711, not by the read or the serial links. At
80 SPS / 5 cells, **more than 90 % of CPU time is free** for the detection. The RATE pin
must be moved to +3.3 V to reach 80 SPS.

## 2. Detection compute cost

Measured by replaying a real print (60 layers, of which 4 base layers with a long
exposure ~2000 samples) through the real `detEchantillon()` function of the firmware:

| Metric | On this PC | Budget (80 SPS) |
|---|---|---|
| Mean time per sample | ~0.05 us | — |
| **Worst case** (sample finalizing a layer) | ~37 us | **12 500 us** |
| Margin on the worst case (PC) | ~340x | — |

The cost is not spread evenly: most of the work (median sorts for the features) happens
**at the end of each layer**, not on each sample.

**ESP32 extrapolation** (240 MHz, double emulated in software): even assuming the ESP32 is
up to 50x slower per operation than this PC, the worst case stays ~**1.8 ms**, well below
the 12.5 ms, with a margin of at least 7x. Real time is met comfortably. *(Value to confirm
on hardware; not measured on the ESP32 here.)*

## 3. Noise robustness

The sensor noise (HX711 noise, 50 Hz mains, motor vibration, thermal drift, spikes) is
injected by the simulator model, with a detection threshold that adapts to the measured
noise (like after a calibration). Fault injected at layer 30. "noise" is a multiplier:
1 = nominal level of the 0.03 N sensor.

| Noise | False alarms (normal) | Detachment | Supports | Debris | Loose film |
|---|---|---|---|---|---|
| 0.5 | rare (1/6 draws) | 100 % (lat 0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 1 (nominal) | **0** | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 2 | 0 | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 4 | 0 | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 6 | 0 | 100 % (0) | 100 % (0) | **miss** | 100 % (5) |
| 8 | 0 | 100 % (0) | ~100 % | **miss** | **miss** |

(lat = latency in layers after the incident; "miss" = fault not detected.)

**Conclusion.**
- **No false alarm** from the nominal noise level (the adaptive threshold absorbs the
  noise). At very low noise, rare false alarms appear because the floor threshold
  dominates; this has no practical effect.
- **Clear** faults (detachment, support failure) are detected up to ~8x the nominal noise,
  at the incident layer.
- The **debris** is the weakest signal (small push / small compression): it drops around
  **6x the nominal noise**. It sets the practical limit.
- Safety margin: the detection stays reliable up to **~4-5x the nominal noise**. For more
  margin, lower the noise (shielded cable, common ground, spike filter) or use a finer
  sensor (sensRes 0.01 N).
