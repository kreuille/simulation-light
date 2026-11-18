/*
  msla_detect — bibliothèque de détection MSLA, portable et sans allocation dynamique
  ===================================================================================
  Logique de détection extraite du simulateur (print/simulateur/index.html) et du
  firmware (print/firmware/banc_msla/banc_msla.ino), réunie en une bibliothèque C++17
  pure, utilisable à la fois :
    - par le firmware ESP32 (sans dépendance Arduino dans la bibliothèque) ;
    - par les tests de parité sur PC (mêmes verdicts que le simulateur).

  Règles :
    - aucune allocation dynamique : tous les tampons sont fournis par l'appelant,
      ou de taille fixe (MSLA_MAX_COUCHES, BUF du contrôle de bac) ;
    - double précision, comme le Number de JavaScript, pour des verdicts identiques ;
    - commentaires et messages en français.

  Modules :
    msla_filtres   — médiane, med3, moyenne glissante
    msla_couche    — détection par couche (features, apprentissage, verdict)
    msla_stabilite — stabilisation de la résine
    msla_bac       — contrôle du bac avant impression, planéité, pesée, fuite

  Inclure cet en-tête donne accès à tout.
*/
#ifndef MSLA_DETECT_H
#define MSLA_DETECT_H

#include "msla_filtres.h"
#include "msla_couche.h"
#include "msla_stabilite.h"
#include "msla_bac.h"

#endif  // MSLA_DETECT_H
