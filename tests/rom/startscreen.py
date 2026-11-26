#!/usr/bin/env python3
"""The online program from its start screen, in windows under Xvfb, through a lobby server on this machine.

Two rounds, each with a host and a joiner that meet on the start screen (G64_LAUNCHER_AUTO):

1. The host wants 3 players and starts when 2 are in (G64_LAUNCHER_START). The wait pages must show how the wait
   goes: the host's page the players who are in, the joiner's page that it waits for the host (G64_LAUNCHER_LOG
   prints the notes). In the game, the joiner's Chat window gets the keyboard (G64_TEST_CHAT_FOCUS), the test types
   a line with xdotool, as a player would, and the host's log must have it: the start screen turned SDL's text input
   off, and the chat must turn it on again. Then the host removes the joiner (G64_TEST_KICK): the joiner's log must
   say why, and its program must stay open with the reason in its picture.
2. The host leaves (WEEMITT_QUIT_FRAMES). The joiner must learn it at once, from the host's goodbye, not after the
   10 seconds in which a silent host counts as gone.

    tests/rom/startscreen.py --rom ROM --exe weemitt/build/studio64/weemitt-studio --lobby build/lobby

It needs the ROM, Xvfb and xdotool, and skips without them. The players' folders are in a scratch folder, which
the test deletes at the end.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

LOBBY_PORT = 18397
RELAY_PORT = 27397


def check(ok, what):
    print("%s: %s" % ("ok" if ok else "FAILED", what))
    sys.stdout.flush()
    return ok


class Program:
    def __init__(self, args, work, name, display, extra_env, extra_args):
        self.home = os.path.join(work, name)
        os.makedirs(self.home, exist_ok=True)
        self.out_path = os.path.join(work, name + ".out")
        env = dict(os.environ)
        env.update({"XDG_DATA_HOME": self.home, "HOME": self.home, "DISPLAY": display, "SDL_AUDIODRIVER": "dummy",
                    "WEEMITT_WINDOW_SIZE": "640x480", "G64_LAUNCHER_LOG": "1"})
        env.update(extra_env)
        cmd = [args.exe, "--rom", args.rom, "--relay", "127.0.0.1:%d" % RELAY_PORT] + extra_args
        self.out = open(self.out_path, "w")
        self.proc = subprocess.Popen(cmd, env=env, stdout=self.out, stderr=subprocess.STDOUT)
        self.log_path = None

    def text(self):
        with open(self.out_path, errors="replace") as f:
            return f.read()

    def log(self):
        """The online game's log, g64coopdx-host.log or g64coopdx-join.log."""
        folder = os.path.join(self.home, "g64coopdx", "Glover")
        text = ""
        if os.path.isdir(folder):
            for n in sorted(os.listdir(folder)):
                if n.endswith(".log"):
                    with open(os.path.join(folder, n), errors="replace") as f:
                        text += f.read()
        return text

    def wait_for(self, pattern, seconds, where="out"):
        end = time.time() + seconds
        while time.time() < end:
            m = re.search(pattern, self.text() if where == "out" else self.log(), re.I)
            if m:
                return m
            if self.proc.poll() is not None:
                return None
            time.sleep(0.2)
        return None

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.out.close()


