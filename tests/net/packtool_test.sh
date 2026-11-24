#!/bin/sh
# The pack tool (tools/g64pack.c): it makes packs from a level file, with a script and with models, refuses what is
# not a pack, and gives the same ids as the pack reader's own test. The sample pack keeps its id. With GLOVER_ROM it
# also takes every level out of the ROM, and checks packs against the ROM's rules.
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
# Glovebox's cube (tests/fixtures), and a copy with a flag that the format does not have.
cp "$here/tests/fixtures/cube.models.bin" cube.bin
{ head -c 8 cube.bin; printf '\002'; tail -c +10 cube.bin; } > badflag.bin
: > empty.bin
cp -r "$here/packs/garib-island" garib

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
echo "a pack with models: its id starts with dfffdef0a1eb5c74, as the reader's test prints"
run 0 make m --name "A test level" --level 10 --lev test.lev --models cube.bin
echo "a folder that holds a models.bin is not made again without --models"
run 1 make m --name "No models named" --level 10 --lev test.lev
run 1 make c2 --name "ok" --level 1 --lev test.lev --models badflag.bin
run 1 make c2 --name "ok" --level 1 --lev test.lev --models empty.bin
run 1 make c2 --name "ok" --level 1 --lev test.lev --models missing.bin
echo "a pack whose models.bin changed is refused"
cp badflag.bin m/models.bin
run 1 check m
echo "a file that is not a ROM is refused for the ROM's rules"
run 1 check a --rom test.lev
run 1 check a --rom missing.z64
echo "the sample pack keeps its id"
id=$($tool check garib 2>&1 | tr -d '\r' | grep '^id:')
echo "$id"
if [ "$id" != "id: 7446d7ba27354ed8364cb61a0f067308" ]; then
    echo "FAIL: the sample pack's id changed"
    failed=$((failed + 1))
fi
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
    echo "the ROM's rules: a pack with the cube passes, and a pack with one of the ROM's levels does not"
    $tool make mr --name "A test level" --level 10 --lev test.lev --models cube.bin > /dev/null 2>&1
    run 0 check mr --rom "$GLOVER_ROM"
    run 0 check garib --rom "$GLOVER_ROM"
    run 1 check rom5 --rom "$GLOVER_ROM"
    echo "a ROM cut to 4 MiB: banks of both kinds, but some skipped; the tool refuses it, as the online program does"
    head -c 4194304 "$GLOVER_ROM" > short.z64
    run 1 check mr --rom short.z64
    rm -f short.z64
else
    echo "skipped: no ROM (the pack tool on a ROM)"
fi
if [ $failed = 0 ]; then
    echo "ok: 0 failures"
else
    echo "FAILED: $failed failures"
fi
exit $failed
