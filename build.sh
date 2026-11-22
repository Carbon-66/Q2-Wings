#!/bin/sh
# Builds q2-wings for the Shanling Q2 (Ingenic X2000: MIPS32r2 little-endian, o32 with FR=1 and
# NaN2008, glibc 2.28, as Q2 Pod's own helpers are built) and lays out the card folder.
#
#   ./build.sh   -> build/q2-wings and dist/.wings/ (copy .wings to the card's root)
#
# Needs clang, ld.lld and python3, no MIPS sysroot: sysroot/include declares what's used, and the
# stub libraries tools/stubs.py makes carry the real sonames, so on the Q2 the rootfs's own glibc
# and libm are what run. rt/start.c is the entry point crt1.o would give, and exports
# _IO_stdin_used: without it glibc takes the program for a libc5-era one. src/assets.h comes from
# tools/gen_assets.py (python3 with Pillow), and is checked in.
set -e
cd "$(dirname "$0")"

TARGET="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008"
# -G0: no $gp-relative small data. glibc calls main with its own $gp (libc is PIC), so a
# gp-relative global in our non-PIC code would land in libc's memory.
CFLAGS="$TARGET -mabicalls -fno-pic -G0 -nostdlibinc -isystem sysroot/include -O2 -fno-math-errno \
        -fno-strict-aliasing -ffunction-sections -fdata-sections -fno-stack-protector \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -Wall -Wextra -Wno-unused-parameter -Werror"

mkdir -p build/obj
python3 tools/stubs.py build/stubs
for src in src/game.c src/q2.c rt/start.c rt/rt.c; do
    clang $CFLAGS -c "$src" -o build/obj/$(basename "$src" .c).o
done

ld.lld -m elf32ltsmip -e __start --dynamic-linker /lib/ld-linux-mipsn8.so.1 \
    --image-base=0x400000 -z noexecstack --gc-sections --hash-style=sysv --build-id=none -s \
    --export-dynamic-symbol=_IO_stdin_used \
    --as-needed build/obj/*.o build/stubs/libm.so.6 build/stubs/libc.so.6 -o build/q2-wings

rm -rf dist
mkdir -p dist/.wings
cp build/q2-wings dist/.wings/q2-wings
cp card/q2app card/icon.png card/icon-40.png dist/.wings/
cp card/README.txt dist/.wings/README.txt
cp LICENSE dist/.wings/LICENSE.txt
cp card/OFL-Poppins.txt dist/.wings/
echo "built: build/q2-wings ($(wc -c < build/q2-wings) bytes), card folder: dist/.wings"
