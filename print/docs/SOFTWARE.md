# MSLA Force Bench — Software Documentation

Complete documentation of the software part: architecture, modules, detection
algorithms, firmware structure, serial interfaces, API, thresholds, performance,
tests, and extension points.

Audience: software engineers who build, maintain, or extend the detection software.
For the hardware and the assembly, see `DETECTION_SPEC.md`. For the serial protocol
details, see `firmware/PROTOCOLE.md`. For a shorter API-first view, see
`SOFTWARE_DEV_GUIDE.md`.

---

## 1. Scope and goals

The software turns an MSLA resin printer into a measurement and detection system. It has
two independent detection functions:

1. **In-print fault detection** — during a print, it learns the normal behaviour layer by
   layer and flags detachment, support failure, debris, and loose film.
2. **Vat control before a print** — debris on the film, platform planarity, vat presence
   and placement, resin quantity, and leak.

Design goals that shape the whole software:

- **The detection runs on the device.** The ESP32 performs the detection; a PC is only
  used for logging, tuning, and running the simulator and the tests.
- **One source of truth.** All detection logic lives in a portable C++ library. The
  firmware and the tests use the same library.
- **Verifiable without hardware.** A JavaScript simulator produces scenarios (force traces
  and the correct verdict from ground truth); the C++ library must give the same verdict
  (parity tests).
- **Embedded constraints.** The library uses no dynamic memory and has no Arduino
  dependency, so it compiles and runs the same on the ESP32 and on a PC.

---

## 2. Architecture overview

```
   Simulator (JavaScript, simulateur/index.html)
     - physics model + reference detection
     - exports scenarios (traces + expected verdict)
            |
            |  parity (the C++ library must match the JS verdict)
            v
   Library (C++17, lib/msla_detect/)         <-- all detection logic, no Arduino, no malloc
     msla_filtres   median, med3, moving average
     msla_couche    per-layer features, learning, verdict (batch + streaming)
     msla_stabilite resin settle
     msla_bac       vat control, planarity, weigh, leak
            ^
            |  uses
            |
   Firmware (Arduino/ESP32, firmware/banc_msla/banc_msla.ino)
     - sensor I/O (HX711, STEP/DIR, UV), calibration, Preferences
     - USB CSV stream (to the PC/logger)
     - Serial2 protocol (to the printer motherboard)
     - live detection pipeline: sample -> phase label -> library -> verdict
```

**Build model.** The Arduino IDE compiles the sketch folder and its `src/` subfolder.
`firmware/banc_msla/src/msla_detect` is a symbolic link to `lib/msla_detect`, so the
device build and the PC build share the exact same library sources. On a host, the tests
include the library directly and mock the Arduino API.

**Why a separate library.** Keeping the logic out of the `.ino` makes it testable on a PC,
reusable (for example on another board or a measurement coprocessor), and verifiable
against the simulator by parity.

---

## 3. Repository layout

| Path | Role |
|---|---|
| `simulateur/index.html` | Physics simulator + reference detection (JavaScript, single file) |
| `lib/msla_detect/` | Portable C++17 detection library |
| `firmware/banc_msla/banc_msla.ino` | ESP32 firmware (I/O + serial + calls the library) |
| `firmware/banc_msla/src/msla_detect` | Symlink to the library (so the Arduino IDE compiles it) |
| `firmware/PROTOCOLE.md` | Serial protocol specification (FR/EN/中文) |
| `tests/lib/` | Parity: the C++ library must match the simulator |
| `tests/couche_live/` | The live firmware detection path on simulator scenarios |
| `tests/firmware_host/` | The vat control, with a mock Arduino |
| `tests/protocole/` | The serial protocol |
| `tests/perf/` | Performance analysis (timing, compute, noise) |
| `outils/logger.py` | Records the USB CSV stream to a file with a metadata header |
| `outils/layers_from_images.py` | Computes the exposed area per layer from the layer PNGs |

---

## 4. Build, upload, and test

### 4.1 Firmware (device)
1. Install Arduino IDE 2 and the Espressif ESP32 board package.
2. Open `firmware/banc_msla/banc_msla.ino`, select "ESP32 Dev Module", upload.
3. Open the serial terminal at 921600 baud, line end LF, type `aide`.

Note: on Windows, if the `src/msla_detect` symlink is not restored by git, replace it with
a copy of `lib/msla_detect/`.

### 4.2 Library (PC)
```
g++ -std=c++17 -c lib/msla_detect/msla_*.cpp
```

