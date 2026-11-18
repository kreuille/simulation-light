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
// Logique de détection partagée avec le simulateur et les tests (dossier src/ compilé
// automatiquement par l'IDE Arduino ; lien vers ../../lib/msla_detect).
#include "src/msla_detect/msla_detect.h"


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

// ------------------------- Contrôle du bac (bibliothèque msla_detect) -------------------------
msla::BacRef ref{};                           // référence enregistrée (bac propre), persistée en Preferences
msla::BacControleur bac;                      // contrôleur du bac en flux (état + tampons)
int ledMode = 0; unsigned long ledT0 = 0;     // 0 normal, 1 clignote (bac propre), 2 fixe (résidu)
// pesée : zéro sans bac, poids et répartition du bac vide de référence
const float GN = 0.00980665f;                  // newtons par gramme
struct Weigh { uint32_t magic = 0; long emptyOff[5] = {0,0,0,0,0}; float vatG = 0; float share[4] = {0.25f,0.25f,0.25f,0.25f}; float density = 1.1f; } wg;

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
  if (prefs.getBytesLength("pesee") == sizeof(wg)) prefs.getBytes("pesee", &wg, sizeof(wg));
  prefs.end();
  dirInv = cfg.dirInvert;
}
void saveRef() {
  prefs.begin("banc", false);
  prefs.putBytes("ref", &ref, sizeof(ref));
  prefs.end();
}

// ------------------------- Pesée du bac et de la résine (4 cellules) -------------------------
void saveWg() { prefs.begin("banc", false); prefs.putBytes("pesee", &wg, sizeof(wg)); prefs.end(); }
// poids sur chaque coin, en grammes, depuis le zéro sans bac
bool cornerGrams(float g[4], int n) {
  long avg[5];
  if (!hxAverage(avg, n)) return false;
  for (int c = 0; c < 4; c++) g[c] = -(float)(avg[c + 1] - wg.emptyOff[c + 1]) / cfg.scale[c + 1] / GN;
  return true;
}
void cmdPesee() {
  if (cfg.nCells != 5 || wg.magic != 0xBAC0) { Serial.println("# PESEE : il faut les 4 cellules (cells 5) et un zero sans bac (tarevide)"); return; }
  float g[4]; if (!cornerGrams(g, 160)) { Serial.println("# PESEE : lecture impossible"); return; }
  msla::PeseeConfig pc{cfg.vatW, cfg.vatH};
  msla::PeseeRef pr{wg.vatG, {wg.share[0], wg.share[1], wg.share[2], wg.share[3]}, wg.density};
  msla::PeseeResultat r = msla::analysePesee(g, pc, pr);
  Serial.printf("# PESEE : coins %.0f / %.0f / %.0f / %.0f g (avant G, avant D, arriere D, arriere G), total %.0f g\n", g[0], g[1], g[2], g[3], r.total);
  if (!r.present) { Serial.println("# PESEE : pas de bac"); return; }
  const char* nom[4] = {"avant gauche", "avant droit", "arriere droit", "arriere gauche"};
  if (r.malPose) Serial.printf("# PESEE : BAC MAL POSE, le coin %s ne porte que %.0f %% du poids\n", nom[r.coinFaible], r.sh[r.coinFaible] * 100);
  else if (r.decale) Serial.printf("# PESEE : bac decale d'environ %.0f mm (centre x=%.0f y=%.0f mm)\n", r.off, r.cx, r.cy);
  else Serial.printf("# PESEE : bac bien pose (ecart maximal %.1f %%)\n", r.dev * 100);
  if (r.resineConnue) Serial.printf("# PESEE : resine estimee %.0f ml\n", r.resineMl);
  else Serial.println("# PESEE : lance « refbac » avec le bac VIDE bien pose pour connaitre le poids du bac");
  ledMode = r.malPose ? 2 : 1; ledT0 = millis();
}
void cmdFuite(float minutes) {
  if (cfg.nCells != 5 || wg.magic != 0xBAC0) { Serial.println("# FUITE : il faut les 4 cellules (cells 5) et un zero sans bac (tarevide)"); return; }
  float g[4]; if (!cornerGrams(g, 160)) return;
  float m0 = g[0] + g[1] + g[2] + g[3]; unsigned long t0 = millis(), last = t0;
  Serial.printf("# FUITE : mesure pendant %.0f min, ne touche pas l'imprimante\n", minutes);
  long r[5];
  while (millis() - t0 < (unsigned long)(minutes * 60000)) {
    hxWaitRead(r);                                // garde les HX711 actifs
    if (millis() - last > 60000) { last = millis(); Serial.printf("# FUITE : %.0f min ecoulees\n", (last - t0) / 60000.0f); }
  }
  if (!cornerGrams(g, 160)) return;
  float m1 = g[0] + g[1] + g[2] + g[3];
  msla::FuiteResultat fr = msla::analyseFuite(m0, m1, wg.density, minutes);
  Serial.printf("# FUITE : %.1f g perdus en %.0f min, soit %.1f ml/h%s\n", fr.lost, minutes, fr.rate, fr.fuiteProbable ? " : FUITE PROBABLE" : " : pas de fuite mesurable");
}

