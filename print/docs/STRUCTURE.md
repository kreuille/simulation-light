# Repository structure

What each directory contains and what it is for. The repository holds **two
projects**: `print/` (the MSLA force bench — the main work) and `light-engine/`
(a separate, earlier project).

## Visual tree

```
simulation-light/
├── print/                          # MSLA force bench (main project)
│   ├── README.md                   # project readme (French, with EN/中文 overview)
│   ├── BOM.md                      # bill of materials (FR / EN / 中文)
│   ├── simulateur/
│   │   └── index.html              # physics simulator + reference detection (JavaScript)
│   ├── lib/
│   │   └── msla_detect/            # portable C++17 detection library (no Arduino, no malloc)
│   │       ├── msla_filtres.*      #   median, med3, moving average
│   │       ├── msla_couche.*       #   per-layer features, learning, verdict (batch + streaming)
│   │       ├── msla_stabilite.*    #   resin settle
│   │       ├── msla_bac.*          #   vat control, planarity, weigh, leak
│   │       └── msla_detect.h       #   umbrella header
│   ├── firmware/
│   │   ├── PROTOCOLE.md            # serial protocol spec, bench <-> motherboard (FR/EN/中文)
│   │   └── banc_msla/
│   │       ├── banc_msla.ino       # ESP32 firmware (sensor I/O + serial + calls the library)
│   │       └── src/msla_detect ->  # symlink to ../../../lib/msla_detect (Arduino build)
│   ├── docs/
│   │   ├── DETECTION_SPEC.md       # full spec (ASD-STE100 English + 中文), with wiring diagrams
│   │   ├── SOFTWARE.md             # full software documentation (English)
│   │   ├── SOFTWARE_DEV_GUIDE.md   # developer guide: API, data flow, thresholds (EN + 中文)
│   │   └── STRUCTURE.md            # this file
│   ├── tests/
│   │   ├── simulateur.test.js      # simulator model + detection
│   │   ├── test_fusion_geometrie.py#  geometry of the 10.1" study
│   │   ├── lib/                    # parity JS <-> C++ (export scenarios, replay in the library)
│   │   ├── couche_live/            # live per-layer detection through the real firmware path
│   │   ├── firmware_host/          # vat control compiled on a PC (mock Arduino in mock/)
│   │   ├── protocole/              # serial protocol
│   │   └── perf/                   # performance analysis (+ RAPPORT.md), not pass/fail
│   ├── outils/
│   │   ├── logger.py               # record the USB CSV stream to a file
│   │   └── layers_from_images.py   # exposed area per layer from the slice PNGs
│   ├── exemples/                   # example CSV files (test the chain without hardware)
│   └── conception/                 # 10.1" MSLA machine design study (Fusion 360 script)
├── light-engine/                   # separate earlier project (LCD/DLP precision simulator)
└── .github/workflows/tests.yml     # CI: runs all tests on every push and pull request
```

## `print/` — MSLA force bench (the main project)

| Path | Contents | Purpose |
|---|---|---|
| `simulateur/` | `index.html` (single file, FR/EN/中文) | **Physics simulator** + reference detection (JavaScript). Opens in a browser; used to understand, tune, and **fit** the model to your records. It is the **reference** that the C++ library must reproduce. |
| `lib/msla_detect/` | `msla_filtres`, `msla_couche`, `msla_stabilite`, `msla_bac` (.h/.cpp) + `msla_detect.h` | **C++ detection library**: all the logic (filters, per-layer learning, settle, vat control). Portable, no Arduino, no dynamic memory → runs the **same on the ESP32 and on a PC**. |
| `firmware/banc_msla/` | `banc_msla.ino` + `src/msla_detect` (symlink to `lib/`) | **ESP32 firmware**: HX711 reads, Z counting (STEP/DIR), UV sensor, USB CSV stream, motherboard protocol, and the live detection. `src/` is a symlink so the Arduino IDE compiles the library. |
| `firmware/PROTOCOLE.md` | — | **Serial protocol** spec, bench ↔ printer motherboard (trilingual). |
| `docs/` | `DETECTION_SPEC.md`, `SOFTWARE.md`, `SOFTWARE_DEV_GUIDE.md`, `STRUCTURE.md` | **Documentation**: full build/detection spec + wiring, full software documentation, developer guide, and this structure guide. |
| `tests/` | see below | **Test suites**, run in CI on every commit. |
| `outils/` | `logger.py`, `layers_from_images.py` | **PC tools**: record the ESP32 CSV stream; compute the exposed area per layer from slice images. |
| `exemples/` | `enregistrement_exemple.csv`, `couches_figurine.csv` | **Example data** to test the chain (simulator, fit) **without hardware**. |
| `conception/` | `fusion360/MSLA_10pouces/` + `README.md` | **Design study** of a 10.1" MSLA machine (generative Fusion 360 script). A separate topic from the bench. |
| `README.md`, `BOM.md` | — | Project readme and hardware **bill of materials**. |

### `print/tests/`

| Path | Purpose |
|---|---|
| `lib/` | **Parity** JS ↔ C++: `export_scenarios.js` exports scenarios from the simulator, `parite.cpp` replays them in the library and checks **identical verdicts**. |
| `couche_live/` | Tests the **live** per-layer detection through the real firmware path (fault at the correct layer, no false alarm). |
| `firmware_host/` | Compiles the **firmware on a PC** with a mock Arduino (`mock/`) and tests the vat control on synthetic CSV. |
| `protocole/` | Tests the **serial protocol** (commands + VAT verdict). |
| `perf/` | **Performance analysis** (not in CI): real-time budget, compute cost, noise robustness, + `RAPPORT.md`. |
| `simulateur.test.js`, `test_fusion_geometrie.py` | Simulator model + detection; geometry of the 10.1" study. |

## `light-engine/` — separate earlier project

An LCD / DLP precision simulator (with its own `simulateur/` and `tests/`), from an
earlier commit. Independent of the MSLA force bench; untouched by the bench work.

## Build and CI

The Arduino IDE compiles `firmware/banc_msla/` and its `src/` subfolder (the library,
via the symlink). On a PC, the tests include the library directly and mock the Arduino
API. `.github/workflows/tests.yml` runs every pass/fail test on each push and pull
request.
