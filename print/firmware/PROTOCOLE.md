# Protocole série / Serial protocol / 串口协议 — banc ↔ carte mère

Langues : [Français](#français) · [English](#english) · [中文](#中文)

Liaison entre le **banc de mesure** (ESP32) et la **carte mère** de l'imprimante, sur
`Serial2`. La liaison USB brute (CSV à 921600 bauds) reste indépendante.

Link between the **measurement bench** (ESP32) and the printer **motherboard**, on
`Serial2`. The raw USB link (CSV at 921600 baud) stays independent.

测量台（ESP32）与打印机**主板**之间的 `Serial2` 链路。原始 USB 链路（921600 波特 CSV）相互独立。

---

## Français

### 1. Couche physique
| Paramètre | Valeur |
|---|---|
| Port ESP32 | `Serial2` (UART2) |
| Vitesse | 115200 bauds, 8N1 |
| Broches | RX2 = GPIO16, TX2 = GPIO17 (3,3 V) |
| Trames | lignes ASCII terminées par `\n` (LF), 32 caractères max |
| Maître | la carte mère |

### 2. Carte mère → banc
| Trame | Effet |
|---|---|
| `L <n> [aire]` | début de la couche n ; `aire` = surface exposée en mm² (recommandée) |
| `U 1` / `U 0` | UV allumé / éteint |
| `V` | contrôler le bac ; répond `VAT …` |
| `P` / `R` | pause / reprise |
| `X` | arrêt ; finalise la dernière couche |
| `?` | état ; répond `RDY` |

### 3. Banc → carte mère
| Trame | Signification |
|---|---|
| `RDY` | banc prêt |
| `VAT OK` / `VAT RESIDU` / `VAT ERREUR` | résultat du contrôle du bac |
| `OK <n>` | couche n saine |
| `PAUSE <code> <n>` | anomalie récupérable à la couche n |
| `STOP <code> <n>` | danger à la couche n |
| `ALRM FORCE` | effort au-delà du seuil |
| `ERR` | trame non reconnue |

Correspondance drapeau → verdict : `CRASH`→`STOP CRASH`, `OBSTACLE`→`STOP OBSTACLE`,
`APPUI`→`PAUSE APPUI`, `FAIBLE`→`PAUSE FAIBLE`, `FEP`→`PAUSE FEP`.

### 4. Machine à états et délais
Le verdict d'une couche est émis quand elle est finalisée (au début de l'arrachement de la
couche suivante), ou pour la dernière couche sur `X`. `STOP` est prioritaire : la carte
mère coupe l'UV et remonte le plateau. `PAUSE`/`STOP` sont des **demandes** ; la carte mère
reste maître. Perte de liaison (silence > 2 s pendant une impression) : la carte mère décide.

### 5. État de l'implémentation
Émis : `RDY`, `VAT …`, `OK`/`PAUSE`/`STOP <code> <n>`, `ALRM FORCE`, `ERR`.
Réservé : `WAIT`/`GO` (mise en attente de l'exposition tant que la résine bouge ; la mesure
existe dans `msla_stabilite`).

---

## English

### 1. Physical layer
| Parameter | Value |
|---|---|
| ESP32 port | `Serial2` (UART2) |
| Speed | 115200 baud, 8N1 |
| Pins | RX2 = GPIO16, TX2 = GPIO17 (3.3 V) |
| Frames | ASCII lines end with `\n` (LF), 32 characters max |
| Master | the motherboard |

### 2. Motherboard to bench
| Frame | Function |
|---|---|
| `L <n> [area]` | start of layer n; `area` = exposed area in mm² (recommended) |
| `U 1` / `U 0` | UV on / off |
| `V` | do the vat control; answers `VAT …` |
| `P` / `R` | pause / resume |
| `X` | stop; finalize the last layer |
| `?` | state; answers `RDY` |