// ------------------------- Contrôle du bac : liaison avec la bibliothèque -------------------------
// Construit la configuration de la bibliothèque à partir de la configuration du firmware.
static msla::BacConfig bacConfig() {
  return { cfg.nCells, cfg.holdS, cfg.ckStatN, cfg.ckDescN, cfg.vatW, cfg.vatH, cfg.zMaxN, cfg.layerMm };
}
bool ckEnCours() { return bac.actif(); }       // utilisé par loop() et le test hôte

void ckArm(int mode) {
  ledMode = 0;
  if (bac.armer(mode, bacConfig(), &ref) == msla::BAC_ARM_ERR_PLANEITE_CELLS) {
    Serial.println("# PLANEITE : il faut les 4 cellules du bac (cells 5)"); return;
  }
  Serial.println(mode == 1 ? "# REFERENCE : en attente du test (bac propre)"
                 : mode == 2 ? "# PLANEITE : en attente de la descente lente du plateau vide"
                 : "# CONTROLE : en attente du test");
}

static const char* risqueTexte(msla::BacRisque r) {
  switch (r) {
    case msla::RISQUE_DANGER: return "DANGER";
    case msla::RISQUE_ELEVE:  return "eleve";
    case msla::RISQUE_MODERE: return "modere";
    default:                  return "aucun";
  }
}
static void afficheControle(const msla::ControleResultat& r) {
  if (r.erreur) { Serial.println("# CONTROLE : maintien en bas trop court (augmente l'attente avant exposition du fichier de test)"); return; }
  Serial.printf("# CONTROLE %s : appui %.2f N", r.residu ? "RESIDU" : "OK", r.appui);
  if (r.contact) Serial.printf(", contact a %.2f mm au-dessus de l'arret (epaisseur estimee %.2f mm)", r.zContact, r.epaisseur);
  if (cfg.nCells == 5 && r.position) Serial.printf(", position estimee x=%.0f y=%.0f mm", r.px, r.py);
  Serial.printf(", risque %s%s\n", risqueTexte(r.risque), r.hasRef ? "" : " (pas de reference : lance « ref » sur bac propre)");
  ledMode = r.residu ? 2 : 1; ledT0 = millis();
}
// Le côté vers lequel le centre d'effort se décale est le côté le plus bas du plateau.
static void affichePlaneite(const msla::PlaneiteResultat& r) {
  if (!r.atteint5N) { Serial.println("# PLANEITE : la somme des 4 cellules n'a jamais atteint 5 N (descente trop rapide ou arret trop haut)"); return; }
  const char* fb = r.dy < -4 ? "avant" : r.dy > 4 ? "arriere" : "";
  const char* lr = r.dx < -4 ? "gauche" : r.dx > 4 ? "droit" : "";
  Serial.printf("# PLANEITE : decalage du centre d'effort dx=%.1f mm dy=%.1f mm a 5 N, a %.2f mm de l'arret", r.dx, r.dy, r.zA5N);
  if (!fb[0] && !lr[0]) Serial.println(" : plateau parallele a l'ecran");
  else Serial.printf(" : cote le plus bas %s%s%s (convertir en angle dans le simulateur)\n", fb, (fb[0] && lr[0]) ? " " : "", lr);
}

