#!/usr/bin/env python3
"""
MSLA force bench logger.

Reads the measurements sent by the ESP32 (banc_msla.ino firmware) on the USB port and
writes them to a CSV file, with a metadata header (machine, resin, temperature, notes).
The file loads as is into the simulator, section "Real data and fit".

The command-line help and messages below are kept in French, for the operator.

Install:   pip install pyserial
Examples:
  python logger.py --liste
  python logger.py --port COM5 --tare --machine "Saturn atelier" --resine "Grise std" --temp 24
  python logger.py --port /dev/ttyUSB0 --duree 1800 --note "test supports fins"

Stop: Ctrl+C (the firmware receives "stop" and the file is closed cleanly).
"""
import argparse
import datetime as dt
import json
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("Il manque pyserial : lance « pip install pyserial ».")


def main():
    ap = argparse.ArgumentParser(description="Enregistre les mesures du banc MSLA dans un CSV.")
    ap.add_argument("--port", help="port série (ex. COM5, /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--sortie", help="fichier CSV (par défaut : banc_AAAAMMJJ_HHMMSS.csv)")
    ap.add_argument("--machine", default="")
    ap.add_argument("--resine", default="")
    ap.add_argument("--temp", type=float, help="température de la résine (°C)")
    ap.add_argument("--note", default="")
    ap.add_argument("--duree", type=float, help="arrêt automatique après N secondes")
    ap.add_argument("--tare", action="store_true", help="envoie « tare » avant de démarrer")
    ap.add_argument("--liste", action="store_true", help="liste les ports série et quitte")
    a = ap.parse_args()

    if a.liste or not a.port:
        ports = list(list_ports.comports())
        if not ports:
            print("Aucun port série trouvé. Branche l'ESP32 en USB.")
        for p in ports:
            print(f"{p.device}\t{p.description}")
        if not a.port:
            print("\nIndique le port avec --port.")
        return

    out = a.sortie or dt.datetime.now().strftime("banc_%Y%m%d_%H%M%S.csv")
    meta = {"debut": dt.datetime.now().isoformat(timespec="seconds"), "machine": a.machine,
            "resine": a.resine, "temperature_C": a.temp, "note": a.note}

    ser = serial.Serial(a.port, a.baud, timeout=1)
    time.sleep(1.5)                 # the ESP32 restarts when the port is opened
    ser.reset_input_buffer()
    ser.write(b"stop\n")
    time.sleep(0.2)
    if a.tare:
        print("Tare en cours (ne touche à rien)…")
        ser.write(b"tare\n")
        time.sleep(1.5)
    ser.reset_input_buffer()
    ser.write(b"info\n")
    time.sleep(0.3)
    while ser.in_waiting:
        line = ser.readline().decode(errors="replace").strip()
        if line.startswith("#"):
            print(line)
    ser.write(b"start\n")

    n = 0
    t_start = time.time()
    last_print = 0
    header_written = False
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# meta " + json.dumps(meta, ensure_ascii=False) + "\n")
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").strip()
                if not line:
                    continue
                if line.startswith("#"):
                    f.write(line + "\n")
                    print(line)
                    continue
                if line.startswith("t_s"):
                    if not header_written:
                        f.write(line + "\n")
                        header_written = True
                    continue
                if not header_written:
                    continue
                f.write(line + "\n")
                n += 1
                now = time.time()
                if now - last_print > 1:
                    last_print = now
                    parts = line.split(",")
                    print(f"\r{n:>8} mesures  t={parts[0]} s  Fz={parts[1]} N  z={parts[-3]} mm  UV={parts[-2]}   ", end="")
                if a.duree and now - t_start > a.duree:
                    break
        except KeyboardInterrupt:
            pass
        finally:
            ser.write(b"stop\n")
            ser.close()
    print(f"\n{n} mesures enregistrées dans {out}")


if __name__ == "__main__":
    main()