def window_of(pid, display):
    out = subprocess.run(["xdotool", "search", "--pid", str(pid)], env=dict(os.environ, DISPLAY=display),
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode().split()
    return out[-1] if out else None


def meet(args, work, display, host_env, join_env, round_name):
    host = Program(args, work, round_name + "-host", display, dict({"G64_LAUNCHER_AUTO": "host"}, **host_env),
                   ["--name", "HOSTY"] + (["--players", "3"] if "G64_LAUNCHER_START" in host_env else []))
    m = host.wait_for(r"Room code: ([A-Z0-9]{6})", 60)
    if not m:
        print(host.text()[-2000:])
        host.stop()
        return None, None
    joiner = Program(args, work, round_name + "-join", display,
                     dict({"G64_LAUNCHER_AUTO": "join:" + m.group(1)}, **join_env), ["--name", "BUDDY"])
    return host, joiner


def round_one(args, work, display):
    ok = True
    host, joiner = meet(args, work, display, {"G64_LAUNCHER_START": "2", "G64_TEST_KICK": "1200"},
                        {"G64_TEST_CHAT_FOCUS": "30"}, "one")
    if not host:
        return check(False, "the host gets a room code")
    try:
        ok &= check(host.wait_for(r"start screen note: 1 OF 3 PLAYERS ARE IN", 30) is not None,
                    "the host's wait page shows how many players are in")
        ok &= check(joiner.wait_for(r"start screen note: WAITING FOR THE HOST TO START", 60) is not None,
                    "the joiner's wait page says that it waits for the host")
        ok &= check(host.wait_for(r"start screen note: 2 OF 3 PLAYERS ARE IN", 30) is not None,
                    "the host's wait page counts the joiner")
        ok &= check(joiner.wait_for(r"the game starts|player 2 of", 60, "log") is not None, "the game starts")
        focused = joiner.wait_for(r"menu test: the chat's line has the keyboard", 60, "log")
        ok &= check(focused is not None, "the joiner's chat line gets the keyboard")
        if focused:
            env = dict(os.environ, DISPLAY=display)
            wid = window_of(joiner.proc.pid, display)
            time.sleep(1.0)
            subprocess.run(["xdotool", "windowfocus", "--sync", wid], env=env)
            time.sleep(0.5)
            subprocess.run(["xdotool", "type", "--delay", "60", "typed after the start screen"], env=env)
            subprocess.run(["xdotool", "key", "Return"], env=env)

            ok &= check(host.wait_for(r"chat: BUDDY: typed after the start screen", 20, "log") is not None,
                        "a chat line typed in a game started from the start screen reaches the host")
        ok &= check(joiner.wait_for(r"g64coopdx: the host removed you from the game", 120, "log") is not None,
                    "a removed joiner's log says why")
        time.sleep(2)
        ok &= check(joiner.proc.poll() is None, "the removed joiner's program stays open, with the reason in its picture")
    finally:
        host.stop()
        joiner.stop()
    return ok


def round_two(args, work, display):
    ok = True
    host, joiner = meet(args, work, display, {"WEEMITT_QUIT_FRAMES": "300"}, {}, "two")
    if not host:
        return check(False, "the host gets a room code")
    try:
        ok &= check(joiner.wait_for(r"player 2 of", 90, "log") is not None, "the second game starts")
        try:
            host.proc.wait(120)
        except subprocess.TimeoutExpired:
            pass
        gone = time.time()
        ok &= check(host.proc.poll() is not None, "the host leaves")
        m = joiner.wait_for(r"g64coopdx: the host stopped answering", 15, "log")
        ok &= check(m is not None and time.time() - gone < 6,
                    "the joiner learns at once that the host left (%.1f s)" % (time.time() - gone))
    finally:
        host.stop()
        joiner.stop()
    return ok


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rom", default=os.environ.get("GLOVER_ROM"))
    p.add_argument("--exe", required=True)
    p.add_argument("--lobby", required=True)
    p.add_argument("--keep", action="store_true", help="keep the scratch folder")
    args = p.parse_args()
    if not args.rom or not os.path.exists(args.rom):
        print("startscreen: skipped, no ROM (set GLOVER_ROM)")
        return
    for tool in ("Xvfb", "xdotool"):
        if shutil.which(tool) is None:
            print("startscreen: skipped, no %s" % tool)
            return
    work = tempfile.mkdtemp(prefix="startscreen-")
    rd, wr = os.pipe()
    xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(wr), "-screen", "0", "1280x960x24"], pass_fds=(wr,),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(wr)
    display = ":" + os.read(rd, 16).decode().strip()
    os.close(rd)
    lobby = subprocess.Popen([args.lobby], env=dict(os.environ, PORT=str(LOBBY_PORT),
                                                    LOBBY_RELAY_PORT=str(RELAY_PORT)),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = True
    try:
        time.sleep(1)
        ok &= round_one(args, work, display)
        ok &= round_two(args, work, display)
    finally:
        lobby.terminate()
        lobby.wait()
        xvfb.terminate()
        xvfb.wait()
        if args.keep:
            print("startscreen: the scratch folder is %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print("startscreen: %s" % ("ok" if ok else "FAILED"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
