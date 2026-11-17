# Imprimante MSLA 10,1 pouces instrumentée — modèle Fusion 360
#
# Utilisation : Fusion 360 > Utilitaires > Compléments > Scripts et compléments > « + » (ajouter un script
# existant) > choisir ce dossier, puis Exécuter. Un nouveau document est créé avec un composant par
# sous-ensemble. Toutes les cotes sont en millimètres dans les constantes ci-dessous : modifie-les puis
# relance le script pour obtenir une nouvelle version.
#
# Repère : origine au centre de l'écran au niveau du sol, X vers la droite, Y vers l'arrière, Z vers le haut.
# Étude complète : print/conception/README.md du dépôt simulation-light.

import math
import traceback

try:
    import adsk.core
    import adsk.fusion
except ImportError:          # permet de tester la géométrie hors de Fusion 360
    adsk = None

# ---------------------------------------------------------------- paramètres (mm)
Z_POS = 40.0                 # hauteur du plateau au-dessus de sa position basse (0 à 250)
TRAVEL = 250.0               # course utile en Z

FOOT_H = 8.0                 # pieds amortis
BASE_W, BASE_D = 360.0, 340.0
BASE_T = 6.0                 # plaque de fond
WALL = 2.0                   # parois du socle
DECK_Z = 120.0               # dessus des parois = dessous du pont
DECK_T = 8.0                 # pont supérieur
DECK_WIN = (240.0, 145.0)    # fenêtre de lumière dans le pont

SCREEN = (236.0, 143.0, 3.0)        # module LCD 10,1 pouces (zone utile ≈ 218 × 123)
ACTIVE = (218.0, 123.0)
GLASS = (240.0, 145.0, 1.0)         # verre de protection
CELL = (80.0, 12.7, 12.7)           # cellule poutre 20 kg (longueur selon X)
CELL_X0, CELL_Y = 65.0, 85.0        # extrémité fixe à |x| = 65, cellules à y = ±85
SPACER_T = 5.0                      # entretoises isolantes
CRADLE = (290.0, 200.0, 8.0)        # plaque du berceau
CRADLE_WIN = (230.0, 135.0)
STOP_GAP = 0.3                      # jeu des butées de surcharge

VAT_IN = (250.0, 155.0, 50.0)       # intérieur du bac
VAT_WALL = 6.0
FEP_T = 0.5                         # film (épaissi pour être visible)

PLATE = (225.0, 130.0, 12.0)        # plateau d'impression
ROTULE = (50.0, 50.0, 40.0)
ARM = (120.0, 110.0, 25.0)
ZCELL = (30.0, 130.0, 22.0)         # cellule 50 kg à flexion parallèle (longueur selon Y)
ZCELL_Y0 = -26.0                    # extrémité libre, à l'aplomb du centre du plateau

COL = (200.0, 20.0)                 # colonne : largeur, épaisseur
COL_Y0 = 140.0                      # face avant de la colonne
RAIL_DX = 60.0                      # rails à x = ±60
SCREW_Y = 122.0                     # axe de la vis à billes (x = 0)

# masses volumiques (g/cm³) pour le calcul de masse du berceau
RHO = {'alu': 2.70, 'verre': 2.50, 'acier': 7.85, 'plastique': 1.20, 'g10': 1.85, 'pmma': 1.19, 'fep': 2.15, 'electronique': 1.6}
K_CELL = 784.0                      # raideur d'une cellule du berceau (N/mm)
RESIN_L = 1.0                       # litres de résine pour le calcul de fréquence
RESIN_RHO = 1.1

COLORS = {'alu': (190, 194, 199), 'acier': (120, 124, 130), 'verre': (120, 170, 200), 'plastique': (40, 40, 44),
          'g10': (200, 180, 90), 'pmma': (235, 120, 20), 'fep': (230, 240, 245), 'electronique': (20, 110, 60),
          'capteur': (60, 120, 220), 'uv': (120, 60, 200)}


# ---------------------------------------------------------------- géométrie (indépendante de Fusion)
def box(x0, x1, y0, y1, z0, z1):
    return ('box', (min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1), min(z0, z1), max(z0, z1)))


def cbox(cx, cy, z0, w, d, h):
    return box(cx - w / 2, cx + w / 2, cy - d / 2, cy + d / 2, z0, z0 + h)


def cyl(cx, cy, z0, z1, r):
    return ('cyl', (cx, cy, z0, z1, r))


