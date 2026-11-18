#!/usr/bin/env python3
"""Budget temps réel de la chaîne d'acquisition du banc MSLA.

Calcule, à partir des constantes du firmware (banc_msla.ino) et de la fiche du
HX711, le temps de lecture des cellules, la cadence réelle d'échantillonnage et
l'occupation des liaisons série (USB vers le PC, Serial2 vers la carte mère).

Purement analytique, sans dépendance. Les hypothèses sont affichées et faciles à
ajuster. Lancer : python3 budget.py
"""

# ------- Hypothèses matérielles -------
# HX711 : conversion à 10 SPS (RATE à la masse) ou 80 SPS (RATE au +3,3 V).
HX711_SPS_LENT = 10
HX711_SPS_RAPIDE = 80
# Lecture d'un échantillon (hxReadAll) : 24 bits de données + 1 impulsion de gain.
# Chaque impulsion : digitalWrite HIGH, delayMicroseconds(1), lectures DOUT, LOW, delayMicroseconds(1).
PULSES = 25
DELAY_US_PAR_IMPULSION = 2.0        # 2 × delayMicroseconds(1) explicites dans le code
# Coûts Arduino-ESP32 mesurés en ordre de grandeur (HAL, pas de registre direct ici) :
DIGITALWRITE_US = 0.9               # ~0,9 µs par digitalWrite sur ESP32 Arduino
DIGITALREAD_US = 0.6               # ~0,6 µs par digitalRead

# Liaisons série (8N1 : 10 bits par octet transmis).
USB_BAUD = 921600
SERIAL2_BAUD = 115200

def temps_lecture_us(ncells):
    """Durée d'une lecture hxReadAll pour ncells cellules (horloge SCK commune)."""
    par_impulsion = DELAY_US_PAR_IMPULSION + 2 * DIGITALWRITE_US + ncells * DIGITALREAD_US
    return PULSES * par_impulsion

def longueur_ligne_csv(ncells):
    """Longueur approximative d'une ligne CSV émise sur l'USB (octets)."""
    # t_s(7) , fz(6) [, c(6)]*4(si 5) , z(7) , uv(1) , uv_raw(4) + virgules + \n
    base = 7 + 1 + 6 + 1 + 7 + 1 + 1 + 1 + 4 + 1  # sans les coins
    if ncells == 5:
        base += 4 * (1 + 6)
    return base

def octets_par_s(baud):
    return baud / 10.0  # 8N1

print("=== Budget temps réel — chaîne d'acquisition du banc MSLA ===\n")
print("Hypothèses : HX711 %d/%d SPS ; digitalWrite≈%.1f µs, digitalRead≈%.1f µs ;"
      % (HX711_SPS_LENT, HX711_SPS_RAPIDE, DIGITALWRITE_US, DIGITALREAD_US))
print("            %d impulsions/lecture dont %.0f µs de delayMicroseconds explicites.\n" % (PULSES, DELAY_US_PAR_IMPULSION))

print("--- 1. Lecture des cellules (HX711 → ESP32) ---")
for nc in (1, 5):
    tl = temps_lecture_us(nc)
    print("  %d cellule(s) : lecture = %6.1f µs  (horloge commune : les %d convertisseurs sont lus ensemble)"
          % (nc, tl, nc))
print()

print("--- 2. Cadence d'échantillonnage ---")
for sps in (HX711_SPS_LENT, HX711_SPS_RAPIDE):
    periode_ms = 1000.0 / sps
    tl5 = temps_lecture_us(5) / 1000.0
    occ = 100.0 * tl5 / periode_ms
    print("  %2d SPS : période = %6.2f ms ; lecture (5 cell.) = %.2f ms ; le CPU lit %.1f %% du temps,"
          % (sps, periode_ms, tl5, occ))
    print("           le reste (%.2f ms) est l'attente de conversion du HX711 (temps CPU libre pour le reste)."
          % (periode_ms - tl5))
print("  => La cadence est limitée par le HX711, pas par la lecture : 80 SPS exige RATE au +3,3 V.\n")

print("--- 3. Liaison USB (CSV vers le PC, %d bauds) ---" % USB_BAUD)
bps = octets_par_s(USB_BAUD)
for nc in (1, 5):
    L = longueur_ligne_csv(nc)
    for sps in (HX711_SPS_LENT, HX711_SPS_RAPIDE):
        debit = L * sps
        occ = 100.0 * debit / bps
        t_ligne = 1000.0 * L / bps
        print("  %d cell. @ %2d SPS : ligne≈%2d o, débit=%5d o/s = %4.1f %% de l'USB ; émission d'une ligne = %.2f ms"
              % (nc, sps, L, debit, occ, t_ligne))
print("  => Large marge : l'USB à %d bauds n'est jamais le goulot d'étranglement.\n" % USB_BAUD)

print("--- 4. Liaison carte mère (Serial2, %d bauds) ---" % SERIAL2_BAUD)
bps2 = octets_par_s(SERIAL2_BAUD)
# Trames typiques : "L 30 2.00\n"(10), "U 1\n"(4), "OK 30\n"(6), "STOP OBSTACLE 30\n"(17)
trames_par_couche = 10 + 4 + 4 + 6   # L + U1 + U0 + verdict, ordre de grandeur en octets
# une couche dure typiquement quelques secondes
for duree_couche_s in (3, 8):
    debit = trames_par_couche / duree_couche_s
    occ = 100.0 * debit / bps2
    print("  ~%d o/couche, couche de %d s : débit≈%.1f o/s = %.3f %% de Serial2"
          % (trames_par_couche, duree_couche_s, debit, occ))
print("  => Négligeable : le protocole échange quelques octets par couche.\n")

pct_lecture = 100.0 * (temps_lecture_us(5) / 1000.0) / (1000.0 / HX711_SPS_RAPIDE)
print("Conclusion : à 80 SPS, 5 cellules, l'ESP32 passe ~%.0f %% du temps à lire les HX711 et" % pct_lecture)
print("~5 %% à émettre le CSV ; il reste >90 %% de temps CPU pour la détection (voir bench.cpp)."
      .replace("%%", "%"))
