#!/bin/sh
# Host tests:
#  1. sim: whole runs with the autopilot on three seeds, each must reach night and end
#  2. main(): the real q2.c under fbshim.c's fake /dev/fb0, in a folder laid out like the card,
#     driven through the input socket as Q2 Pod would: Centre wakes Pip, touches dive, the pause
#     button pauses, a TERM ends it cleanly with Q2 Pod's page back on show. Writes
#     test/out/main_*.png (the panel as the Q2 shows it).
set -e
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p $OUT test/out
CF="-O2 -g -Wall -Wextra -Wno-unused-parameter -Werror -fno-strict-aliasing"
clang $CF -DGAME_TEST src/game.c test/sim.c -o $OUT/sim -lm
clang $CF src/game.c src/q2.c -o $OUT/q2-wings -lm
clang -shared -fPIC -O1 test/fbshim.c -o $OUT/fbshim.so -ldl

echo "== sim"
for seed in 1 2 3; do $OUT/sim test/out "$seed" | tail -1; done

echo "== main()"
CARD=test/out/card-$$   # a fresh folder each run, left in test/out
mkdir -p $CARD/.wings
cp $OUT/q2-wings $CARD/.wings/q2-wings
echo 4321 > $CARD/.wings/best.txt
# (q2-wings removes a stale socket itself before binding)
send() { python3 -c "import socket,sys; s=socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); s.sendto(sys.argv[1].encode(), '/tmp/q2-wings.input.sock')" "$1"; }
shot() { cp test/out/main.raw "test/out/main_$1.raw"; python3 test/view.py "test/out/main_$1.raw" "test/out/main_$1.png"; }
FB_DUMP=$PWD/test/out/main.raw FB_FINAL=$PWD/test/out/main.final LD_PRELOAD=$PWD/$OUT/fbshim.so \
    $CARD/.wings/q2-wings >/dev/null 2>&1 & P=$!
sleep 1
[ -S /tmp/q2-wings.input.sock ] || { echo "FAIL: no input socket"; kill $P; exit 1; }
shot title
send "k d 218"; sleep 0.05; send "k u 218"      # Centre: wake Pip
sleep 1.5
for i in 1 2 3 4 5 6; do                         # dive with touches
    send "t d 200 200"; sleep 0.5; send "t m 210 205"; send "t u 210 205"; sleep 0.6
done
shot play
send "t d 340 30"; sleep 0.05; send "t u 340 30" # the pause button
sleep 0.4
shot paused
send "k d 218"; send "k u 218"                   # carry on
sleep 0.4
send "k d 170"; sleep 0.1; send "k u 170"        # a short Return: pause
sleep 0.3
for i in 1 2 3 4 5 6; do send "k d 170"; sleep 0.1; done   # Return held (repeats): stays paused
shot held_return
send "k u 170"; sleep 0.2                        # (a long hold: no toggle on release either)
# the pause menu with the wheel and Centre: Restart, then Keep playing; Restart again, then Restart
send "w -40"; sleep 0.1; send "k d 218"; send "k u 218"; sleep 0.3
shot confirm
send "k d 218"; send "k u 218"; sleep 0.3        # Keep playing (chosen first): back in the game
send "k d 171"; send "k u 171"; sleep 0.2        # Play/Pause: the menu again
send "w -40"; sleep 0.1; send "k d 218"; send "k u 218"; sleep 0.2   # Restart: asks
send "w -40"; sleep 0.1; send "k d 218"; send "k u 218"; sleep 0.4   # Restart
shot restarted
# and with taps: wake, pause with the button, tap Restart twice
send "k d 218"; send "k u 218"; sleep 1
send "t d 340 30"; send "t u 340 30"; sleep 0.3
shot menu
send "t d 187 195"; send "t u 187 195"; sleep 0.3
send "t d 187 195"; send "t u 187 195"; sleep 0.4
# Quit: wake, pause, the wheel down twice to Quit, Centre; Keep playing first, then Quit itself
send "k d 218"; send "k u 218"; sleep 0.8
send "k d 171"; send "k u 171"; sleep 0.2
send "w -30"; send "w -30"; sleep 0.1; send "k d 218"; send "k u 218"; sleep 0.3
shot quit_ask
send "k d 218"; send "k u 218"; sleep 0.3        # Keep playing
send "t d 340 30"; send "t u 340 30"; sleep 0.3  # pause again with the button
send "t d 187 239"; send "t u 187 239"; sleep 0.3   # tap Quit
send "w -30"; sleep 0.1; send "k d 218"           # choose Quit and hold Centre a while:
sleep 0.6
kill -0 $P 2>/dev/null || { echo "FAIL: left while Centre was still down"; exit 1; }
send "k u 218"                                   # it takes the release itself, then leaves
for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $P 2>/dev/null || break; sleep 0.1; done
if kill -0 $P 2>/dev/null; then kill -TERM $P; echo "FAIL: still running after Quit"; exit 1; fi
wait $P || { echo "FAIL: exit status $?"; exit 1; }
grep -q "Quit from the menu" $CARD/.wings/wings.log || { echo "FAIL: no quit in the log"; exit 1; }
[ ! -e /tmp/q2-wings.input.sock ] || { echo "FAIL: socket left behind"; exit 1; }
grep -q "yoffset 0 " test/out/main.final || { echo "FAIL: panel not back on Q2 Pod's page: $(cat test/out/main.final)"; exit 1; }
grep -q "best 4321" $CARD/.wings/wings.log || { echo "FAIL: best score not read"; cat $CARD/.wings/wings.log; exit 1; }
grep -q "Leaving" $CARD/.wings/wings.log || { echo "FAIL: no clean exit in the log"; exit 1; }
states=$(grep "^State:" $CARD/.wings/wings.log | sed 's/State: //' | tr '\n' ' ')
want="title playing paused playing paused restart? playing paused restart? title playing paused restart? title playing paused quit? playing paused quit? "
[ "$states" = "$want" ] || { echo "FAIL: states were: $states"; echo "      wanted:      $want"; exit 1; }
n=$(grep -c "pause/resume" $CARD/.wings/wings.log || true)
[ "$n" = 3 ] || { echo "FAIL: pause toggled $n times by keys (want 3: the short Return, Play/Pause twice)"; exit 1; }
echo "main: ok ($(cat test/out/main.final))"
cat $CARD/.wings/wings.log