def parts(z_pos=Z_POS):
    """Liste des pièces : (composant, nom, forme, découpes, matière, couleur, opacité)."""
    P = []

    def add(comp, name, shape, cuts=(), mat='alu', color=None, opacity=1.0):
        P.append(dict(comp=comp, name=name, shape=shape, cuts=list(cuts), mat=mat, color=color or mat, opacity=opacity))

    bx, by = BASE_W / 2, BASE_D / 2
    z_floor = FOOT_H
    deck_top = DECK_Z + DECK_T

    # --- socle
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (1, 1), (-1, 1)]):
        add('Socle', f'Pied {i + 1}', cyl(sx * (bx - 20), sy * (by - 20), 0, FOOT_H, 12), mat='plastique')
    add('Socle', 'Plaque de fond', box(-bx, bx, -by, by, z_floor, z_floor + BASE_T))
    zw0 = z_floor + BASE_T
    add('Socle', 'Paroi avant', box(-bx, bx, -by, -by + WALL, zw0, DECK_Z))
    add('Socle', 'Paroi arrière', box(-bx, bx, by - WALL, by, zw0, DECK_Z),
        cuts=[cbox(0, by - WALL / 2, 40, 120, 4, 50)])
    add('Socle', 'Paroi gauche', box(-bx, -bx + WALL, -by + WALL, by - WALL, zw0, DECK_Z),
        cuts=[box(-bx - 1, -bx + WALL + 1, -40, 40, 30, 110)])
    add('Socle', 'Paroi droite', box(bx - WALL, bx, -by + WALL, by - WALL, zw0, DECK_Z))
    add('Socle', 'Pont supérieur', box(-bx, bx, -by, by, DECK_Z, deck_top),
        cuts=[cbox(0, 0, DECK_Z - 1, DECK_WIN[0], DECK_WIN[1], DECK_T + 2), cyl(0, SCREW_Y, DECK_Z - 1, deck_top + 1, 12)])

    # --- source UV (fixée au socle, ne touche pas le berceau)
    add('Source UV', 'Dissipateur', cbox(0, 0, zw0, 200, 120, 40))
    add('Source UV', 'Matrice LED 405 nm', cbox(0, 0, zw0 + 40, 180, 110, 3), mat='electronique', color='uv')
    add('Source UV', 'Ventilateur 80 mm', box(-bx + WALL + 1, -bx + WALL + 26, -40, 40, 30, 110), mat='plastique')
    fz = DECK_Z - 4 - 2
    add('Source UV', 'Lentille de Fresnel', cbox(0, 0, fz, DECK_WIN[0] - 2, DECK_WIN[1] - 2, 2), mat='pmma', color='verre', opacity=0.5)
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (1, 1), (-1, 1)]):
        add('Source UV', f'Colonnette Fresnel {i + 1}', cyl(sx * 112, sy * 66, zw0 + 40, fz, 3), mat='acier')

    # --- électronique
    add('Electronique', 'Alimentation 24 V 150 W', box(120, 158, -10, 150, zw0, zw0 + 98), mat='acier')
    add('Electronique', 'Carte mere MSLA', box(-150, -40, 85, 165, zw0, zw0 + 20), mat='electronique')
    add('Electronique', 'ESP32 de mesure', box(25, 80, 120, 148, zw0, zw0 + 12), mat='electronique')
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (1, 1), (-1, 1)]):
        add('Electronique', f'Convertisseur coin {i + 1}', cbox(sx * 102, sy * 116, deck_top, 34, 21, 3), mat='electronique')

    # --- 4 cellules sous le berceau (extrémité fixe vers le centre, appui vers l'extérieur)
    cz0 = deck_top + SPACER_T
    cz1 = cz0 + CELL[2]
    cradle_z0 = cz1 + SPACER_T
    names = ['avant gauche', 'avant droit', 'arrière droit', 'arrière gauche']
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (1, 1), (-1, 1)]):
        x_in, x_out = sx * CELL_X0, sx * (CELL_X0 + CELL[0])
        y = sy * CELL_Y
        add('Capteurs bac', f'Cellule 20 kg {names[i]}', box(x_in, x_out, y - CELL[1] / 2, y + CELL[1] / 2, cz0, cz1), color='capteur')
        add('Capteurs bac', f'Cale fixe {names[i]}', box(x_in, x_in + sx * 15, y - CELL[1] / 2, y + CELL[1] / 2, deck_top, cz0), mat='g10')
        add('Capteurs bac', f'Cale appui {names[i]}', box(x_out - sx * 15, x_out, y - CELL[1] / 2, y + CELL[1] / 2, cz1, cradle_z0), mat='g10')
    for i, sx in enumerate([-1, 1]):
        add('Capteurs bac', f'Butée de surcharge {i + 1}', box(sx * 125, sx * 138, -10, 10, deck_top, cradle_z0 - STOP_GAP), mat='acier')

    # --- berceau optique (pesé)
    cw, cd, ct = CRADLE
    cradle_z1 = cradle_z0 + ct
    add('Berceau', 'Plaque du berceau', box(-cw / 2, cw / 2, -cd / 2, cd / 2, cradle_z0, cradle_z1),
        cuts=[cbox(0, 0, cradle_z0 - 1, CRADLE_WIN[0], CRADLE_WIN[1], ct + 2)])
    sz1 = cradle_z1 + SCREEN[2]
    add('Berceau', 'Ecran LCD 10,1 pouces', cbox(0, 0, cradle_z1, SCREEN[0], SCREEN[1], SCREEN[2]), mat='verre',
        cuts=[cbox(0, 0, sz1 - 0.2, ACTIVE[0], ACTIVE[1], 1)])
    add('Berceau', 'Zone utile', cbox(0, 0, sz1 - 0.2, ACTIVE[0], ACTIVE[1], 0.2), mat='verre', color='plastique')
    gz1 = sz1 + GLASS[2]
    add('Berceau', 'Verre de protection', cbox(0, 0, sz1, GLASS[0], GLASS[1], GLASS[2]), mat='verre', opacity=0.4)

    # --- bac (posé sur le berceau, autour de l'écran)
    vw, vd, vh = VAT_IN
    ow, od = vw + 2 * VAT_WALL, vd + 2 * VAT_WALL
    fep_z0 = gz1
    fep_z1 = fep_z0 + FEP_T
    add('Bac', 'Cadre bas du bac', box(-ow / 2, ow / 2, -od / 2, od / 2, cradle_z1, fep_z0),
        cuts=[cbox(0, 0, cradle_z1 - 1, GLASS[0] + 4, GLASS[1] + 2, 20)])
    add('Bac', 'Parois du bac', box(-ow / 2, ow / 2, -od / 2, od / 2, fep_z0, fep_z1 + vh),
        cuts=[cbox(0, 0, fep_z0 - 1, vw, vd, vh + 10)])
    add('Bac', 'Film FEP', cbox(0, 0, fep_z0, vw, vd, FEP_T), mat='fep', opacity=0.5)
    for i, sx in enumerate([-1, 1]):
        add('Bac', f'Vis moletée {i + 1}', cyl(sx * (ow / 2 + 7), 0, cradle_z1, cradle_z1 + 14, 5), mat='plastique')

    # --- bras instrumenté (suit Z_POS)
    pz = fep_z1 + 0.05 + z_pos
    pw, pd, pt = PLATE
    add('Bras instrumente', 'Plateau', cbox(0, 0, pz, pw, pd, pt))
    rz = pz + pt
    add('Bras instrumente', 'Rotule et vis de blocage', cbox(0, 0, rz, *ROTULE), mat='acier')
    az = rz + ROTULE[2]
    add('Bras instrumente', 'Bras', cbox(0, 0, az, *ARM))
    sz = az + ARM[2]
    zc0 = sz + 5
    zc1 = zc0 + ZCELL[2]
    add('Bras instrumente', 'Cale extrémité libre', box(-15, 15, ZCELL_Y0, ZCELL_Y0 + 30, sz, zc0), mat='acier')
    add('Bras instrumente', 'Cellule 50 kg flexion parallèle', box(-ZCELL[0] / 2, ZCELL[0] / 2, ZCELL_Y0, ZCELL_Y0 + ZCELL[1], zc0, zc1), color='capteur')
    y_fix1 = ZCELL_Y0 + ZCELL[1]
    car_y1 = y_fix1 + 10
    add('Bras instrumente', 'Support extrémité fixe', box(-30, 30, y_fix1 - 30, y_fix1, zc0 - 22, zc0))
    add('Bras instrumente', 'Butée de surcharge Z (compression)', box(-15, 15, ZCELL_Y0, ZCELL_Y0 + 30, zc1 + 0.25, zc1 + 6), mat='acier')
    add('Bras instrumente', 'Equerre de butée', box(-15, 15, ZCELL_Y0 + 30, y_fix1, zc1 + 0.25, zc1 + 6), mat='acier')

    # --- chariot
    cpz0 = pz + 20
    add('Chariot', 'Plaque du chariot', box(-75, 75, y_fix1, car_y1, cpz0, cpz0 + 140))
    for i, (sx, zc) in enumerate([(-1, 50), (1, 50), (-1, 130), (1, 130)]):
        add('Chariot', f'Patin MGN15H {i + 1}', cbox(sx * RAIL_DX, (car_y1 + COL_Y0 - 10) / 2, pz + zc - 29, 32, COL_Y0 - 10 - car_y1, 58), mat='acier')
    add('Chariot', 'Ecrou de vis à billes', box(-17, 17, car_y1, COL_Y0 - 10, pz + 70, pz + 110), mat='acier',
        cuts=[cyl(0, SCREW_Y, pz + 69, pz + 111, 6.2)])

    # --- tour Z
    col_z0 = deck_top
    col_z1 = cradle_z0 + 20 + 160 + TRAVEL + 60
    add('Tour Z', 'Colonne', box(-COL[0] / 2, COL[0] / 2, COL_Y0, COL_Y0 + COL[1], col_z0, col_z1))
    add('Tour Z', 'Renfort arrière', box(-COL[0] / 2, COL[0] / 2, COL_Y0 + COL[1], by - 6, col_z0, col_z0 + 170))
    rail_z0 = fep_z1 + 0.05 + 20
    for i, sx in enumerate([-1, 1]):
        add('Tour Z', f'Rail MGN15 {i + 1}', box(sx * RAIL_DX - 7.5, sx * RAIL_DX + 7.5, COL_Y0 - 10, COL_Y0, rail_z0, rail_z0 + TRAVEL + 160 + 10), mat='acier')
    add('Tour Z', 'Vis à billes SFU1204', cyl(0, SCREW_Y, DECK_Z, col_z1 - 10, 6), mat='acier')
    add('Tour Z', 'Palier BF10', box(-20, 20, SCREW_Y - 10, SCREW_Y + 10, deck_top, deck_top + 20), mat='acier',
        cuts=[cyl(0, SCREW_Y, deck_top - 1, deck_top + 21, 6.2)])
    add('Tour Z', 'Support haut et palier BK10', box(-30, 30, SCREW_Y - 15, COL_Y0, col_z1 - 10, col_z1 + 10), mat='acier',
        cuts=[cyl(0, SCREW_Y, col_z1 - 11, col_z1 + 11, 6.2)])
    add('Tour Z', 'Accouplement', cyl(0, SCREW_Y, DECK_Z - 22, DECK_Z, 10), mat='acier')
    add('Tour Z', 'Moteur NEMA 17 boucle fermée', cbox(0, SCREW_Y, DECK_Z - 22 - 63, 42, 42, 63), mat='plastique')

    # --- capot filtrant les UV (creux)
    hz1 = col_z1 + 20
    add('Capot', 'Capot UV', box(-bx, bx, -by, by, deck_top, hz1),
        cuts=[box(-bx + 3, bx - 3, -by + 3, by - 3, deck_top - 1, hz1 - 3)], mat='pmma', opacity=0.25)
    return P


