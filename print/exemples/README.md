# Quickstart — brancher vos données sur la détection

Objectif : partir de **vos données capteurs** (un CSV) et obtenir un **verdict par
couche** (OK / PAUSE / STOP), avec la même bibliothèque `msla_detect` que le firmware.
Une fois ce chemin compris, le passage à la détection **en direct** sur la carte
(ESP32) est immédiat (même bibliothèque, mêmes résultats).

> Les specs formelles (matériel, concept de détection, API complète) sont dans
> `../docs/DETECTION_SPEC.md` et `../docs/SOFTWARE_DEV_GUIDE.md` (EN / 中文).
> Ce dossier est le **raccourci pratique** pour l'intégration.

---

## 1. Lancer l'exemple

```bash
bash run.sh                 # sur le CSV de démo
bash run.sh mes_donnees.csv # sur vos données
```

Sortie attendue sur le CSV de démo :

```
read 48035 samples, 60 layers from capteurs_demo.csv

  channel "Z arm cell" (key=z, nm=1.000)
      layer 30 -> PAUSE APPUI
      first fault at layer 30  (0 STOP, 1 PAUSE)
  channel "vat (4 cells)" (key=vat, nm=1.000)
      layer 30 -> PAUSE APPUI
      ...
```

Le défaut (un résidu avant la couche 30) est détecté **à la couche 30** sur chaque
voie — identique au simulateur.

Compilation seule (sans le script) :

```bash
c++ -std=c++17 -O2 -I../lib quickstart_detection.cpp \
    ../lib/msla_detect/*.cpp -o quickstart_detection
```

Aucune dépendance externe : la bibliothèque est du **C++17 pur, sans malloc, sans
Arduino**. Le même code tourne sur l'ESP32.

---

## 2. Le format de vos données (CSV)

Une **ligne = un échantillon** (tous capteurs lus au même instant). Colonnes
repérées **par leur nom** dans l'en-tête (l'ordre est libre, les colonnes en trop
sont ignorées). Les lignes commençant par `#` sont des commentaires.

| Colonne | Sens | Unité |
|---|---|---|
| `t`    | temps | s |
| `z`    | position Z du plateau | mm |
| `ph`   | phase | 0=exposition(UV) · 1=arrachement · 2=descente · 3=attente_haute · 4=attente_basse |
| `ly`   | index de couche (0, 1, 2, …), `-1` hors couche | — |
| `area` | aire exposée de la couche `ly` | **cm²** |
| `fz`   | force cellule bras Z | N |
| `fvat` | force somme des 4 cellules du bac | N |
| `fsg`  | StallGuard (optionnel) | N équivalent |

Ce qu'il faut préparer côté banc :

- **Convertir en newtons** : entrez `fz`, `fvat` déjà étalonnés (vos volts/counts →
  N). La détection raisonne en forces.
- **Les phases `ph`** : soit vous les fournissez, soit — plus simple sur du vrai
  matériel — vous laissez le firmware les déduire du mouvement Z + l'UV (voir §4).
- **L'aire** vient de votre trancheur (aire exposée par couche). Une valeur par
  couche, répétée sur les lignes de la couche.

L'exemple détecte automatiquement les colonnes `fz`, `fvat`, `fsg` présentes et
lance une voie de détection pour chacune, plus une voie **fusion** Z+bac.

---

## 3. Ce que fait le programme (le pipeline)

Pour **chaque voie capteur**, 4 appels de bibliothèque :

```
force brute  --prep()------>  force filtrée (médiane, moyenne glissante)
série        --features()-->  caractéristiques par couche (pic d'arrachement,
                              compression à la descente, appui résiduel, course)
features     --detect()---->  drapeaux par couche (apprentissage sur les
                              6 dernières bonnes couches, puis écart)
drapeaux     --> OK / PAUSE / STOP   (CRASH/OBSTACLE = STOP ; APPUI/FAIBLE/FEP = PAUSE)
```

Réglages par voie (repris du simulateur, ne pas changer sans raison) :

| Voie | `prep` médiane | `prep` moyenne | seuil `nm` |
|---|---|---|---|
| `z`   | oui | non | `noise · resFactor` |
| `vat` | oui | oui | `noise · resFactor` |
| `sg`  | oui | oui | `noise` (sans resFactor) |
| `fus` | oui | non | `noise · resFactor` |

Calibration (en haut de `quickstart_detection.cpp`, à adapter à votre banc) :

- `NOISE` : niveau de bruit global de vos lectures ;
- `SENS_RES` : résolution des capteurs de force (N) → `resFactor = max(1, SENS_RES/0.05)` ;
- `OBSTACLE_MUL = 2.5`, `LOW_RATIO = 0.6` : paramètres application ;
- `FSTALL = 140` : force de blocage du moteur Z (N).

La **fusion** Z+bac est pondérée par l'inverse de la variance du bruit de chaque
montage (la voie la moins bruitée pèse plus). L'exemple estime ce bruit sur la phase
d'attente haute (`ph==3`), où la force est une ligne de base plate.

---

## 4. L'étape suivante : la détection en direct (firmware)

Le même moteur tourne **en flux** sur l'ESP32, couche par couche, **résultat
identique** (vérifié par la parité). Côté carte mère, on annonce la couche et l'UV,
et on pousse les échantillons :

```
L <ly> <area>     # début de couche + aire
U 1 / U 0         # UV allumé / éteint
puis, à chaque échantillon : detEchantillon(t, z, f)
```

Le banc **déduit lui-même les phases** du mouvement Z et de l'UV, puis émet
`OK <n>` / `PAUSE <code> <n>` / `STOP <code> <n>` vers la carte mère.

En interne ce sont les classes `msla::SuiviCouches` (construit les *features* en
flux) + `msla::DetecteurCouche` (apprentissage + verdict). Voir :

- `../firmware/banc_msla/banc_msla.ino` — l'intégration réelle (fonctions
  `detEchantillon`, `detFlux`, `detDemarrer`) ;
- `../tests/couche_live/harness.cpp` — un exemple exécutable qui rejoue des données
  dans ce chemin firmware ;
- `../firmware/PROTOCOLE.md` — le protocole série carte mère ↔ banc.

Donc : **ce quickstart (batch) sert à valider vos données et votre calibration hors
ligne** ; une fois les verdicts corrects, vous reprenez les mêmes réglages dans le
firmware pour la détection temps réel.

---

## 5. Fichiers de ce dossier

| Fichier | Rôle |
|---|---|
| `quickstart_detection.cpp` | l'exemple d'intégration (CSV → verdicts) |
| `run.sh` | compile et lance l'exemple |
| `capteurs_demo.csv` | données de démo (scénario résidu, défaut couche 30) |
| `couches_figurine.csv`, `enregistrement_exemple.csv` | autres jeux d'exemple |
