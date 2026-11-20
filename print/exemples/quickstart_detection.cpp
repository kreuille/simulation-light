/*
  quickstart_detection — minimal integration example for the msla_detect library
  ==============================================================================
  Goal: show, from a CSV of YOUR sensor data, how to run the detection and print a
  per-layer verdict (OK / PAUSE / STOP). This is the batch (offline / PC) path. The
  live (firmware) path gives identical results and is described in the README.

  Pipeline, per sensor channel:
      raw force  --prep()-->  filtered force
      series     --features()-->  per-layer features (peel peak, compression, push...)
      features   --detect()-->  per-layer flags (learned from the last 6 good layers)
      flags      --> OK / PAUSE / STOP   (same mapping as the firmware)

  Build & run:  see print/exemples/README.md  (or run.sh)

  The library is pure C++17, no dynamic memory, no Arduino dependency: the SAME code
  runs on the ESP32 firmware and here on a PC.

  中文：msla_detect 库的最小集成示例。从你的传感器 CSV 出发，运行检测并逐层输出
  OK/PAUSE/STOP。此为离线（PC）路径；实时（固件）路径结果一致，见 README。
*/
#include "msla_detect/msla_detect.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace msla;

// ---- Calibration (match it to your bench; here: the demo CSV values) ----------
// nm = noise * resFactor, with resFactor = max(1, sensRes / 0.05).
// For StallGuard ("sg") the simulator uses nm = noise (no resFactor).
static double NOISE       = 1.0;    // global noise level of your readings
static double SENS_RES    = 0.03;   // force sensor resolution (N)
static double OBSTACLE_MUL = 2.5;   // application parameter (APPS)
static double LOW_RATIO    = 0.6;   // application parameter (APPS)
static double FSTALL       = 140.0; // Z motor block force (N)

static double resFactor() { return std::max(1.0, SENS_RES / 0.05); }

// Flag name, for display.
static const char* flagName(Flag f) {
  switch (f) {
    case F_CRASH: return "CRASH"; case F_OBSTACLE: return "OBSTACLE";
    case F_APPUI: return "APPUI"; case F_FAIBLE: return "FAIBLE";
    case F_FEP: return "FEP"; default: return "NODATA";
  }
}

// Columns read from the CSV (parallel arrays: one cell per sample).
struct Data {
  std::vector<double> t, z;
  std::vector<int>    ph, ly;
  std::vector<double> area;                         // area[ly] (cm2), one cell per layer
  std::map<std::string, std::vector<double>> force; // "fz","fvat","fsg" -> force series (N)
  int nLayers = 0;
};

// Minimal CSV reader: skips '#' comment lines, reads the header to find the columns
// by name (so extra columns are allowed), then the samples.
static bool readCsv(const std::string& path, Data& d) {
  std::ifstream in(path);
  if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
  std::string line;
  std::vector<std::string> cols;
  auto split = [](const std::string& s) {
    std::vector<std::string> out; std::string cur; std::stringstream ss(s);
    while (std::getline(ss, cur, ',')) out.push_back(cur);
    return out;
  };
  // header
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    cols = split(line);
    break;
  }
  std::map<std::string, int> idx;
  for (int i = 0; i < (int)cols.size(); i++) idx[cols[i]] = i;
  const char* need[] = {"t", "z", "ph", "ly", "area"};
  for (const char* k : need) if (!idx.count(k)) { std::fprintf(stderr, "missing column: %s\n", k); return false; }
  const char* forces[] = {"fz", "fvat", "fsg"};
  for (const char* k : forces) if (idx.count(k)) d.force[k] = {};
  // samples
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> v = split(line);
    if ((int)v.size() < (int)cols.size()) continue;
    const int ly = std::stoi(v[idx["ly"]]);
    d.t.push_back(std::stod(v[idx["t"]]));
    d.z.push_back(std::stod(v[idx["z"]]));
    d.ph.push_back(std::stoi(v[idx["ph"]]));
    d.ly.push_back(ly);
    for (auto& kv : d.force) kv.second.push_back(std::stod(v[idx[kv.first]]));
    if (ly >= 0) {
      if (ly + 1 > d.nLayers) { d.nLayers = ly + 1; d.area.resize(d.nLayers, 0.0); }
      d.area[ly] = std::stod(v[idx["area"]]);   // area of this layer (constant across its samples)
    }
  }
  return !d.t.empty();
}

