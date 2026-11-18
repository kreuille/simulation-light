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
| `L <n> [aire]` | début de la couche n (n ≥ 0) ; `aire` = surface exposée en mm² (optionnelle mais recommandée) | couche courante = n ; démarre la détection au premier `L` |
| `U 1` / `U 0` | UV allumé / éteint | état d'exposition mémorisé (sert à déduire les phases) |
| `V` | contrôler le bac avant impression | arme le contrôle du bac ; répond `VAT …` à la fin du test |
| `P` | pause | passe en pause (suspend les verdicts de couche) |
| `R` | reprise | sort de pause |
| `X` | arrêt / fin d'impression | finalise la dernière couche, désarme tout, retour au repos |
| `?` | demande d'état | répond `RDY` |

L'`aire` sert à comparer les couches entre elles (une grande surface arrache plus
qu'une petite) : sans elle, la détection suppose une surface constante et devient
moins fiable sur les pièces à géométrie variable. Le trancheur la connaît par couche.

## 3. Banc → carte mère (réponses et événements)

| Trame | Quand | Signification |
|---|---|---|
| `RDY` | démarrage, réponse à `?` | banc prêt |
| `VAT OK` | après `V` | bac propre : impression possible |
| `VAT <code>` | après `V` | bac non conforme (`RESIDU`, `ERREUR`) : impression déconseillée |
| `WAIT` | fin de descente, résine encore en mouvement | la carte mère doit retarder l'exposition *(réservé, cf. §6)* |
| `GO` | résine stabilisée | l'exposition peut commencer *(réservé, cf. §6)* |
| `OK <n>` | couche n analysée, saine | — |
| `PAUSE <code> <n>` | anomalie récupérable à la couche n | demande de pause (inspection opérateur) |
| `STOP <code> <n>` | danger à la couche n | demande d'arrêt immédiat |
| `ALRM <code>` | événement capteur | `FORCE` (effort au-delà du seuil) |
| `ERR` | trame non reconnue | — |

Le verdict d'une couche est émis quand elle est finalisée (au début de l'arrachement
de la couche suivante), ou pour la dernière couche sur `X`. Correspondance entre les
drapeaux de la bibliothèque `msla_detect` et le verdict :

| Drapeau | Signification | Verdict |
|---|---|---|
| `CRASH` | blocage / écrasement du moteur | `STOP CRASH <n>` |
| `OBSTACLE` | obstacle à la descente | `STOP OBSTACLE <n>` |
| `APPUI` | appui résiduel (corps étranger) | `PAUSE APPUI <n>` |
| `FAIBLE` | arrachement trop faible (décollement probable) | `PAUSE FAIBLE <n>` |
| `FEP` | film détendu | `PAUSE FEP <n>` |

Les dangers (plateau bloqué ou qui écrase) donnent `STOP` ; les anomalies
récupérables donnent `PAUSE`. La carte mère reste maître de la décision (cf. §4).

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
- Verdict d'une couche émis quand elle est finalisée (au début de l'arrachement de
  la couche suivante). Une couche saine donne `OK <n>`, un défaut `PAUSE`/`STOP`.
  La toute dernière couche est finalisée sur `X`.
- Perte de liaison (silence du banc > 2 s pendant une impression) : la carte mère
  décide (continuer, ou mettre en pause par sécurité).

## 6. État de l'implémentation firmware

Émis aujourd'hui par `banc_msla.ino` : `RDY`, `VAT OK`/`VAT RESIDU`/`VAT ERREUR`,
`OK <n>`, `PAUSE <code> <n>` et `STOP <code> <n>` (détection par couche par
apprentissage : `CRASH`, `OBSTACLE`, `APPUI`, `FAIBLE`, `FEP`), `ALRM FORCE`, `ERR`.

La détection par couche tourne en direct : le banc déduit les phases (descente,
arrachement, exposition, attentes) du mouvement Z et de l'UV annoncés par la carte
mère, construit les features de chaque couche et les passe au détecteur de la
bibliothèque `msla_detect`. Le comportement est vérifié par le test
`tests/couche_live` : sur les scénarios du simulateur, le défaut est signalé à la
bonne couche, sans fausse alerte sur une impression normale.

Réservé (défini par le protocole, pas encore émis) : `WAIT`/`GO`, mise en attente de
l'exposition tant que la résine n'est pas stabilisée (la bibliothèque sait mesurer
cette stabilisation, cf. `msla_stabilite` ; il reste à la câbler sur `Serial2`).

## 7. Exemple d'échange

```
carte mère → banc    V
banc → carte mère    VAT OK
carte mère → banc    L 0 18.0        (couche 0, 18 mm² exposés)
carte mère → banc    U 1
carte mère → banc    U 0
carte mère → banc    L 1 18.0
carte mère → banc    U 1
banc → carte mère    OK 0            (verdict de la couche 0, à l'arrachement de la 1)
carte mère → banc    U 0
...
carte mère → banc    L 30 2.0
banc → carte mère    OK 29
carte mère → banc    U 1
...
banc → carte mère    STOP OBSTACLE 30
carte mère → banc    X
```

Remarque sur le temps : le verdict de la couche n arrive pendant l'impression de la
couche n+1 (dès son arrachement). La carte mère reçoit donc `STOP OBSTACLE 30`
alors qu'elle commence la couche 31 ; à elle de couper l'UV et de remonter.
