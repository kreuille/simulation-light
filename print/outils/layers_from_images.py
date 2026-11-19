#!/usr/bin/env python3
"""
Computes, layer by layer, the exposed area of a resin print from the layer images
(PNG), for the simulator (profile "Printed part" -> "Imported from a layer file").

The command-line help and messages below are kept in French, for the operator.

Where the images come from:
  - UVtools: open the sliced file (.ctb, .goo, .pwmx, .prz, ...),
    menu "File > Extract file contents" or "Export layers to images" -> folder of PNG;
  - or a .zip / .sl1 file that already contains the PNG (Lychee, PrusaSlicer, Chitubox export zip).

Install:   pip install numpy pillow        (scipy optional: counts islands)
Exemples :
  python layers_from_images.py couches/ --pixel-um 34.4 --sortie piece_couches.csv
  python layers_from_images.py export.zip --res 5760 3600 --ecran 198.15 123.84

Le CSV produit : layer, area_mm2, cx_mm, cy_mm, islands
"""
import argparse
import io
import re
import sys
import zipfile
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("Il manque numpy ou pillow : lance « pip install numpy pillow ».")

try:
    from scipy import ndimage
except ImportError:
    ndimage = None


def natural_key(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def iter_images(source):
    p = Path(source)
    if p.is_dir():
        files = sorted([f for f in p.iterdir() if f.suffix.lower() == ".png"], key=lambda f: natural_key(f.name))
        for f in files:
            yield f.name, Image.open(f)
    elif zipfile.is_zipfile(p):
        with zipfile.ZipFile(p) as z:
            names = sorted([n for n in z.namelist() if n.lower().endswith(".png") and "thumbnail" not in n.lower() and "preview" not in n.lower()], key=natural_key)
            for n in names:
                yield n, Image.open(io.BytesIO(z.read(n)))
    else:
        sys.exit(f"{source} n'est ni un dossier ni une archive zip.")


def main():
    ap = argparse.ArgumentParser(description="Surface exposée par couche à partir des PNG de couches.")
    ap.add_argument("source", help="dossier de PNG ou archive .zip/.sl1")
    ap.add_argument("--pixel-um", type=float, help="taille d'un pixel de l'écran en micromètres (ex. 34.4, 28.5, 22)")
    ap.add_argument("--res", type=int, nargs=2, metavar=("LARGEUR_PX", "HAUTEUR_PX"), help="résolution de l'écran")
    ap.add_argument("--ecran", type=float, nargs=2, metavar=("LARGEUR_MM", "HAUTEUR_MM"), help="dimensions utiles de l'écran")
    ap.add_argument("--seuil", type=int, default=128, help="niveau de gris compté comme exposé (0-255)")
    ap.add_argument("--sortie", default="couches.csv")
    a = ap.parse_args()

    px_x = px_y = None
    if a.pixel_um:
        px_x = px_y = a.pixel_um / 1000.0
    elif a.res and a.ecran:
        px_x, px_y = a.ecran[0] / a.res[0], a.ecran[1] / a.res[1]
    else:
        sys.exit("Indique la taille du pixel : --pixel-um, ou --res et --ecran.")

    rows = []
    for i, (name, img) in enumerate(iter_images(a.source)):
        g = np.asarray(img.convert("L"))
        m = g >= a.seuil
        n = int(m.sum())
        area = n * px_x * px_y
        if n:
            ys, xs = np.nonzero(m)
            cx, cy = xs.mean() * px_x, (g.shape[0] - ys.mean()) * px_y
        else:
            cx = cy = float("nan")
        islands = ""
        if ndimage is not None and n:
            islands = int(ndimage.label(m)[1])
        rows.append((i, area, cx, cy, islands))
        if i % 50 == 0:
            print(f"couche {i} ({name}) : {area:.1f} mm²")

    if not rows:
        sys.exit("Aucune image PNG trouvée.")
    with open(a.sortie, "w", encoding="utf-8") as f:
        f.write("layer,area_mm2,cx_mm,cy_mm,islands\n")
        for r in rows:
            f.write(f"{r[0]},{r[1]:.3f},{r[2]:.2f},{r[3]:.2f},{r[4]}\n")
    print(f"{len(rows)} couches, surface maximale {max(r[1] for r in rows):.0f} mm², fichier : {a.sortie}")


if __name__ == "__main__":
    main()
