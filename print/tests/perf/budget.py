#!/usr/bin/env python3
"""Real-time budget of the MSLA bench acquisition chain.

Computes, from the firmware constants (banc_msla.ino) and the HX711 data sheet, the
cell read time, the real sample rate, and the load of the serial links (USB to the PC,
Serial2 to the motherboard).

Purely analytical, no dependency. The assumptions are printed and easy to adjust.
Run: python3 budget.py
"""

# ------- Hardware assumptions -------
# HX711: conversion at 10 SPS (RATE to ground) or 80 SPS (RATE to +3.3 V).
HX711_SPS_LENT = 10
HX711_SPS_RAPIDE = 80
# Read of one sample (hxReadAll): 24 data bits + 1 gain pulse.
# Each pulse: digitalWrite HIGH, delayMicroseconds(1), DOUT reads, LOW, delayMicroseconds(1).
PULSES = 25
DELAY_US_PAR_IMPULSION = 2.0        # 2 x delayMicroseconds(1) explicit in the code
# Arduino-ESP32 order-of-magnitude costs (HAL, no direct register here):
DIGITALWRITE_US = 0.9               # ~0.9 us per digitalWrite on ESP32 Arduino
DIGITALREAD_US = 0.6               # ~0.6 us per digitalRead

# Serial links (8N1: 10 bits per transmitted byte).
USB_BAUD = 921600
SERIAL2_BAUD = 115200

def temps_lecture_us(ncells):
    """Duration of one hxReadAll read for ncells cells (common SCK clock)."""
    par_impulsion = DELAY_US_PAR_IMPULSION + 2 * DIGITALWRITE_US + ncells * DIGITALREAD_US
    return PULSES * par_impulsion

def longueur_ligne_csv(ncells):
    """Approximate length of a CSV line sent on the USB (bytes)."""
    # t_s(7) , fz(6) [, c(6)]*4(if 5) , z(7) , uv(1) , uv_raw(4) + commas + \n
    base = 7 + 1 + 6 + 1 + 7 + 1 + 1 + 1 + 4 + 1  # without the corners
    if ncells == 5:
        base += 4 * (1 + 6)
    return base

def octets_par_s(baud):
    return baud / 10.0  # 8N1

print("=== Real-time budget - MSLA bench acquisition chain ===\n")
print("Assumptions: HX711 %d/%d SPS ; digitalWrite~%.1f us, digitalRead~%.1f us ;"
      % (HX711_SPS_LENT, HX711_SPS_RAPIDE, DIGITALWRITE_US, DIGITALREAD_US))
print("            %d pulses/read of which %.0f us of explicit delayMicroseconds.\n" % (PULSES, DELAY_US_PAR_IMPULSION))

print("--- 1. Cell read (HX711 -> ESP32) ---")
for nc in (1, 5):
    tl = temps_lecture_us(nc)
    print("  %d cell(s) : read = %6.1f us  (common clock: the %d converters are read together)"
          % (nc, tl, nc))
print()

print("--- 2. Sample rate ---")
for sps in (HX711_SPS_LENT, HX711_SPS_RAPIDE):
    periode_ms = 1000.0 / sps
    tl5 = temps_lecture_us(5) / 1000.0
    occ = 100.0 * tl5 / periode_ms
    print("  %2d SPS : period = %6.2f ms ; read (5 cells) = %.2f ms ; the CPU reads %.1f %% of the time,"
          % (sps, periode_ms, tl5, occ))
    print("           the rest (%.2f ms) is the HX711 conversion wait (CPU time free for the rest)."
          % (periode_ms - tl5))
print("  => The rate is limited by the HX711, not by the read: 80 SPS needs RATE at +3.3 V.\n")

print("--- 3. USB link (CSV to the PC, %d baud) ---" % USB_BAUD)
bps = octets_par_s(USB_BAUD)
for nc in (1, 5):
    L = longueur_ligne_csv(nc)
    for sps in (HX711_SPS_LENT, HX711_SPS_RAPIDE):
        debit = L * sps
        occ = 100.0 * debit / bps
        t_ligne = 1000.0 * L / bps
        print("  %d cells @ %2d SPS : line~%2d B, rate=%5d B/s = %4.1f %% of the USB ; sending one line = %.2f ms"
              % (nc, sps, L, debit, occ, t_ligne))
print("  => Large margin: the USB at %d baud is never the bottleneck.\n" % USB_BAUD)

print("--- 4. Motherboard link (Serial2, %d baud) ---" % SERIAL2_BAUD)
bps2 = octets_par_s(SERIAL2_BAUD)
# Typical frames: "L 30 2.00\n"(10), "U 1\n"(4), "OK 30\n"(6), "STOP OBSTACLE 30\n"(17)
trames_par_couche = 10 + 4 + 4 + 6   # L + U1 + U0 + verdict, order of magnitude in bytes
# a layer typically lasts a few seconds
for duree_couche_s in (3, 8):
    debit = trames_par_couche / duree_couche_s
    occ = 100.0 * debit / bps2
    print("  ~%d B/layer, layer of %d s : rate~%.1f B/s = %.3f %% of Serial2"
          % (trames_par_couche, duree_couche_s, debit, occ))
print("  => Negligible: the protocol exchanges a few bytes per layer.\n")

pct_lecture = 100.0 * (temps_lecture_us(5) / 1000.0) / (1000.0 / HX711_SPS_RAPIDE)
print("Conclusion: at 80 SPS, 5 cells, the ESP32 spends ~%.0f %% of the time reading the HX711 and" % pct_lecture)
print("~5 %% sending the CSV; more than 90 %% of CPU time is left for the detection (see bench.cpp)."
      .replace("%%", "%"))
