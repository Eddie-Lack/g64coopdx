#!/usr/bin/env python3
"""The online program from its start screen, in windows under Xvfb, through a lobby server on this machine.

Three rounds. In the first two a host and a joiner meet on the start screen:

1. The host wants 3 players and starts when 2 are in (G64_LAUNCHER_START). The wait pages must show how the wait
   goes: the host's page the players who are in, the joiner's page that it waits for the host (G64_LAUNCHER_LOG
   prints the notes). In the game, the joiner's Chat window gets the keyboard (G64_TEST_CHAT_FOCUS), the test types
   a line with xdotool, as a player would, and the host's log must have it: the start screen turned SDL's text input
   off, and the chat must turn it on again. The chat key (Y) must open a line that sends too. Then the host removes
   the joiner (G64_TEST_KICK): the joiner's log must say why, and its program must stay open with the reason in its
   picture.
2. The joiner types its name with the controller's steps and the room code with stray letters (G64_LAUNCHER_KEYS),
   and joins. The host leaves (WEEMITT_QUIT_FRAMES). The joiner must learn it at once, from the host's goodbye, not
   after the 10 seconds in which a silent host counts as gone.
3. A pack alone, from HOST GAME and with --try-pack: the game starts in the pack's level. QUIT ends the program.

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
import json
import urllib.request

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


def junk_code(code):
    """The room code with letters that no code has (I, O, 0, 1) between its own: the CODE bar must drop them."""
    return "".join(c + "I0O1"[i % 4] for i, c in enumerate(code))


def meet(args, work, display, host_env, join_env, round_name, by_keys=False, join_name="BUDDY"):
    host = Program(args, work, round_name + "-host", display, dict({"G64_LAUNCHER_AUTO": "host"}, **host_env),
                   ["--name", "HOSTY"] + (["--players", "3"] if "G64_LAUNCHER_START" in host_env else []))
    m = host.wait_for(r"Room code: ([A-Z0-9]{6})", 60)
    if not m:
        print(host.text()[-2000:])
        host.stop()
        return None, None
    if by_keys:
        # A player with a controller: on OPTIONS, NAME, the old name goes, and the controller makes "BB" (A adds an
        # A, right makes it a B). Then JOIN GAME, and the code typed with stray letters.
        keys = ["down", "down", "a"] + ["back"] * 6 + ["pa", "pr", "pa", "pr", "b", "up", "a",
                                                      "t:" + junk_code(m.group(1)), "a"]
        joiner = Program(args, work, round_name + "-join", display, dict({"G64_LAUNCHER_KEYS": ",".join(keys)},
                                                                         **join_env), [])
    else:
        joiner = Program(args, work, round_name + "-join", display,
                         dict({"G64_LAUNCHER_AUTO": "join:" + m.group(1)}, **join_env),
                         ["--name", join_name] if join_name else [])
    return host, joiner


def round_one(args, work, display):
    ok = True
    # The joiner has no name: it's "Player 2". At frame 300 of the hub it takes the Atlantis door (G64_TEST_EXITS, the
    # same on both machines): the host's chat notes where it went.
    exits = {"G64_TEST_EXITS": "0:300:1:6"}
    host, joiner = meet(args, work, display, dict({"G64_LAUNCHER_START": "2", "G64_TEST_KICK": "1200"}, **exits),
                        dict({"G64_TEST_CHAT_FOCUS": "30"}, **exits), "one", join_name=None)
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

            ok &= check(host.wait_for(r"chat: Player 2: typed after the start screen", 20, "log") is not None,
                        "a chat line typed in a game started from the start screen reaches the host")
            # The chat key: Escape closes the menu, Y opens the chat's line, Enter sends it.
            subprocess.run(["xdotool", "key", "Escape"], env=env)
            time.sleep(1.0)
            subprocess.run(["xdotool", "key", "y"], env=env)
            time.sleep(1.5)
            subprocess.run(["xdotool", "type", "--delay", "60", "said with the chat key"], env=env)
            subprocess.run(["xdotool", "key", "Return"], env=env)
            ok &= check(host.wait_for(r"chat: Player 2: said with the chat key", 20, "log") is not None,
                        "the chat key opens a line that sends, without the key's own letter")
        ok &= check(host.wait_for(r"chat: \* Player 2 went to Wayroom", 60, "log") is not None,
                    "a player with no name is Player 2, and the chat notes their move to another level")
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
    host, joiner = meet(args, work, display, {"WEEMITT_QUIT_FRAMES": "600"}, {}, "two", by_keys=True)
    if not host:
        return check(False, "the host gets a room code")
    try:
        ok &= check(joiner.wait_for(r"player 2 of", 90, "log") is not None,
                    "a code typed with letters that no code has joins (the bar drops them)")
        name = os.path.join(joiner.home, "g64coopdx", "Glover", "name.txt")
        ok &= check(os.path.exists(name) and open(name).read().strip() == "BB",
                    "the controller types a name on the NAME bar")
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


def round_three(args, work, display):
    """A pack alone: from HOST GAME (PACK, then SOLO under START), and with --try-pack. The game starts at once in the
    pack's level. Then QUIT on the first page ends the program."""
    ok = True
    keys = ["a"] + ["down"] * 5 + ["right", "down", "down", "a"]
    one = Program(args, work, "three-screen", display, {"G64_LAUNCHER_KEYS": ",".join(keys)}, [])
    two = Program(args, work, "three-try", display, {}, ["--try-pack", "packs/garib-island"])
    try:
        ok &= check(one.wait_for(r"player 1 of 2, level 10,", 90, "log") is not None,
                    "HOST GAME's PACK and SOLO start the pack's level alone")
        ok &= check(two.wait_for(r"player 1 of 2, level 10,", 90, "log") is not None,
                    "--try-pack starts the pack's level alone")
    finally:
        one.stop()
        two.stop()
    ok &= pause_leave(args, work, display)
    ok &= hosting_leave(args, work, display)
    ok &= full_screen(args, work, display)
    quit_keys = ["down"] * 5 + ["a"]
    three = Program(args, work, "three-quit", display, {"G64_LAUNCHER_KEYS": ",".join(quit_keys)}, [])
    try:
        code = three.proc.wait(60)
    except subprocess.TimeoutExpired:
        code = None
    three.stop()
    ok &= check(code == 0, "QUIT on the first page ends the program")
    return ok


