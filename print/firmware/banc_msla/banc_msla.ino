/*
  Banc de mesure MSLA — firmware ESP32
  ------------------------------------
  Mesure en continu :
    - la force sur le bras Z (cellule de charge 0) et, en option, sous les 4 coins du bac (cellules 1 à 4),
      avec des HX711 lus en parallèle (horloge SCK commune) ;
    - la position Z, en comptant les impulsions STEP/DIR du driver du moteur Z ;
    - l'état de l'UV, avec un capteur UV (GUVA-S12SD ou photodiode) placé près de l'écran.
  Envoie une ligne CSV par mesure sur le port USB (921600 bauds), lisible par outils/logger.py
  et par le simulateur (section « Données réelles et recalage »).

  Format :  t_s,fz_N,c0_N,c1_N,c2_N,c3_N,z_mm,uv,uv_raw
  Convention de signe : + = traction sur le film (arrachement), - = compression (descente).

  Carte : ESP32 DevKit (ESP32-WROOM-32). Arduino IDE 2.x, paquet « esp32 » d'Espressif (2.x ou 3.x).
  Aucune bibliothèque externe.

  Contrôle du bac avant impression (sans PC) :
    « ref »       : enregistre la référence pendant le prochain test, bac PROPRE ;
    « controle »  : analyse le prochain test et donne un verdict (LED + ligne « # CONTROLE … »).
  Le test = un fichier d'impression d'une couche noire (aucun durcissement) avec descente lente
  et attente avant exposition de 5 s : voir le README, section « Contrôle du bac ».

  Commandes (terminal série, fin de ligne LF) : tapez « aide ».
*/
#include <Arduino.h>
#include <Preferences.h>
#include "soc/gpio_reg.h"


// ------------------------- Câblage (à adapter) -------------------------
const int PIN_HX_SCK = 18;                          // SCK commun à tous les HX711
const int PIN_HX_DOUT[5] = {19, 21, 22, 23, 25};    // 0 = bras Z, 1..4 = coins du bac (avant-gauche, avant-droit, arrière-droit, arrière-gauche)
const int PIN_STEP = 26;                            // STEP du driver Z (via diviseur ou optocoupleur, 3,3 V max)
const int PIN_DIR  = 27;                            // DIR du driver Z
const int PIN_UV   = 34;                            // sortie analogique du capteur UV (ADC1, entrée seule)
const int PIN_ALERT = 2;                            // LED d'alerte (LED bleue de la carte DevKit)
static_assert(PIN_DIR < 32, "PIN_DIR doit etre < 32 (lecture rapide par GPIO_IN_REG)");

// ------------------------- Réglages sauvegardés -------------------------
Preferences prefs;
struct Config {
  int   nCells      = 1;        // 1 = bras Z seul, 5 = bras Z + 4 coins
  long  offset[5]   = {0,0,0,0,0};
  float scale[5]    = {-21000,-21000,-21000,-21000,-21000};   // points HX711 par newton (valeur par défaut à étalonner)
  float stepsPerMm  = 400;      // ex. vis T8 pas 8 mm, moteur 200 pas, micro-pas 1/16 : 200*16/8 = 400
  bool  dirInvert   = false;    // inverse le sens de comptage si Z diminue quand le plateau monte
  int   uvHigh      = 900;      // seuil ADC UV allumé
  int   uvLow       = 600;      // seuil ADC UV éteint (hystérésis)
  float alertN      = 60;       // allume la LED d'alerte au-delà de cette force (valeur absolue)
  float holdS       = 5;        // durée du maintien en bas pendant le test (= attente avant exposition du fichier de test)
  float ckStatN     = 0.3;      // seuil d'appui statique (N) pendant le maintien
  float ckDescN     = 0.4;      // seuil d'écart (N) pendant la descente
  float vatW        = 200;      // dimensions du bac entre les cellules (mm), pour localiser un résidu
  float vatH        = 125;
  float zMaxN       = 140;      // force maximale du moteur Z (N)
  float layerMm     = 0.05;     // hauteur d'arrêt du test = épaisseur de la première couche (mm)
} cfg;