### 4.3 Tests (PC)
Needs `node`, `g++` (C++17), and `python3` with `numpy`.
```
node tests/simulateur.test.js        # simulator model and detection
bash tests/lib/run.sh                # parity: C++ library == simulator
bash tests/firmware_host/run.sh      # vat control (firmware on a PC)
bash tests/protocole/run.sh          # serial protocol
bash tests/couche_live/run.sh        # live per-layer detection path
python3 tests/test_fusion_geometrie.py
bash tests/perf/run.sh               # performance (analysis, not pass/fail)
```
The CI (`.github/workflows/tests.yml`) runs all the pass/fail tests on each push and pull
request.

---

## 5. Data model

A sampled series is a set of parallel arrays owned by the caller:

```cpp
struct Serie { const double* t, *f, *z; const int* ph, *ly; int n; };  // f = filtered force
```

Each sample has a **phase** and a **layer index**:

| Phase ph | Meaning | Platform |
|---|---|---|
| 4 | bottom wait | down, UV off, before exposure |
| 0 | exposure | down, UV on |
| 1 | peel | moves up (pulls the layer off the film) |
| 3 | top wait | up, stopped |
| 2 | descent | moves down to the next layer |

Within one layer the block order is **4 → 0 → 1 → 3 → 2**. `ly` is -1 outside an identified
layer.

Per-layer **features**:

```cpp
struct Feature { double peak, comp, travel, stat; int iPeak, iComp; };  // NaN = absent
```
- **peak** — max peel force (phase 1), above the top tare.
- **comp** — min compression (phase 2 of this layer, plus phases 0/4 of the next layer).
- **travel** — Z at the peak.
- **stat** — change of the end-of-exposure force between successive layers.

Per-layer **flags** and **verdict**:

```cpp
enum Flag { F_NODATA, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags { uint8_t n; Flag f[4]; };          // ordered flags
struct Verdict { int first; Flag kind; int fa, nodata, lat; bool hasFirst; };
```

---

## 6. Signal processing (`msla_filtres`)

- `mediane(a, n, scratch)` — median of n values (needs a scratch buffer, does not modify a).
- `med3(in, out, n)` — 3-point running median; the ends are copied.
- `moy_glissante(in, out, n, w)` — centered moving average of window w.

The Z channel uses `med3` (removes read spikes without smoothing the peak). The vat and
StallGuard channels add a 3-point moving average.

All detection math is in **double precision**, like the JavaScript Number, so that threshold
comparisons give the same result as the simulator.

---

## 7. In-print detection (`msla_couche`)

### 7.1 Feature extraction (`features`)
For each layer:
- `top[L]` = median of all phase-3 (top wait) forces.
- `pre[L]` = median of the last 30% of phase-0 (exposure) forces.
- `peak[L]` = max over phase 1 of `ff - pre[L]`; `travel[L]` = Z at that max.
- `comp[L]` = min over {phase 2 of L, phases 0/4 of L+1} of `ff - top[L]`.
- `stat[L]` = `pre[L+1] - pre[L]`.

### 7.2 Learning and flags (`detect`)
The detector learns the normal level from the **last 6 good layers** and divides `peak` and
`comp` by the layer **area** to remove the area effect. It needs **3 good layers** before it
starts. `nm` is the noise level (adapted to the sensor resolution), `fstall` the maximum Z
motor force, and the floor thresholds come from `floorPour(key)`.

A layer gets a flag when (summary):

| Flag | Condition |
|---|---|
| CRASH | `comp <= -0.93 * fstall` (StallGuard: `-min(95, 0.68*fstall)`) |
| OBSTACLE | `comp < obstacleMul * min(expectedComp, 0) - c * nm` |
| APPUI | `stat < -s * nm` (not for StallGuard) |
| FAIBLE | `expectedPeak > 2*p*nm` and `peak < lowRatio * expectedPeak - 0.5*p*nm` |
| FEP | `travel/peak > 1.7 * median(travel/peak over big good layers) + 0.01` |

Flag order in a layer: **CRASH or OBSTACLE, then APPUI, then FAIBLE, then FEP**. If **4
FAIBLE layers** come in a row, the detector learns the new level (this stops false alarms
when the part area changes correctly).

### 7.3 Verdict (`verdict`)
Given the flags and a scenario (used by the tests), it returns the first faulty layer, the
kind, the number of false alarms before the start, the layers with no data, and the
latency. In the firmware, the live detector maps the flags directly to a protocol verdict
(see section 9.3) rather than using `verdict`.

