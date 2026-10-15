"""analizza_wav.py - dove c'e' suono in una registrazione AMIGA_WAV.

AMIGA_WAV scrive campioni grezzi stereo a 16 bit, 48000 Hz (o un WAV con
intestazione): qui si stampano i tratti non silenziosi, con l'energia.

Uso: python tools/analizza_wav.py <file> [--hz 48000] [--estrai da a uscita.wav]
"""
import argparse
import math
import struct
import wave

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("--hz", type=int, default=48000)
ap.add_argument("--finestra", type=float, default=0.02, help="secondi per finestra")
ap.add_argument("--estrai", nargs=3, metavar=("DA", "A", "USCITA"))
arg = ap.parse_args()

dati = open(arg.file, "rb").read()
if dati[:4] == b"RIFF":
    dati = dati[44:]
n = len(dati) // 4
sx = struct.unpack_from(f"<{n * 2}h", dati)
print(f"{n} campioni stereo, {n / arg.hz:.2f} s")
w = int(arg.hz * arg.finestra)
tratti, dentro, inizio, picco = [], False, 0, 0
for i in range(0, n, w):
    blocco = sx[2 * i:2 * (i + w)]
    if not blocco:
        break
    rms = math.sqrt(sum(v * v for v in blocco) / len(blocco))
    if rms > 200 and not dentro:
        dentro, inizio, picco = True, i, rms
    elif dentro:
        picco = max(picco, rms)
        if rms <= 200:
            tratti.append((inizio, i, picco))
            dentro = False
if dentro:
    tratti.append((inizio, n, picco))
for a, b, p in tratti:
    print(f"  {a / arg.hz:8.2f} - {b / arg.hz:8.2f} s  ({(b - a) / arg.hz:5.2f} s)  rms max {p:7.0f}")
if arg.estrai:
    da, a = int(float(arg.estrai[0]) * arg.hz), int(float(arg.estrai[1]) * arg.hz)
    with wave.open(arg.estrai[2], "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(arg.hz)
        f.writeframes(dati[4 * da:4 * a])
    print(f"estratto {arg.estrai[2]}")