// Runs the full pipeline for ONE channel and prints its per-layer verdict.
static void runChannel(const std::string& chanName, const char* key,
                       const Data& d, const std::vector<double>& force,
                       bool med, bool avg, double nm) {
  const int ns = (int)d.t.size(), nL = d.nLayers;
  std::vector<double> ff(ns), scratch(ns);
  prep(force.data(), ns, med, avg, ff.data(), scratch.data());

  Serie s{d.t.data(), ff.data(), d.z.data(), d.ph.data(), d.ly.data(), ns};
  std::vector<Feature> feat(nL);
  features(s, nL, feat.data(), scratch.data(), ns);

  std::vector<LayerFlags> flags(nL);
  detect(feat.data(), d.area.data(), nL, key, nm, OBSTACLE_MUL, LOW_RATIO, FSTALL, flags.data());

  // Per-layer verdict, same mapping as the firmware (danger -> STOP, anomaly -> PAUSE).
  int firstBad = -1, nStop = 0, nPause = 0;
  std::printf("  channel \"%s\" (key=%s, nm=%.3f)\n", chanName.c_str(), key, nm);
  for (int i = 0; i < nL; i++) {
    const char* stop = nullptr; const char* pause = nullptr;
    for (int j = 0; j < flags[i].n; j++) switch (flags[i].f[j]) {
      case F_CRASH: case F_OBSTACLE: stop = flagName(flags[i].f[j]); break;
      case F_APPUI: case F_FAIBLE: case F_FEP: if (!pause) pause = flagName(flags[i].f[j]); break;
      default: break;
    }
    if (stop || pause) {
      if (firstBad < 0) firstBad = i;
      if (stop) nStop++; else nPause++;
      std::printf("      layer %2d -> %-5s %s\n", i, stop ? "STOP" : "PAUSE", stop ? stop : pause);
    }
  }
  if (firstBad < 0) std::printf("      all %d layers OK\n", nL);
  else std::printf("      first fault at layer %d  (%d STOP, %d PAUSE)\n", firstBad, nStop, nPause);
}

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "capteurs_demo.csv";
  Data d;
  if (!readCsv(path, d)) return 1;
  std::printf("read %zu samples, %d layers from %s\n\n", d.t.size(), d.nLayers, path.c_str());

  const double nmFull = NOISE * resFactor();   // for z / vat / fus
  const double nmSg   = NOISE;                 // StallGuard does not use resFactor

  // --- physical channels: run detection on each force column present ---
  // prep() flags per channel are the simulator's: z (median only), vat/sg (median + moving avg).
  if (d.force.count("fz"))   runChannel("Z arm cell",     "z",   d, d.force["fz"],   true, false, nmFull);
  if (d.force.count("fvat")) runChannel("vat (4 cells)",  "vat", d, d.force["fvat"], true, true,  nmFull);
  if (d.force.count("fsg"))  runChannel("StallGuard",     "sg",  d, d.force["fsg"],  true, true,  nmSg);

  // --- fused channel: inverse-variance weighting of Z and vat (like the firmware) ---
  // The weight favours the quieter mount. Here we estimate each channel's noise from
  // the top-wait phase (ph==3), where the true force is a flat baseline.
  if (d.force.count("fz") && d.force.count("fvat")) {
    auto rmsTop = [&](const std::vector<double>& f) {
      double s = 0; int c = 0;
      for (int i = 1; i < (int)f.size(); i++) if (d.ph[i] == 3) { const double dv = f[i] - f[i - 1]; s += dv * dv; c++; }
      return c ? std::sqrt(s / c) : 1.0;
    };
    const double vz = rmsTop(d.force["fz"]), vv = rmsTop(d.force["fvat"]);
    const double wz = (vv * vv) / (vz * vz + vv * vv);   // weight on Z
    std::vector<double> fus(d.t.size());
    for (size_t i = 0; i < fus.size(); i++) fus[i] = wz * d.force["fz"][i] + (1 - wz) * d.force["fvat"][i];
    std::printf("  (fusion weight on Z = %.2f)\n", wz);
    runChannel("fusion Z+vat", "fus", d, fus, true, false, nmFull);
  }

  std::printf("\nExpected on the demo CSV: fault detected at layer 30 (APPUI) on every channel.\n");
  return 0;
}