def hold(env, key, seconds=0.2):
    """A key held long enough for the game's frames to see it (20 a second in a level)."""
    subprocess.run(["xdotool", "keydown", key], env=env)
    time.sleep(seconds)
    subprocess.run(["xdotool", "keyup", key], env=env)
    time.sleep(0.4)


def pause_leave(args, work, display):
    """The pause menu's Leave asks once more: No keeps the game, Yes closes the program."""
    ok = True
    one = Program(args, work, "three-leave", display, {}, ["--try-pack", "packs/garib-island"])
    env = dict(os.environ, DISPLAY=display)
    try:
        if not check(one.wait_for(r"player 1 of 2, level 10,", 90, "log") is not None, "the pack's level starts"):
            return False
        time.sleep(3)
        subprocess.run(["xdotool", "windowfocus", "--sync", window_of(one.proc.pid, display)], env=env)
        time.sleep(0.5)
        # Start opens the menu; Down three times reaches Leave; A asks; A again answers No (chosen at first).
        for key in ["Return", "Down", "Down", "Down", "c", "c"]:
            hold(env, key)
        time.sleep(2)
        ok &= check(one.proc.poll() is None, "Leave asks first, and No keeps the game")
        # No leaves the menu open at Leave: A asks again, Up chooses Yes, A answers.
        for key in ["c", "Up", "c"]:
            hold(env, key)
        try:
            code = one.proc.wait(30)
        except subprocess.TimeoutExpired:
            code = None
        ok &= check(code == 0, "Leave, then Yes, closes the program")
    finally:
        one.stop()
    return ok


def rooms():
    with urllib.request.urlopen("http://127.0.0.1:%d/v1/info" % LOBBY_PORT, timeout=5) as r:
        return json.loads(r.read().decode())["relay"]["rooms"]


def hosting_leave(args, work, display):
    """Esc on the HOSTING page asks once more: NO keeps the room, YES closes it at the relay."""
    ok = True
    # Each key waits 15 of the start screen's frames: "w" is a key that does nothing. NO comes soon after the
    # question, YES much later.
    wait = ["w"] * 20
    keys = wait + ["b"] + wait + ["a"] + wait * 6 + ["b", "up", "a"] + wait
    host = Program(args, work, "three-hosting", display, {"G64_LAUNCHER_AUTO": "host",
                                                          "G64_LAUNCHER_KEYS": ",".join(keys)}, [])
    try:
        ok &= check(host.wait_for(r"Room code: ", 60) is not None, "the host gets a room")
        ok &= check(host.wait_for(r"start screen note: Leave\? The game ends for everyone\.", 60) is not None,
                    "Esc on HOSTING asks: Leave? The game ends for everyone.")
        time.sleep(25)
        kept = rooms()
        end = time.time() + 120
        while time.time() < end and rooms() != 0:
            time.sleep(0.5)
        ok &= check(kept == 1 and rooms() == 0, "NO keeps the room, and YES closes it at the relay")
    finally:
        host.stop()
    return ok


def full_screen(args, work, display):
    """F11 switches full screen, also on the start screen, and the settings keep the choice."""
    one = Program(args, work, "three-full", display, {}, ["--launcher"])
    env = dict(os.environ, DISPLAY=display)
    ok = True
    try:
        time.sleep(8)
        subprocess.run(["xdotool", "windowfocus", "--sync", window_of(one.proc.pid, display)], env=env)
        time.sleep(0.5)
        subprocess.run(["xdotool", "key", "F11"], env=env)
        time.sleep(2)
        path = os.path.join(one.home, "g64coopdx", "Glover", "settings.ini")
        text = open(path).read() if os.path.exists(path) else ""
        ok &= check("fullscreen=1" in text, "F11 is kept in the settings")
    finally:
        one.stop()
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
        ok &= round_three(args, work, display)
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
