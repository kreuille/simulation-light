/*
  Runner de parité : rejoue les scénarios exportés par export_scenarios.js dans
  la bibliothèque msla_detect (C++) et vérifie que les verdicts d'apprentissage
  par couche et les temps de stabilisation sont IDENTIQUES à ceux du simulateur.

  Usage : parite <dossier> [scenario ...]
  Lit <dossier>/scenarios.txt si aucun scénario n'est passé en argument.

  Le programme de test peut allouer (std::vector) ; la bibliothèque, elle, reste
  sans allocation dynamique. Sortie non nulle en cas d'écart.
*/
#include "msla_detect.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace msla;

static const char* nomFlag(Flag f) {
  switch (f) {
    case F_CRASH: return "crash";
    case F_OBSTACLE: return "obstacle";
    case F_APPUI: return "appui";
    case F_FAIBLE: return "faible";
    case F_FEP: return "fep";
    default: return "none";
  }
}

static int echecs = 0, canaux = 0;

static void verifChannel2(const std::string& scenNom, const std::string& scen, int L, int n,
                          double nmE, double rate, double obstacleMul, double lowRatio, double fstall,
                          const std::vector<double>& area,
                          const std::vector<double>& restStart, const std::vector<double>& topStart,
                          const std::string& key, bool med, bool avg, double nmDet,
                          int efirst, const std::string& ekind, int efa, int enodata, int elat,
                          const std::vector<int>& sdFound, const std::vector<double>& sdTs,
                          const std::vector<int>& stFound, const std::vector<double>& stTs,
                          const std::vector<double>& t, const std::vector<double>& f,
                          const std::vector<double>& z, const std::vector<int>& ph,
                          const std::vector<int>& ly) {
  canaux++;
  const int ns = (int)t.size();
  std::vector<double> ff(ns), scratch(ns);
  prep(f.data(), ns, med, avg, ff.data(), scratch.data());

  Serie s{t.data(), ff.data(), z.data(), ph.data(), ly.data(), ns};
  std::vector<Feature> feat(n);
  features(s, n, feat.data(), scratch.data(), ns);

  std::vector<LayerFlags> flags(n);
  detect(feat.data(), area.data(), n, key.c_str(), nmDet, obstacleMul, lowRatio, fstall, flags.data());
  Verdict v = verdict(flags.data(), n, scen.c_str(), L);

  // Parité des versions en flux (chaîne live du firmware).
  auto egal = [](double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
  // 1) SuiviCouches doit reproduire features().
  SuiviCouches suivi; suivi.init();
  std::vector<Feature> sfeat(n);
  for (int i = 0; i < n; i++) { sfeat[i].peak = sfeat[i].comp = sfeat[i].travel = sfeat[i].stat = absente(); }
  Feature tmp; double aireTmp;
  for (int k = 0; k < ns; k++) { int l = suivi.pousser(ff[k], z[k], ph[k], ly[k], area[ly[k]], &tmp, &aireTmp); if (l >= 0) sfeat[l] = tmp; }
  { int l; while ((l = suivi.terminer(&tmp, &aireTmp)) >= 0) sfeat[l] = tmp; }
  int fEch = 0;
  for (int i = 0; i < n; i++)
    if (!egal(sfeat[i].peak, feat[i].peak) || !egal(sfeat[i].comp, feat[i].comp) ||
        !egal(sfeat[i].travel, feat[i].travel) || !egal(sfeat[i].stat, feat[i].stat)) fEch++;
  if (fEch) { echecs++; std::printf("  ECHEC features flux %s/%s : %d couches divergentes\n", scenNom.c_str(), key.c_str(), fEch); }
  // 2) DetecteurCouche doit reproduire les drapeaux de detect().
  DetecteurCouche det; det.init(key.c_str(), nmDet, obstacleMul, lowRatio, fstall);
  int dEch = 0;
  for (int i = 0; i < n; i++) {
    LayerFlags lf = det.pousser(feat[i], area[i]);
    bool ok2 = lf.n == flags[i].n;
    for (int j = 0; ok2 && j < lf.n; j++) ok2 = lf.f[j] == flags[i].f[j];
    if (!ok2) dEch++;
  }
  if (dEch) { echecs++; std::printf("  ECHEC detecteur flux %s/%s : %d couches divergentes\n", scenNom.c_str(), key.c_str(), dEch); }

  const int cfirst = v.hasFirst ? v.first : -1;
  const std::string ckind = nomFlag(v.hasFirst ? v.kind : F_NODATA);
  const int clat = v.hasFirst ? v.lat : -1;
  bool ok = (cfirst == efirst) && (ckind == ekind) && (v.fa == efa) && (v.nodata == enodata) && (clat == elat);
  if (!ok) {
    echecs++;
    std::printf("  ECHEC verdict %s/%s : C++ {first=%d kind=%s fa=%d nodata=%d lat=%d} != JS {first=%d kind=%s fa=%d nodata=%d lat=%d}\n",
                scenNom.c_str(), key.c_str(), cfirst, ckind.c_str(), v.fa, v.nodata, clat,
                efirst, ekind.c_str(), efa, enodata, elat);
  }

  // Stabilité (sauf sg).
  if (key != "sg") {
    StabOpt opt{rate, nmE};
    Starts starts{restStart.data(), topStart.data()};
    std::vector<Settle> down(n), top(n);
    stability(s, key.c_str(), opt, starts, n, down.data(), top.data());
    int stabEch = 0;
    for (int i = 0; i < n; i++) {
      const bool ef = sdFound[i] != 0, cf = down[i].found;
      if (ef != cf || (ef && std::fabs(sdTs[i] - down[i].ts) > 1e-6)) stabEch++;
      const bool ef2 = stFound[i] != 0, cf2 = top[i].found;
      if (ef2 != cf2 || (ef2 && std::fabs(stTs[i] - top[i].ts) > 1e-6)) stabEch++;
    }
    if (stabEch) { echecs++; std::printf("  ECHEC stabilite %s/%s : %d couches divergentes\n", scenNom.c_str(), key.c_str(), stabEch); }
  }
}

static void lireVecteur(std::istream& in, int n, std::vector<double>& v) {
  v.resize(n);
  for (int i = 0; i < n; i++) in >> v[i];
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: parite <dossier> [scenario ...]\n"); return 2; }
  const std::string dir = argv[1];
  std::vector<std::string> noms;
  if (argc > 2) for (int a = 2; a < argc; a++) noms.push_back(argv[a]);
  else {
    std::ifstream sc(dir + "/scenarios.txt");
    std::string l;
    while (std::getline(sc, l)) if (!l.empty()) noms.push_back(l);
  }

  for (const auto& nom : noms) {
    std::ifstream in(dir + "/" + nom + ".txt");
    if (!in) { std::fprintf(stderr, "scenario introuvable : %s\n", nom.c_str()); echecs++; continue; }
    std::string tok, scen;
    int L = 0, n = 0;
    double noise = 0, nmE = 0, rate = 0, wait = 0, topWait = 0, margin = 0, fstall = 0, obstacleMul = 0, lowRatio = 0;
    std::vector<double> area, restStart, topStart, settleDown, settleTop;
    while (in >> tok) {
      if (tok == "SCEN") in >> scen;
      else if (tok == "L") in >> L;
      else if (tok == "N") in >> n;
      else if (tok == "NOISE") in >> noise;
      else if (tok == "NME") in >> nmE;
      else if (tok == "RATE") in >> rate;
      else if (tok == "WAIT") in >> wait;
      else if (tok == "TOPWAIT") in >> topWait;
      else if (tok == "MARGIN") in >> margin;
      else if (tok == "FSTALL") in >> fstall;
      else if (tok == "OBSTACLEMUL") in >> obstacleMul;
      else if (tok == "LOWRATIO") in >> lowRatio;
      else if (tok == "AREA") lireVecteur(in, n, area);
      else if (tok == "RESTSTART") lireVecteur(in, n, restStart);
      else if (tok == "TOPSTART") lireVecteur(in, n, topStart);
      else if (tok == "SETTLEDOWN") lireVecteur(in, n, settleDown);
      else if (tok == "SETTLETOP") lireVecteur(in, n, settleTop);
      else if (tok == "BEGIN_CHANNEL") {
        std::string key, k2; bool med = false, avg = false; double nmDet = 0;
        in >> key;
        in >> k2 >> med;    // MED
        in >> k2 >> avg;    // AVG
        in >> k2 >> nmDet;  // NMDET
        // VERDICT first <> kind <> fa <> nodata <> lat <>
        int efirst = 0, efa = 0, enodata = 0, elat = 0; std::string ekind;
        in >> k2;           // VERDICT
        in >> k2 >> efirst >> k2 >> ekind >> k2 >> efa >> k2 >> enodata >> k2 >> elat;
        // STABDOWN / STABTOP : 2n valeurs (found ts)
        std::vector<int> sdFound(n), stFound(n); std::vector<double> sdTs(n), stTs(n);
        in >> k2;           // STABDOWN
        for (int i = 0; i < n; i++) in >> sdFound[i] >> sdTs[i];
        in >> k2;           // STABTOP
        for (int i = 0; i < n; i++) in >> stFound[i] >> stTs[i];
        // SAMPLES <count> puis count lignes
        int ns = 0; in >> k2 >> ns;
        std::vector<double> t(ns), f(ns), z(ns); std::vector<int> ph(ns), ly(ns);
        for (int i = 0; i < ns; i++) in >> t[i] >> f[i] >> z[i] >> ph[i] >> ly[i];
        in >> k2;           // END_CHANNEL
        verifChannel2(nom, scen, L, n, nmE, rate, obstacleMul, lowRatio, fstall, area,
                      restStart, topStart, key, med, avg, nmDet,
                      efirst, ekind, efa, enodata, elat, sdFound, sdTs, stFound, stTs,
                      t, f, z, ph, ly);
      }
    }
  }

  if (echecs) { std::printf("PARITE : %d ecart(s) sur %d canaux.\n", echecs, canaux); return 1; }
  std::printf("PARITE : %d canaux conformes, verdicts et stabilite identiques au simulateur.\n", canaux);
  return 0;
}
