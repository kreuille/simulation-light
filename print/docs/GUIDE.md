# Guide complet — Banc MSLA & détection / Full guide / 完整指南

Langues / Languages / 语言 : [Français](#français) · [English](#english) · [中文](#中文)

> Guide développeur du banc de mesure de forces MSLA et de sa détection : dépôt, arborescence, algorithmes, fonctions, bibliothèque et usage. Généré depuis la version française (maîtresse).

---

## Français

Public : ingénieurs qui reprennent, intègrent ou étendent le banc de mesure MSLA et sa
détection. Objectif : comprendre **toute** la chaîne — dépôt, arborescence, algorithmes,
fonctions, bibliothèque, et comment utiliser chaque brique — pour implémenter sans zone d'ombre.

Pour le **matériel** et la spécification STE100, voir `DETECTION_SPEC.md`. Pour le protocole
série détaillé, `firmware/PROTOCOLE.md`. Pour démarrer vite, `exemples/README.md`.

---

### 1. Vue d'ensemble

Le projet est un **banc de mesure de forces** pour imprimante résine MSLA (bottom-up) et son
**système de détection de défauts d'impression**. Trois façons de mesurer la force sont
modélisées : une **cellule de charge dans le bras Z**, **4 cellules sous le bac**, et le
**StallGuard** du driver moteur TMC2209. À partir de ces forces, couche par couche, le système
apprend le comportement normal et signale les anomalies (arrachement trop faible, obstacle,
blocage, appui résiduel, film détendu), plus des contrôles avant impression (résidu dans le bac,
planéité, pesée, fuite).

Le dépôt contient **trois implémentations cohérentes** de la même logique :

| Brique | Langage | Rôle |
|---|---|---|
| **Simulateur** `simulateur/index.html` | JavaScript | Référence physique + détection. Génère les signaux, sert de vérité de référence. |
| **Bibliothèque** `lib/msla_detect/` | C++17 pur | Moteur de détection portable, **sans malloc, sans Arduino**. Tourne sur PC et ESP32. |
| **Firmware** `firmware/banc_msla/` | C++/Arduino (ESP32) | Lit les capteurs, étiquette les phases, appelle la bibliothèque, formate les trames série. |

**Règle de conception :** toute la logique de détection est dans la bibliothèque. Le firmware
ne fait qu'E/S + appels. Le simulateur JavaScript est la **référence** ; la bibliothèque C++
doit rendre des verdicts **identiques** — c'est vérifié automatiquement par les tests de
**parité**. Précision `double` partout dans la détection pour une égalité au bit près avec le
`Number` JavaScript.

---

### 2. Arborescence (`print/`)

```
print/
├── README.md                      readme du projet (FR, aperçu EN/中文)
├── BOM.md                         nomenclature (FR/EN/中文)
├── simulateur/
│   └── index.html                 simulateur physique + détection de référence (JS)
├── lib/msla_detect/               bibliothèque C++17 portable (ni Arduino ni malloc)
│   ├── msla_filtres.{h,cpp}         médiane, med3, moyenne glissante
│   ├── msla_couche.{h,cpp}          features par couche, apprentissage, verdict (batch + flux)
│   ├── msla_stabilite.{h,cpp}       stabilisation de la résine
│   ├── msla_bac.{h,cpp}             contrôle du bac, planéité, pesée, fuite
│   └── msla_detect.h                en-tête parapluie (inclut tout)
├── firmware/
│   ├── banc_msla/banc_msla.ino      firmware ESP32 (E/S capteurs + série + appels bibliothèque)
│   ├── banc_msla/src/msla_detect    lien symbolique vers lib/ (pour l'IDE Arduino)
│   └── PROTOCOLE.md                 protocole série (FR/EN/中文)
├── exemples/
│   ├── quickstart_detection.cpp     exemple d'intégration (CSV → verdicts)
│   ├── run.sh, capteurs_demo.csv    build+run, jeu de démo
│   └── README.md                    format des données, calibration, passage au temps réel
├── outils/
│   ├── layers_from_images.py        aire exposée par couche depuis les images de tranches
│   └── logger.py                    journalise les mesures de l'ESP32 en CSV
├── tests/
│   ├── simulateur.test.js           modèle + détection (Node)
│   ├── lib/                         parité : bibliothèque C++ == simulateur
│   ├── couche_live/                 détection par couche en direct (chemin firmware)
│   ├── firmware_host/               contrôle du bac (firmware sur PC, Arduino simulé)
│   ├── protocole/                   protocole série
│   ├── perf/                        analyse de performance (temps, calcul, bruit)
│   └── test_fusion_geometrie.py     géométrie de l'étude 10,1″ (Python)
├── docs/                           DETECTION_SPEC, SOFTWARE, SOFTWARE_DEV_GUIDE, STRUCTURE, GUIDE
└── conception/                     modèle Fusion 360 (bâti 10,1″)
```

---

### 3. Flux de données

#### 3.1 Détection en impression (d'un échantillon à un verdict)

```
force HX711 f[0]  ──►  med3 (médiane 3 points, retard 1 échantillon)
                         │
vitesse Z + UV + L ──►  étiquette de phase (0,1,2,3,4) + index de couche + aire
                         │
                         ▼
          SuiviCouches.pousser(ff, z, ph, ly, area)   # construit un Feature par couche
                         │  (renvoie une couche finalisée au début de l'arrachement suivant)
                         ▼
          DetecteurCouche.pousser(Feature, area)       # apprentissage + drapeaux
                         │
                         ▼
          drapeaux  ──►  verdict  OK / PAUSE <code> / STOP <code>   ──►  Serial2
```

Dans le firmware, `detEchantillon()` fait le med3 et l'étiquetage de phase, appelle les deux
classes de la bibliothèque, puis `detFlux()` traduit les drapeaux en verdict série.

#### 3.2 Contrôle du bac (machine à états en flux)

```
échantillons (t, z, f[5], uv)  ──►  BacControleur.maj(...)   # états : WAIT→DESC→HOLD
                                        │ (renvoie un évènement quand un verdict est prêt)
                                        ▼
        ControleResultat / PlaneiteResultat / ReferenceResultat  ──►  texte série
```

---

### 4. Les algorithmes, un par un

#### 4.1 Filtres (`msla_filtres`)
- **`mediane`** : médiane d'un tableau (tri dans un tampon fourni). Robuste aux pics isolés.
- **`med3`** : médiane glissante à 3 points ; enlève les pics de lecture (glitch HX711) avec
  un retard d'un seul échantillon → utilisable en temps réel.
- **`moy_glissante`** : moyenne glissante centrée de fenêtre `w` (ne moyenne que les voisins
  valides) ; lisse le bruit haute fréquence. Appliquée au bac et au StallGuard, pas au bras Z.

#### 4.2 Étiquetage des phases
Chaque échantillon reçoit une phase : `0`=exposition(UV), `1`=arrachement (montée), `2`=descente,
`3`=attente haute, `4`=attente basse. Le firmware la **déduit** du signe de la vitesse Z et de
l'état de l'UV (commandes `U`), et l'index de couche vient de la commande `L`. Le simulateur, lui,
connaît les phases exactes.

#### 4.3 Caractéristiques par couche (`features`)
Pour chaque couche, on extrait quatre grandeurs (structure `Feature`) :
- **`peak`** : pic d'arrachement = force max en phase 1, au-dessus de la tare du haut.
- **`comp`** : compression min (phase 2, ou phases 0/4 de la couche suivante) → un obstacle ou un
  blocage pousse cette valeur fortement négative.
- **`travel`** : position Z atteinte au pic (course).
- **`stat`** : appui statique = différence de force en fin d'exposition entre deux couches
  successives, une fois la résine immobile → révèle un résidu/corps étranger (insensible à la
  dérive lente). `iPeak`/`iComp` donnent l'index d'échantillon du pic et de la compression.

#### 4.4 Apprentissage et verdict par couche (`detect`)
Principe : on **apprend le niveau normal sur les 6 dernières bonnes couches**, puis on signale
toute couche qui s'en écarte. Il faut 3 bonnes couches avant de commencer. Seuils plancher par
capteur (voir §9) et paramètres application (`lowRatio`, `obstacleMul`). Drapeaux possibles :

- **`CRASH`** (danger) : `comp ≤ −0,93 × fstall` → le moteur Z est au blocage.
- **`OBSTACLE`** (danger) : compression anormale à la descente → quelque chose gêne.
- **`APPUI`** (anomalie) : `stat` au-dessus du niveau appris → appui résiduel (résidu collé).
- **`FAIBLE`** (anomalie) : `peak < lowRatio × attendu − 0,5 × p × nm` → arrachement trop faible
  (décollement manqué / support détaché).
- **`FEP`** (anomalie) : signature de film détendu.

**Ordre des drapeaux** dans une couche (il fixe le verdict) : CRASH ou OBSTACLE, puis APPUI, puis
FAIBLE, puis FEP. **Ré-apprentissage** : après 4 couches `FAIBLE` d'affilée, le niveau normal est
réappris (évite les fausses alertes quand l'aire de la pièce change légitimement).

Correspondance **drapeau → verdict série** : CRASH/OBSTACLE → `STOP`, APPUI/FAIBLE/FEP → `PAUSE`.

#### 4.5 Fusion des voies (bras Z + bac)
La voie `fus` combine la cellule Z et la somme du bac par une **moyenne pondérée par l'inverse de
la variance du bruit** de chaque montage : `wZ = varBac / (varZ + varBac)`. La voie la moins
bruitée pèse plus. C'est souvent le meilleur détecteur, car il garde la finesse du bac et la
dynamique du bras Z.

#### 4.6 Stabilisation de la résine (`msla_stabilite`)
Mesure, par couche, le temps que met la force à redevenir stable après la descente (attente avant
exposition) et après l'arrachement (attente en haut). `slopeAt` = pente par régression linéaire,
`resStd` = écart-type des résidus ; la stabilité est détectée quand la pente passe sous un seuil
lié au bruit. Sert à confirmer qu'un capteur « voit » la résine immobile avant l'exposition, et à
comparer au temps d'attente du profil (base des messages réservés `WAIT`/`GO`).

#### 4.7 Contrôle du bac avant impression (`msla_bac`)
Machine à états en flux `WAIT → DESC → HOLD` (classe `BacControleur`). Trois modes : **contrôle**
(cherche un résidu : compare l'appui résiduel et la courbe de descente à une référence de bac
propre), **référence** (enregistre la courbe d'un bac propre), **planéité** (mesure le décalage
du centre d'effort à 5 N → bac/plateau non parallèle). Verdicts en structures
(`ControleResultat`, `PlaneiteResultat`, `ReferenceResultat`), pas d'écriture série : le firmware
formate à partir de là. Détection de contact pendant la descente, épaisseur du résidu, position
estimée (si 5 cellules), niveau de risque (`AUCUN/MODERE/ELEVE/DANGER`).

#### 4.8 Pesée et fuite (`msla_bac`)
- **`analysePesee`** : à partir des grammes par coin, donne le total, la répartition, le coin le
  moins chargé, « mal posé » si déséquilibre, la position du centre de masse, et le volume de
  résine si le poids du bac vide est connu.
- **`analyseFuite`** : compare deux masses avant/après sur une durée → perte, débit, « fuite
  probable ».

#### 4.9 Modèle physique du capteur Z (simulateur)
Le simulateur modélise la **rigidité de l'axe Z** (`kZ`) et le **montage du capteur** (série ou
parallèle), car ils changent ce que la cellule Z mesure :
- **Série** : toute la force traverse la cellule ; `kZ` n'agit que sur la **dynamique** (montée
  arrondie + rebond au décollement si l'axe est souple), moyenne = vraie force.
- **Parallèle** : la force se partage entre la structure rigide et la cellule au prorata des
  raideurs → amplitude Z atténuée (`aZ = 2·kRef/(kRef+kZ)`) ; plus l'axe est rigide, plus le
  signal Z est faible et relativement bruité. La voie bac (balance dédiée) reste fidèle.

Ce modèle sert à comprendre la **sensibilité** de chaque voie et à régler `nm`. Il n'affecte que
la génération des signaux ; la logique de détection est inchangée.

---

### 5. Référence de l'API — bibliothèque C++ (`namespace msla`)

Le code appelant possède tous les tampons ; la bibliothèque n'alloue rien.

#### 5.1 `msla_filtres`
```cpp
double mediane(const double* a, int n, double* scratch);          // scratch de taille >= n
void   med3(const double* in, double* out, int n);                // médiane glissante 3 points
void   moy_glissante(const double* in, double* out, int n, int w);// moyenne glissante centrée
```

#### 5.2 `msla_couche` — détection par couche
Types :
```cpp
struct Serie   { const double* t,*f,*z; const int* ph,*ly; int n; };   // f = force filtrée
struct Feature { double peak, comp, travel, stat; int iPeak, iComp; }; // NaN = absent
enum   Flag    { F_NODATA, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags { uint8_t n; Flag f[4]; };
struct Verdict { int first; Flag kind; int fa, nodata, lat; bool hasFirst; };
double absente();  bool estAbsent(double v);                 // équivalent du null JS
Floor  floorPour(const char* key);                           // seuils plancher "z"/"fus"/"vat"/"sg"
```
Fonctions batch (lisent une série entière ; utilisées par la parité) :
```cpp
void    prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);
void    features(const Serie& s, int nLayers, Feature* out, double* scratch, int scratchLen);
void    detect(const Feature* feat, const double* area, int nLayers, const char* key,
               double nm, double obstacleMul, double lowRatio, double fstall, LayerFlags* flags);
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);
```
Classes en flux (un échantillon / une couche à la fois ; utilisées par le firmware) :
```cpp
class DetecteurCouche {           // detect() incrémental ; mêmes drapeaux que detect()
  void       init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);     // pousser les couches dans l'ordre
};
class SuiviCouches {              // features() incrémental ; mêmes features que features()
  void init();
  int  pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea);
  int  terminer(Feature* out, double* outArea);           // vide la dernière couche en fin d'impression
};
```
`key` ∈ {`"z"`,`"vat"`,`"sg"`,`"fus"`} choisit les seuils plancher. `prep` : `med`=médiane 3 pts,
`avg`=moyenne glissante (réglages simulateur : z→(med,—), vat/sg→(med,avg), fus→(med,—)).

#### 5.3 `msla_stabilite`
```cpp
double slopeAt(const double* t, const double* f, int a, int b);   // pente (régression) sur [a,b]
double resStd(const double* t, const double* f, int a, int b);    // écart-type des résidus
struct Settle { double ts; bool found, tooShort; double sig, thr; };
void   stability(const Serie& s, const char* key, const StabOpt& opt, const Starts& starts,
                 int nLayers, Settle* down, Settle* top);          // down[] après descente, top[] après arrachement
SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers);
```

#### 5.4 `msla_bac` — bac, planéité, pesée, fuite (`float`, comme le firmware)
```cpp
class BacControleur {
  BacArm   armer(int mode, const BacConfig& cfg, BacRef* ref);  // mode 0=contrôle,1=référence,2=planéité
  BacEvent maj(float t, float z, const float f[5], bool uv);    // 1 échantillon ; évènement quand prêt
  bool     actif() const; int mode() const; void desarmer();
  const ControleResultat&  controle()  const;   // après BAC_EV_CONTROLE
  const PlaneiteResultat&  planeite()  const;    // après BAC_EV_PLANEITE
  const ReferenceResultat& reference() const;    // après BAC_EV_REFERENCE
};
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);
```
Config : `BacConfig{nCells, holdS, ckStatN, ckDescN, vatW, vatH, zMaxN, layerMm}`.
Constante : `MSLA_GN = 0,00980665` N/g.

---

### 6. Référence de l'API — simulateur (JavaScript)

`simulateur/index.html` exporte (en Node, via `module.exports`) :

| Fonction / objet | Rôle |
|---|---|
| `simulate(P)` | simule une impression → séries temporelles (force, Z, phase, vérité terrain). |
| `sensors(sim, P)` | génère les signaux capteurs (zc, vat 4 cellules, sg, fusion) avec bruit/montage. |
| `runAll(P)` | pipeline complet : simulate + sensors + features + detect + verdict + stabilité. |
| `vatCheck`, `planarityTest`, `residueCheck`, `convertOffset` | contrôles bac / planéité / résidu. |
| `fitModel`, `parseRecording`, `exampleRecording` | recalage du modèle sur un enregistrement réel. |
| `parseGeometry`, `segment` | lecture d'un fichier de couches, segmentation. |
| `MACHINES, RESINS, APPS, GEOMS, PROC0` | profils (machine, résine, application, géométrie, procédé). |
| `clone`, `DIRS` | utilitaires. |

`P` est un objet profil : `{machine, resin, proc, app, geom, scen, L, noise, rate, seed, …}`.
`runAll(P).ver` donne les verdicts par voie `{z, vat, sg, fus}`.

---

### 7. Protocole série (carte mère ↔ banc)

Liaison `Serial2`, 115200 bauds 8N1, lignes ASCII `\n` (≤ 32 car.), maître = carte mère. Détail
complet et machine à états : `firmware/PROTOCOLE.md`.

**Carte mère → banc :** `L <n> [aire mm²]` (début de couche) · `U 1`/`U 0` (UV) · `V` (contrôle
bac) · `P`/`R` (pause/reprise) · `X` (arrêt, finalise) · `?` (état → `RDY`).

**Banc → carte mère :** `RDY` · `VAT OK|RESIDU|ERREUR` · `OK <n>` · `PAUSE <code> <n>` ·
`STOP <code> <n>` · `ALRM FORCE` · `ERR`. `STOP` est prioritaire (UV coupé, plateau remonté).
`PAUSE`/`STOP` sont des **demandes** : la carte mère reste maître. Silence > 2 s → la carte décide.

---

### 8. Firmware — commandes USB (console opérateur)

Liaison USB brute indépendante (CSV à 921600 bauds). Commandes (messages opérateur en français) :
`start`/`stop` (flux CSV), `tare`, `cal`, `sign <i>`, `cells <1|5>`, `stepsmm <v>`, `dir`, `zero`,
`uv <haut> <bas>`, `alerte <N>`, `raw`, `ref`/`controle`/`planeite` (arment un test bac),
`tarevide`/`refbac` (bac vide / référence), `pesee`, `fuite [min]`, `densite <v>`, `maintien <s>`,
`seuils <statN> <descN>`, `bac <W> <H>`, `couche <mm>`, `fmax <N>`, `ack`, `info`.

---

### 9. Référence des seuils

Seuils plancher par capteur (`msla_couche.cpp`, `floorPour()`) :

| key | p (pic) | c (compression) | s (appui) |
|---|---|---|---|
| z, fus | 0,4 | 0,8 | 0,25 |
| vat | 2,0 | 2,5 | 1,0 |
| sg | 10 | 12 | (non utilisé) |

Paramètres application (profil `APPS` ; défauts « figurine ») : `lowRatio = 0,6`
(FAIBLE si `peak < lowRatio × attendu − 0,5 × p × nm`), `obstacleMul = 2,5`
(OBSTACLE si `comp < obstacleMul × attendu − c × nm`), `settleMargin = 0`.

`nm = noise × resFactor`, avec `resFactor = max(1, sensRes / 0,05)` (pour `sg` : `nm = noise`).
`fstall` = force max du moteur Z (CRASH si `comp ≤ −0,93 × fstall`). Le firmware les tient dans
`detNoise`, `detObstacleMul`, `detLowRatio`, et `cfg.zMaxN` pour `fstall`.

---

### 10. Tests et intégration continue

`.github/workflows/tests.yml` lance à chaque push/PR :

| Suite | Vérifie |
|---|---|
| `node tests/simulateur.test.js` | modèle physique + détection de référence (JS) |
| `bash tests/lib/run.sh` | **parité** : bibliothèque C++ == simulateur (verdicts et stabilité) |
| `bash tests/firmware_host/run.sh` | contrôle du bac (firmware compilé sur PC, Arduino simulé) |
| `bash tests/protocole/run.sh` | protocole série |
| `bash tests/couche_live/run.sh` | détection par couche en direct (chemin firmware) |
| `python tests/test_fusion_geometrie.py` | géométrie de l'étude 10,1″ |
| `bash exemples/run.sh` | exemple d'intégration quickstart |
| `bash tests/perf/run.sh` | performance (analyse, pas un test pass/fail) |

Prérequis : `node`, `g++` (C++17), `python3` + `numpy`.

---

### 11. Outils (`outils/`)

- **`layers_from_images.py`** : calcule l'aire exposée par couche (et le centre `cx, cy`) depuis
  les images de tranches (PNG issus d'UVtools/Lychee/…), produit un CSV `layer, area_mm2, cx_mm,
  cy_mm, islands` chargeable dans le simulateur (profil « Pièce imprimée » → importé).
- **`logger.py`** : lit le flux CSV de l'ESP32 sur l'USB et l'écrit dans un fichier avec en-tête
  (machine, résine, température, note), rechargeable dans le simulateur (« Données réelles »).

---

### 12. Démarrage rapide de l'intégration

`exemples/quickstart_detection.cpp` : part d'un CSV capteurs (`t,z,ph,ly,area,fz,fvat,fsg`) et sort
un verdict par couche (OK/PAUSE/STOP) via `prep → features → detect`, pour chaque voie, plus la
fusion. `bash exemples/run.sh` compile et teste sur `capteurs_demo.csv` (défaut détecté à la
couche 30). **La même calibration se reporte telle quelle dans le firmware** pour le temps réel ;
le résultat est garanti identique par la parité. Détail : `exemples/README.md`.

---

### 13. Points d'extension

- **Ajouter un capteur** : choisir une `key`, définir ses seuils plancher dans `floorPour()`,
  régler `nm`. Le reste du pipeline est générique.
- **Ajouter un scénario de défaut** : l'implémenter dans le simulateur (`simulate`), l'ajouter à
  la table `TRIGGER`, exporter via `tests/lib/export_scenarios.js`, vérifier la parité.
- **Changer un seuil** : modifier dans le simulateur d'abord (référence), puis la parité garantit
  que la bibliothèque suit. Ne jamais diverger la logique entre JS et C++.

---

## English

Audience: engineers who take over, integrate or extend the MSLA measurement bench and its
detection. Goal: understand the **whole** chain — repository, directory tree, algorithms,
functions, library, and how to use each piece — so you can implement without any blind spots.

For the **hardware** and the STE100 specification, see `DETECTION_SPEC.md`. For the detailed
serial protocol, `firmware/PROTOCOLE.md`. To get started quickly, `exemples/README.md`.

---

### 1. Overview

The project is a **force-measurement bench** for MSLA (bottom-up) resin printers and its
**print-defect detection system**. Three ways of measuring force are
modeled: a **load cell in the Z arm**, **4 cells under the vat**, and the
**StallGuard** of the TMC2209 motor driver. From these forces, layer by layer, the system
learns normal behavior and flags anomalies (peel force too low, obstacle,
stall, residual pressure, slack film), plus pre-print checks (residue in the vat,
flatness, weighing, leak).

The repository contains **three consistent implementations** of the same logic:

| Component | Language | Role |
|---|---|---|
| **Simulator** `simulateur/index.html` | JavaScript | Physical reference + detection. Generates the signals, serves as the reference truth. |
| **Library** `lib/msla_detect/` | Pure C++17 | Portable detection engine, **no malloc, no Arduino**. Runs on PC and ESP32. |
| **Firmware** `firmware/banc_msla/` | C++/Arduino (ESP32) | Reads the sensors, labels the phases, calls the library, formats the serial frames. |

**Design rule:** all the detection logic lives in the library. The firmware
only does I/O + calls. The JavaScript simulator is the **reference**; the C++ library
must return **identical** verdicts — this is checked automatically by the **parity**
tests. `double` precision is used everywhere in the detection for bit-exact equality with the
JavaScript `Number`.

---

### 2. Directory tree (`print/`)

```
print/
├── README.md                      readme du projet (FR, aperçu EN/中文)
├── BOM.md                         nomenclature (FR/EN/中文)
├── simulateur/
│   └── index.html                 simulateur physique + détection de référence (JS)
├── lib/msla_detect/               bibliothèque C++17 portable (ni Arduino ni malloc)
│   ├── msla_filtres.{h,cpp}         médiane, med3, moyenne glissante
│   ├── msla_couche.{h,cpp}          features par couche, apprentissage, verdict (batch + flux)
│   ├── msla_stabilite.{h,cpp}       stabilisation de la résine
│   ├── msla_bac.{h,cpp}             contrôle du bac, planéité, pesée, fuite
│   └── msla_detect.h                en-tête parapluie (inclut tout)
├── firmware/
│   ├── banc_msla/banc_msla.ino      firmware ESP32 (E/S capteurs + série + appels bibliothèque)
│   ├── banc_msla/src/msla_detect    lien symbolique vers lib/ (pour l'IDE Arduino)
│   └── PROTOCOLE.md                 protocole série (FR/EN/中文)
├── exemples/
│   ├── quickstart_detection.cpp     exemple d'intégration (CSV → verdicts)
│   ├── run.sh, capteurs_demo.csv    build+run, jeu de démo
│   └── README.md                    format des données, calibration, passage au temps réel
├── outils/
│   ├── layers_from_images.py        aire exposée par couche depuis les images de tranches
│   └── logger.py                    journalise les mesures de l'ESP32 en CSV
├── tests/
│   ├── simulateur.test.js           modèle + détection (Node)
│   ├── lib/                         parité : bibliothèque C++ == simulateur
│   ├── couche_live/                 détection par couche en direct (chemin firmware)
│   ├── firmware_host/               contrôle du bac (firmware sur PC, Arduino simulé)
│   ├── protocole/                   protocole série
│   ├── perf/                        analyse de performance (temps, calcul, bruit)
│   └── test_fusion_geometrie.py     géométrie de l'étude 10,1″ (Python)
├── docs/                           DETECTION_SPEC, SOFTWARE, SOFTWARE_DEV_GUIDE, STRUCTURE, GUIDE
└── conception/                     modèle Fusion 360 (bâti 10,1″)
```

---

### 3. Data flow

#### 3.1 Detection during printing (from a sample to a verdict)

```
force HX711 f[0]  ──►  med3 (médiane 3 points, retard 1 échantillon)
                         │
vitesse Z + UV + L ──►  étiquette de phase (0,1,2,3,4) + index de couche + aire
                         │
                         ▼
          SuiviCouches.pousser(ff, z, ph, ly, area)   # construit un Feature par couche
                         │  (renvoie une couche finalisée au début de l'arrachement suivant)
                         ▼
          DetecteurCouche.pousser(Feature, area)       # apprentissage + drapeaux
                         │
                         ▼
          drapeaux  ──►  verdict  OK / PAUSE <code> / STOP <code>   ──►  Serial2
```

In the firmware, `detEchantillon()` does the med3 and the phase labeling, calls the two
library classes, then `detFlux()` translates the flags into a serial verdict.

#### 3.2 Vat check (streaming state machine)

```
échantillons (t, z, f[5], uv)  ──►  BacControleur.maj(...)   # états : WAIT→DESC→HOLD
                                        │ (renvoie un évènement quand un verdict est prêt)
                                        ▼
        ControleResultat / PlaneiteResultat / ReferenceResultat  ──►  texte série
```

---

### 4. The algorithms, one by one

#### 4.1 Filters (`msla_filtres`)
- **`mediane`**: median of an array (sorted in a caller-supplied buffer). Robust to isolated spikes.
- **`med3`**: 3-point sliding median; removes reading spikes (HX711 glitches) with
  a delay of just one sample → usable in real time.
- **`moy_glissante`**: centered moving average with window `w` (averages only valid
  neighbors); smooths high-frequency noise. Applied to the vat and to StallGuard, not to the Z arm.

#### 4.2 Phase labeling
Each sample receives a phase: `0`=exposure (UV), `1`=peel (rising), `2`=descent,
`3`=high wait, `4`=low wait. The firmware **infers** it from the sign of the Z velocity and from
the UV state (`U` commands), and the layer index comes from the `L` command. The simulator, for its part,
knows the exact phases.

#### 4.3 Per-layer features (`features`)
For each layer, four quantities are extracted (`Feature` struct):
- **`peak`**: peel peak = max force in phase 1, above the top tare.
- **`comp`**: min compression (phase 2, or phases 0/4 of the next layer) → an obstacle or a
  stall pushes this value strongly negative.
- **`travel`**: Z position reached at the peak (travel).
- **`stat`**: static pressure = difference in force at the end of exposure between two successive
  layers, once the resin is motionless → reveals a residue/foreign body (insensitive to slow
  drift). `iPeak`/`iComp` give the sample index of the peak and of the compression.

#### 4.4 Per-layer learning and verdict (`detect`)
Principle: the system **learns the normal level from the last 6 good layers**, then flags
any layer that deviates from it. 3 good layers are needed before starting. Floor thresholds per
sensor (see §9) and application parameters (`lowRatio`, `obstacleMul`). Possible flags:

- **`CRASH`** (danger): `comp ≤ −0,93 × fstall` → the Z motor is stalled.
- **`OBSTACLE`** (danger): abnormal compression on the descent → something is in the way.
- **`APPUI`** (anomaly): `stat` above the learned level → residual pressure (stuck residue).
- **`FAIBLE`** (anomaly): `peak < lowRatio × attendu − 0,5 × p × nm` → peel force too low
  (missed separation / detached support).
- **`FEP`** (anomaly): slack-film signature.

**Flag order** within a layer (it determines the verdict): CRASH or OBSTACLE, then APPUI, then
FAIBLE, then FEP. **Relearning**: after 4 consecutive `FAIBLE` layers, the normal level is
relearned (avoids false alarms when the part's area legitimately changes).

**Flag → serial verdict** mapping: CRASH/OBSTACLE → `STOP`, APPUI/FAIBLE/FEP → `PAUSE`.

#### 4.5 Channel fusion (Z arm + vat)
The `fus` channel combines the Z cell and the vat sum with a **weighted average using the inverse of
the noise variance** of each setup: `wZ = varBac / (varZ + varBac)`. The less noisy channel
weighs more. It is often the best detector, because it keeps the fine resolution of the vat and the
dynamic range of the Z arm.

#### 4.6 Resin stabilization (`msla_stabilite`)
Measures, per layer, the time the force takes to become stable again after the descent (wait before
exposure) and after the peel (wait at the top). `slopeAt` = slope by linear regression,
`resStd` = standard deviation of the residuals; stability is detected when the slope drops below a threshold
tied to the noise. Used to confirm that a sensor "sees" the resin motionless before exposure, and to
compare against the profile's wait time (basis of the reserved `WAIT`/`GO` messages).

#### 4.7 Pre-print vat check (`msla_bac`)
Streaming state machine `WAIT → DESC → HOLD` (class `BacControleur`). Three modes: **check**
(looks for residue: compares the residual pressure and the descent curve to a clean-vat
reference), **reference** (records the curve of a clean vat), **flatness** (measures the offset
of the center of force at 5 N → vat/plate not parallel). Verdicts as structs
(`ControleResultat`, `PlaneiteResultat`, `ReferenceResultat`), no serial writes: the firmware
formats from there. Contact detection during the descent, residue thickness, estimated
position (with 5 cells), risk level (`AUCUN/MODERE/ELEVE/DANGER`).

#### 4.8 Weighing and leak (`msla_bac`)
- **`analysePesee`**: from the grams per corner, gives the total, the distribution, the
  least-loaded corner, "badly seated" if unbalanced, the position of the center of mass, and the resin
  volume if the empty vat weight is known.
- **`analyseFuite`**: compares two masses before/after over a duration → loss, flow rate, "probable
  leak".

#### 4.9 Physical model of the Z sensor (simulator)
The simulator models the **stiffness of the Z axis** (`kZ`) and the **sensor mounting** (series or
parallel), because they change what the Z cell measures:
- **Series**: all the force passes through the cell; `kZ` affects only the **dynamics** (rounded
  rise + rebound at separation if the axis is compliant), mean = true force.
- **Parallel**: the force is shared between the rigid structure and the cell in proportion to the
  stiffnesses → attenuated Z amplitude (`aZ = 2·kRef/(kRef+kZ)`); the stiffer the axis, the weaker and
  relatively noisier the Z signal. The vat channel (dedicated scale) remains faithful.

This model serves to understand the **sensitivity** of each channel and to tune `nm`. It only affects
signal generation; the detection logic is unchanged.

---

### 5. API reference — C++ library (`namespace msla`)

The calling code owns all the buffers; the library allocates nothing.

#### 5.1 `msla_filtres`
```cpp
double mediane(const double* a, int n, double* scratch);          // scratch de taille >= n
void   med3(const double* in, double* out, int n);                // médiane glissante 3 points
void   moy_glissante(const double* in, double* out, int n, int w);// moyenne glissante centrée
```

#### 5.2 `msla_couche` — per-layer detection
Types:
```cpp
struct Serie   { const double* t,*f,*z; const int* ph,*ly; int n; };   // f = force filtrée
struct Feature { double peak, comp, travel, stat; int iPeak, iComp; }; // NaN = absent
enum   Flag    { F_NODATA, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags { uint8_t n; Flag f[4]; };
struct Verdict { int first; Flag kind; int fa, nodata, lat; bool hasFirst; };
double absente();  bool estAbsent(double v);                 // équivalent du null JS
Floor  floorPour(const char* key);                           // seuils plancher "z"/"fus"/"vat"/"sg"
```
Batch functions (read an entire series; used by the parity tests):
```cpp
void    prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);
void    features(const Serie& s, int nLayers, Feature* out, double* scratch, int scratchLen);
void    detect(const Feature* feat, const double* area, int nLayers, const char* key,
               double nm, double obstacleMul, double lowRatio, double fstall, LayerFlags* flags);
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);
```
Streaming classes (one sample / one layer at a time; used by the firmware):
```cpp
class DetecteurCouche {           // detect() incrémental ; mêmes drapeaux que detect()
  void       init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);     // pousser les couches dans l'ordre
};
class SuiviCouches {              // features() incrémental ; mêmes features que features()
  void init();
  int  pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea);
  int  terminer(Feature* out, double* outArea);           // vide la dernière couche en fin d'impression
};
```
`key` ∈ {`"z"`,`"vat"`,`"sg"`,`"fus"`} selects the floor thresholds. `prep`: `med`=3-point median,
`avg`=moving average (simulator settings: z→(med,—), vat/sg→(med,avg), fus→(med,—)).

#### 5.3 `msla_stabilite`
```cpp
double slopeAt(const double* t, const double* f, int a, int b);   // pente (régression) sur [a,b]
double resStd(const double* t, const double* f, int a, int b);    // écart-type des résidus
struct Settle { double ts; bool found, tooShort; double sig, thr; };
void   stability(const Serie& s, const char* key, const StabOpt& opt, const Starts& starts,
                 int nLayers, Settle* down, Settle* top);          // down[] après descente, top[] après arrachement
SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers);
```

#### 5.4 `msla_bac` — vat, flatness, weighing, leak (`float`, like the firmware)
```cpp
class BacControleur {
  BacArm   armer(int mode, const BacConfig& cfg, BacRef* ref);  // mode 0=contrôle,1=référence,2=planéité
  BacEvent maj(float t, float z, const float f[5], bool uv);    // 1 échantillon ; évènement quand prêt
  bool     actif() const; int mode() const; void desarmer();
  const ControleResultat&  controle()  const;   // après BAC_EV_CONTROLE
  const PlaneiteResultat&  planeite()  const;    // après BAC_EV_PLANEITE
  const ReferenceResultat& reference() const;    // après BAC_EV_REFERENCE
};
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);
```
Config: `BacConfig{nCells, holdS, ckStatN, ckDescN, vatW, vatH, zMaxN, layerMm}`.
Constant: `MSLA_GN = 0,00980665` N/g.

---

### 6. API reference — simulator (JavaScript)

`simulateur/index.html` exports (in Node, via `module.exports`):

| Function / object | Role |
|---|---|
| `simulate(P)` | simulates a print → time series (force, Z, phase, ground truth). |
| `sensors(sim, P)` | generates the sensor signals (zc, vat 4 cells, sg, fusion) with noise/mounting. |
| `runAll(P)` | full pipeline: simulate + sensors + features + detect + verdict + stability. |
| `vatCheck`, `planarityTest`, `residueCheck`, `convertOffset` | vat / flatness / residue checks. |
| `fitModel`, `parseRecording`, `exampleRecording` | fitting the model to a real recording. |
| `parseGeometry`, `segment` | reading a layer file, segmentation. |
| `MACHINES, RESINS, APPS, GEOMS, PROC0` | profiles (machine, resin, application, geometry, process). |
| `clone`, `DIRS` | utilities. |

`P` is a profile object: `{machine, resin, proc, app, geom, scen, L, noise, rate, seed, …}`.
`runAll(P).ver` gives the verdicts per channel `{z, vat, sg, fus}`.

---

### 7. Serial protocol (mainboard ↔ bench)

`Serial2` link, 115200 baud 8N1, ASCII lines ending in `\n` (≤ 32 chars), master = mainboard. Full
detail and state machine: `firmware/PROTOCOLE.md`.

**Mainboard → bench:** `L <n> [aire mm²]` (start of layer) · `U 1`/`U 0` (UV) · `V` (vat
check) · `P`/`R` (pause/resume) · `X` (stop, finalize) · `?` (status → `RDY`).

**Bench → mainboard:** `RDY` · `VAT OK|RESIDU|ERREUR` · `OK <n>` · `PAUSE <code> <n>` ·
`STOP <code> <n>` · `ALRM FORCE` · `ERR`. `STOP` takes priority (UV off, plate raised).
`PAUSE`/`STOP` are **requests**: the mainboard stays in charge. Silence > 2 s → the mainboard decides.

---

### 8. Firmware — USB commands (operator console)

Independent raw USB link (CSV at 921600 baud). Commands (operator messages in French):
`start`/`stop` (CSV stream), `tare`, `cal`, `sign <i>`, `cells <1|5>`, `stepsmm <v>`, `dir`, `zero`,
`uv <haut> <bas>`, `alerte <N>`, `raw`, `ref`/`controle`/`planeite` (arm a vat test),
`tarevide`/`refbac` (empty vat / reference), `pesee`, `fuite [min]`, `densite <v>`, `maintien <s>`,
`seuils <statN> <descN>`, `bac <W> <H>`, `couche <mm>`, `fmax <N>`, `ack`, `info`.

---

### 9. Threshold reference

Floor thresholds per sensor (`msla_couche.cpp`, `floorPour()`):

| key | p (peak) | c (compression) | s (pressure) |
|---|---|---|---|
| z, fus | 0.4 | 0.8 | 0.25 |
| vat | 2.0 | 2.5 | 1.0 |
| sg | 10 | 12 | (not used) |

Application parameters (`APPS` profile; "figurine" defaults): `lowRatio = 0.6`
(FAIBLE if `peak < lowRatio × attendu − 0,5 × p × nm`), `obstacleMul = 2.5`
(OBSTACLE if `comp < obstacleMul × attendu − c × nm`), `settleMargin = 0`.

`nm = noise × resFactor`, with `resFactor = max(1, sensRes / 0,05)` (for `sg`: `nm = noise`).
`fstall` = maximum force of the Z motor (CRASH if `comp ≤ −0,93 × fstall`). The firmware keeps them in
`detNoise`, `detObstacleMul`, `detLowRatio`, and `cfg.zMaxN` for `fstall`.

---

### 10. Tests and continuous integration

`.github/workflows/tests.yml` runs on every push/PR:

| Suite | Verifies |
|---|---|
| `node tests/simulateur.test.js` | physical model + reference detection (JS) |
| `bash tests/lib/run.sh` | **parity**: C++ library == simulator (verdicts and stability) |
| `bash tests/firmware_host/run.sh` | vat check (firmware compiled on PC, Arduino simulated) |
| `bash tests/protocole/run.sh` | serial protocol |
| `bash tests/couche_live/run.sh` | live per-layer detection (firmware path) |
| `python tests/test_fusion_geometrie.py` | geometry of the 10.1″ study |
| `bash exemples/run.sh` | quickstart integration example |
| `bash tests/perf/run.sh` | performance (analysis, not a pass/fail test) |

Prerequisites: `node`, `g++` (C++17), `python3` + `numpy`.

---

### 11. Tools (`outils/`)

- **`layers_from_images.py`**: computes the exposed area per layer (and the center `cx, cy`) from
  slice images (PNGs from UVtools/Lychee/…), and produces a CSV `layer, area_mm2, cx_mm,
  cy_mm, islands` that can be loaded into the simulator ("Printed part" profile → imported).
- **`logger.py`**: reads the ESP32's CSV stream over USB and writes it to a file with a header
  (machine, resin, temperature, note), which can be reloaded into the simulator ("Real data").

---

### 12. Integration quick start

`exemples/quickstart_detection.cpp`: starts from a sensor CSV (`t,z,ph,ly,area,fz,fvat,fsg`) and outputs
a verdict per layer (OK/PAUSE/STOP) via `prep → features → detect`, for each channel, plus the
fusion. `bash exemples/run.sh` builds and tests on `capteurs_demo.csv` (defect detected at
layer 30). **The same calibration carries over unchanged into the firmware** for real time;
the result is guaranteed identical by the parity tests. Details: `exemples/README.md`.

---

### 13. Extension points

- **Add a sensor**: choose a `key`, define its floor thresholds in `floorPour()`,
  tune `nm`. The rest of the pipeline is generic.
- **Add a defect scenario**: implement it in the simulator (`simulate`), add it to
  the `TRIGGER` table, export via `tests/lib/export_scenarios.js`, check parity.
- **Change a threshold**: change it in the simulator first (reference), then the parity tests guarantee
  that the library follows. Never let the logic diverge between JS and C++.

---

## 中文

读者对象：接手、集成或扩展 MSLA 测量台架及其检测功能的工程师。目标：理解**整条**链路——仓库、目录结构、算法、函数、库，以及如何使用每个模块——从而在没有盲区的情况下完成实现。

**硬件**及 STE100 规格请参见 `DETECTION_SPEC.md`。详细的串口协议见 `firmware/PROTOCOLE.md`。若要快速上手，请参见 `exemples/README.md`。

---

### 1. 总体概览

本项目是一个面向 MSLA（底部上拉式）树脂打印机的**力测量台架**，以及其**打印缺陷检测系统**。共建模了三种测力方式：**Z 轴臂上的一个称重传感器**、**料槽下方的 4 个称重传感器**，以及电机驱动器 TMC2209 的 **StallGuard**。系统基于这些力数据，逐层学习正常行为并报告异常（剥离力过小、障碍物、堵转、残余压力、膜松弛），此外还包括打印前检查（料槽残留物、平整度、称重、泄漏）。

仓库包含同一套逻辑的**三个一致的实现**：

| 模块 | 语言 | 作用 |
|---|---|---|
| **模拟器** `simulateur/index.html` | JavaScript | 物理参考与检测。生成信号，作为基准真值参考。 |
| **库** `lib/msla_detect/` | 纯 C++17 | 可移植的检测引擎，**无 malloc、无 Arduino 依赖**。可在 PC 和 ESP32 上运行。 |
| **固件** `firmware/banc_msla/` | C++/Arduino (ESP32) | 读取传感器、标注阶段、调用库、格式化串口帧。 |

**设计原则：**全部检测逻辑都在库中。固件只负责 I/O 和调用。JavaScript 模拟器是**参考实现**；C++ 库必须给出**完全一致**的判定——这由**一致性**测试自动验证。检测中全程使用 `double` 精度，以便与 JavaScript 的 `Number` 做到逐位相等。

---

### 2. 目录结构（`print/`）

```
print/
├── README.md                      readme du projet (FR, aperçu EN/中文)
├── BOM.md                         nomenclature (FR/EN/中文)
├── simulateur/
│   └── index.html                 simulateur physique + détection de référence (JS)
├── lib/msla_detect/               bibliothèque C++17 portable (ni Arduino ni malloc)
│   ├── msla_filtres.{h,cpp}         médiane, med3, moyenne glissante
│   ├── msla_couche.{h,cpp}          features par couche, apprentissage, verdict (batch + flux)
│   ├── msla_stabilite.{h,cpp}       stabilisation de la résine
│   ├── msla_bac.{h,cpp}             contrôle du bac, planéité, pesée, fuite
│   └── msla_detect.h                en-tête parapluie (inclut tout)
├── firmware/
│   ├── banc_msla/banc_msla.ino      firmware ESP32 (E/S capteurs + série + appels bibliothèque)
│   ├── banc_msla/src/msla_detect    lien symbolique vers lib/ (pour l'IDE Arduino)
│   └── PROTOCOLE.md                 protocole série (FR/EN/中文)
├── exemples/
│   ├── quickstart_detection.cpp     exemple d'intégration (CSV → verdicts)
│   ├── run.sh, capteurs_demo.csv    build+run, jeu de démo
│   └── README.md                    format des données, calibration, passage au temps réel
├── outils/
│   ├── layers_from_images.py        aire exposée par couche depuis les images de tranches
│   └── logger.py                    journalise les mesures de l'ESP32 en CSV
├── tests/
│   ├── simulateur.test.js           modèle + détection (Node)
│   ├── lib/                         parité : bibliothèque C++ == simulateur
│   ├── couche_live/                 détection par couche en direct (chemin firmware)
│   ├── firmware_host/               contrôle du bac (firmware sur PC, Arduino simulé)
│   ├── protocole/                   protocole série
│   ├── perf/                        analyse de performance (temps, calcul, bruit)
│   └── test_fusion_geometrie.py     géométrie de l'étude 10,1″ (Python)
├── docs/                           DETECTION_SPEC, SOFTWARE, SOFTWARE_DEV_GUIDE, STRUCTURE, GUIDE
└── conception/                     modèle Fusion 360 (bâti 10,1″)
```

---

### 3. 数据流

#### 3.1 打印过程中的检测（从一个采样点到一个判定）

```
force HX711 f[0]  ──►  med3 (médiane 3 points, retard 1 échantillon)
                         │
vitesse Z + UV + L ──►  étiquette de phase (0,1,2,3,4) + index de couche + aire
                         │
                         ▼
          SuiviCouches.pousser(ff, z, ph, ly, area)   # construit un Feature par couche
                         │  (renvoie une couche finalisée au début de l'arrachement suivant)
                         ▼
          DetecteurCouche.pousser(Feature, area)       # apprentissage + drapeaux
                         │
                         ▼
          drapeaux  ──►  verdict  OK / PAUSE <code> / STOP <code>   ──►  Serial2
```

在固件中，`detEchantillon()` 完成 med3 滤波和阶段标注，调用库中的两个类，然后由 `detFlux()` 将标志转换为串口判定。

#### 3.2 料槽检查（流式状态机）

```
échantillons (t, z, f[5], uv)  ──►  BacControleur.maj(...)   # états : WAIT→DESC→HOLD
                                        │ (renvoie un évènement quand un verdict est prêt)
                                        ▼
        ControleResultat / PlaneiteResultat / ReferenceResultat  ──►  texte série
```

---

### 4. 逐个算法详解

#### 4.1 滤波器（`msla_filtres`）
- **`mediane`**：数组的中位数（在所提供的缓冲区中排序）。对孤立尖峰具有鲁棒性。
- **`med3`**：3 点滑动中位数；可消除读数尖峰（HX711 毛刺），仅延迟一个采样点 → 可用于实时处理。
- **`moy_glissante`**：窗口为 `w` 的居中滑动平均（仅对有效的相邻点求平均）；用于平滑高频噪声。应用于料槽通道和 StallGuard，不应用于 Z 轴臂。

#### 4.2 阶段标注
每个采样点都会被赋予一个阶段：`0`=曝光（UV），`1`=剥离（上升），`2`=下降，`3`=高位等待，`4`=低位等待。固件根据 Z 速度的符号和 UV 状态（`U` 命令）来**推断**阶段，层索引则来自 `L` 命令。而模拟器则直接知道准确的阶段。

#### 4.3 每层特征（`features`）
对每一层提取四个量（结构体 `Feature`）：
- **`peak`**：剥离峰值 = 阶段 1 中的最大力，相对于顶部皮重而言。
- **`comp`**：最小压缩量（阶段 2，或下一层的阶段 0/4）→ 障碍物或堵转会使该值变为很大的负值。
- **`travel`**：达到峰值时的 Z 位置（行程）。
- **`stat`**：静态压力 = 相邻两层在曝光结束时的力之差，此时树脂已静止 → 可揭示残留物/异物（对缓慢漂移不敏感）。`iPeak`/`iComp` 给出峰值和压缩量对应的采样点索引。

#### 4.4 逐层学习与判定（`detect`）
原理：**以最近 6 个良好层学习正常水平**，然后标记偏离该水平的任何层。开始前需要有 3 个良好层。按传感器设定的下限阈值（见 §9）以及应用参数（`lowRatio`、`obstacleMul`）。可能的标志：

- **`CRASH`**（危险）：`comp ≤ −0,93 × fstall` → Z 电机处于堵转状态。
- **`OBSTACLE`**（危险）：下降时压缩异常 → 有东西造成阻碍。
- **`APPUI`**（异常）：`stat` 高于已学习的水平 → 残余压力（粘附的残留物）。
- **`FAIBLE`**（异常）：`peak < lowRatio × attendu − 0,5 × p × nm` → 剥离力过小（脱模失败/支撑脱落）。
- **`FEP`**（异常）：膜松弛的特征。

一层之内的**标志顺序**（决定判定结果）：CRASH 或 OBSTACLE，然后是 APPUI，然后是 FAIBLE，最后是 FEP。**重新学习**：连续 4 个 `FAIBLE` 层之后，将重新学习正常水平（避免在零件面积合理变化时出现误报）。

**标志 → 串口判定**的对应关系：CRASH/OBSTACLE → `STOP`，APPUI/FAIBLE/FEP → `PAUSE`。

#### 4.5 通道融合（Z 轴臂 + 料槽）
`fus` 通道通过**按各自安装方式噪声方差倒数加权的平均**，将 Z 称重传感器与料槽总和结合起来：`wZ = varBac / (varZ + varBac)`。噪声较小的通道权重更大。它通常是最佳检测器，因为既保留了料槽通道的精细度，又保留了 Z 轴臂的动态范围。

#### 4.6 树脂稳定（`msla_stabilite`）
逐层测量力在下降之后（曝光前的等待）以及剥离之后（顶部等待）重新变得稳定所需的时间。`slopeAt` = 线性回归斜率，`resStd` = 残差的标准差；当斜率降到与噪声相关的阈值以下时，即判定为稳定。用于确认传感器在曝光前能够“看到”静止的树脂，并与工艺配置中的等待时间进行比较（保留消息 `WAIT`/`GO` 的依据）。

#### 4.7 打印前料槽检查（`msla_bac`）
流式状态机 `WAIT → DESC → HOLD`（类 `BacControleur`）。三种模式：**检查**（查找残留物：将残余压力和下降曲线与洁净料槽的参考进行比较）、**参考**（记录洁净料槽的曲线）、**平整度**（测量 5 N 时受力中心的偏移 → 料槽/平台不平行）。判定以结构体形式给出（`ControleResultat`、`PlaneiteResultat`、`ReferenceResultat`），不做串口写入：由固件据此进行格式化。包括下降过程中的接触检测、残留物厚度、估计位置（如有 5 个传感器）、风险等级（`AUCUN/MODERE/ELEVE/DANGER`）。

#### 4.8 称重与泄漏（`msla_bac`）
- **`analysePesee`**：根据每个角的克数，给出总重、分布、负载最轻的角、不平衡时的“放置不当”、质心位置，以及在已知空料槽重量时的树脂体积。
- **`analyseFuite`**：比较一段时间前后的两个质量 → 损失量、流量、“可能泄漏”。

#### 4.9 Z 传感器的物理模型（模拟器）
模拟器对 **Z 轴刚度**（`kZ`）和**传感器安装方式**（串联或并联）进行建模，因为它们会改变 Z 传感器所测得的值：
- **串联**：全部力都经过称重传感器；`kZ` 仅影响**动态特性**（上升被圆滑化 + 轴较软时在脱离处出现回弹），平均值 = 真实力。
- **并联**：力按刚度比例在刚性结构与传感器之间分配 → Z 幅值衰减（`aZ = 2·kRef/(kRef+kZ)`）；轴越刚，Z 信号越弱，相对噪声越大。料槽通道（专用秤）保持准确。

该模型用于理解各通道的**灵敏度**并调整 `nm`。它只影响信号的生成；检测逻辑保持不变。

---

### 5. API 参考——C++ 库（`namespace msla`）

调用方持有所有缓冲区；库不进行任何分配。

#### 5.1 `msla_filtres`
```cpp
double mediane(const double* a, int n, double* scratch);          // scratch de taille >= n
void   med3(const double* in, double* out, int n);                // médiane glissante 3 points
void   moy_glissante(const double* in, double* out, int n, int w);// moyenne glissante centrée
```

#### 5.2 `msla_couche` — 逐层检测
类型：
```cpp
struct Serie   { const double* t,*f,*z; const int* ph,*ly; int n; };   // f = force filtrée
struct Feature { double peak, comp, travel, stat; int iPeak, iComp; }; // NaN = absent
enum   Flag    { F_NODATA, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags { uint8_t n; Flag f[4]; };
struct Verdict { int first; Flag kind; int fa, nodata, lat; bool hasFirst; };
double absente();  bool estAbsent(double v);                 // équivalent du null JS
Floor  floorPour(const char* key);                           // seuils plancher "z"/"fus"/"vat"/"sg"
```
批处理函数（读取整个序列；用于一致性测试）：
```cpp
void    prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);
void    features(const Serie& s, int nLayers, Feature* out, double* scratch, int scratchLen);
void    detect(const Feature* feat, const double* area, int nLayers, const char* key,
               double nm, double obstacleMul, double lowRatio, double fstall, LayerFlags* flags);
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);
```
流式类（一次一个采样点/一层；由固件使用）：
```cpp
class DetecteurCouche {           // detect() incrémental ; mêmes drapeaux que detect()
  void       init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);     // pousser les couches dans l'ordre
};
class SuiviCouches {              // features() incrémental ; mêmes features que features()
  void init();
  int  pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea);
  int  terminer(Feature* out, double* outArea);           // vide la dernière couche en fin d'impression
};
```
`key` ∈ {`"z"`,`"vat"`,`"sg"`,`"fus"`} 用于选择下限阈值。`prep`：`med`=3 点中位数，`avg`=滑动平均（模拟器设置：z→(med,—)，vat/sg→(med,avg)，fus→(med,—)）。

#### 5.3 `msla_stabilite`
```cpp
double slopeAt(const double* t, const double* f, int a, int b);   // pente (régression) sur [a,b]
double resStd(const double* t, const double* f, int a, int b);    // écart-type des résidus
struct Settle { double ts; bool found, tooShort; double sig, thr; };
void   stability(const Serie& s, const char* key, const StabOpt& opt, const Starts& starts,
                 int nLayers, Settle* down, Settle* top);          // down[] après descente, top[] après arrachement
SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers);
```

#### 5.4 `msla_bac` — 料槽、平整度、称重、泄漏（`float`，与固件一致）
```cpp
class BacControleur {
  BacArm   armer(int mode, const BacConfig& cfg, BacRef* ref);  // mode 0=contrôle,1=référence,2=planéité
  BacEvent maj(float t, float z, const float f[5], bool uv);    // 1 échantillon ; évènement quand prêt
  bool     actif() const; int mode() const; void desarmer();
  const ControleResultat&  controle()  const;   // après BAC_EV_CONTROLE
  const PlaneiteResultat&  planeite()  const;    // après BAC_EV_PLANEITE
  const ReferenceResultat& reference() const;    // après BAC_EV_REFERENCE
};
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);
```
配置：`BacConfig{nCells, holdS, ckStatN, ckDescN, vatW, vatH, zMaxN, layerMm}`。
常量：`MSLA_GN = 0,00980665` N/g。

---

### 6. API 参考——模拟器（JavaScript）

`simulateur/index.html` 导出（在 Node 中，通过 `module.exports`）：

| 函数 / 对象 | 作用 |
|---|---|
| `simulate(P)` | 模拟一次打印 → 时间序列（力、Z、阶段、基准真值）。 |
| `sensors(sim, P)` | 生成传感器信号（zc、vat 4 个传感器、sg、融合），含噪声/安装方式。 |
| `runAll(P)` | 完整流水线：simulate + sensors + features + detect + verdict + 稳定性。 |
| `vatCheck`, `planarityTest`, `residueCheck`, `convertOffset` | 料槽 / 平整度 / 残留物检查。 |
| `fitModel`, `parseRecording`, `exampleRecording` | 根据真实录制数据校准模型。 |
| `parseGeometry`, `segment` | 读取层文件、分段。 |
| `MACHINES, RESINS, APPS, GEOMS, PROC0` | 配置档案（设备、树脂、应用、几何、工艺）。 |
| `clone`, `DIRS` | 工具函数。 |

`P` 是一个配置对象：`{machine, resin, proc, app, geom, scen, L, noise, rate, seed, …}`。
`runAll(P).ver` 给出各通道的判定 `{z, vat, sg, fus}`。

---

### 7. 串口协议（主板 ↔ 台架）

链路为 `Serial2`，115200 波特 8N1，ASCII 行以 `\n` 结尾（≤ 32 个字符），主机 = 主板。完整细节及状态机见 `firmware/PROTOCOLE.md`。

**主板 → 台架：**`L <n> [aire mm²]`（层开始）· `U 1`/`U 0`（UV）· `V`（料槽检查）· `P`/`R`（暂停/恢复）· `X`（停止，收尾）· `?`（状态 → `RDY`）。

**台架 → 主板：**`RDY` · `VAT OK|RESIDU|ERREUR` · `OK <n>` · `PAUSE <code> <n>` · `STOP <code> <n>` · `ALRM FORCE` · `ERR`。`STOP` 优先级最高（关闭 UV，平台抬起）。`PAUSE`/`STOP` 是**请求**：主板仍是主机。静默超过 2 秒 → 由主板决定。

---

### 8. 固件——USB 命令（操作员控制台）

独立的原始 USB 链路（CSV，921600 波特）。命令（操作员消息为法语）：
`start`/`stop`（CSV 流）、`tare`、`cal`、`sign <i>`、`cells <1|5>`、`stepsmm <v>`、`dir`、`zero`、
`uv <haut> <bas>`、`alerte <N>`、`raw`、`ref`/`controle`/`planeite`（启动料槽测试）、
`tarevide`/`refbac`（空料槽 / 参考）、`pesee`、`fuite [min]`、`densite <v>`、`maintien <s>`、
`seuils <statN> <descN>`、`bac <W> <H>`、`couche <mm>`、`fmax <N>`、`ack`、`info`。

---

### 9. 阈值参考

按传感器设定的下限阈值（`msla_couche.cpp`，`floorPour()`）：

| key | p (峰值) | c (压缩) | s (压力) |
|---|---|---|---|
| z, fus | 0.4 | 0.8 | 0.25 |
| vat | 2.0 | 2.5 | 1.0 |
| sg | 10 | 12 | (未使用) |

应用参数（配置档案 `APPS`；默认为“手办”）：`lowRatio = 0,6`（当 `peak < lowRatio × attendu − 0,5 × p × nm` 时为 FAIBLE），`obstacleMul = 2,5`（当 `comp < obstacleMul × attendu − c × nm` 时为 OBSTACLE），`settleMargin = 0`。

`nm = noise × resFactor`，其中 `resFactor = max(1, sensRes / 0,05)`（对于 `sg`：`nm = noise`）。`fstall` = Z 电机的最大力（当 `comp ≤ −0,93 × fstall` 时为 CRASH）。固件将这些值保存在 `detNoise`、`detObstacleMul`、`detLowRatio` 中，`fstall` 则保存在 `cfg.zMaxN` 中。

---

### 10. 测试与持续集成

`.github/workflows/tests.yml` 在每次 push/PR 时运行：

| 测试套件 | 验证内容 |
|---|---|
| `node tests/simulateur.test.js` | 物理模型 + 参考检测（JS） |
| `bash tests/lib/run.sh` | **一致性**：C++ 库 == 模拟器（判定与稳定性） |
| `bash tests/firmware_host/run.sh` | 料槽检查（固件在 PC 上编译，Arduino 为模拟） |
| `bash tests/protocole/run.sh` | 串口协议 |
| `bash tests/couche_live/run.sh` | 实时逐层检测（固件路径） |
| `python tests/test_fusion_geometrie.py` | 10.1″ 研究的几何 |
| `bash exemples/run.sh` | 快速入门集成示例 |
| `bash tests/perf/run.sh` | 性能（分析，不是通过/失败测试） |

前置条件：`node`、`g++`（C++17）、`python3` + `numpy`。

---

### 11. 工具（`outils/`）

- **`layers_from_images.py`**：根据切片图像（来自 UVtools/Lychee/… 的 PNG）计算每层的曝光面积（以及中心 `cx, cy`），生成 CSV `layer, area_mm2, cx_mm, cy_mm, islands`，可加载到模拟器中（配置“打印件”→ 导入）。
- **`logger.py`**：通过 USB 读取 ESP32 的 CSV 流并写入带表头的文件（设备、树脂、温度、备注），可重新加载到模拟器中（“真实数据”）。

---

### 12. 集成快速入门

`exemples/quickstart_detection.cpp`：以传感器 CSV（`t,z,ph,ly,area,fz,fvat,fsg`）为输入，针对每个通道以及融合通道，经由 `prep → features → detect` 输出每层一个判定（OK/PAUSE/STOP）。`bash exemples/run.sh` 会在 `capteurs_demo.csv` 上编译并测试（在第 30 层检测到缺陷）。**同一套标定可原样移植到固件中**用于实时处理；一致性测试保证结果完全相同。详情：`exemples/README.md`。

---

### 13. 扩展点

- **添加传感器**：选择一个 `key`，在 `floorPour()` 中定义其下限阈值，调整 `nm`。流水线的其余部分是通用的。
- **添加缺陷场景**：在模拟器（`simulate`）中实现，将其加入 `TRIGGER` 表，通过 `tests/lib/export_scenarios.js` 导出，并验证一致性。
- **修改阈值**：先在模拟器（参考实现）中修改，随后一致性测试保证库同步跟进。切勿让 JS 与 C++ 的逻辑出现分歧。