// ------------------------- Contrôle du bac -------------------------
const int CK_BINS = 61;                       // 0 à 3 mm au-dessus de l'arrêt, pas de 0,05 mm
struct Ref { uint32_t magic = 0; float curve[5][CK_BINS]; float stat[5]; } ref;
enum CkState { CK_OFF, CK_WAIT, CK_DESC, CK_HOLD };
CkState ck = CK_OFF;
bool ckRefMode = false;
const int BUF = 1600;                         // 20 s de descente à 80 mesures/s
float bz[BUF]; float bf[BUF][5]; int bn = 0, bhead = 0;
float vHistT[9], vHistZ[9]; int vIdx = 0, vCount = 0;
float stillSince = -1, zStop = 0, tStop = 0;
double holdAcc[5]; int holdN = 0;
int ledMode = 0; unsigned long ledT0 = 0;
const int HB = 64; float hT[HB]; float hF[HB][5]; int hHead = 0, hCount = 0;    // 0 normal, 1 clignote (bac propre), 2 fixe (résidu)

// ------------------------- État -------------------------
volatile long stepCount = 0;
volatile bool dirInv = false;
bool streaming = false;
bool uvState = false;
unsigned long t0us = 0;

// Lecture rapide de DIR dans l'interruption (PIN_DIR doit être < 32 : registre GPIO_IN_REG)
static inline bool IRAM_ATTR readDirFast() {
  return (REG_READ(GPIO_IN_REG) >> PIN_DIR) & 1;
}
void IRAM_ATTR onStepEdge() {
  bool d = readDirFast() ^ dirInv;
  if (d) stepCount = stepCount + 1; else stepCount = stepCount - 1;
}

