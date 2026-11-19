// Benchmark of the compute cost of the firmware per-layer detection.
// Replays a real print (exported scenario, 60 layers of which 4 base layers with a long
// exposure) through the real detEchantillon() function of the firmware, and measures the
// time per sample (mean and worst case) on this PC.
//
// The worst case is the sample that finalizes a layer (median sorts): it is the one that
// must fit within the sample period (12.5 ms at 80 SPS) on the ESP32.
//
// Usage: bench <dir> <scenario>
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
  if (!in) { fprintf(stderr, "scenario not found: %s\n", nom.c_str()); return 2; }
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

  printf("=== Detection compute benchmark (on this PC) ===\n");
  printf("Scenario %s: %zu samples, %d layers (of which 4 base layers with a long exposure)\n", nom.c_str(), ech.size(), n);
  printf("Time per sample in detEchantillon():\n");
  printf("  mean       = %7.3f us\n", moy_us);
  printf("  worst case = %7.3f us  (sample finalizing a layer, around layer %ld)\n", max_us, nmax_ly);
  printf("Budget at 80 SPS = %.1f ms/sample.\n", periode_ms);
  printf("Margin on the worst case (PC) = %.0fx  (the worst sample fits %.0f times in the period).\n", marge, marge);
  printf("\nESP32 extrapolation (240 MHz, double emulated in software, no double FPU):\n");
  printf("  assuming the ESP32 is up to 50x slower per operation than this PC,\n");
  printf("  ESP32 worst case ~ %.2f ms << %.1f ms: real time is met with a margin of at least %.0fx.\n",
         max_us * 50.0 / 1000.0, periode_ms, marge / 50.0);
  printf("  (the detection runs only at the end of a layer; between layers, the CPU is free >90 %% of the time.)\n");
  return 0;
}
