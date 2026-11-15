"""The Linux and Windows builds of the copy's game give the same state and memory hash every frame.

It runs weemitt's regression runs (the attract mode and a start of every level, with regress.py's input) twice:
on the 64-bit Linux headless build, and on the 64-bit Windows library under Wine through weemitt's test host. Each
frame's state trace and each memory hash (every 10 frames) must be equal.

Usage: python3 tests/rom/crossbuild.py [--rom ROM] [--frames N] [--runs NAME...] [--jobs N]
  make crossbuild builds what it needs and runs it. Without a ROM it prints that it was skipped and exits with 0.
  Wine comes from $WINE64 (default: wine64 on PATH, else wine), with $WINEPREFIX64 (default: ~/.wine64).

Both runs start from an empty save file of their own: the save is in the game's memory, so the hash covers it.
"""
import argparse
import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
W = ROOT / "weemitt"
sys.path.insert(0, str(W / "tools"))
import regress  # noqa: E402

LINUX = W / "build" / "headless64" / "weemitt"
HOST = W / "build" / "libhost-windows64" / "glover-host.exe"
DLL = W / "build" / "lib-windows64-stub" / "glover.dll"


def wine():
    exe = os.environ.get("WINE64") or shutil.which("wine64") or shutil.which("wine") or "/usr/lib/wine/wine64"
    env = {"WINEDEBUG": "-all", "WINEPREFIX": os.environ.get("WINEPREFIX64", os.path.expanduser("~/.wine64")),
           "GLOVER_LIB": str(DLL)}
    return exe, env


def trace(cmd, rom, name, args, frames, work, tag, env=None):
    """The 'frame' lines of one run and its exit code."""
    save = work / f"{name}-{tag}.sav"
    save.write_bytes(b"")
    full = [*cmd, "--rom", rom, "--headless", str(frames), "--trace", "--hash-every", "10", "--save", str(save),
            *args]
    if name.startswith("level"):
        script = work / f"{name}.input"
        if not script.exists():
            script.write_text(regress.input_script(int(name[5:]), frames))
        full += ["--input", str(script)]
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run(full, capture_output=True, text=True, env=e)
    return [l for l in r.stdout.splitlines() if l.startswith("frame ")], r.returncode


def compare(name, a, b):
    (la, ra), (lb, rb) = a, b
    if ra or rb:
        return f"exit {ra} on Linux and {rb} on Windows"
    if not la:
        return "no frames"
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            return f"first difference at frame {i}:\n    {x}\n    {y}"
    if len(la) != len(lb):
        return f"{len(la)} frames on Linux and {len(lb)} on Windows"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=regress.default_rom())
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--runs", nargs="*")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args()
    if not pathlib.Path(a.rom).exists():
        print(f"skipped: no ROM at {a.rom} (set GLOVER_ROM or pass --rom)")
        return 0
    for f in (LINUX, HOST, DLL):
        if not f.exists():
            sys.exit(f"{f} is missing: run make crossbuild")
    wexe, wenv = wine()
    todo = [r for r in regress.runs(a.frames) if not a.runs or r[0] in a.runs]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        work = pathlib.Path(tmp)
        jobs = {}
        for name, args, frames in todo:
            jobs[name] = (ex.submit(trace, [str(LINUX)], a.rom, name, args, frames, work, "linux"),
                          ex.submit(trace, [wexe, str(HOST)], a.rom, name, args, frames, work, "windows", wenv))
        for name in sorted(jobs):
            lin, win = (f.result() for f in jobs[name])
            d = compare(name, lin, win)
            if d:
                failed += 1
                print(f"{name}: {d}")
            else:
                print(f"{name}: {len(lin[0])} frames equal")
    print(f"{failed} of {len(todo)} runs differ between Linux and Windows")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