// ------------------------- HX711 -------------------------
bool hxAllReady() {
  for (int i = 0; i < cfg.nCells; i++) if (digitalRead(PIN_HX_DOUT[i])) return false;
  return true;
}
// Lit les nCells convertisseurs en même temps (horloge commune). Gain 128, voie A.
void hxReadAll(long out[5]) {
  long v[5] = {0,0,0,0,0};
  for (int b = 0; b < 24; b++) {
    digitalWrite(PIN_HX_SCK, HIGH);
    delayMicroseconds(1);
    for (int i = 0; i < cfg.nCells; i++) v[i] = (v[i] << 1) | digitalRead(PIN_HX_DOUT[i]);
    digitalWrite(PIN_HX_SCK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_HX_SCK, HIGH); delayMicroseconds(1);
  digitalWrite(PIN_HX_SCK, LOW);  delayMicroseconds(1);
  for (int i = 0; i < cfg.nCells; i++) {
    if (v[i] & 0x800000) v[i] |= 0xFF000000;   // extension du signe sur 32 bits
    out[i] = v[i];
  }
}
bool hxWaitRead(long out[5], unsigned long timeoutMs = 500) {
  unsigned long t = millis();
  while (!hxAllReady()) { if (millis() - t > timeoutMs) return false; delayMicroseconds(100); }
  hxReadAll(out);
  return true;
}
bool hxAverage(long avg[5], int n) {
  double acc[5] = {0,0,0,0,0};
  long r[5];
  for (int k = 0; k < n; k++) {
    if (!hxWaitRead(r)) return false;
    for (int i = 0; i < cfg.nCells; i++) acc[i] += r[i];
  }
  for (int i = 0; i < cfg.nCells; i++) avg[i] = (long)(acc[i] / n);
  return true;
}

// ------------------------- Sauvegarde -------------------------
void saveCfg() {
  prefs.begin("banc", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
}
void loadCfg() {
  prefs.begin("banc", true);
  if (prefs.getBytesLength("cfg") == sizeof(cfg)) prefs.getBytes("cfg", &cfg, sizeof(cfg));
  if (prefs.getBytesLength("ref") == sizeof(ref)) prefs.getBytes("ref", &ref, sizeof(ref));
  prefs.end();
  dirInv = cfg.dirInvert;
}
void saveRef() {
  prefs.begin("banc", false);
  prefs.putBytes("ref", &ref, sizeof(ref));
  prefs.end();
}

// ------------------------- Contrôle du bac : analyse -------------------------
void ckArm(bool refMode) {
  ckRefMode = refMode; ck = CK_WAIT; bn = 0; bhead = 0; stillSince = -1; holdN = 0;
  for (int i = 0; i < 5; i++) holdAcc[i] = 0;
  ledMode = 0;
  Serial.println(refMode ? "# REFERENCE : en attente du test (bac propre)" : "# CONTROLE : en attente du test");
}
// échantillon n (0 = le plus ancien) du tampon circulaire
static inline int bidx(int n) { return (bhead - bn + n + BUF) % BUF; }

void ckFinish() {
  int nch = cfg.nCells;
  if (holdN < 10) { Serial.println("# CONTROLE : maintien en bas trop court (augmente l'attente avant exposition du fichier de test)"); ck = CK_OFF; return; }
  // tare dans la résine : moyenne autour de zStop + 3 mm, pendant la descente
  float tare[5] = {0,0,0,0,0}; int tn = 0;
  for (int n = 0; n < bn; n++) { int k = bidx(n); float d = bz[k] - zStop;
    if (d <= 3.0f && d >= 2.75f) { for (int c = 0; c < nch; c++) tare[c] += bf[k][c]; tn++; } }
  if (tn == 0) { for (int n = 0; n < min(bn, 5); n++) { int k = bidx(n); for (int c = 0; c < nch; c++) tare[c] += bf[k][c]; tn++; } }
  for (int c = 0; c < nch; c++) tare[c] /= max(tn, 1);
  // courbe de descente par tranches de 0,05 mm
  float curve[5][CK_BINS]; int cnt[CK_BINS];
  for (int b = 0; b < CK_BINS; b++) { cnt[b] = 0; for (int c = 0; c < 5; c++) curve[c][b] = 0; }
  for (int n = 0; n < bn; n++) { int k = bidx(n); float d = bz[k] - zStop; int b = (int)lroundf(d / 0.05f);
    if (b >= 0 && b < CK_BINS) { for (int c = 0; c < nch; c++) curve[c][b] += bf[k][c] - tare[c]; cnt[b]++; } }
  for (int b = 0; b < CK_BINS; b++) for (int c = 0; c < nch; c++) curve[c][b] = cnt[b] ? curve[c][b] / cnt[b] : NAN;
  float stat[5]; for (int c = 0; c < nch; c++) stat[c] = (float)(holdAcc[c] / holdN) - tare[c];

  if (ckRefMode) {
    ref.magic = 0xB4C0;
    for (int c = 0; c < 5; c++) { ref.stat[c] = c < nch ? stat[c] : 0; for (int b = 0; b < CK_BINS; b++) ref.curve[c][b] = c < nch ? curve[c][b] : NAN; }
    saveRef();
    Serial.printf("# REFERENCE enregistree : appui de reference %.3f N\n", stat[0]);
    ck = CK_OFF; return;
  }
  bool hasRef = ref.magic == 0xB4C0;
  // contact pendant la descente : 2 tranches consécutives sous le seuil, en partant du haut
  float zContact = -1; int run = 0;
  for (int b = CK_BINS - 1; b >= 1; b--) {
    float r = hasRef && !isnan(ref.curve[0][b]) ? ref.curve[0][b] : 0;
    if (!isnan(curve[0][b]) && curve[0][b] - r < -cfg.ckDescN) { if (++run >= 2) { zContact = (b + 1) * 0.05f; break; } } else run = 0;
  }
  float appui = -(stat[0] - (hasRef ? ref.stat[0] : 0));
  bool seen = appui > cfg.ckStatN || zContact >= 0;
  const char* risque = !seen ? "aucun" : appui >= 0.9f * cfg.zMaxN ? "DANGER" : appui > 3 ? "eleve" : "modere";
  Serial.printf("# CONTROLE %s : appui %.2f N", seen ? "RESIDU" : "OK", appui);
  if (zContact >= 0) Serial.printf(", contact a %.2f mm au-dessus de l'arret (epaisseur estimee %.2f mm)", zContact, zContact + cfg.layerMm);
  if (nch == 5) {
    float dc[4], S = 0;
    for (int c = 0; c < 4; c++) { dc[c] = -(stat[c + 1] - (hasRef ? ref.stat[c + 1] : 0)); S += dc[c]; }
    if (S > 1.0f) Serial.printf(", position estimee x=%.0f y=%.0f mm", cfg.vatW * (dc[1] + dc[2]) / S, cfg.vatH * (dc[2] + dc[3]) / S);
  }
  Serial.printf(", risque %s%s\n", risque, hasRef ? "" : " (pas de reference : lance « ref » sur bac propre)");
  ledMode = seen ? 2 : 1; ledT0 = millis();
  ck = CK_OFF;
}

void ckUpdate(float t, float z, const float f[5], bool uv) {
  if (ck == CK_OFF) return;
  vHistT[vIdx] = t; vHistZ[vIdx] = z; vIdx = (vIdx + 1) % 9; if (vCount < 9) vCount++;
  if (vCount < 9) return;
  int old = vIdx;   // le plus ancien des 9
  float dt = t - vHistT[old]; float v = dt > 0 ? (z - vHistZ[old]) / dt : 0;   // mm/s
  if (ck == CK_WAIT) { if (v < -0.05f) { ck = CK_DESC; bn = 0; bhead = 0; stillSince = -1; } return; }
  if (ck == CK_DESC) {
    bz[bhead] = z; for (int c = 0; c < 5; c++) bf[bhead][c] = c < cfg.nCells ? f[c] : 0;
    bhead = (bhead + 1) % BUF; if (bn < BUF) bn++;
    if (v > 0.05f) { ck = CK_WAIT; return; }
    if (fabsf(v) < 0.02f && !uv) { if (stillSince < 0) stillSince = t; if (t - stillSince > 0.3f) { ck = CK_HOLD; zStop = z; tStop = stillSince; hHead = 0; hCount = 0; } }
    else stillSince = -1;
    return;
  }
  if (ck == CK_HOLD) {
    float h = t - tStop;
    bool ended = uv || fabsf(v) > 0.05f || h > cfg.holdS;
    if (!ended && h >= 1.0f) {                 // fenêtre glissante des 0,6 dernières secondes
      hT[hHead] = t; for (int c = 0; c < 5; c++) hF[hHead][c] = c < cfg.nCells ? f[c] : 0;
      hHead = (hHead + 1) % HB; if (hCount < HB) hCount++;
    }
    if (ended) {
      float tEnd = hCount ? hT[(hHead - 1 + HB) % HB] : t;
      holdN = 0; for (int c = 0; c < 5; c++) holdAcc[c] = 0;
      for (int n = 0; n < hCount; n++) { int k = (hHead - 1 - n + HB) % HB; if (hT[k] < tEnd - 0.6f) break;
        for (int c = 0; c < cfg.nCells; c++) holdAcc[c] += hF[k][c]; holdN++; }
      if (h < cfg.holdS - 0.2f && holdN >= 10) Serial.printf("# CONTROLE : maintien reel %.1f s (plus court que %.1f s), analyse sur la fin disponible\n", h, cfg.holdS);
      ckFinish();
    }
  }
}

// ------------------------- Commandes -------------------------
void printHeader() {
  Serial.print("t_s,fz_N");
  if (cfg.nCells == 5) Serial.print(",c0_N,c1_N,c2_N,c3_N");
  Serial.println(",z_mm,uv,uv_raw");
}
void printInfo() {
  Serial.printf("# cellules=%d pas/mm=%.2f inversion_dir=%d uv_haut=%d uv_bas=%d alerte=%.1f N\n",
                cfg.nCells, cfg.stepsPerMm, cfg.dirInvert, cfg.uvHigh, cfg.uvLow, cfg.alertN);
  for (int i = 0; i < cfg.nCells; i++)
    Serial.printf("# cellule %d : zero=%ld echelle=%.2f points/N\n", i, cfg.offset[i], cfg.scale[i]);
  Serial.printf("# z=%.3f mm (pas=%ld) uv_brut=%d\n", stepCount / cfg.stepsPerMm, (long)stepCount, analogRead(PIN_UV));
  Serial.printf("# controle : maintien=%.1f s seuils=%.2f/%.2f N bac=%.0fx%.0f mm fmax=%.0f N reference=%s\n", cfg.holdS, cfg.ckStatN, cfg.ckDescN, cfg.vatW, cfg.vatH, cfg.zMaxN, ref.magic == 0xB4C0 ? "oui" : "non");
}
void printHelp() {
  Serial.println(F("# Commandes :"));
  Serial.println(F("#  start / stop         demarre ou arrete l'envoi des mesures"));
  Serial.println(F("#  tare                 met toutes les cellules a zero (rien ne doit appuyer)"));
  Serial.println(F("#  cal <i> <grammes>    etalonne la cellule i : masse suspendue au plateau (i=0) ou posee sur le coin (i=1..4)"));
  Serial.println(F("#  sign <i>             inverse le signe de la cellule i"));
  Serial.println(F("#  cells <1|5>          bras Z seul, ou bras Z + 4 coins du bac"));
  Serial.println(F("#  stepsmm <valeur>     pas moteur par mm de l'axe Z"));
  Serial.println(F("#  dir                  inverse le sens de comptage de Z"));
  Serial.println(F("#  zero                 Z = 0 a la position actuelle"));
  Serial.println(F("#  uv <haut> <bas>      seuils ADC du capteur UV"));
  Serial.println(F("#  alerte <newtons>     seuil de la LED d'alerte"));
  Serial.println(F("#  raw                  affiche 10 lectures brutes (diagnostic)"));
  Serial.println(F("#  ref                  enregistre la reference pendant le prochain test (bac propre)"));
  Serial.println(F("#  controle             analyse le prochain test du bac et donne un verdict"));
  Serial.println(F("#  ack                  eteint la LED de residu"));
  Serial.println(F("#  maintien <s>         duree d'attente en bas du fichier de test (defaut 5)"));
  Serial.println(F("#  seuils <appui> <descente>   seuils du controle en N (defaut 0.3 0.4)"));
  Serial.println(F("#  bac <largeur> <profondeur>  distance entre les cellules du bac en mm"));
  Serial.println(F("#  fmax <N>             force maximale du moteur Z"));
  Serial.println(F("#  couche <mm>          epaisseur de la premiere couche (hauteur d'arret du test)"));
  Serial.println(F("#  info / aide"));
}
void handleCommand(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  cmd.toLowerCase();

  if (cmd == "start") { t0us = micros(); streaming = true; printHeader(); }
  else if (cmd == "stop") { streaming = false; Serial.println("# arret"); }
  else if (cmd == "tare") {
    bool was = streaming; streaming = false;
    long avg[5];
    if (hxAverage(avg, 40)) { for (int i = 0; i < cfg.nCells; i++) cfg.offset[i] = avg[i]; saveCfg(); Serial.println("# tare faite"); }
    else Serial.println("# ERREUR : un HX711 ne repond pas (cablage DOUT/SCK ?)");
    streaming = was;
  }
  else if (cmd == "cal") {
    int i = arg.toInt(); float g = arg.substring(arg.indexOf(' ') + 1).toFloat();
    if (i < 0 || i >= cfg.nCells || g <= 0) { Serial.println("# usage : cal <cellule> <grammes>"); return; }
    bool was = streaming; streaming = false;
    long avg[5];
    if (hxAverage(avg, 40)) {
      float newtons = g * 0.00980665f;
      // Bras Z (i = 0) : masse SUSPENDUE au plateau = tire vers le bac comme l'arrachement -> force positive.
      // Coins du bac (i = 1..4) : masse POSEE sur le bac = appui comme la compression -> force negative.
      float sgn = (i == 0) ? 1.0f : -1.0f;
      cfg.scale[i] = sgn * (float)(avg[i] - cfg.offset[i]) / newtons;
      saveCfg();
      Serial.printf("# cellule %d etalonnee : %.2f points/N\n", i, cfg.scale[i]);
    } else Serial.println("# ERREUR : lecture impossible");
    streaming = was;
  }
  else if (cmd == "sign") { int i = arg.toInt(); if (i >= 0 && i < 5) { cfg.scale[i] = -cfg.scale[i]; saveCfg(); Serial.println("# signe inverse"); } }
  else if (cmd == "cells") { int n = arg.toInt(); if (n == 1 || n == 5) { cfg.nCells = n; saveCfg(); Serial.println("# ok, refaire tare"); } }
  else if (cmd == "stepsmm") { float v = arg.toFloat(); if (v > 0) { cfg.stepsPerMm = v; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "dir") { cfg.dirInvert = !cfg.dirInvert; dirInv = cfg.dirInvert; saveCfg(); Serial.println("# sens Z inverse"); }
  else if (cmd == "zero") { noInterrupts(); stepCount = 0; interrupts(); Serial.println("# z = 0"); }
  else if (cmd == "uv") { int a = arg.toInt(); int b = arg.substring(arg.indexOf(' ') + 1).toInt(); if (a > b && b > 0) { cfg.uvHigh = a; cfg.uvLow = b; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "alerte") { float v = arg.toFloat(); if (v > 0) { cfg.alertN = v; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "raw") {
    bool was = streaming; streaming = false; long r[5];
    for (int k = 0; k < 10; k++) { if (!hxWaitRead(r)) { Serial.println("# pas de reponse"); break; }
      Serial.print("# "); for (int i = 0; i < cfg.nCells; i++) { Serial.print(r[i]); Serial.print(i + 1 < cfg.nCells ? " " : "\n"); } }
    streaming = was;
  }
  else if (cmd == "ref") ckArm(true);
  else if (cmd == "controle") ckArm(false);
  else if (cmd == "ack") { ledMode = 0; Serial.println("# ok"); }
  else if (cmd == "maintien") { float v = arg.toFloat(); if (v >= 1) { cfg.holdS = v; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "seuils") { float a = arg.toFloat(), b = arg.substring(arg.indexOf(' ') + 1).toFloat(); if (a > 0 && b > 0) { cfg.ckStatN = a; cfg.ckDescN = b; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "bac") { float a = arg.toFloat(), b = arg.substring(arg.indexOf(' ') + 1).toFloat(); if (a > 0 && b > 0) { cfg.vatW = a; cfg.vatH = b; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "couche") { float v = arg.toFloat(); if (v > 0) { cfg.layerMm = v; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "fmax") { float v = arg.toFloat(); if (v > 0) { cfg.zMaxN = v; saveCfg(); Serial.println("# ok"); } }
  else if (cmd == "info") printInfo();
  else printHelp();
}

// ------------------------- Programme -------------------------
void setup() {
  Serial.setRxBufferSize(512);
  Serial.begin(921600);
  pinMode(PIN_HX_SCK, OUTPUT); digitalWrite(PIN_HX_SCK, LOW);
  for (int i = 0; i < 5; i++) pinMode(PIN_HX_DOUT[i], INPUT_PULLUP);
  pinMode(PIN_STEP, INPUT); pinMode(PIN_DIR, INPUT);
  pinMode(PIN_ALERT, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_UV, ADC_11db);
  loadCfg();
  attachInterrupt(digitalPinToInterrupt(PIN_STEP), onStepEdge, RISING);
  delay(300);
  Serial.println("# Banc de mesure MSLA pret. Tapez « aide ».");
  printInfo();
}

String rx;
void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (rx.length()) handleCommand(rx); rx = ""; }
    else if (rx.length() < 80) rx += c;
  }
  // LED : clignotement « bac propre » 3 fois, fixe si résidu
  if (ledMode == 1) { unsigned long e = millis() - ledT0; if (e > 1500) ledMode = 0; else digitalWrite(PIN_ALERT, (e / 250) % 2 == 0); }
  else if (ledMode == 2) digitalWrite(PIN_ALERT, HIGH);
  if (!streaming && ck == CK_OFF) return;
  if (!hxAllReady()) return;

  long raw[5];
  hxReadAll(raw);
  float t = (micros() - t0us) * 1e-6f;   // t0us = 0 si « start » n'a pas été envoyé : temps depuis le démarrage
  float f[5];
  for (int i = 0; i < cfg.nCells; i++) f[i] = (float)(raw[i] - cfg.offset[i]) / cfg.scale[i];

  noInterrupts(); long st = stepCount; interrupts();
  float z = st / cfg.stepsPerMm;

  int uvRaw = analogRead(PIN_UV);
  if (!uvState && uvRaw > cfg.uvHigh) uvState = true;
  else if (uvState && uvRaw < cfg.uvLow) uvState = false;

  float fmax = fabsf(f[0]);
  if (cfg.nCells == 5) { float s = f[1] + f[2] + f[3] + f[4]; fmax = fmaxf(fmax, fabsf(s)); }
  if (ledMode == 0) digitalWrite(PIN_ALERT, fmax > cfg.alertN ? HIGH : LOW);
  ckUpdate(t, z, f, uvState);
  if (!streaming) return;

  char buf[160];
  int n = snprintf(buf, sizeof(buf), "%.4f,%.3f", t, f[0]);
  if (cfg.nCells == 5) n += snprintf(buf + n, sizeof(buf) - n, ",%.3f,%.3f,%.3f,%.3f", f[1], f[2], f[3], f[4]);
  snprintf(buf + n, sizeof(buf) - n, ",%.4f,%d,%d", z, uvState ? 1 : 0, uvRaw);
  Serial.println(buf);
}