def bbox(shape):
    k, v = shape
    if k == 'box':
        return v
    cx, cy, z0, z1, r = v
    return (cx - r, cx + r, cy - r, cy + r, z0, z1)


def inside(shape, x, y, z):
    k, v = shape
    if k == 'box':
        return v[0] <= x <= v[1] and v[2] <= y <= v[3] and v[4] <= z <= v[5]
    cx, cy, z0, z1, r = v
    return z0 <= z <= z1 and (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def solid(part, x, y, z):
    return inside(part['shape'], x, y, z) and not any(inside(c, x, y, z) for c in part['cuts'])


def volume_cm3(part, n=24):
    """Volume approché (pour le test hors Fusion)."""
    b = bbox(part['shape'])
    dx, dy, dz = (b[1] - b[0]) / n, (b[3] - b[2]) / n, (b[5] - b[4]) / n
    c = 0
    for i in range(n):
        for j in range(n):
            for k in range(n):
                if solid(part, b[0] + (i + .5) * dx, b[2] + (j + .5) * dy, b[4] + (k + .5) * dz):
                    c += 1
    return c * dx * dy * dz / 1000


def cradle_report(mass_g):
    """Masse portée par les cellules et fréquence propre, à vide et avec la résine."""
    k = 4 * K_CELL * 1000                    # N/m
    m0 = mass_g / 1000
    m1 = m0 + RESIN_L * RESIN_RHO
    f = lambda m: math.sqrt(k / m) / (2 * math.pi)
    return m0, m1, f(m0), f(m1)


# ---------------------------------------------------------------- construction dans Fusion 360
def _cm(v):
    return v / 10.0


def _temp_body(tbm, shape):
    k, v = shape
    if k == 'box':
        x0, x1, y0, y1, z0, z1 = v
        c = adsk.core.Point3D.create(_cm((x0 + x1) / 2), _cm((y0 + y1) / 2), _cm((z0 + z1) / 2))
        obb = adsk.core.OrientedBoundingBox3D.create(c, adsk.core.Vector3D.create(1, 0, 0), adsk.core.Vector3D.create(0, 1, 0),
                                                     _cm(x1 - x0), _cm(y1 - y0), _cm(z1 - z0))
        return tbm.createBox(obb)
    cx, cy, z0, z1, r = v
    return tbm.createCylinderOrCone(adsk.core.Point3D.create(_cm(cx), _cm(cy), _cm(z0)), _cm(r),
                                    adsk.core.Point3D.create(_cm(cx), _cm(cy), _cm(z1)), _cm(r))


def _appearance(app, design, cache, key):
    """Apparence colorée créée par copie d'une apparence de la bibliothèque (sans effet si introuvable)."""
    if key in cache:
        return cache[key]
    cache[key] = None
    try:
        name = 'MSLA ' + key
        a = design.appearances.itemByName(name)
        if not a:
            base = None
            for lib in app.materialLibraries:
                if 'ppearance' not in lib.name:
                    continue
                for i in range(min(lib.appearances.count, 400)):
                    cand = lib.appearances.item(i)
                    if 'Plastic' in cand.name or 'Paint' in cand.name:
                        props = cand.appearanceProperties
                        if props.itemByName('Color') or props.itemById('opaque_albedo'):
                            base = cand
                            break
                if base:
                    break
            if not base:
                return None
            a = design.appearances.addByCopy(base, name)
            r, g, b = COLORS.get(key, (150, 150, 150))
            for pid in ('opaque_albedo', 'generic_diffuse'):
                p = a.appearanceProperties.itemById(pid)
                if p:
                    adsk.core.ColorProperty.cast(p).value = adsk.core.Color.create(r, g, b, 255)
                    break
            else:
                p = a.appearanceProperties.itemByName('Color')
                if p:
                    adsk.core.ColorProperty.cast(p).value = adsk.core.Color.create(r, g, b, 255)
        cache[key] = a
    except Exception:
        cache[key] = None
    return cache[key]


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.DirectDesignType
        design.fusionUnitsManager.distanceDisplayUnits = adsk.fusion.DistanceUnits.MillimeterDistanceUnits
        root = design.rootComponent
        tbm = adsk.fusion.TemporaryBRepManager.get()

        comps, cache = {}, {}
        mass_cradle = 0.0
        for p in parts(Z_POS):
            if p['comp'] not in comps:
                occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
                occ.component.name = p['comp']
                comps[p['comp']] = occ.component
            comp = comps[p['comp']]
            body = _temp_body(tbm, p['shape'])
            for c in p['cuts']:
                tbm.booleanOperation(body, _temp_body(tbm, c), adsk.fusion.BooleanTypes.DifferenceBooleanType)
            b = comp.bRepBodies.add(body)
            b.name = p['name']
            a = _appearance(app, design, cache, p['color'])
            if a:
                b.appearance = a
            if p['opacity'] < 1:
                try:
                    b.opacity = p['opacity']
                except Exception:
                    pass
            if p['comp'] in ('Berceau', 'Bac'):
                mass_cradle += b.volume * RHO.get(p['mat'], 2.7)     # volume en cm³

        app.activeViewport.fit()
        m0, m1, f0, f1 = cradle_report(mass_cradle)
        ui.messageBox(
            'Modèle créé : {} composants.\n\n'
            'Berceau pesé (écran + bac) : {:.2f} kg à vide, {:.2f} kg avec {:.1f} L de résine.\n'
            'Fréquence propre sur 4 cellules de {:.0f} N/mm : {:.0f} Hz à vide, {:.0f} Hz plein.\n'
            '(objectif > 100 Hz, loin du moteur 37 Hz, du ventilateur 47 Hz et du secteur 50 Hz)\n\n'
            'Espacement des cellules à saisir dans le firmware : bac {:.0f} {:.0f}'
            .format(len(comps), m0, m1, RESIN_L, K_CELL, f0, f1, 2 * (CELL_X0 + CELL[0] - 7.5), 2 * CELL_Y),
            'MSLA 10,1 pouces')
    except Exception:
        if ui:
            ui.messageBox('Erreur :\n{}'.format(traceback.format_exc()))
