# Simulation de performance du banc MSLA

Trois volets, lancés par `bash tests/perf/run.sh` :
1. **Budget temps réel** de la chaîne d'acquisition (`budget.py`, analytique) ;
2. **Coût de calcul** de la détection (`bench.cpp`, mesure réelle sur PC) ;
3. **Robustesse au bruit** du signal (`noise_sweep.js`, balayage).

Ce sont des outils d'analyse (pas des tests pass/fail) ; les chiffres ci-dessous
sont ceux obtenus sur cette base de code. Relancer les scripts pour les reproduire.

## 1. Budget temps réel (transfert capteur → électronique → liaisons)

| Élément | Valeur | Commentaire |
|---|---|---|
| Lecture d'un échantillon, 1 cellule | ≈ 110 µs | bit-bang 25 impulsions, horloge commune |
| Lecture d'un échantillon, 5 cellules | ≈ 170 µs | les 5 HX711 lus **ensemble** (SCK commun) |
| Cadence 10 SPS | période 100 ms | RATE à la masse |
| Cadence 80 SPS | période 12,5 ms | RATE au +3,3 V (recommandé) |
| CPU occupé à lire (80 SPS, 5 cell.) | ≈ 1,4 % | le reste est l'attente de conversion du HX711 |
| USB CSV (921 600 bauds), 5 cell. @ 80 SPS | ≈ 5 % du débit | émission d'une ligne ≈ 0,63 ms |
| Serial2 carte mère (115 200 bauds) | < 0,1 % | quelques octets par couche |

**Conclusion.** La cadence est limitée par le HX711, pas par la lecture ni par les
liaisons série. À 80 SPS / 5 cellules, il reste **> 90 % de temps CPU libre** pour la
détection. La broche RATE doit être passée au +3,3 V pour atteindre 80 SPS.

## 2. Coût de calcul de la détection

Mesuré en rejouant une impression réelle (60 couches, dont 4 couches de base à
exposition longue ≈ 2000 échantillons) à travers la vraie fonction `detEchantillon()`
du firmware :

| Métrique | Sur ce PC | Budget (80 SPS) |
|---|---|---|
| Temps moyen par échantillon | ≈ 0,05 µs | — |
| **Pire cas** (échantillon finalisant une couche) | ≈ 37 µs | **12 500 µs** |
| Marge sur le pire cas (PC) | ≈ 340× | — |

Le coût n'est pas réparti uniformément : l'essentiel du travail (tris de médianes pour
les features) a lieu **à la fin de chaque couche**, pas à chaque échantillon.

**Extrapolation ESP32** (240 MHz, double émulé en logiciel) : même en supposant l'ESP32
jusqu'à 50× plus lent par opération que ce PC, le pire cas reste ≈ **1,8 ms**, soit bien
en dessous des 12,5 ms, avec une marge d'au moins 7×. Le temps réel est tenu
confortablement. *(Chiffre à confirmer sur matériel ; non mesuré sur ESP32 ici.)*

## 3. Robustesse au bruit du signal

Le bruit du capteur (bruit HX711, 50 Hz secteur, vibrations moteur, dérive thermique,
pics) est injecté par le modèle du simulateur, avec un seuil de détection qui s'adapte
au bruit mesuré (comme après étalonnage). Panne injectée à la couche 30. « bruit » est
un multiplicateur : 1 = niveau nominal du capteur 0,03 N.

| Bruit | Fausses alertes (normal) | Décollement | Supports | Résidu | Film détendu |
|---|---|---|---|---|---|
| 0,5 | rares (1/6 tirages) | 100 % (lat 0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 1 (nominal) | **0** | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 2 | 0 | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 4 | 0 | 100 % (0) | 100 % (0) | 100 % (0) | 100 % (4) |
| 6 | 0 | 100 % (0) | 100 % (0) | **raté** | 100 % (5) |
| 8 | 0 | 100 % (0) | ~100 % | **raté** | **raté** |

(lat = latence en couches après l'incident ; « raté » = panne non détectée.)

**Conclusion.**
- **Aucune fausse alerte** dès le niveau de bruit nominal (le seuil adaptatif absorbe le
  bruit). À très faible bruit, de rares fausses alertes apparaissent car le seuil plancher
  domine ; c'est sans conséquence pratique.
- Les pannes **franches** (décollement, rupture de supports) sont détectées jusqu'à ~8× le
  bruit nominal, à la couche de l'incident.
- Le **résidu** est le signal le plus faible (petit appui / petite compression) : il décroche
  vers **6× le bruit nominal**. C'est lui qui fixe la limite pratique.
- Marge de sécurité : la détection reste fiable jusqu'à **~4-5× le bruit nominal**. Pour
  gagner en marge, baisser le bruit (câble blindé, masse commune, filtre anti-pics) ou
  utiliser un capteur plus fin (sensRes 0,01 N).
