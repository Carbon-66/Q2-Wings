# Wings for the Shanling Q2

A hill-gliding game for the Shanling Q2, running as a Q2 Pod card game. Pip slides down striped, paper-textured hills, flies off the tops, and hops from island to island before the sun sets. It's built on [Tiny Wings Remake](https://github.com/haqu/tiny-wings) by Sergey Tikhonov (MIT), rewritten in C with a software renderer for the Q2's screen.

## Install

1. Copy the `.wings` folder to the root of the card. On a Mac, press ⌘⇧. in Finder to see dot-folders.
2. Eject the card and open **Library → Games → Wings**.

## Controls

| Input | Action |
|---|---|
| Touch and hold anywhere, or hold **Centre** | Dive: Pip gets heavy and speeds down slopes |
| Let go | Fly: Pip launches off the next rise |
| Pause button (top right), **Play/Pause** | Pause |
| Hold **Return** | Leave the game |

## How to play

- Hold on the way down a hill and let go before the next one rises.
- Land smoothly on a downslope, then fly off the next rise: that's a **perfect slide**. Three in a row starts **fever** (double points).
- Coins, long flights and each new island also score.
- The sun is your clock. It sinks as the day runs out, and each island you reach gives some daylight back. When night falls Pip gets sleepy, and the run ends when Pip stops.

## Features

- Procedural islands that get longer and steeper, each with its own random stripe colours
- Hills, sky and grain textures in the style of the remake: stripes, a dark gradient, a pale highlight and an edge line
- The sky and hills change through the day: blue, then gold, then sunset red, then a starry night with a moon
- Camera zoom as Pip climbs, clouds and far hills drifting behind, and a sea between islands
- Best score saved to `best.txt`; `wings.log` records each run and its frame rate
- No sound: your music keeps playing while you play

## Building

```sh
./build.sh        # the card folder, in dist/.wings
./test/run.sh     # tests on a PC: full simulated runs, and the real program on a fake Q2 screen
```

Needs `clang`, `ld.lld` and `python3`. `tools/gen_assets.py` (needs Pillow) regenerates `src/assets.h` and the icons.

## Credits

- Game base: [Tiny Wings Remake](https://github.com/haqu/tiny-wings) by Sergey Tikhonov, MIT licence (see `LICENSE`). The paper grain texture comes from it.
- Pip and the icons are original art for this port.
- Font: Poppins, SIL Open Font License 1.1.
- Build approach (no MIPS sysroot, stub libraries, the entry point) from [q2-doom](https://github.com/Carbon-66/q2-doom).
