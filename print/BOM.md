# Nomenclature / Bill of Materials / 物料清单 — banc de mesure MSLA

Langues : [Français](#français) · [English](#english) · [中文](#中文)

Détails de montage et de câblage : voir `README.md` et `docs/DETECTION_SPEC.md`.
Assembly and wiring: see `README.md` and `docs/DETECTION_SPEC.md`.
装配与接线详见 `README.md` 和 `docs/DETECTION_SPEC.md`。

---

## Français

Deux configurations :
- **Minimale** (`cells 1`) : bras Z seul — détection d'impression et contrôle du bac.
- **Complète** (`cells 5`) : + 4 cellules sous le bac — pesée, positionnement, fuite,
  planéité et localisation du résidu.

| Qté | Composant | Réf. / caractéristique | Rôle | Config |
|---|---|---|---|---|
| 1 | Carte MCU | ESP32 DevKit (ESP32-WROOM-32) | Mesure, calcul, USB + liaison carte mère | Toutes |
| 1 | Cellule de charge en poutre | 20 kg (10 kg suffit en petit format) | Force sur le bras Z | Toutes |
| 1 | Module | HX711 | Convertisseur 24 bits de la cellule Z | Toutes |
| 1 | Capteur UV analogique | GUVA-S12SD (ou prise directe de la commande des LED UV) | Détecter l'exposition | Toutes |
| 2 | Optocoupleur rapide | 6N137 + résistances 330 Ω et 10 kΩ | Prélever STEP et DIR sans perturber l'imprimante | Toutes |
| — | Câblage | Câble blindé 4 conducteurs, connecteurs, gaine | Liaisons cellules / capteurs | Toutes |
| — | Mécanique | Pièces de fixation imprimées ou usinées | Adaptateurs chariot Z ↔ cellule ↔ plateau | Toutes |
| 4 | Cellules de charge en poutre | 10 kg (coins du bac) | Pesée, positionnement, fuite, planéité | Complète |
| 4 | Modules | HX711 supplémentaires | Convertisseurs des 4 coins | Complète |

Budget indicatif : 25 à 45 €. Bras Z ≈ 10 € ; 4 coins ≈ 20 €.

**Attention :** la cellule du bras Z doit avoir une capacité ≥ force maximale du moteur Z,
sinon un écrasement la détruit.

---

## English

Two configurations:
- **Minimum** (`cells 1`): the Z arm only — in-print detection and basic vat control.
- **Full** (`cells 5`): with 4 vat load cells — weigh, position, leak, and planarity.

| Qty | Part | Specification | Function | Config |
|---|---|---|---|---|
| 1 | Microcontroller board | ESP32 DevKit (ESP32-WROOM-32) | Measurement, calculation, USB and motherboard link | All |
| 1 | Beam load cell | 20 kg (10 kg is enough on a small printer) | Force on the Z arm | All |
| 1 | ADC module | HX711 | Reads the Z arm load cell | All |
| 1 | UV sensor | GUVA-S12SD (or a direct pickup of the UV LED signal) | Finds the exposure | All |
| 2 | Fast optocoupler | 6N137, with 330 Ω and 10 kΩ resistors | Picks up STEP and DIR safely | All |
| — | Cable | Shielded, 4 conductors, connectors, sleeve | Sensor connections | All |
| — | Mechanical parts | 3D-printed or machined brackets | Adapters for the Z arm and the cell | All |
| 4 | Beam load cell | 10 kg (vat corners) | Weigh, position, leak, planarity | Full |
| 4 | ADC module | HX711 | Reads the 4 corner load cells | Full |

Indicative budget: 25 to 45 EUR. Z arm about 10 EUR; 4 corners about 20 EUR.

**Caution:** The Z arm load cell must have a capacity equal to or more than the maximum Z
motor force. If not, a crush event can break the cell.

---

## 中文

两种配置：
- **最小配置**（`cells 1`）：仅 Z 轴力臂 — 打印中检测与基本料槽检查。
- **完整配置**（`cells 5`）：加 4 个料槽测力传感器 — 称重、定位、漏液与平面度。

| 数量 | 部件 | 规格 | 功能 | 配置 |
|---|---|---|---|---|
| 1 | 微控制器板 | ESP32 DevKit（ESP32-WROOM-32） | 测量、计算、USB 与主板通信 | 全部 |
| 1 | 梁式测力传感器 | 20 kg（小型机 10 kg 足够） | Z 轴力臂受力 | 全部 |
| 1 | ADC 模块 | HX711 | 读取 Z 轴力臂传感器 | 全部 |
| 1 | 紫外光传感器 | GUVA-S12SD（或直接取 UV LED 信号） | 检测曝光 | 全部 |
| 2 | 高速光耦 | 6N137，配 330 Ω 和 10 kΩ 电阻 | 安全地采集 STEP 和 DIR | 全部 |
| — | 线缆 | 屏蔽 4 芯线、连接器、套管 | 传感器连接 | 全部 |
| — | 机械件 | 3D 打印或机加工支架 | Z 轴力臂与传感器的适配件 | 全部 |
| 4 | 梁式测力传感器 | 10 kg（料槽四角） | 称重、定位、漏液、平面度 | 完整 |
| 4 | ADC 模块 | HX711 | 读取四角传感器 | 完整 |

参考预算：25 至 45 欧元。Z 轴约 10 欧元；四角约 20 欧元。

**注意：** Z 轴力臂传感器的量程必须 ≥ Z 轴电机最大推力，否则一次挤压即可损坏传感器。
