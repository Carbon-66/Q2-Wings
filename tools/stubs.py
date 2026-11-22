#!/usr/bin/env python3
"""Stub shared libraries to link q2-wings against: each has the real library's soname and the
symbols q2-wings imports, so lld writes the right NEEDED entries and dynamic relocations. On the
Q2, the rootfs's own glibc 2.28 and libm resolve them. Usage: stubs.py OUTDIR

Versions: an unversioned reference binds to a symbol's oldest version. On the Q2's MIPS glibc,
fopen, fclose and pthread_create have an old GLIBC_2.0 compatibility version beside the current
one (q2-doom found that this breaks stdio); q2-wings uses none of them (files go through open,
read and write), and every symbol here exists at one version only, so all stay unversioned.
If you add a libc or libm call, add it here too (the link fails until you do)."""
import pathlib, subprocess, sys

LIBS = {
    'libc.so.6': '''__libc_start_main __errno_location bind chdir clock_gettime close ioctl memmove
        memset memcpy mmap open read recv rename signal snprintf socket strcpy strrchr unlink
        usleep vsnprintf write'''.split(),
    'libm.so.6': 'sinf cosf sincosf atanf atan2f floorf ceilf fmodf'.split(),
}

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for soname, names in LIBS.items():
    c = out / (soname + '.c')
    c.write_text(''.join(f'void {t}(void) {{}}\n' for t in names))
    subprocess.run(['clang', '--target=mipsel-linux-gnu', '-march=mips32r2', '-mabi=32', '-mfp64',
                    '-mnan=2008', '-fPIC', '-nostdlib', '-shared', '-fuse-ld=lld', '-fno-builtin', '-w',
                    f'-Wl,-soname,{soname}', '-o', out / soname, c], check=True)