### 7.4 Streaming versions
The batch `features`/`detect` read a whole series. For the firmware, two incremental
classes process one sample or one layer at a time and give **identical** results (verified
by parity):
- `SuiviCouches` — builds one `Feature` per layer from the stream. It buffers only the
  exposure (phase 0) and top-wait (phase 3) samples for the medians, with a sliding window
  sized for the long base-layer exposures. It finalizes layer L when the peel of layer L+1
  starts, and the last layer on demand.
- `DetecteurCouche` — the incremental learning; push `Feature` values in increasing order,
  it returns the flags.

---

## 8. Resin settle (`msla_stabilite`)

Measures, per layer, the time for the force to become stable after the descent and after
the peel (`slopeAt`, `resStd`, `stability`). It is the basis for the reserved `WAIT`/`GO`
protocol messages (hold the exposure until the resin is still). The measurement exists;
wiring it to Serial2 is a future step.

---

## 9. Firmware structure

### 9.1 Main loop and I/O
`loop()` reads the HX711 modules (bit-banged, common clock) into `f[5]`, counts the STEP/DIR
pulses into `z`, reads the UV ADC, then:
- streams one CSV line on USB (if streaming);
- feeds the vat controller (`ckUpdate`) and the live detector (`detEchantillon`);
- drives the alert LED and sends `ALRM FORCE` on a rising force edge.

Calibration and settings (cell zero/scale, steps per mm, UV thresholds, vat thresholds,
detection parameters) are set by serial commands and kept in flash (`Preferences`). Type
`aide` for the full command list.

### 9.2 USB data format
One CSV line per sample at 921600 baud:
```
t_s,fz_N[,c0_N,c1_N,c2_N,c3_N],z_mm,uv,uv_raw
```
Sign convention: `+` = pull on the film (peel), `-` = compression.

### 9.3 Live detection pipeline
In `detEchantillon(t, z, f0)`:
1. **med3** running median on the force (1-sample delay), aligned to the middle sample.
2. **Phase label** from the Z velocity and the UV: velocity down → phase 2, up → phase 1;
   still and UV on → phase 0; still and UV off → phase 3 (Z high) or phase 4 (Z low).
3. The **layer** and the **area** come from the motherboard (`L n [area]`).
4. The med3 window is **reset at each layer change**, so one layer's force does not leak
   into the next.
5. Feed `SuiviCouches.pousser(...)`; when a layer is finalized, feed `DetecteurCouche` and
   map the flags: CRASH/OBSTACLE → `STOP`, APPUI/FAIBLE/FEP → `PAUSE`, else `OK`, sent on
   Serial2 as `OK <n>` / `PAUSE <code> <n>` / `STOP <code> <n>`.

The verdict of layer L is sent at the start of the peel of layer L+1 (one layer of delay),
and the last layer on `X`.

### 9.4 Vat control
`ckArm`/`ckUpdate` wrap the library `BacControleur` (a streaming state machine
WAIT→DESC→HOLD). The firmware formats the French operator messages from the returned
result structs. The reference is recorded once on a clean vat (`ref`); a check gives
`CONTROLE OK` / `CONTROLE RESIDU` with a risk level, and the `V` protocol command returns
`VAT OK` / `VAT RESIDU`.

---

## 10. Serial interfaces

| Interface | Port | Use |
|---|---|---|
| USB CSV | Serial, 921600 baud | Raw measurement stream to the PC/logger |
| Operator terminal | Serial (same USB) | Calibration and vat-control commands (`aide`) |
| Motherboard protocol | Serial2, 115200 baud, GPIO16/17 | Layer/UV in, verdicts out |

The motherboard protocol (full spec in `firmware/PROTOCOLE.md`): motherboard → bench
`L n [area]`, `U 1/0`, `V`, `P`, `R`, `X`, `?`; bench → motherboard `RDY`, `VAT ...`,
`OK <n>`, `PAUSE <code> <n>`, `STOP <code> <n>`, `ALRM FORCE`, `ERR`. Reserved: `WAIT`/`GO`.

---

## 11. Public API reference

All in `namespace msla`. The caller owns every buffer; the library allocates nothing.

**msla_filtres**: `mediane`, `med3`, `moy_glissante`.

