# Banc de mesure MSLA / MSLA Force Bench / MSLA 测力台

Langues / Languages / 语言 : [Français](#français) · [English](#english-overview) · [中文](#中文-概览)

Documentation détaillée trilingue (FR/EN/中文) : `BOM.md`, `firmware/PROTOCOLE.md`,
`docs/DETECTION_SPEC.md`, `docs/SOFTWARE_DEV_GUIDE.md`.

## English (overview)

This kit changes an LCD resin printer into a measurement bench. It also checks the resin
vat before each print. It records the force on the build platform (and, as an option, below
the 4 corners of the vat), the Z position, and the UV state, layer by layer. The records
load into the **MSLA resin sensor simulator** (`simulateur/index.html`), which cuts the
layers, measures the peel peaks and the compression, and fits the model to your machine and
resin.

The system also finds faults during a print (detachment, support failure, debris, loose
film) and sends verdicts to the printer motherboard on a serial link.

Start here:
- Parts and wiring: `BOM.md` and `docs/DETECTION_SPEC.md` (with diagrams).
- Full build and detection specification (ASD-STE100): `docs/DETECTION_SPEC.md`.
- Software architecture and API: `docs/SOFTWARE_DEV_GUIDE.md`.
- Serial protocol to the motherboard: `firmware/PROTOCOLE.md`.
- Build and upload the firmware with Arduino IDE 2 (board "ESP32 Dev Module").
- Run the tests on a PC: `node tests/simulateur.test.js`, `bash tests/lib/run.sh`,
  `bash tests/firmware_host/run.sh`, `bash tests/protocole/run.sh`,
  `bash tests/couche_live/run.sh`, `python3 tests/test_fusion_geometrie.py`.

## 中文 (概览)

本套件把一台 LCD 树脂打印机改造成测力台，并在每次打印前检查料槽。它逐层记录构建平台受力
（可选：料槽四角下方）、Z 轴位置与紫外状态。记录导入 **MSLA 树脂传感器模拟器**
（`simulateur/index.html`）进行分层、测量剥离峰值与压缩，并把模型标定到你的机器和树脂。

系统还能在打印过程中发现故障（脱落、支撑断裂、碎屑、离型膜松弛），并通过串口向打印机主板发送判定。

从这里开始：
- 物料与接线：`BOM.md` 与 `docs/DETECTION_SPEC.md`（含接线图）。
- 完整搭建与检测规范（ASD-STE100）：`docs/DETECTION_SPEC.md`。
- 软件架构与 API：`docs/SOFTWARE_DEV_GUIDE.md`。
- 与主板的串口协议：`firmware/PROTOCOLE.md`。
- 用 Arduino IDE 2 编译并烧录固件（开发板“ESP32 Dev Module”）。
- 在 PC 上运行测试：`node tests/simulateur.test.js`、`bash tests/lib/run.sh`、
  `bash tests/firmware_host/run.sh`、`bash tests/protocole/run.sh`、
  `bash tests/couche_live/run.sh`、`python3 tests/test_fusion_geometrie.py`。

---

## Français

Ce kit transforme une imprimante résine LCD en banc de mesure, et sert aussi à contrôler le bac avant chaque impression (§7). Il enregistre la force sur le plateau (et, en option, sous les 4 coins du bac), la position de l'axe Z et l'état de l'UV, couche par couche. Les fichiers obtenus se chargent dans le **Simulateur capteurs résine MSLA** (`simulateur/index.html`) (section 5, « Données réelles et recalage »), qui découpe les couches, mesure les pics d'arrachement, la compression et le temps d'immobilisation de la résine, puis recale le modèle sur ta machine et ta résine.

## Contenu du kit

| Fichier | Rôle |
|---|---|
| `firmware/banc_msla/banc_msla.ino` | Programme de l'ESP32 : lecture des HX711, comptage des pas Z, capteur UV, envoi CSV, contrôle du bac, protocole carte mère |
| `firmware/PROTOCOLE.md` | Protocole série (Serial2) entre le banc de mesure et la carte mère de l'imprimante |
| `BOM.md` | Nomenclature du matériel (composants, câblage, budget) |
| `docs/DETECTION_SPEC.md` | Spécification complète de la détection (anglais ASD-STE100 + 中文), schémas de câblage inclus |
| `docs/SOFTWARE_DEV_GUIDE.md` | Guide développeur logiciel (anglais + 中文) : API, flux de données, seuils, points d'extension |
| `docs/SOFTWARE.md` | Documentation logicielle complète (anglais) : architecture, modules, algorithmes, firmware, protocole, API, perf, tests |
| `docs/STRUCTURE.md` | Arborescence du dépôt expliquée (anglais) : rôle de chaque répertoire, avec schéma visuel |
| `outils/logger.py` | Enregistre les mesures de l'ESP32 dans un fichier CSV avec les métadonnées de l'essai |
| `outils/layers_from_images.py` | Calcule la surface exposée de chaque couche à partir des images de couches du fichier tranché |
| `exemples/enregistrement_exemple.csv` | Enregistrement synthétique (60 couches) pour tester la chaîne sans matériel |
| `exemples/couches_figurine.csv` | Fichier de couches correspondant à l'exemple |
| `simulateur/index.html` | Simulateur capteurs résine MSLA (s'ouvre directement dans un navigateur, sans installation) |
| `lib/msla_detect/` | Bibliothèque C++ de détection (filtres, apprentissage par couche, stabilité, contrôle du bac, planéité, pesée, fuite), portable et sans allocation dynamique, utilisée par le firmware et les tests |
| `tests/` | Tests du simulateur (Node.js), parité simulateur↔bibliothèque C++ et contrôle du bac du firmware (sur PC) |

## 1. Matériel

| Qté | Élément | Remarque |
|---|---|---|
| 1 | ESP32 DevKit (ESP32-WROOM-32) | USB vers le PC |
| 1 | Cellule de charge en poutre 20 kg (bras Z) | 10 kg suffit sur un petit format. Choisir une capacité au moins égale à la force maximale du moteur Z, pour qu'un écrasement ne la détruise pas |
| 4 | Cellules de charge en poutre 10 kg (coins du bac) | Option « 4 cellules sous le bac » |
| 1 ou 5 | Modules HX711 | Il faut pouvoir passer la broche RATE au +3,3 V pour 80 mesures/s (voir §3) |
| 1 | Capteur UV analogique GUVA-S12SD | Ou prise directe du signal de commande des LED UV (plus fiable) |
| 2 | Optocoupleurs rapides 6N137 + résistances 330 Ω et 10 kΩ | Pour prélever STEP et DIR sans perturber la carte de l'imprimante |
| — | Câble blindé 4 conducteurs, connecteurs, gaine | Éloigner les câbles des câbles moteur |
| — | Pièces de fixation imprimées ou usinées | Adaptateurs entre le chariot Z, la cellule et le support de plateau |

Budget indicatif : 25 à 45 € selon l'option.

## 2. Montage mécanique

**Cellule dans le bras Z.** La cellule en poutre se place entre le chariot de l'axe Z et le support du plateau : une extrémité vissée côté chariot, l'autre côté plateau, la flèche gravée dans le sens de la traction vers le bac. Elle travaille en flexion, donc elle ajoute un peu de souplesse (quelques centièmes à dixièmes de mm à pleine charge). Après montage :

1. refaire la mise à niveau du plateau ;
2. vérifier qu'aucun câble ne frotte pendant la course Z ;
3. imprimer une pièce de test et comparer la précision des premières couches avec une impression sans cellule.

**4 cellules sous le bac (option).** Une cellule sous chaque coin du bac, numérotées 1 = avant-gauche, 2 = avant-droit, 3 = arrière-droit, 4 = arrière-gauche (vue de face). Le bac doit rester guidé latéralement sans frottement vertical, sinon une partie de la force passe par les guides. Le bac est rehaussé : refaire la mise à niveau. Isoler les cellules de la chaleur de l'écran (cale en matière plastique), sinon la dérive thermique augmente.

**Sécurité.** L'écran LCD est fragile. Règle le seuil d'alerte (`alerte 60`) et surveille la LED pendant les premiers essais. Ne provoque jamais volontairement un écrasement à pleine force : utilise le simulateur pour ce scénario.

## 3. Câblage

| ESP32 | Relié à |
|---|---|
| 3V3 / GND | VCC / GND de tous les HX711 et du capteur UV |
| GPIO 18 | SCK de **tous** les HX711 (horloge commune) |
| GPIO 19 | DOUT du HX711 du bras Z (cellule 0) |
| GPIO 21, 22, 23, 25 | DOUT des HX711 des coins 1, 2, 3, 4 |
| GPIO 26 | STEP du driver Z, via optocoupleur |
| GPIO 27 | DIR du driver Z, via optocoupleur |
| GPIO 34 | Sortie analogique du capteur UV |
| GPIO 2 | LED d'alerte (déjà présente sur la carte) |

Cellule vers HX711 : rouge E+, noir E−, blanc A−, vert A+ (code le plus courant, à vérifier sur la fiche de ta cellule).

**80 mesures/s.** Sur beaucoup de modules HX711, la broche RATE (broche 15 de la puce) est reliée à la masse, ce qui donne 10 mesures/s. Couper la piste ou le pont et relier RATE au +3,3 V. Le simulateur montre le compromis : 80 mesures/s capte bien le pic d'arrachement mais laisse passer le 50 Hz du secteur, 10 mesures/s supprime le 50 Hz mais lisse le pic.

**STEP et DIR.** Ce sont les signaux que la carte de l'imprimante envoie au driver du moteur Z. On les trouve sur le support du driver s'il est enfichable, sinon sur des points de test ou les pistes qui arrivent au driver. Côté imprimante : anode de la LED du 6N137 sur STEP (ou DIR) via 330 Ω, cathode sur la masse de l'imprimante. Côté ESP32 : sortie du 6N137 sur GPIO 26 (ou 27) avec une résistance de tirage de 10 kΩ au 3,3 V. Les deux montages restent électriquement séparés. Si tu ne trouves pas ces signaux, un capteur de distance laser (VL6180X) pointé sur le plateau est une alternative : elle demande une modification du firmware.

**UV.** Deux possibilités :
- capteur GUVA-S12SD placé au bord de l'écran, hors du bac, orienté vers la lumière qui s'échappe par le côté quand l'UV est allumé ;
- plus fiable : prélever la commande des LED UV (grille du transistor ou entrée du driver de LED) via un optocoupleur, et l'envoyer sur GPIO 34 avec une résistance de tirage.

## 4. Programme de l'ESP32

1. Installer l'Arduino IDE 2, puis dans *Préférences › URL de gestionnaire de cartes* ajouter `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, et installer le paquet « esp32 » d'Espressif.
2. Ouvrir `firmware/banc_msla/banc_msla.ino`, choisir la carte « ESP32 Dev Module », téléverser.
3. Ouvrir le moniteur série à 921600 bauds, fin de ligne « LF », taper `aide`.

Si tu as changé une broche, modifie le bloc « Câblage » en tête du fichier. DIR doit rester sur une broche inférieure à 32.

Le firmware partage sa logique de détection avec le simulateur via la bibliothèque `lib/msla_detect/`. L'IDE Arduino compile automatiquement le sous-dossier `firmware/banc_msla/src/`, qui pointe vers cette bibliothèque par un lien symbolique (`src/msla_detect → ../../lib/msla_detect`). Sous Windows, si le lien n'est pas suivi à l'extraction du dépôt, remplace `firmware/banc_msla/src/msla_detect` par une **copie** du dossier `lib/msla_detect`.

| Commande | Effet |
|---|---|
| `cells 1` / `cells 5` | Bras Z seul / bras Z + 4 coins |
| `tare` | Met toutes les cellules à zéro |
| `cal <i> <grammes>` | Étalonne la cellule i avec une masse connue |
| `sign <i>` | Inverse le signe de la cellule i |
| `stepsmm <valeur>` | Pas moteur par mm de l'axe Z |
| `dir` | Inverse le sens de comptage de Z |
| `zero` | Z = 0 à la position actuelle |
| `uv <haut> <bas>` | Seuils du capteur UV (valeurs brutes 0 à 4095) |
| `alerte <newtons>` | Seuil d'allumage de la LED d'alerte |
| `raw` | 10 lectures brutes, pour vérifier le câblage |
| `info` | Affiche les réglages et la position Z |
| `start` / `stop` | Démarre ou arrête l'envoi des mesures |
| `ref` / `controle` / `ack` | Contrôle du bac avant impression (voir §7) |
| `maintien`, `seuils`, `bac`, `couche`, `fmax` | Réglages du contrôle du bac |

Les réglages sont gardés dans la mémoire de l'ESP32 après coupure.

## 5. Étalonnage (une fois, puis à chaque changement mécanique)

1. **Zéro.** Plateau monté, immobile, hors de la résine, bac en place : `tare`.
2. **Bras Z.** Suspendre au plateau une masse connue (par exemple une bouteille d'eau pesée sur une balance de cuisine, accrochée par une ficelle) : `cal 0 1000` pour 1000 g. La force affichée doit devenir positive : c'est le même sens que l'arrachement.
3. **Coins du bac.** Poser une masse connue exactement au-dessus du coin 1 : `cal 1 500`. Recommencer pour les coins 2, 3 et 4. Une masse posée donne une force négative, comme la compression.
4. **Axe Z.** Calculer les pas/mm : `200 × micro-pas ÷ pas de la vis` (exemple : vis T8 au pas de 8 mm, micro-pas 16 → 400). `stepsmm 400`. Monter le plateau de 10 mm depuis le menu de l'imprimante et vérifier avec `info` que z a augmenté de 10. S'il a diminué : `dir`.
5. **UV.** Lancer une exposition de test depuis le menu de l'imprimante et lire `uv_brut` avec `info` pendant et hors exposition. Régler `uv <haut> <bas>` entre les deux valeurs (par exemple `uv 900 600`).
6. **Vérification.** `start`, puis lancer une petite impression : pendant chaque remontée, fz_N doit monter (positif) puis retomber ; pendant chaque descente, il doit devenir légèrement négatif.

## 6. Enregistrer un essai

```
pip install pyserial
python outils/logger.py --liste
python outils/logger.py --port COM5 --tare --machine "Imprimante atelier" --resine "Grise standard" --temp 24 --note "figurine, supports moyens"
```

Lancer l'enregistrement **avant** de démarrer l'impression, l'arrêter avec Ctrl+C à la fin. Le fichier `banc_AAAAMMJJ_HHMMSS.csv` contient en tête une ligne `# meta` avec les informations de l'essai.

Pour comparer couche par couche, calculer aussi les surfaces de la pièce imprimée :

```
pip install numpy pillow scipy
python outils/layers_from_images.py couches/ --pixel-um 34.4 --sortie piece_couches.csv
```

Les images de couches s'obtiennent avec UVtools (ouvrir le fichier tranché, puis exporter les couches en PNG) ou directement depuis un fichier .zip / .sl1. La taille du pixel figure dans la fiche technique de l'écran (exemples courants : 50, 34,4, 28,5 ou 22 µm).

## 7. Contrôle du bac avant chaque impression

Le but : savoir, avant de lancer une impression, s'il reste au fond du bac un résidu (pointe de support, éclat de pièce ratée) que la première couche va écraser contre le film et l'écran.

**Principe.** Un fichier de test fait descendre le plateau vide, lentement et sans durcir de résine, jusqu'à la hauteur de la première couche, puis le laisse 5 s en bas. Le banc compare la force à une référence enregistrée sur bac propre :
- pendant la descente, un gros résidu se voit au moment du contact, et la hauteur du contact donne son épaisseur ;
- pendant le maintien, une fois la résine immobile, un petit résidu laisse une force d'appui permanente. C'est cette mesure qui détecte les résidus fins (environ 70 µm pour un éclat de 0,4 mm de diamètre, d'après le simulateur) ;
- avec les 4 cellules du bac, le banc indique aussi la position du résidu.

Le test n'écrase rien de plus que la première couche ne l'aurait fait, et il le fait lentement, avant que les couches de base (longues, sur une grande surface) n'enfoncent le résidu dans le film.

**Ce que le contrôle ne voit pas.** Un résidu hors de la zone du plateau n'est pas écrasé, donc aucun capteur ne le voit. Il n'abîme rien tant qu'il reste là, mais le mouvement de la résine peut l'amener sous la pièce : filtre la résine après chaque impression ratée. Un résidu plus fin que la première couche n'est pas écrasé non plus.

**Préparer le fichier de test (une fois).**
1. Trancher n'importe quelle petite pièce avec les réglages habituels de la résine.
2. L'ouvrir dans UVtools, supprimer toutes les couches sauf la première, et rendre cette couche entièrement noire (aucun pixel allumé).
3. Régler pour cette couche : vitesse de descente lente (40 à 60 mm/min), attente avant exposition de 5 s (« wait time before cure » / « rest time before exposure » selon le logiciel et le format), exposition la plus courte acceptée. Sans pixel allumé, rien ne durcit.
4. Enregistrer sous le nom `controle_bac` dans le format de l'imprimante. Si ton format ou ta machine n'accepte pas l'attente avant exposition, utilise à la place le temps d'exposition de la couche noire comme maintien en bas.

Les noms exacts des menus varient selon la version d'UVtools et le format de fichier : vérifie après la première utilisation que le plateau descend bien lentement et reste quelques secondes en bas sans que l'UV ne s'allume.

**Enregistrer la référence (bac propre).** Bac filtré ou neuf, niveau de résine habituel, résine à température normale :

```
ref
```

puis lancer `controle_bac` sur l'imprimante. Le banc répond `# REFERENCE enregistree`. Refaire la référence en cas de changement de résine, de film, de plateau ou de fichier de test.

**Avant chaque impression.**

```
controle
```

puis lancer `controle_bac`. Le banc répond par exemple :

```
# CONTROLE OK : appui 0.03 N, risque aucun
# CONTROLE RESIDU : appui 1.20 N, position estimee x=106 y=52 mm, risque modere
# CONTROLE RESIDU : appui 30.02 N, contact a 0.25 mm au-dessus de l'arret (epaisseur estimee 0.30 mm), position estimee x=106 y=59 mm, risque eleve
```

La LED clignote 3 fois si le bac est propre, et reste allumée en cas de résidu (commande `ack` pour l'éteindre). Le test fonctionne sans PC ; le PC sert seulement à lire le verdict détaillé (moniteur série ou `logger.py`).

| Verdict | Que faire |
|---|---|
| OK | Lancer l'impression |
| Résidu, risque modéré (appui jusqu'à 3 N) | Couche de nettoyage (exposition pleine surface d'environ 10 à 15 s, puis décoller la plaque durcie à la main), puis refaire le contrôle |
| Résidu, risque élevé | Ne pas imprimer. Vider le bac, filtrer la résine, inspecter le film à la lumière ; puis couche de nettoyage et nouveau contrôle |
| DANGER (force proche du maximum du moteur) | Arrêter l'imprimante, retirer le plateau, vider le bac et chercher le débris : il est épais et dur |

Réglages du contrôle : `maintien <s>` (durée de l'attente en bas, 5 s par défaut), `seuils <appui> <descente>` (0,3 et 0,4 N par défaut), `bac <largeur> <profondeur>` (distance entre les cellules, pour la position), `couche <mm>` (épaisseur de la première couche), `fmax <N>` (force maximale du moteur Z).

**Limite importante.** Le banc ne peut pas arrêter l'imprimante. Pendant le test, un très gros débris (plusieurs dixièmes de mm, dur) subit déjà toute la force du moteur avant que tu puisses réagir. Après une impression ratée, fais donc toujours une couche de nettoyage et filtre la résine avant de lancer le contrôle : le contrôle sert à trouver ce qui reste et qu'on ne voit pas.

## 7 bis. Bac, résine et planéité (4 cellules sous le bac, `cells 5`)

Une seule fois, **sans bac** : `tarevide` (zéro des 4 cellules). Puis pose le bac **vide** bien enclenché : `refbac` (poids et répartition de référence).

| Commande | Ce qu'elle fait |
|---|---|
| `pesee` | présence du bac, coin mal enclenché, bac décalé, quantité de résine (ml) |
| `fuite <min>` | pèse au début et à la fin (30 min par défaut), imprimante à l'arrêt ; alerte au-delà de 3 g perdus |
| `densite <g/ml>` | densité de la résine (1,1 par défaut) |
| `planeite` | analyse la prochaine descente lente du plateau vide (fichier de test du §7, 30 mm/min) |

`planeite` donne le décalage du centre d'effort (dx, dy en mm) quand la somme des 4 cellules atteint 5 N et le côté le plus bas du plateau. Entre ces deux valeurs dans le convertisseur de la section « Bac, résine et planéité » du simulateur pour obtenir l'angle (résolution ≈ 0,02°). Le capteur du bras Z seul ne donne que l'ampleur, pas la direction.

Limite : une couche polymérisée sur une pièce immergée ne change pas le poids du bac (poussée d'Archimède) ; la consommation se voit seulement quand la pièce sort de la résine.

## 8. Programme d'essais conseillé

Note la température de la résine à chaque essai : c'est le paramètre qui change le plus les forces.

| Série | But | Comment |
|---|---|---|
| A | Répétabilité | 3 impressions identiques de la même pièce |
| B | Effet de la température | Même pièce à 18, 25 et 30 °C |
| C | Effet de la vitesse | Remontée lente à 60, 120 et 240 mm/min |
| D | Attentes | Attente avant exposition à 0 ; 0,5 ; 1 et 2 s : repérer quand la résine est immobile |
| E | Supports qui cassent | Une moitié de pièce avec des supports volontairement trop fins |
| F | Décollement | Couches de base sous-exposées, sur une petite pièce sans valeur, plateau propre |
| G | Film usé | Même pièce avec un film usé puis un film neuf |

Ne fais pas d'essai de résidu ou d'écrasement réel pendant une impression : utilise la section « Contrôle du bac avant impression » du simulateur, ou le contrôle du §7 avec un éclat fin et mou.

## 9. Charger les mesures dans le simulateur

1. Section 1 « Profils » : choisir la machine, la résine et l'application les plus proches. Pour « Pièce imprimée », choisir « Importée » et charger `piece_couches.csv`.
2. Section 5 « Données réelles et recalage » : charger le CSV de `logger.py`. Le simulateur indique le nombre de couches reconnues, les attentes mesurées, la vitesse de remontée, et compare mesure et modèle couche par couche.
3. Cliquer sur « Appliquer le recalage aux profils » : l'adhérence de la résine, la raideur du film, la vitesse d'écoulement, l'intensité des parasites et les temps d'attente prennent les valeurs mesurées.
4. Enregistrer le jeu de profils (section 1) sous un nom clair, par exemple « Imprimante atelier + Grise standard 24 °C ».
5. Relancer les scénarios de panne avec ces profils : les seuils d'alerte sont alors réglés sur ta machine.

Pour tester la chaîne sans matériel, utilise « Charger un enregistrement d'exemple » ou `exemples/enregistrement_exemple.csv` : il a été généré avec des paramètres cachés (adhérence ×1,35, raideur du film 21 N/mm, écoulement ×1,7, attente 1,2 s, attente en haut 0,8 s). Le recalage doit les retrouver.

## 10. Format du fichier CSV

```
# meta {"debut": "...", "machine": "...", "resine": "...", "temperature_C": 24, "note": "..."}
t_s,fz_N,c0_N,c1_N,c2_N,c3_N,z_mm,uv,uv_raw
0.0125,0.012,0.004,-0.010,0.003,0.001,12.3500,0,412
```

- `t_s` : temps depuis `start`, en secondes.
- `fz_N` : force sur le bras Z, en newtons. Positif = traction vers le bac (arrachement), négatif = compression.
- `c0_N` à `c3_N` : force sur chaque coin du bac, même convention (présent seulement avec `cells 5`).
- `z_mm` : position du plateau, en mm.
- `uv` : 1 pendant l'exposition, 0 sinon. `uv_raw` : valeur brute du capteur.

Le simulateur accepte aussi un fichier sans les colonnes des coins, ou sans `fz_N` si les 4 coins sont présents. Les colonnes `z_mm` et `uv` sont obligatoires pour découper les couches.

## 11. Dépannage

| Symptôme | Cause probable |
|---|---|
| « un HX711 ne répond pas » | DOUT ou SCK mal câblé, module non alimenté, ou `cells 5` alors que 4 coins ne sont pas branchés |
| La force ne bouge pas quand on appuie | Fils A+/A− inversés ou cellule montée dans le mauvais sens de flexion |
| Force négative pendant l'arrachement | Faire `sign 0` (ou le coin concerné) |
| Mesure qui saute de plusieurs newtons | Câble non blindé près d'un câble moteur, masse commune manquante ; activer le filtre anti-pics du simulateur |
| z ne change pas | STEP non branché ou optocoupleur à l'envers ; tester avec `info` en bougeant Z depuis le menu |
| z dérive après plusieurs couches | Pas perdus à la lecture : signal STEP trop faible ou parasité ; vérifier la résistance de tirage de 10 kΩ |
| uv toujours à 0 ou à 1 | Seuils mal réglés : lire `uv_brut` avec `info` et régler `uv <haut> <bas>` |
| Le simulateur reconnaît trop peu de couches | Colonnes z_mm ou uv absentes ou incorrectes |

## 12. Limites

- Le firmware fait de la mesure seulement : il ne met pas l'imprimante en pause. La LED d'alerte peut servir de point de départ pour un arrêt, à condition de passer par une entrée prévue par la carte de l'imprimante.
- Les profils de machines et de résines du simulateur sont génériques. Seul le recalage sur tes enregistrements donne des valeurs fiables pour ta machine.
- Le recalage ajuste 7 grandeurs. Les autres paramètres du modèle (forme exacte de l'oscillation du film, effet ventouse, comportement des résines chargées) restent des approximations.

## 13. Tests

```
node tests/simulateur.test.js        # modèle, détection des pannes, contrôle du bac, recalage
bash tests/lib/run.sh                # parité simulateur (JS) ↔ bibliothèque msla_detect (C++)
bash tests/firmware_host/run.sh      # contrôle du bac du firmware, compilé sur PC (g++, python3, numpy)
bash tests/protocole/run.sh          # protocole série carte mère ↔ banc (Serial2), compilé sur PC
bash tests/couche_live/run.sh        # détection par couche EN DIRECT du firmware, sur les scénarios du simulateur
python3 tests/test_fusion_geometrie.py   # étude 10,1″ : géométrie du script Fusion 360
```

Le test `couche_live` rejoue les scénarios du simulateur à travers le chemin réel du firmware (commandes `L`/`U` de la carte mère + échantillons de force, le firmware déduisant lui-même les phases du mouvement Z) et vérifie que chaque défaut est signalé à la bonne couche, sans aucune fausse alerte sur une impression normale.

Simulation de performance (outils d'analyse, hors CI, ~2 min) :

```
bash tests/perf/run.sh               # budget temps réel + coût de calcul + robustesse au bruit
```

`tests/perf/` chiffre le budget temps réel de la chaîne (HX711→ESP32, liaisons série), mesure le coût de calcul réel de la détection (pire cas vs période d'échantillonnage) et balaie le bruit du signal pour trouver la limite de détection. Les conclusions sont figées dans `tests/perf/RAPPORT.md`.

Le test de parité fait exporter par le simulateur un jeu de scénarios (traces + verdict attendu) et vérifie que la bibliothèque C++ retrouve exactement les mêmes verdicts d'apprentissage par couche et les mêmes temps de stabilisation, sans aucune fausse alerte.

Le test du firmware compile le firmware sur PC avec de fausses bibliothèques Arduino et lui fait analyser des enregistrements synthétiques : bac propre, résidus de 60, 100 et 300 µm, niveau de résine différent de la référence. Comme le firmware utilise désormais la bibliothèque `lib/msla_detect/`, ce test valide aussi le contrôle du bac de la bibliothèque.
