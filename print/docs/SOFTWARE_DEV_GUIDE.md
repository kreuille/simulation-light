# Software Developer Guide — MSLA Detection / 软件开发者指南

Languages / 语言 : [English](#english) · [中文](#中文)

## English

Audience: software development engineers who continue or extend the detection software.
Goal: give you the architecture, the public API, the data flow, the thresholds, the test
method, and the extension points, so you can take over the code.

For the hardware, the assembly, and the STE100 specification, see `DETECTION_SPEC.md`.

---

## 1. Repository map

| Path | Content |
|---|---|
| `lib/msla_detect/` | Portable C++17 detection library. No Arduino, no dynamic memory. |
| `firmware/banc_msla/banc_msla.ino` | ESP32 firmware. Sensor I/O + serial + it calls the library. |
| `firmware/banc_msla/src/msla_detect` | Symlink to `lib/msla_detect/` (so the Arduino IDE compiles it). |
| `firmware/PROTOCOLE.md` | Serial protocol (French). |
| `simulateur/index.html` | Physics simulator + reference detection (JavaScript). |
| `tests/lib/` | Parity: the C++ library must match the simulator. |
| `tests/couche_live/` | The live firmware detection path on simulator scenarios. |
| `tests/firmware_host/` | The vat control, with a mock Arduino. |
| `tests/protocole/` | The serial protocol. |
| `tests/perf/` | Performance analysis (timing, compute, noise). |

Design rule: the library holds all the detection logic. The firmware only reads sensors,
labels the phases, calls the library, and formats the serial messages. The JavaScript
simulator is the reference; the C++ library must give the same verdicts (tested by parity).

Note: The library header comments are in French. The behavior is also defined by the
tests, which are language-independent. Ask the maintainer for a translation if your team
needs the comments in English.

---

## 2. Build and test

Needs `node`, `g++` (C++17), and `python3` with `numpy`.

```
node tests/simulateur.test.js        # simulator model and detection
bash tests/lib/run.sh                # parity: C++ library == simulator
bash tests/firmware_host/run.sh      # vat control (firmware on PC)
bash tests/protocole/run.sh          # serial protocol
bash tests/couche_live/run.sh        # live per-layer detection path
python3 tests/test_fusion_geometrie.py
bash tests/perf/run.sh               # performance (not a pass/fail test)
```

The continuous integration (`.github/workflows/tests.yml`) runs all the pass/fail tests on
each push and pull request.

To compile the library alone:
```
g++ -std=c++17 -c lib/msla_detect/msla_*.cpp
```

---

## 3. Data flow

### 3.1 In-print detection (one sample to one verdict)

```
HX711 force f[0]  ──►  med3 (3-point median, 1-sample delay)
                         │
Z velocity + UV + L ──►  phase label (0,1,2,3,4) + layer index + area
                         │
                         ▼
              SuiviCouches.pousser(ff, z, ph, ly, area)   # builds one Feature per layer
                         │  (returns a finalized layer at the start of the next peel)
                         ▼
              DetecteurCouche.pousser(Feature, area)       # learning + flags
                         │
                         ▼
              flags  ──►  verdict  OK / PAUSE <code> / STOP <code>   ──►  Serial2
```

In the firmware, `detEchantillon()` does the med3 and the phase label, then calls the two
library classes, then `detFlux()` maps the flags to the serial verdict.

### 3.2 Vat control (a streaming state machine)

```
samples (t, z, f[5], uv)  ──►  BacControleur.maj(...)   # state machine: WAIT→DESC→HOLD
                                      │ (returns an event when a verdict is ready)
                                      ▼
            ControleResultat / PlaneiteResultat / ReferenceResultat  ──►  serial text
```

---

## 4. Public API (by module)

All functions are in `namespace msla`. The caller owns every buffer; the library allocates
nothing. The detection math uses `double` to match the JavaScript reference bit-for-bit.

### 4.1 `msla_filtres`
```cpp
double mediane(const double* a, int n, double* scratch);     // scratch length >= n
void   med3(const double* in, double* out, int n);           // 3-point running median
void   moy_glissante(const double* in, double* out, int n, int w);  // centered moving average
```

### 4.2 `msla_couche` — layer detection
Data types:
```cpp
struct Serie   { const double* t,*f,*z; const int* ph,*ly; int n; };   // f = filtered force
struct Feature { double peak, comp, travel, stat; int iPeak, iComp; }; // NaN = absent
enum   Flag    { F_NODATA, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags { uint8_t n; Flag f[4]; };
```
Batch functions (used by the parity tests; they read a whole series):
```cpp
void    prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);
void    features(const Serie& s, int nLayers, Feature* out, double* scratch, int scratchLen);
void    detect(const Feature* feat, const double* area, int nLayers, const char* key,
               double nm, double obstacleMul, double lowRatio, double fstall, LayerFlags* flags);
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);
```
Streaming classes (used by the firmware; they process one sample or one layer at a time):
```cpp
class DetecteurCouche {          // incremental detect(); same flags as detect()
  void       init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);   // push layers in increasing order
};
class SuiviCouches {             // incremental features(); same features as features()
  void init();
  int  pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea);
  int  terminer(Feature* out, double* outArea);         // flush the last layer at end of print
  // pousser()/terminer() return the finalized layer index, or -1.
};
```
`key` is `"z"`, `"vat"`, `"sg"`, or `"fus"` (the sensor type). It selects the floor
thresholds (see section 5).

### 4.3 `msla_stabilite` — resin settle
```cpp
double slopeAt(const double* t, const double* f, int a, int b);
double resStd(const double* t, const double* f, int a, int b);
struct Settle { double ts; bool found; bool tooShort; double sig, thr; };
void   stability(const Serie& s, const char* key, const StabOpt& opt, const Starts& starts,
                 int nLayers, Settle* down, Settle* top);
```
This measures the time for the force to become stable after the descent and after the peel.
It is the base for the reserved `WAIT`/`GO` protocol messages (see section 7).

### 4.4 `msla_bac` — vat control, planarity, weigh, leak
```cpp
class BacControleur {
  BacArm   armer(int mode, const BacConfig& cfg, BacRef* ref);   // mode 0=control,1=reference,2=planarity
  BacEvent maj(float t, float z, const float f[5], bool uv);     // feed one sample; event when ready
  bool     actif() const; int mode() const; void desarmer();
  const ControleResultat&  controle()  const;   // after BAC_EV_CONTROLE
  const PlaneiteResultat&  planeite()  const;    // after BAC_EV_PLANEITE
  const ReferenceResultat& reference() const;    // after BAC_EV_REFERENCE
};
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);
```
This module uses `float` because it is the reference firmware algorithm and runs on the
sensor values directly.

---

## 5. Threshold reference

The detection compares each layer to the learned level with floor thresholds per sensor
(`msla_couche.cpp`, `floorPour()`):

| key | p (peak) | c (compression) | s (push) |
|---|---|---|---|
| z, fus | 0.4 | 0.8 | 0.25 |
| vat | 2.0 | 2.5 | 1.0 |
| sg | 10 | 12 | (not used) |

The application parameters come from the profile (`APPS` in the simulator; defaults for a
figurine):

| Parameter | Default | Meaning |
|---|---|---|
| lowRatio | 0.6 | FAIBLE if peak < lowRatio × expected − 0.5 × p × nm |
| obstacleMul | 2.5 | OBSTACLE if compression < obstacleMul × expected − c × nm |
| settleMargin | 0 | Settle margin for the stability test |

`nm` is the noise level, adapted to the sensor resolution. `fstall` is the maximum Z motor
force (CRASH if compression ≤ −0.93 × fstall). The firmware holds these as the globals
`detNoise`, `detObstacleMul`, `detLowRatio`, and uses `cfg.zMaxN` for `fstall`.

Flag order in a layer (important for the verdict): CRASH or OBSTACLE, then APPUI, then
FAIBLE, then FEP. The learning uses the last 6 good layers and needs 3 good layers before
it starts. After 4 FAIBLE layers in a row, it learns the new level (this stops false
alarms when the part area changes correctly).

---

## 6. Firmware integration points

In `banc_msla.ino`:

| Function | Role |
|---|---|
| `loop()` | Reads the HX711 modules → `f[5]`, `z`, `uvState`. Calls `ckUpdate()` and `detEchantillon()`. |
| `detEchantillon(t,z,f0)` | med3, phase label, `detSuivi.pousser()` → `detDet.pousser()` → `detFlux()`. |
| `detFlux(ft,area,ly)` | Maps the flags to `OK` / `PAUSE <code>` / `STOP <code>` on Serial2. |
| `detDemarrer()` / `detArreter()` | Start the detection (first `L`) / finalize at `X`. |
| `ckArm()` / `ckUpdate()` | Wrap `BacControleur` for the vat control; format the USB messages. |
| `protocolePoll()` / `protocoleCommande()` | Read and run the Serial2 commands. |

Phase label (in `detEchantillon`): Z velocity gives descent (2) and peel (1); when the
platform is stopped, the UV gives exposure (0), and the Z height gives top wait (3, high)
or bottom wait (4, low). The med3 window is reset at each layer change, so the force of one
layer does not pass into the next.

---

## 7. How to extend

### 7.1 Add the WAIT / GO messages (exposure hold)
The library already measures the settle time (`msla_stabilite`). To add `WAIT`/`GO`:
1. After the descent (phase 2 → stop), run a streaming settle test on the Z-arm force.
2. Send `WAIT` while the slope is above the threshold.
3. Send `GO` when the force is stable.
4. The motherboard holds the exposure until `GO` or a timeout.
Entry point: add the logic in `detEchantillon()` or in a new function, and send on Serial2.

### 7.2 Change the thresholds
- Noise and application parameters: the globals `detNoise`, `detObstacleMul`,
  `detLowRatio` in the firmware. Add a serial command to set them if needed.
- Floor thresholds per sensor: `floorPour()` in `msla_couche.cpp`.
Run `bash tests/lib/run.sh` and `bash tests/couche_live/run.sh` after a change.

### 7.3 Use the 5-cell fusion sensor
The library supports the key `"fus"` (the fusion of the Z arm and the 4 corners), which the
simulator uses for a more sensitive detection. To use it in the firmware, feed the fused
force to the detection instead of `f[0]`, and call `detDet.init("fus", ...)`.

### 7.4 Add a new fault flag
1. Add the flag to the `Flag` enum and the `features`/`detect` logic (and the JavaScript
   simulator, to keep the parity).
2. Map the flag to a verdict in `detFlux()`.
3. Add a scenario to the simulator export and the `couche_live` test.

---

## 8. The tests are the behavior specification

The exact detection behavior is defined by the parity test: the simulator makes scenarios
(force traces and the correct verdict from ground truth), and the C++ library must give the
same verdict. If you change the detection, change the simulator and the library together,
and keep `tests/lib/run.sh` green. This is how the project confirms the firmware detection
without the hardware.

---

## 中文

面向继续开发或扩展检测软件的软件工程师。目标：提供架构、公共 API、数据流、阈值、测试方法与扩展点，
以便你接手代码。硬件、装配与 STE100 规范见 `DETECTION_SPEC.md`。API 签名、代码块与表格为语言无关，
见上方英文版对应小节。

### 1. 仓库结构
`lib/msla_detect/` = 可移植 C++17 检测库（无 Arduino、无动态内存）；
`firmware/banc_msla/banc_msla.ino` = ESP32 固件（传感器 I/O + 串口，调用库）；
`firmware/banc_msla/src/msla_detect` = 指向库的符号链接（供 Arduino IDE 编译）；
`firmware/PROTOCOLE.md` = 串口协议；`simulateur/index.html` = 物理模拟器 + 参考检测（JS）；
`tests/lib/` = 一致性（C++ 库须与模拟器一致）；`tests/couche_live/` = 固件打印中检测路径；
`tests/firmware_host/` = 料槽检查；`tests/protocole/` = 串口协议；`tests/perf/` = 性能分析。

设计原则：所有检测逻辑在库中；固件只读传感器、推断相位、调用库、格式化串口消息。JS 模拟器为参考，
C++ 库须给出相同判定（由一致性测试保证）。
注：库头文件注释为英文+中文双语；实现文件（.cpp/.ino）注释为英文；行为亦由语言无关的测试定义。

### 2. 编译与测试
需 node、g++（C++17）、python3+numpy。测试命令见上方英文版第 2 节。CI 在每次推送/PR 运行全部通过性测试。
单独编译库：`g++ -std=c++17 -c lib/msla_detect/msla_*.cpp`。

### 3. 数据流
**打印中检测（一个采样到一个判定）：** HX711 力 f[0] → med3（3 点中值，延迟 1 个采样）；
由 Z 速度 + UV + 层号 → 相位（0–4）+ 层号 + 面积；→ `SuiviCouches.pousser(...)`（逐层构建一个 Feature，
在下一层剥离开始时返回已结算层）→ `DetecteurCouche.pousser(...)`（学习 + 标志）→ 判定
`OK/PAUSE/STOP` → Serial2。固件中 `detEchantillon()` 完成 med3 与相位标注，再调用两个库类，
最后 `detFlux()` 把标志映射为串口判定。
**料槽检查（流式状态机）：** 采样 (t,z,f[5],uv) → `BacControleur.maj(...)`（状态机 WAIT→DESC→HOLD，
就绪时返回事件）→ `ControleResultat/PlaneiteResultat/ReferenceResultat` → 串口文本。

### 4. 公共 API
全部在 `namespace msla`。调用方拥有所有缓冲区，库不分配内存。检测运算用 `double` 以与 JS 参考逐位一致。
各模块签名见上方英文版第 4 节：`msla_filtres`（mediane / med3 / moy_glissante）、
`msla_couche`（Serie、Feature、Flag、LayerFlags；批处理 prep/features/detect/verdict；
流式类 `DetecteurCouche`、`SuiviCouches`）、`msla_stabilite`（stability 等，用于预留的 WAIT/GO）、
`msla_bac`（`BacControleur`、analysePesee、analyseFuite）。`key` 取 `"z"/"vat"/"sg"/"fus"`，选择地板阈值。

### 5. 阈值表
每传感器的地板阈值（`floorPour()`）：z/fus = {p 0.4, c 0.8, s 0.25}；vat = {2.0, 2.5, 1.0}；
sg = {10, 12, 不用}。应用参数（默认，figurine）：lowRatio 0.6、obstacleMul 2.5、settleMargin 0。
`nm` 为噪声水平（随传感器分辨率自适应）；`fstall` 为 Z 电机最大推力（comp ≤ −0.93 × fstall 判 CRASH）。
固件以全局量 `detNoise`、`detObstacleMul`、`detLowRatio` 保存，`fstall` 用 `cfg.zMaxN`。
一层内标志顺序：CRASH 或 OBSTACLE → APPUI → FAIBLE → FEP。学习用最近 6 个正常层，需 3 个正常层才开始；
连续 4 个 FAIBLE 后学习新水平。

### 6. 固件集成点
`loop()`：读 HX711 → f[5]、z、uvState，调用 `ckUpdate()` 与 `detEchantillon()`；
`detEchantillon(t,z,f0)`：med3、相位标注、`detSuivi.pousser()` → `detDet.pousser()` → `detFlux()`；
`detFlux()`：把标志映射为 Serial2 上的 `OK`/`PAUSE`/`STOP`；
`detDemarrer()`/`detArreter()`：首个 `L` 启动 / `X` 结算；
`ckArm()`/`ckUpdate()`：封装 `BacControleur` 做料槽检查；
`protocolePoll()`/`protocoleCommande()`：读取并执行 Serial2 命令。
相位标注：Z 速度给出下降(2)/剥离(1)；静止时 UV 给出曝光(0)，Z 高度给出顶部等待(3,高)/底部等待(4,低)。
med3 窗口在换层时清空，使一层的力不渗入下一层。

### 7. 如何扩展
**加入 WAIT/GO（曝光等待）：** 库已能测稳定时间（`msla_stabilite`）。下降停止后对 Z 轴力做流式稳定判定，
斜率高于阈值时发 `WAIT`，稳定后发 `GO`，主板据此暂缓曝光直到 `GO` 或超时。入口在 `detEchantillon()` 或新函数。
**改阈值：** 噪声/应用参数用固件全局量；地板阈值在 `floorPour()`。改后运行一致性与 couche_live 测试。
**用 5 传感器融合：** 库支持 `"fus"`；在固件中把融合力喂给检测，并 `detDet.init("fus", ...)`。
**加新故障标志：** 在 `Flag` 枚举与 features/detect 中加入（同时改 JS 模拟器以保持一致），
在 `detFlux()` 映射为判定，并在模拟器导出与 couche_live 测试中加场景。

### 8. 测试即行为规范
确切的检测行为由一致性测试定义：模拟器生成场景（受力曲线 + 来自真值的正确判定），C++ 库回放并须给出相同判定。
改动检测时须同时改模拟器与库，并保持 `tests/lib/run.sh` 通过。这是本项目在没有硬件时确认固件检测的方式。
