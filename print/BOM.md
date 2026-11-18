# Nomenclature (BOM) — banc de mesure MSLA

Liste du matériel pour transformer une imprimante résine LCD/MSLA en banc de mesure
(force sur le plateau, position Z, état UV) et contrôle du bac avant impression.
Détails de montage et de câblage : voir `README.md` (§1 à §3, §7).

Deux configurations :
- **Minimale** (`cells 1`) : bras Z seul — détection d'impression et contrôle du bac.
- **Complète** (`cells 5`) : + 4 cellules sous le bac — pesée, positionnement, fuite,
  planéité et localisation du résidu.

## 1. Composants

| Qté | Composant | Réf. / caractéristique | Rôle | Config |
|---|---|---|---|---|
| 1 | Carte MCU | ESP32 DevKit (ESP32-WROOM-32) | Acquisition, calcul, USB + liaison carte mère (`Serial2`) | Toutes |
| 1 | Cellule de charge en poutre | 20 kg (10 kg suffit en petit format) | Force sur le bras Z (arrachement / compression) | Toutes |
| 1 | Module | HX711 | Convertisseur 24 bits de la cellule Z | Toutes |
| 1 | Capteur UV analogique | GUVA-S12SD (ou prise directe de la commande des LED UV) | Détecter l'exposition | Toutes |
| 2 | Optocoupleur rapide | 6N137 + résistances 330 Ω et 10 kΩ | Prélever STEP et DIR du driver Z sans perturber l'imprimante | Toutes |
| — | Câblage | Câble blindé 4 conducteurs, connecteurs, gaine | Liaisons cellules / capteurs (à éloigner des câbles moteur) | Toutes |
| — | Mécanique | Pièces de fixation imprimées ou usinées | Adaptateurs chariot Z ↔ cellule ↔ support de plateau | Toutes |
| 4 | Cellules de charge en poutre | 10 kg (coins du bac) | Pesée, positionnement, fuite, planéité | Complète (`cells 5`) |
| 4 | Modules | HX711 supplémentaires | Convertisseurs des 4 coins | Complète |

**Budget indicatif : 25 à 45 €** selon l'option. Repères par capteur (côté simulateur) :
bras Z ≈ 10 €, 4 coins du bac ≈ 20 €, StallGuard TMC2209 (sans matériel, via UART) 0 €.

## 2. Câblage ESP32 (résumé)

| Broche ESP32 | Relié à |
|---|---|
| 3V3 / GND | VCC / GND de **tous** les HX711 et du capteur UV |
| GPIO 18 | SCK commun à **tous** les HX711 (horloge commune) |
| GPIO 19 | DOUT du HX711 du bras Z (cellule 0) |
| GPIO 21, 22, 23, 25 | DOUT des HX711 des coins 1, 2, 3, 4 (avant-G, avant-D, arrière-D, arrière-G) |
| GPIO 26 | STEP du driver Z, via optocoupleur 6N137 |
| GPIO 27 | DIR du driver Z, via optocoupleur 6N137 |
| GPIO 34 | Sortie analogique du capteur UV |
| GPIO 16 / 17 | RX2 / TX2 — protocole série vers la carte mère (voir `firmware/PROTOCOLE.md`) |

Cellule vers HX711 : rouge E+, noir E−, blanc A−, vert A+ (code courant, à vérifier sur
la fiche de la cellule). DIR doit rester sur une broche < 32.

## 3. Points sensibles

- **80 mesures/s** : passer la broche RATE du HX711 (broche 15 de la puce) au +3,3 V
  (souvent reliée à la masse d'origine = 10 mesures/s).
- **Capacité de la cellule Z** ≥ force maximale du moteur Z, pour qu'un écrasement ne la
  détruise pas.
- **Cellules du bac** : les isoler thermiquement de la chaleur de l'écran (cale plastique)
  et guider le bac latéralement sans frottement vertical.
- **STEP/DIR introuvables** : alternative par capteur de distance laser VL6180X pointé sur
  le plateau (demande une modification du firmware).
- Les réglages (étalonnage, pas/mm, seuils…) sont conservés dans la mémoire de l'ESP32.

> L'étude `conception/` (machine MSLA 10,1″) est un sujet distinct de ce banc de mesure
> et aurait sa propre nomenclature.