**msla_couche**: types `Serie`, `Feature`, `Flag`, `LayerFlags`, `Verdict`; batch
`prep`, `features`, `detect`, `verdict`; streaming classes `DetecteurCouche`
(`init`, `pousser`) and `SuiviCouches` (`init`, `pousser`, `terminer`). `key` selects the
floor thresholds: `"z"`, `"vat"`, `"sg"`, `"fus"`.

**msla_stabilite**: `slopeAt`, `resStd`, `stability`, `sensSummary`.

**msla_bac**: `BacControleur` (`armer`, `maj` → `BacEvent`, `actif`, `desarmer`,
`controle`, `reference`, `planeite`); `analysePesee`; `analyseFuite`; structs
`BacConfig`, `BacRef`, `ControleResultat`, `PlaneiteResultat`, `PeseeResultat`,
`FuiteResultat`. This module uses `float` (it is the reference firmware algorithm).

Header comments are in English with a Chinese module summary; the exact signatures are in
`lib/msla_detect/*.h`.

---

## 12. Threshold and parameter reference

Floor thresholds per sensor (`floorPour`):

| key | p (peak) | c (compression) | s (push) |
|---|---|---|---|
| z, fus | 0.4 | 0.8 | 0.25 |
| vat | 2.0 | 2.5 | 1.0 |
| sg | 10 | 12 | (not used) |

Application parameters (defaults for a figurine): `lowRatio` 0.6, `obstacleMul` 2.5,
`settleMargin` 0. The firmware holds the detection calibration as the globals `detNoise`
(the adapted noise `nm`), `detObstacleMul`, `detLowRatio`, and uses `cfg.zMaxN` for
`fstall`. Vat-control thresholds: `ckStatN` 0.3 N, `ckDescN` 0.4 N, hold 5 s, risk levels
moderate / high / danger.

---

## 13. Performance (real time)

From `tests/perf/`:
- Reading 5 cells takes ~170 µs; at 80 SPS (period 12.5 ms) the CPU reads ~1.4% of the
  time, the USB uses ~5% of its capacity, the motherboard link <0.1%. More than 90% of CPU
  time is free for the detection.
- The detection worst case is ~37 µs on a PC (the sample that finalizes a layer); the
  estimated ESP32 worst case is ~1.8 ms, well below the 12.5 ms budget.
- The detection is robust: no false alarm at the nominal noise level, clear faults detected
  up to ~8x the nominal noise, debris (the weakest signal) down to ~6x.

---

## 14. Testing and the parity method

The exact detection behaviour is the **parity test**: the simulator makes scenarios (force
traces and the correct verdict from ground truth) and the C++ library must give the same
verdict. `tests/couche_live` drives the real firmware path (deriving the phases itself from
the Z movement) and checks that the fault is found at the correct layer with no false
alarm. If you change the detection, change the simulator and the library together and keep
`tests/lib/run.sh` green.

---

## 15. Extension points

- **WAIT/GO (exposure hold):** run a streaming settle test (from `msla_stabilite`) after the
  descent and send `WAIT`/`GO` on Serial2. Entry point: `detEchantillon()` or a new function.
- **Change thresholds:** the globals `detNoise`, `detObstacleMul`, `detLowRatio`; the floor
  thresholds in `floorPour()`. Re-run the parity and `couche_live` tests.
- **5-cell fusion sensor:** the library supports `"fus"`; feed the fused force and call
  `detDet.init("fus", ...)`.
- **New fault flag:** add it to the `Flag` enum and the `features`/`detect` logic (and the
  simulator, to keep the parity), map it in `detFlux()`, and add a scenario.

---

## 16. Conventions and constraints

- **No dynamic memory** in the library; fixed-size buffers or caller-provided buffers.
- **Double precision** for the detection math (parity with the JavaScript Number).
- **Comment language:** library and firmware code comments are in English; library headers
  add a Chinese module summary. The firmware serial messages to the operator and the tool
  command-line help stay in French (operator UI), and the simulator API values
  (`'modere'`, `'hors'`, `'leve'`, ...) stay as defined.
- **No PC required** for the detection: it runs on the ESP32; the PC is for logging, tuning,
  and the simulator.

---

## 17. Glossary

| Term | Meaning |
|---|---|
| Peel / peak | the force to pull a cured layer off the film |
| Compression (comp) | the most negative force on the descent/contact |
| Push (appui) | residual static load (a foreign body) |
| Travel | the Z position at the peel peak |
| Stat | end-of-exposure force difference between layers |
| nm | the noise level, adapted to the sensor resolution |
| fstall | the maximum Z motor force |
| Parity | the C++ library gives the same verdict as the simulator |
