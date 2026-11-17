// Fait tourner l'analyse du contrôle du bac du firmware sur PC, à partir de fichiers CSV.
// Compilé et lancé par run.sh.
#include "Arduino.h"
SerialT Serial;
#define setup sketch_setup
#define loop sketch_loop
#include "../../firmware/banc_msla/banc_msla.ino"
#undef setup
#undef loop
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
  cfg.nCells = 5; cfg.vatW = 200; cfg.vatH = 125;
  for (int a = 1; a < argc; a++) {
    std::string fn = argv[a];
    ckArm(fn.find("ref") != std::string::npos ? 1 : fn.find("plan") != std::string::npos ? 2 : 0);
    std::ifstream in(fn); std::string line; std::getline(in, line);
    while (std::getline(in, line)) {
      std::stringstream ss(line); float t, z, u, f[5]; char c;
      ss >> t >> c >> f[0] >> c >> f[1] >> c >> f[2] >> c >> f[3] >> c >> f[4] >> c >> z >> c >> u;
      ckUpdate(t, z, f, u > 0.5f);
    }
    if (ck != CK_OFF) printf("# (fin du fichier sans verdict, etat=%d)\n", (int)ck);
    printf("  <- %s\n", fn.c_str());
  }
}
