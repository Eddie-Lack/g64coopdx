#!/bin/sh
# The pack tool (tools/g64pack.c): it makes packs from a level file, refuses what is not a pack, and gives the same
# ids as the pack reader's own test. With GLOVER_ROM it also takes every level out of the ROM.
#
#   packtool_test.sh WORKDIR COMMAND...     COMMAND runs the tool: its path, or wine and the .exe
#
# The Linux and the Windows program must print the same (make test-windows).
dir=$1
shift
rm -rf "$dir"
mkdir -p "$dir"
here=$(pwd)
tool=""
for a in "$@"; do
    case "$a" in
    /*) tool="$tool $a" ;;
    */*) tool="$tool $here/$a" ;;
    *) tool="$tool $a" ;;
    esac
done
cd "$dir" || exit 1
failed=0
# Runs the tool, shows its output without carriage returns, and checks that it ended as expected (0 or not 0).
run() {
    want=$1
    shift
    out=$($tool "$@" 2>&1)
    code=$?
    printf '%s\n' "$out" | tr -d '\r'
    if [ "$want" = 0 ] && [ "$code" != 0 ]; then
        echo "FAIL: this should have worked: $*"
        failed=$((failed + 1))
    elif [ "$want" != 0 ] && [ "$code" = 0 ]; then
        echo "FAIL: this should have been refused: $*"
        failed=$((failed + 1))
    fi
}

# The level file of the pack reader's own test (tests/net/pack_test.c): 12 bytes, which start with their count.
printf '\000\000\000\014test.ln\000' > test.lev
printf 'function on_frame() end\n' > script.lua
printf '\033Lua' > compiled.lua
printf '\000\000\000\015test.ln\000' > wrong.lev

echo "a pack from a level file: its id starts with de99044ca5595dc7, as the reader's test prints"
run 0 make a --name "A test level" --level 10 --lev test.lev
run 0 check a
echo "the same with a script: its id starts with another value"
run 0 make b --name "A test level" --level 10 --lev test.lev --script script.lua
echo "what the tool refuses"
run 1 make c --name "A test level" --level 48 --lev test.lev
run 1 make c --name "" --level 1 --lev test.lev
run 1 make c --name "A name that is far too long for a pack" --level 1 --lev test.lev
run 1 make c --name "ok" --level 1 --lev wrong.lev
run 1 make c --name "ok" --level 1 --lev missing.lev
run 1 make c --name "ok" --level 1 --lev test.lev --script compiled.lua
run 1 make c --name "ok" --level 1 --lev test.lev --rom test.lev
run 1 make c --name "ok" --level 1 --rom test.lev
run 1 make c --name "ok" --level 1
run 1 make b --name "No script named" --level 10 --lev test.lev
run 1 check missing
run 0 levels

if [ -n "$GLOVER_ROM" ] && [ -f "$GLOVER_ROM" ]; then
    echo "every level of the ROM makes a pack"
    n=0
    while [ $n -lt 48 ]; do
        out=$($tool make "rom$n" --name "Level $n" --level $n --rom "$GLOVER_ROM" 2>&1) || {
            echo "FAIL: level $n of the ROM"
            failed=$((failed + 1))
        }
        printf '%s\n' "$out" | tr -d '\r' | grep '^id:'
        n=$((n + 1))
    done
    run 1 make romx --name "ok" --level 48 --rom "$GLOVER_ROM"
else
    echo "skipped: no ROM (the pack tool on a ROM)"
fi
if [ $failed = 0 ]; then
    echo "ok: 0 failures"
else
    echo "FAILED: $failed failures"
fi
exit $failed
