/* Host simulation: the game with the autopilot holding the button, at 60 fps, with frames saved
 * as raw 375 x 320 BGRX for test/frames.py. Prints how the run went.
 *   sim OUTDIR SEED [frame numbers to save...]   (no frames: just the run) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/game.h"

static uint32_t px[VIEW_W * VIEW_H];

static int want(int f, int argc, char **argv) {
    for (int i = 3; i < argc; ++i)
        if (atoi(argv[i]) == f) return 1;
    return 0;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 1;
    game_init(seed, 1234);
    GameInfo gi;
    int held = 0, f, max_island = 1, frames = 0;
    double render_ms = 0;
    float max_vx = 0, max_y = 0;
    for (f = 0; f < 60 * 400; ++f) {
        game_info(&gi);
        if (f == 90) game_press(1), game_press(0); /* wake Pip */
        if (getenv("SIM_FEVER") && f % 300 == 0 && f) game_test_fever();
        if (f > 90 && gi.state != 3) {
            int want_held = game_autopilot();
            if (want_held != held) game_press(want_held), held = want_held;
        }
        if (gi.state == 3 && f > 0) {
            static int over_at;
            if (!over_at) over_at = f;
            if (f - over_at > 150) break;
        }
        game_update(1.0f / 60);
        game_info(&gi);
        if (gi.vx > max_vx) max_vx = gi.vx;
        if (gi.y > max_y) max_y = gi.y;
        static int fever_seen;
        if (gi.fever && !fever_seen) fever_seen = 1, printf("  frame %5d: fever\n", f);
        if (gi.island > max_island) {
            max_island = gi.island;
            printf("  frame %5d: island %d, score %d, %.0f s left\n", f, gi.island, gi.score, gi.time_left);
        }
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        game_render(px);
        clock_gettime(CLOCK_MONOTONIC, &b);
        render_ms += (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        ++frames;
        if (getenv("SIM_CLIMB") && f > 200 && !gi.grounded && gi.vy > 350 && gi.vx < 600) {
            static int n;
            if (n++ == 6) { /* well into a steep climb */
                printf("  climb frame %d: vx %.0f vy %.0f\n", f, gi.vx, gi.vy);
                char path[256];
                snprintf(path, sizeof path, "%s/climb.raw", out);
                FILE *o = fopen(path, "wb");
                fwrite(px, 4, VIEW_W * VIEW_H, o);
                fclose(o);
            }
        }
        if (getenv("SIM_FALL") && f > 200 && !gi.grounded && gi.vy < -400 && gi.vx < 600) {
            static int m;
            if (m++ == 6) {
                printf("  fall frame %d: vx %.0f vy %.0f\n", f, gi.vx, gi.vy);
                char path[256];
                snprintf(path, sizeof path, "%s/fall.raw", out);
                FILE *o = fopen(path, "wb");
                fwrite(px, 4, VIEW_W * VIEW_H, o);
                fclose(o);
            }
        }
        if (getenv("SIM_MOUTH") && gi.mouth) {
            static int k;
            if (k++ % 40 == 5 && k < 300) {
                char path[256];
                snprintf(path, sizeof path, "%s/mouth%05d.raw", out, f);
                FILE *o = fopen(path, "wb");
                fwrite(px, 4, VIEW_W * VIEW_H, o);
                fclose(o);
            }
        }
        if (want(f, argc, argv)) {
            char path[256];
            snprintf(path, sizeof path, "%s/f%05d.raw", out, f);
            FILE *o = fopen(path, "wb");
            fwrite(px, 4, VIEW_W * VIEW_H, o);
            fclose(o);
        }
    }
    game_info(&gi);
    printf("run: %d frames (%.0f s), state %d, score %d, islands %d, perfect %d, coins %d, bumps %d, "
           "fastest %.0f, highest %.0f, render %.2f ms/frame\n",
           f, f / 60.0, gi.state, gi.score, gi.island, gi.perfects, gi.coins, gi.hits, max_vx, max_y,
           render_ms / frames);
    return gi.state == 3 ? 0 : 1;
}
