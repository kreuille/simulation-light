// Host test of the firmware LIVE per-layer detection (see PROTOCOLE.md).
// Replays the scenarios exported by tests/lib/export_scenarios.js through the real
// firmware path: "L"/"U" commands on Serial2 + samples via detEchantillon, with the
// phase labeling redone by the firmware from the Z movement and the UV.
// Checks the layer of the first fault (STOP/PAUSE) and the absence of a false alarm.
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

struct Ech { double t, f, z; int ph, ly; };

int main(int argc, char** argv) {
  Serial.toStdout = false;
  Serial2.toStdout = false;
  const std::string dir = argc > 1 ? argv[1] : ".";

  for (int a = 2; a < argc; a++) {
    const std::string nom = argv[a];
    std::ifstream in(dir + "/" + nom + ".txt");
    if (!in) { printf("DET %s introuvable\n", nom.c_str()); continue; }

    int n = 0; double nme = 1, fstall = 140;
    std::vector<double> area;
    std::vector<Ech> ech;
    std::string tok;
    bool dansZ = false;
    while (in >> tok) {
      if (tok == "N") in >> n;
      else if (tok == "NME") in >> nme;
      else if (tok == "FSTALL") in >> fstall;
      else if (tok == "AREA") { area.resize(n); for (int i = 0; i < n; i++) in >> area[i]; }
      else if (tok == "BEGIN_CHANNEL") { std::string k; in >> k; dansZ = (k == "z"); }
      else if (tok == "SAMPLES" && dansZ) {
        int k; in >> k; ech.resize(k);
        for (int i = 0; i < k; i++) in >> ech[i].t >> ech[i].f >> ech[i].z >> ech[i].ph >> ech[i].ly;
      } else if (tok == "END_CHANNEL" && dansZ) dansZ = false;
    }

    // Reset the firmware state and set the detection calibration from the scenario.
    detArreter(); protoLayer = -1; protoUV = false; protoArea = 0; detActive = false;
    Serial2.drain();
    detNoise = nme; detObstacleMul = 2.5; detLowRatio = 0.6; cfg.zMaxN = fstall;

    // Replay: the motherboard announces the layer ("L n area") and the UV ("U"), the
    // sample is given to the bench which derives the phases itself from the Z movement.
    int curLy = -1; bool curUV = false;
    for (const Ech& e : ech) {
      if (e.ly != curLy) { char b[64]; snprintf(b, sizeof b, "L %d %.6f\n", e.ly, area[e.ly]); Serial2.feed(b); protocolePoll(); curLy = e.ly; }
      bool uv = (e.ph == 0);
      if (uv != curUV) { Serial2.feed(uv ? "U 1\n" : "U 0\n"); protocolePoll(); curUV = uv; }
      detEchantillon((float)e.t, (float)e.z, (float)e.f);
    }
    Serial2.feed("X\n"); protocolePoll();   // flush the last layers

    // Analyze the emitted verdicts: first faulty layer, and flags before it.
    std::string out = Serial2.drain(), ligne;
    std::stringstream ss(out);
    int premier = -1, nbDefauts = 0;
    while (std::getline(ss, ligne)) {
      if (ligne.rfind("PAUSE", 0) == 0 || ligne.rfind("STOP", 0) == 0) {
        nbDefauts++;
        int ly = atoi(ligne.substr(ligne.rfind(' ') + 1).c_str());
        if (premier < 0 || ly < premier) premier = ly;
      }
    }
    printf("DET %s first=%d faults=%d\n", nom.c_str(), premier, nbDefauts);
  }
  return 0;
}
