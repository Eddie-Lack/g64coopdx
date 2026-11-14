#!/usr/bin/env python3
"""Make the tables for the hand and ball globals from events.json (written by access.py).

Usage: python3 report.py [events.json] > tables.md
"""
import collections, json, os, sys

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "events.json")
ev = json.load(open(path))
defs = [e for e in ev if e["kind"] == "def"]
ev = [e for e in ev if e["kind"] != "def"]
alldefs = {(d["file"], d["func"]) for d in defs}

SYSTEM = {
    "hand.c": "glove", "ball.c": "ball", "ballworld.c": "ball", "camera.c": "camera", "controller.c": "input",
    "game.c": "game flow", "frontend.c": "game flow", "frontend2.c": "game flow", "title.c": "game flow",
    "present.c": "game flow", "main.c": "game flow", "credits.c": "game flow", "options.c": "game flow",
    "actor.c": "actors", "object.c": "actors", "anim.c": "actors", "collision.c": "actors", "platform.c": "actors",
    "enemy.c": "enemies", "nmeprocess.c": "enemies", "nmeprocess2.c": "enemies", "bullet.c": "enemies",
    "spell.c": "enemies", "boss.c": "bosses and bonus", "bonus.c": "bosses and bonus", "puzzle.c": "puzzles",
    "cameo.c": "cameos", "tip.c": "puzzles", "collect.c": "pick-ups", "cheat.c": "pick-ups",
    "water.c": "level features", "wind.c": "level features", "vent.c": "level features",
    "buzzer.c": "level features", "spike.c": "level features", "quake.c": "level features",
    "shadow.c": "effects", "footprint.c": "effects", "light.c": "effects", "blur.c": "effects",
    "weather.c": "effects", "debris.c": "effects", "overlay.c": "HUD and overlays", "text.c": "HUD and overlays",
    "personal.c": "debug", "eeprom.c": "save data", "parse.c": "level parser", "sound.c": "sound",
}
def system(f):
    return SYSTEM.get(os.path.basename(f), "other")

# Callee and argument positions that write the object they receive (judged from the prototypes' names and use).
WRITER = {
    "AnimateActor#0", "AddToOneScaledVector#0", "AddToOneScaledVector2D#0", "AccelerateActor2D#0", "AccelerateActor#0",
    "AccelerateActorAcrossPlane#0", "AccelerateActorFromActor#0", "SetVector2D#0", "SubVector2D#0", "AddVector2D#0",
    "AddScaledVector2D#0", "AddScaledVector#0", "AddOneScaledVector#0", "AddOneScaledVector2D#0", "AddToVector2D#0",
    "RotateVectorByQuaternion#0", "GetQuaternionFromXYZRot#0", "QuaternionMultiply#0", "QuatSlerp#0",
    "QuatSlerpAtFixedSpeed#0", "QuatSlerpAtFixedSpeedOld#0", "ActorHandleRotaim#0", "SquashActor#0", "ActorStartRoll#0",
    "DealWithMaxSpeed#0", "ActorMaxOutVel#0", "UpdateActor#0", "InitActorStructures#0", "InitActorAnim#0",
    "AddConveyorSpeedToVel#0", "GiveActorRandomQRotVel#0", "memclear#0", "SetAnimationSpeed#0", "AddVectorInline#0",
    "RotateVector2D#0", "MakeUnit2D#0", "MatrixToQuaternion#1", "CalculateQuatForPlane#0", "AddActor#0",
    "SubActor#0", "DestroyAndFreeActor#0", "CalcCollisionSpheres#0", "CheckCollisions#0", "AddScaledVector2D#0",
    "UpdateContinuousSample#0", "PlayContinuousSample#0", "StopContinuousSample#0", "GetRotationFromQuaternion#1",
    "MakeUniqueActor#0", "RemoveUniqueActor#0", "AddVectorInline#0", "CalcMaxActorRadius#0",
}

HAND = {"hand", "handControl"}
BALL = {"ball", "ballControl"}
OTHERS = ["player", "camera", "cameraInfo", "gameInfo", "pad", "saveSlot"]

def is_write(e):
    return e["kind"] in ("write", "write+read")

def addr_writer(e):
    return e["kind"] == "addr" and e["extra"].startswith("call:") and e["extra"][5:] in WRITER

out = []
w = out.append

def fset(pred):
    return {(e["file"], e["func"]) for e in ev if pred(e)}

w("## Per global (all of src/game, %d functions defined in %d files)\n" % (len(alldefs), len({d['file'] for d in defs})))
w("| Global | reads | writes | addr taken | of which to a writer callee | functions touching | functions writing |")
w("| --- | --- | --- | --- | --- | --- | --- |")
for g in ["hand", "handControl", "ball", "ballControl"] + OTHERS:
    es = [e for e in ev if e["var"] == g]
    r = sum(1 for e in es if e["kind"] in ("read", "write+read"))
    wr = sum(1 for e in es if is_write(e))
    ad = sum(1 for e in es if e["kind"] == "addr")
    aw = sum(1 for e in es if addr_writer(e))
    ft = {(e["file"], e["func"]) for e in es}
    fw = {(e["file"], e["func"]) for e in es if is_write(e) or addr_writer(e)}
    w("| `%s` | %d | %d | %d | %d | %d | %d |" % (g, r, wr, ad, aw, len(ft), len(fw)))

def anyg(gs):
    return lambda e: e["var"] in gs