void ckUpdate(float t, float z, const float f[5], bool uv) {
  msla::BacEvent ev = bac.maj(t, z, f, uv);
  if (ev == msla::BAC_RAS) return;
  if (bac.maintienCourt())
    Serial.printf("# CONTROLE : maintien reel %.1f s (plus court que %.1f s), analyse sur la fin disponible\n", bac.maintienReel(), cfg.holdS);
  switch (ev) {
    case msla::BAC_EV_CONTROLE: afficheControle(bac.controle()); break;
    case msla::BAC_EV_PLANEITE: affichePlaneite(bac.planeite()); break;
    case msla::BAC_EV_REFERENCE:
      saveRef();
      Serial.printf("# REFERENCE enregistree : appui de reference %.3f N\n", bac.reference().appuiRef);
      break;
    default: break;
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
  Serial.printf("# controle : maintien=%.1f s seuils=%.2f/%.2f N bac=%.0fx%.0f mm fmax=%.0f N reference=%s\n", cfg.holdS, cfg.ckStatN, cfg.ckDescN, cfg.vatW, cfg.vatH, cfg.zMaxN, ref.magic == msla::MSLA_REF_MAGIC ? "oui" : "non");
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
  Serial.println(F("#  planeite             analyse la prochaine descente lente du plateau vide (cells 5)"));
  Serial.println(F("#  tarevide             zero des 4 cellules SANS bac (une fois)"));
  Serial.println(F("#  refbac               poids et repartition du bac VIDE bien pose"));
  Serial.println(F("#  pesee                presence, positionnement du bac, quantite de resine"));
  Serial.println(F("#  fuite <minutes>      mesure une fuite a l'arret (30 min par defaut)"));
  Serial.println(F("#  densite <g/ml>       densite de la resine (1.1 par defaut)"));
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
  else if (cmd == "ref") ckArm(1);
  else if (cmd == "controle") ckArm(0);
  else if (cmd == "planeite") ckArm(2);
  else if (cmd == "tarevide") {
    long avg[5];
    if (cfg.nCells == 5 && hxAverage(avg, 80)) { for (int c = 0; c < 5; c++) wg.emptyOff[c] = avg[c]; wg.magic = 0xBAC0; saveWg(); Serial.println("# zero sans bac enregistre"); }
    else Serial.println("# ERREUR : il faut les 4 cellules (cells 5), sans bac");
  }
  else if (cmd == "refbac") {
    float g[4];
    if (cfg.nCells == 5 && wg.magic == 0xBAC0 && cornerGrams(g, 160)) {
      float S = g[0] + g[1] + g[2] + g[3]; wg.vatG = S; for (int c = 0; c < 4; c++) wg.share[c] = g[c] / S; saveWg();
      Serial.printf("# bac vide de reference : %.0f g (%.0f / %.0f / %.0f / %.0f %%)\n", S, wg.share[0] * 100, wg.share[1] * 100, wg.share[2] * 100, wg.share[3] * 100);
    } else Serial.println("# ERREUR : fais d'abord « tarevide », puis pose le bac VIDE et relance « refbac »");
  }
  else if (cmd == "pesee") cmdPesee();
  else if (cmd == "fuite") { float m = arg.toFloat(); cmdFuite(m > 0 ? m : 30); }
  else if (cmd == "densite") { float v = arg.toFloat(); if (v > 0.5f && v < 3) { wg.density = v; saveWg(); Serial.println("# ok"); } }
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
  if (!streaming && !ckEnCours()) return;
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
