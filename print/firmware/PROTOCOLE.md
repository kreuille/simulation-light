# Protocole série : coprocesseur de mesure ↔ carte mère

Ce document décrit la liaison entre le **banc de mesure** (coprocesseur ESP32,
`banc_msla.ino`) et la **carte mère** de l'imprimante. Le but : la carte mère
pilote l'impression et informe le banc des événements qu'elle connaît (début de
couche, UV) ; le banc mesure, contrôle le bac avant impression, et renvoie des
verdicts (bac OK, attendre, pause, arrêt).

La liaison de mesure brute (CSV à 921600 bauds sur l'USB) est **indépendante** de
ce protocole : elle reste disponible pour l'enregistrement et le recalage.

## 1. Couche physique

| Paramètre | Valeur |
|---|---|
| Port ESP32 | `Serial2` (UART2) |
| Vitesse | 115200 bauds, 8N1 |
| Broches | RX2 = GPIO16, TX2 = GPIO17 (niveau 3,3 V ; prévoir un adaptateur si la carte mère est en 5 V) |
| Trames | lignes ASCII terminées par `\n` (LF) ; `\r` ignoré ; 32 caractères maximum |
| Casse | commandes insensibles à la casse |
| Maître | la carte mère. Le banc ne parle que pour répondre ou signaler un événement. |

Chaque trame = un mot-clé, éventuellement suivi d'arguments séparés par une espace.

## 2. Carte mère → banc (commandes)

| Trame | Signification | Effet sur le banc |
|---|---|---|
| `L <n>` | début de la couche n (n ≥ 0) | couche courante = n, accumulation de couche réinitialisée |
| `U 1` / `U 0` | UV allumé / éteint | état d'exposition mémorisé ; `U 0` clôt la couche et déclenche son verdict |
| `V` | contrôler le bac avant impression | arme le contrôle du bac ; répond `VAT …` à la fin du test |
| `P` | pause | passe en pause (suspend les verdicts de couche) |
| `R` | reprise | sort de pause |
| `X` | arrêt / fin d'impression | désarme tout, retour au repos |
| `?` | demande d'état | répond `RDY` |

## 3. Banc → carte mère (réponses et événements)

| Trame | Quand | Signification |
|---|---|---|
| `RDY` | démarrage, réponse à `?` | banc prêt |
| `VAT OK` | après `V` | bac propre : impression possible |
| `VAT <code>` | après `V` | bac non conforme (`RESIDU`, `ERREUR`) : impression déconseillée |
| `WAIT` | fin de descente, résine encore en mouvement | la carte mère doit retarder l'exposition *(réservé, cf. §6)* |
| `GO` | résine stabilisée | l'exposition peut commencer *(réservé, cf. §6)* |
| `OK <n>` | fin d'exposition de la couche n | couche saine |
| `PAUSE <code>` | anomalie récupérable | demande de pause (inspection opérateur) |
| `STOP <code>` | danger | demande d'arrêt immédiat |
| `ALRM <code>` | événement capteur | `FORCE` (effort au-delà du seuil), `MUET` (HX711 sans réponse) |
| `ERR` | trame non reconnue | — |

Codes de verdict de couche (repris de la bibliothèque `msla_detect`) :
`FAIBLE`, `OBSTACLE`, `CRASH`, `APPUI`, `FEP`, et `FORCE` (seuil d'effort brut).

## 4. Machine à états de l'impression

```
        ┌────────────────────────── X ──────────────────────────┐
        ▼                                                        │
     REPOS ──V──► CONTROLE_BAC ──VAT OK──► PRET ──L n──► COUCHE ─┘
                       │                              ▲    │
                       └── VAT <code> ──► (la carte   │    │ U 1 … U 0
                           mère décide)               └────┘  → OK/PAUSE/STOP n
```

- `STOP` est prioritaire : la carte mère coupe l'UV et remonte le plateau.
- `PAUSE` et `STOP` sont des **demandes** ; la carte mère reste maître de la décision.

## 5. Délais et replis sûrs

- Après `V`, attendre `VAT …` au plus ~30 s (durée du test de descente). Au-delà :
  réessayer ou abandonner.
- Verdict de couche (`OK`/`PAUSE`/`STOP`) émis dans les ~200 ms après `U 0`. S'il
  manque avant la couche suivante, la carte mère considère la couche comme `OK`
  (repli non bloquant) mais peut compter les silences.
- Perte de liaison (silence du banc > 2 s pendant une impression) : la carte mère
  décide (continuer, ou mettre en pause par sécurité).

## 6. État de l'implémentation firmware

Émis aujourd'hui par `banc_msla.ino` : `RDY`, `VAT OK`/`VAT RESIDU`/`VAT ERREUR`,
`OK <n>`, `PAUSE FORCE`, `STOP FORCE`, `ALRM FORCE`, `ERR`.

Réservé (défini par le protocole, pas encore émis par le firmware) : `WAIT`/`GO`
(mise en attente de l'exposition tant que la résine n'est pas stabilisée) et les
verdicts de couche issus de l'apprentissage par couche (`OBSTACLE`, `CRASH`,
`FAIBLE`, `FEP`). La bibliothèque `msla_detect` contient déjà cette détection
(validée par parité avec le simulateur) ; il reste à l'alimenter en continu,
couche par couche, à partir du flux de mesure. Le point d'entrée est prévu
(`protocoleVerdictCouche`).

## 7. Exemple d'échange

```
carte mère → banc    V
banc → carte mère    VAT OK
carte mère → banc    L 0
carte mère → banc    U 1
carte mère → banc    U 0
banc → carte mère    OK 0
carte mère → banc    L 1
carte mère → banc    U 1
carte mère → banc    U 0
banc → carte mère    STOP FORCE
carte mère → banc    X
```
