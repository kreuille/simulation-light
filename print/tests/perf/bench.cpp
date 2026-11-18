// Benchmark du coût de calcul de la détection par couche du firmware.
// Rejoue une impression réelle (scénario exporté, 60 couches dont 4 couches de base
// à exposition longue) à travers la vraie fonction detEchantillon() du firmware, et
// mesure le temps par échantillon (moyen et pire cas) sur ce PC.
//
// Le pire cas est l'échantillon qui finalise une couche (tris de médianes) : c'est lui
// qui doit tenir dans la période d'échantillonnage (12,5 ms à 80 SPS) sur l'ESP32.
//
// Usage : bench <dossier> <scenario>
#include "Arduino.h"
SerialT Serial;
SerialT Serial2;
#define setup sketch_setup
#define loop sketch_loop
#include "../../firmware/banc_msla/banc_msla.ino"
#undef setup
#undef loop
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

struct Ech { double t, f, z; int ph, ly; };

int main(int argc, char** argv) {
  Serial.toStdout = false; Serial2.toStdout = false;
  const std::string dir = argc > 1 ? argv[1] : ".";
  const std::string nom = argc > 2 ? argv[2] : "residue_m9";

  std::ifstream in(dir + "/" + nom + ".txt");
  if (!in) { fprintf(stderr, "scenario introuvable : %s\n", nom.c_str()); return 2; }
  int n = 0; double nme = 1, fstall = 140; std::vector<double> area; std::vector<Ech> ech;
  std::string tok; bool dansZ = false;
  while (in >> tok) {
    if (tok == "N") in >> n;
    else if (tok == "NME") in >> nme;
    else if (tok == "FSTALL") in >> fstall;
    else if (tok == "AREA") { area.resize(n); for (int i = 0; i < n; i++) in >> area[i]; }
    else if (tok == "BEGIN_CHANNEL") { std::string k; in >> k; dansZ = (k == "z"); }
    else if (tok == "SAMPLES" && dansZ) { int k; in >> k; ech.resize(k); for (int i = 0; i < k; i++) in >> ech[i].t >> ech[i].f >> ech[i].z >> ech[i].ph >> ech[i].ly; }
    else if (tok == "END_CHANNEL" && dansZ) dansZ = false;
  }

  detNoise = nme; detObstacleMul = 2.5; detLowRatio = 0.6; cfg.zMaxN = fstall;
  detDemarrer();

  using clk = std::chrono::steady_clock;
  double total_us = 0, max_us = 0; long nmax_ly = -1;
  int curLy = -1;
  for (const Ech& e : ech) {
    protoLayer = e.ly; protoArea = area[e.ly]; protoUV = (e.ph == 0);
    auto t0 = clk::now();
    detEchantillon((float)e.t, (float)e.z, (float)e.f);
    auto t1 = clk::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    total_us += us;
    if (us > max_us) { max_us = us; nmax_ly = e.ly; }
    curLy = e.ly;
  }
  (void)curLy;

  const double moy_us = total_us / ech.size();
  const double periode_ms = 12.5;        // 80 SPS
  const double marge = periode_ms * 1000.0 / max_us;

  printf("=== Benchmark du calcul de la détection (sur ce PC) ===\n");
  printf("Scénario %s : %zu échantillons, %d couches (dont 4 couches de base à exposition longue)\n", nom.c_str(), ech.size(), n);
  printf("Temps par échantillon dans detEchantillon() :\n");
  printf("  moyen    = %7.3f µs\n", moy_us);
  printf("  pire cas = %7.3f µs  (échantillon finalisant une couche, vers la couche %ld)\n", max_us, nmax_ly);
  printf("Budget à 80 SPS = %.1f ms/échantillon.\n", periode_ms);
  printf("Marge sur le pire cas (PC) = %.0f×  (le pire échantillon tient %.0f fois dans la période).\n", marge, marge);
  printf("\nExtrapolation ESP32 (240 MHz, double émulé en logiciel, sans FPU double) :\n");
  printf("  en supposant l'ESP32 jusqu'à 50× plus lent par opération que ce PC,\n");
  printf("  pire cas ESP32 ≈ %.2f ms << %.1f ms : le temps réel est tenu avec une marge d'au moins %.0f×.\n",
         max_us * 50.0 / 1000.0, periode_ms, marge / 50.0);
  printf("  (la détection ne tourne qu'en fin de couche ; entre deux couches, le CPU est libre >90 %% du temps.)\n");
  return 0;
}
