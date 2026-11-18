// Test hôte du protocole série (Serial2) du firmware, voir firmware/PROTOCOLE.md.
// Alimente Serial2 avec des commandes de la carte mère et vérifie les réponses du banc.
// Pour le verdict « VAT », rejoue des traces CSV (générées par firmware_host/generate_cases.py).
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

// Envoie une ligne à la carte mère -> banc, puis traite, et renvoie la réponse du banc.
static std::string echange(const char* ligne) {
  Serial2.feed(ligne);
  protocolePoll();
  return Serial2.drain();
}

int main(int argc, char** argv) {
  Serial.toStdout = false;    // on ne veut que les réponses Serial2 et nos marqueurs
  Serial2.toStdout = false;
  cfg.nCells = 5; cfg.vatW = 200; cfg.vatH = 125;

  // A. Commandes simples
  printf("A1 %s", echange("?\n").c_str());                       // attendu : RDY
  printf("A2 %s", echange("Z\n").c_str());                       // attendu : ERR
  // (les verdicts de couche OK/PAUSE/STOP sont testés par tests/couche_live)

  // Rejoue un fichier CSV dans ckUpdate (le contrôleur doit être armé au préalable).
  auto rejoue = [](const std::string& fn) {
    std::ifstream in(fn); std::string line; std::getline(in, line);
    while (std::getline(in, line)) {
      std::stringstream ss(line); float t, z, u, f[5]; char c;
      ss >> t >> c >> f[0] >> c >> f[1] >> c >> f[2] >> c >> f[3] >> c >> f[4] >> c >> z >> c >> u;
      ckUpdate(t, z, f, u > 0.5f);
    }
  };

  // Le premier fichier établit la référence (bac propre), comme l'opérateur avant impression.
  if (argc > 1) { ckArm(1); rejoue(argv[1]); Serial2.drain(); }

  // B. Verdict du bac via le protocole (« V »), avec la référence en place.
  for (int a = 2; a < argc; a++) {
    std::string fn = argv[a];
    bac.desarmer(); protoVatPending = false; Serial2.drain();
    echange("V\n");
    rejoue(fn);
    std::string r = Serial2.drain();
    if (r.empty()) r = "(aucun verdict)\n";
    printf("B %s  <- %s\n", r.substr(0, r.find('\n')).c_str(), fn.c_str());
  }
  return 0;
}
