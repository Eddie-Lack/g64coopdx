#!/usr/bin/env python3
"""SOLO's save file: the online program plays alone, the game saves, the program quits and starts again.

Each run starts the online program in a window (under Xvfb), picks SOLO on the start screen and plays an input
script at the file select. The script copies a file with the file select's COPY SLOT, and the game writes its
EEPROM. The test reads the save file after each run.

    tests/rom/solosave.py --rom ROM --exe weemitt/build/studio64/weemitt-studio
    tests/rom/solosave.py --rom ROM --exe weemitt/build/studio-windows64/weemitt-studio.exe --wine wine

On Linux the save is the program's own file, eeprom-solo.bin in its folder (XDG_DATA_HOME points at a scratch
folder). Under Wine the test names the file with --save. The runs make their save files in a scratch folder, which
the test deletes at the end. Save files are never committed.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile

SLOT_BASE = 0x1F * 8   # EepromSaveSlots writes the six files at block 0x1F
SLOT_SIZE = 0x1C
CRC_BASE = 0x34 * 8    # EepromSaveCRC writes one checksum a file, then two more
ALL_LEVELS = 0x3FFFFFFF

# Stick moves and A presses, each held a few frames, with time between them for the menu's animation.
A = "8000:0:0"
DOWN = "0:0:-80"
RIGHT = "0:80:0"


def script(steps):
    lines = ["0:0:0:0"]
    frame = 100
    for s in steps:
        lines.append("%d:%s" % (frame, s))
        lines.append("%d:0:0:0" % (frame + 6))
        frame += 90
    return "\n".join(lines) + "\n", frame + 120


# COPY SLOT is the menu's third row. After it, the glove is on the last file that has a game: file 2 when only
# CHOSIS has one, file 3 when it also has one. A picks that file, and the next file to the right is the copy's.
COPY_NEXT = [DOWN, DOWN, A, A, RIGHT, A]


def slot(data, i):
    return data[SLOT_BASE + i * SLOT_SIZE:SLOT_BASE + (i + 1) * SLOT_SIZE]


def is_chosis(s):
    return s[0:3] == b"CHO" and int.from_bytes(s[4:8], "little") == ALL_LEVELS and \
        int.from_bytes(s[8:12], "little") == ALL_LEVELS


def is_empty(s):
    return s[0] == 13


class Runner:
    def __init__(self, args, work):
        self.args = args
        self.work = work
        self.n = 0
        self.save = os.path.join(work, "g64coopdx", "Glover", "eeprom-solo.bin")

    def run(self, steps):
        self.n += 1
        text, quit_frames = script(steps)
        path = os.path.join(self.work, "input%d.txt" % self.n)
        with open(path, "w") as f:
            f.write(text)
        env = dict(os.environ)
        env.update({"XDG_DATA_HOME": self.work, "G64_LAUNCHER_KEYS": "down,down,down,a",
                    "WEEMITT_INPUT_SCRIPT": path, "WEEMITT_QUIT_FRAMES": str(quit_frames),
                    "WEEMITT_WINDOW_SIZE": "640x480", "SDL_AUDIODRIVER": "dummy", "WINEDEBUG": "-all"})
        cmd = [self.args.exe, "--rom", self.args.rom]
        if self.args.wine:
            # Wine's folder for the program is in its prefix: the test names the save file.
            self.save = os.path.join(self.work, "eeprom-solo.bin")
            env["WEEMITT_INPUT_SCRIPT"] = "Z:" + path.replace("/", "\\")
            cmd = [self.args.wine, self.args.exe, "--rom", "Z:" + os.path.abspath(self.args.rom).replace("/", "\\"),
                   "--save", "Z:" + self.save.replace("/", "\\"), "--launcher"]
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"] + cmd
        out = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        log = out.stdout.decode("utf-8", "replace")
        if out.returncode != 0:
            sys.exit("run %d: the program ended with %d\n%s" % (self.n, out.returncode, log[-2000:]))
        if os.path.exists(self.save + ".tmp"):
            sys.exit("run %d: the temporary file is left" % self.n)
        return log

    def read(self):
        with open(self.save, "rb") as f:
            data = f.read()
        if len(data) != 512:
            sys.exit("run %d: the save file has %d bytes, not 512" % (self.n, len(data)))
        return bytearray(data)

    def write(self, data):
        with open(self.save, "wb") as f:
            f.write(data)


def check(ok, what):
    print("%s: %s" % ("ok" if ok else "FAILED", what))
    if not ok:
        sys.exit(1)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rom", default=os.environ.get("GLOVER_ROM"))
    p.add_argument("--exe", required=True)
    p.add_argument("--wine")
    p.add_argument("--keep", action="store_true", help="keep the scratch folder")
    args = p.parse_args()
    if not args.rom or not os.path.exists(args.rom):
        print("solosave: skipped, no ROM (set GLOVER_ROM)")
        return
    work = tempfile.mkdtemp(prefix="solosave-")
    try:
        r = Runner(args, work)

        # 1. No save file yet. File 2 is CHOSIS; its copy in file 3 is the player's progress.
        r.run(COPY_NEXT)
        d = r.read()
        check(is_chosis(slot(d, 1)), "first run: file 2 is CHOSIS on disk")
        check(slot(d, 2) == slot(d, 1), "first run: the copy in file 3 is saved")
        check(is_empty(slot(d, 0)) and all(is_empty(slot(d, i)) for i in (3, 4, 5)), "first run: the others are empty")

        # 2. File 2 on disk becomes an empty file with a right checksum. SOLO fills CHOSIS in its place again, and
        #    file 3 from the first run is still there to copy.
        d[SLOT_BASE + SLOT_SIZE:SLOT_BASE + 2 * SLOT_SIZE] = slot(d, 0)
        d[CRC_BASE + 4:CRC_BASE + 8] = d[CRC_BASE:CRC_BASE + 4]
        r.write(d)
        r.run(COPY_NEXT)
        d2 = r.read()
        check(slot(d2, 2) == slot(d, 2), "second run: file 3 from the first run is kept")
        check(slot(d2, 3) == slot(d, 2), "second run: its copy in file 4 is saved")
        check(is_chosis(slot(d2, 1)), "second run: file 2 is CHOSIS again")

        # 3. A short file that ends before the checksums: they read as zeros and don't match, so every file but
        #    CHOSIS is empty.
        r.write(bytes(d2[:CRC_BASE]))
        log = r.run(COPY_NEXT)
        d3 = r.read()
        check("is short" in log or args.wine, "short file: the log says so")
        check(is_chosis(slot(d3, 1)) and slot(d3, 2) == slot(d3, 1), "short file: CHOSIS and its copy are saved")
        check(all(is_empty(slot(d3, i)) for i in (0, 3, 4, 5)), "short file: the other files are empty")

        # 4. A byte changed in file 3: its checksum is wrong, so it reads as empty and the copy goes into it.
        d3[SLOT_BASE + 2 * SLOT_SIZE + 12] ^= 0x55
        d3[SLOT_BASE + 3 * SLOT_SIZE:SLOT_BASE + 4 * SLOT_SIZE] = slot(d3, 1)
        crc2 = bytes(d3[CRC_BASE + 8:CRC_BASE + 12])
        r.write(d3)
        r.run(COPY_NEXT)
        d4 = r.read()
        check(is_chosis(slot(d4, 2)), "damaged file 3: it read as empty and holds the copy")
        check(is_empty(slot(d4, 3)), "damaged file 4 (no checksum): it read as empty")
        check(bytes(d4[CRC_BASE + 8:CRC_BASE + 12]) == crc2, "damaged file 3: the new checksum is the copy's")

        # 5. Garbage: the cartridge id is wrong, so the game makes a new save at once.
        rnd = random.Random(64)
        r.write(bytes(rnd.randrange(256) for _ in range(512)))
        r.run([])
        d5 = r.read()
        check(d5[0:4] == d4[0:4], "garbage: the game wrote a new save with its id")
        check(all(is_empty(slot(d5, i)) for i in range(6)), "garbage: every file is empty on disk")
        others = [os.path.join(dp, f) for dp, _, fs in os.walk(work) for f in fs
                  if f.startswith("eeprom") and os.path.join(dp, f) != r.save]
        check(not others, "no other save file was written")
        print("solosave: all checks passed")
    finally:
        if args.keep:
            print("solosave: kept", work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