### 3. Bench to motherboard
| Frame | Meaning |
|---|---|
| `RDY` | the bench is ready |
| `VAT OK` / `VAT RESIDU` / `VAT ERREUR` | result of the vat control |
| `OK <n>` | layer n is good |
| `PAUSE <code> <n>` | recoverable fault at layer n |
| `STOP <code> <n>` | danger at layer n |
| `ALRM FORCE` | force above the threshold |
| `ERR` | frame not understood |

Flag to verdict: `CRASH`→`STOP CRASH`, `OBSTACLE`→`STOP OBSTACLE`, `APPUI`→`PAUSE APPUI`,
`FAIBLE`→`PAUSE FAIBLE`, `FEP`→`PAUSE FEP`.

### 4. State machine and timing
The verdict of a layer is sent when the layer is finalized (at the start of the peel of the
next layer), or for the last layer on `X`. `STOP` has priority: the motherboard cuts the UV
and lifts the platform. `PAUSE`/`STOP` are **requests**; the motherboard makes the decision.
Loss of link (silence > 2 s during a print): the motherboard decides.

### 5. Implementation state
Sent: `RDY`, `VAT …`, `OK`/`PAUSE`/`STOP <code> <n>`, `ALRM FORCE`, `ERR`.
Reserved: `WAIT`/`GO` (hold the exposure until the resin is stable; the measurement is in
`msla_stabilite`).

### 6. Example
```
motherboard → bench   V
bench → motherboard   VAT OK
motherboard → bench   L 0 18.0
motherboard → bench   U 1
motherboard → bench   U 0
motherboard → bench   L 1 18.0
motherboard → bench   U 1
bench → motherboard   OK 0
...
bench → motherboard   STOP OBSTACLE 30
motherboard → bench   X
```

---

## 中文

### 1. 物理层
| 参数 | 取值 |
|---|---|
| ESP32 端口 | `Serial2`（UART2） |
| 速率 | 115200 波特，8N1 |
| 引脚 | RX2 = GPIO16，TX2 = GPIO17（3.3 V） |
| 帧 | 以 `\n`（LF）结尾的 ASCII 行，最多 32 字符 |
| 主机 | 主板 |

### 2. 主板 → 测量台
| 帧 | 作用 |
|---|---|
| `L <n> [面积]` | 第 n 层开始；`面积` = 曝光面积（mm²，建议提供） |
| `U 1` / `U 0` | 紫外光 开 / 关 |
| `V` | 执行料槽检查；返回 `VAT …` |
| `P` / `R` | 暂停 / 恢复 |
| `X` | 停止；结算最后一层 |
| `?` | 查询状态；返回 `RDY` |

### 3. 测量台 → 主板
| 帧 | 含义 |
|---|---|
| `RDY` | 测量台就绪 |
| `VAT OK` / `VAT RESIDU` / `VAT ERREUR` | 料槽检查结果 |
| `OK <n>` | 第 n 层正常 |
| `PAUSE <code> <n>` | 第 n 层出现可恢复异常 |
| `STOP <code> <n>` | 第 n 层出现危险 |
| `ALRM FORCE` | 受力超过阈值 |
| `ERR` | 无法识别的帧 |

标志到判定：`CRASH`→`STOP CRASH`、`OBSTACLE`→`STOP OBSTACLE`、`APPUI`→`PAUSE APPUI`、
`FAIBLE`→`PAUSE FAIBLE`、`FEP`→`PAUSE FEP`。

### 4. 状态机与时序
某一层的判定在该层结算时发出（即下一层开始剥离时），最后一层在收到 `X` 时结算。`STOP`
优先：主板应切断紫外光并抬升平台。`PAUSE`/`STOP` 都是**请求**，由主板做最终决定。链路中断
（打印过程中静默 > 2 秒）时由主板决定如何处理。

### 5. 实现状态
已发送：`RDY`、`VAT …`、`OK`/`PAUSE`/`STOP <code> <n>`、`ALRM FORCE`、`ERR`。
预留：`WAIT`/`GO`（在树脂稳定前暂缓曝光；测量已在 `msla_stabilite` 中实现）。
