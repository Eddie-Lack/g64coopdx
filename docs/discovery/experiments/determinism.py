#!/usr/bin/env python3
"""Runs a weemitt headless build (or the library's test host) twice with the same input and compares the trace and
the memory hash at every frame. With --other EXE, also compares a second build with the first.

Set WEEMITT to a weemitt checkout (default ../weemitt): the input generator is tools/regress.py's.
Usage: determinism.py --rom ROM --exe EXE [--other EXE2 [--other-prefix 'wine']] [--frames N] LEVEL... (or attract)
Input is the same generator that weemitt's tools/regress.py uses, so it covers walking, jumps, the ball buttons,
the camera and the pause menu.
"""
import argparse, os, pathlib, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.join(os.environ.get("WEEMITT", "../weemitt"), "tools"))
import regress  # noqa: E402  (only input_script is used)
HASH_EVERY = int(os.environ.get("HASH_EVERY", "5"))


def run(cmd_prefix, exe, rom, name, frames, work, tag, env_extra=None):
    save = work / f"{name}-{tag}.save"
    if save.exists():
        save.unlink()
    cmd = [*cmd_prefix, str(exe), "--rom", rom, "--headless", str(frames), "--trace", "--hash-every", str(HASH_EVERY),
           "--save", str(save)]
    if name != "attract":
        level = int(name)
        script = work / f"{name}.input"
        script.write_text(regress.input_script(level, frames))
        cmd += ["--level", str(level), "--input", str(script)]
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    t = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    dt = time.time() - t
    trace, hashes = [], []
    for l in r.stdout.splitlines():
        if not l.startswith("frame "):
            continue
        if " hash " in l:
            l, h = l.rsplit(" hash ", 1)
            hashes.append(h.strip())
        if " fc " in l:
            trace.append(l.rstrip())
    return trace, hashes, dt, r.returncode


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return None if len(a) == len(b) else min(len(a), len(b))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rom", required=True)
    p.add_argument("--exe", required=True)
    p.add_argument("--other")
    p.add_argument("--other-prefix", default="")
    p.add_argument("--other-env", default="")
    p.add_argument("--frames", type=int, default=6000)
    p.add_argument("--no-hash-compare", action="store_true", help="compare only traces with --other")
    p.add_argument("names", nargs="+")
    a = p.parse_args()
    work = pathlib.Path(tempfile.mkdtemp(prefix="det-"))  # saves and inputs; outside the repository
    for name in a.names:
        t1, h1, d1, rc1 = run([], a.exe, a.rom, name, a.frames, work, "a")
        t2, h2, d2, rc2 = run([], a.exe, a.rom, name, a.frames, work, "b")
        ft, fh = first_diff(t1, t2), first_diff(h1, h2)
        print(f"{name}: {len(t1)} frames, run A {d1:.1f}s rc {rc1}, run B {d2:.1f}s rc {rc2}; "
              f"trace {'same' if ft is None else f'differs at {ft}'}; "
              f"hash {'same' if fh is None else f'differs at {fh}'} ({len(h1)} hashes)", flush=True)
        if a.other:
            env = dict(kv.split("=", 1) for kv in a.other_env.split(",") if kv)
            t3, h3, d3, rc3 = run(a.other_prefix.split(), a.other, a.rom, name, a.frames, work, "o", env)
            ft, fh = first_diff(t1, t3), first_diff(h1, h3)
            msg = f"  other build: {len(t3)} frames, {d3:.1f}s rc {rc3}; trace " + \
                ("same" if ft is None else f"differs at {ft}")
            if not a.no_hash_compare:
                msg += "; hash " + ("same" if fh is None else f"differs at {fh}")
            print(msg, flush=True)
            if ft is not None and ft < len(t1) and ft < len(t3):
                print("   A:", t1[ft]); print("   O:", t3[ft])


main()