H = fset(anyg(HAND)); B = fset(anyg(BALL)); Hn = fset(anyg({"hand"})); Bn = fset(anyg({"ball"}))
Hw = fset(lambda e: e["var"] in HAND and (is_write(e) or addr_writer(e)))
Bw = fset(lambda e: e["var"] in BALL and (is_write(e) or addr_writer(e)))
w("\n## Distinct functions\n")
w("| Set | functions |")
w("| --- | --- |")
w("| touch `hand` or `handControl` | %d |" % len(H))
w("| touch `ball` or `ballControl` | %d |" % len(B))
w("| touch both sides | %d |" % len(H & B))
w("| touch either side | %d |" % len(H | B))
w("| touch the `hand` Actor itself | %d |" % len(Hn))
w("| touch the `ball` Actor itself | %d |" % len(Bn))
w("| touch `hand` and `ball` Actors | %d |" % len(Hn & Bn))
w("| write hand side (incl. address to a writer callee) | %d |" % len(Hw))
w("| write ball side | %d |" % len(Bw))
w("| write both sides | %d |" % len(Hw & Bw))
w("| any of the 10 scanned globals | %d |" % len(fset(lambda e: True)))
w("| functions defined in src/game | %d |" % len(alldefs))

w("\n## By file\n")
w("| File | System | hand side fns | ball side fns | both | hand R | hand W | hand &(writer) | hand & other | ball R | ball W | ball &(writer) | ball & other |")
w("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
files = sorted({e["file"] for e in ev if e["var"] in HAND | BALL})
tot = collections.Counter()
rows = []
for f in files:
    row = collections.Counter()
    hf = {e["func"] for e in ev if e["file"] == f and e["var"] in HAND}
    bf = {e["func"] for e in ev if e["file"] == f and e["var"] in BALL}
    for e in ev:
        if e["file"] != f or e["var"] not in HAND | BALL:
            continue
        s = "h" if e["var"] in HAND else "b"
        if e["kind"] in ("read", "write+read"):
            row[s + "R"] += 1
        if is_write(e):
            row[s + "W"] += 1
        if e["kind"] == "addr":
            row[s + ("AW" if addr_writer(e) else "AO")] += 1
    rows.append((len(hf | bf), f, hf, bf, row))
    tot.update(row)
for n, f, hf, bf, row in sorted(rows, key=lambda t: -(t[4]["hW"] + t[4]["bW"] + t[4]["hR"] + t[4]["bR"])):
    w("| %s | %s | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d |" % (
        f, system(f), len(hf), len(bf), len(hf & bf), row["hR"], row["hW"], row["hAW"], row["hAO"], row["bR"],
        row["bW"], row["bAW"], row["bAO"]))
w("| **total** | | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d |" % (
    len(H), len(B), len(H & B), tot["hR"], tot["hW"], tot["hAW"], tot["hAO"], tot["bR"], tot["bW"], tot["bAW"], tot["bAO"]))

w("\n## By system (files outside hand.c and ball.c show who else reaches in)\n")
w("| System | hand side fns | ball side fns | hand R+W+& | ball R+W+& |")
w("| --- | --- | --- | --- | --- |")
bysys = collections.defaultdict(lambda: [set(), set(), 0, 0])
for e in ev:
    if e["var"] in HAND:
        bysys[system(e["file"])][0].add((e["file"], e["func"])); bysys[system(e["file"])][2] += 1
    if e["var"] in BALL:
        bysys[system(e["file"])][1].add((e["file"], e["func"])); bysys[system(e["file"])][3] += 1
for s, (hs, bs, hn, bn) in sorted(bysys.items(), key=lambda t: -(t[1][2] + t[1][3])):
    w("| %s | %d | %d | %d | %d |" % (s, len(hs), len(bs), hn, bn))

w("\n## Address uses (how the `&hand` / `&ball` / `&hand.pos` forms are used)\n")
w("| Use | hand side | ball side |")
w("| --- | --- | --- |")
uses = collections.Counter()
for e in ev:
    if e["kind"] == "addr" and e["var"] in HAND | BALL:
        u = e["extra"] if not e["extra"].startswith("call:") else ("call (writer)" if addr_writer(e) else "call (other)")
        uses[(u, "h" if e["var"] in HAND else "b")] += 1
for u in sorted({k[0] for k in uses}):
    w("| %s | %d | %d |" % (u, uses[(u, "h")], uses[(u, "b")]))
w("\nNotes: `cmp` is an identity test such as `actor == &hand`. `store-ptr` stores the address in a field (for example `cameraInfo.focus = &hand.pos`, `ball.link = &hand`). `assign` copies the address to a local or global pointer.")

w("\n## Most written fields\n")
w("| Global.field | writes (incl. ++, +=) | functions |")
w("| --- | --- | --- |")
fc = collections.Counter(); ff = collections.defaultdict(set)
for e in ev:
    if is_write(e) and e["var"] in HAND | BALL:
        k = "%s.%s" % (e["var"], e["field"] or "(whole)")
        fc[k] += 1; ff[k].add((e["file"], e["func"]))
for k, n in fc.most_common(30):
    w("| `%s` | %d | %d |" % (k, n, len(ff[k])))

w("\n## Functions that write both hand side and ball side\n")
w("| Function | File |")
w("| --- | --- |")
for f, fn in sorted(Hw & Bw):
    w("| %s | %s |" % (fn, f))

w("\n## Functions outside hand.c and ball.c that write hand side or ball side\n")
w("| Function | File | writes |")
w("| --- | --- | --- |")
for f, fn in sorted(Hw | Bw):
    if os.path.basename(f) in ("hand.c", "ball.c"):
        continue
    ws = sorted({"%s.%s" % (e["var"], e["field"] or "") for e in ev if e["file"] == f and e["func"] == fn and e["var"] in HAND | BALL and (is_write(e) or addr_writer(e))})
    w("| %s | %s | %s |" % (fn, f, ", ".join(ws)))

print("\n".join(out))
