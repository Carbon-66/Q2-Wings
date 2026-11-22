/* Wings: a hill-gliding game for the Shanling Q2, built on Tiny Wings Remake (Sergey Tikhonov,
 * MIT: github.com/haqu/tiny-wings). From the remake: the hills (key points joined by half cosine
 * waves, growing an island at a time), their striped and grained texture (stripes, a gradient
 * to dark below, a pale highlight at the top, an edge line, the paper grain twice), the grained
 * sky, the camera that zooms out as Pip climbs, and the physics: gravity, a held button that
 * adds a strong downward force, a slowest speed forward, and a perfect slide counted when Pip
 * takes off from an upslope. Box2D is replaced by a circle on a height field; cocos2d by a
 * software renderer drawing straight into the frame.
 *
 * Added for the Q2: islands with a launch ramp at the end and a flight over the sea to the next
 * one, a day clock shown by the sun that sets (each island gives time back), the sky and hills
 * turning gold, red and then dark, night ending the run, coins, air time, fever after three
 * perfect slides, a score and a best, a title, pause and game-over screens, Pip, an original
 * bird, and particles. */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "assets.h"
#include "game.h"

#define W VIEW_W
#define H VIEW_H

#ifdef GAME_DEBUG
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

/* ---------------------------------------------------------------------------------------------
 * Tuning. World units are the remake's points (its screen was 480 x 320, ours is 375 x 320). */

#define BIRD_X 86.0f       /* where Pip sits across the screen */
#define RADIUS 13.0f       /* Pip's body */
#define GRAVITY 314.0f     /* the remake's 9.8 m/s^2 at 32 points a metre */
#define DIVE_ACC 1450.0f   /* the held button's pull down */
#define DAMPING 0.05f      /* the remake's linear damping */
#define MIN_VX 96.0f       /* the remake's slowest forward speed, 3 m/s */
#define MAX_VX 1250.0f
#define MIN_VY -1280.0f    /* the remake's fastest fall, 40 m/s */
#define PERFECT_VY 80.0f   /* climbing at least this fast when taking off */
#define SMOOTH_SIN 0.45f   /* a landing this close to the slope (sine of the angle) is smooth */
#define BUMP_SIN 0.6f      /* and this far off it is a bump */
#define ZOOM_HEIGHT 236.0f /* above this the camera zooms out (the remake: 4/5 of the screen) */
#define MIN_SCALE 0.36f
#define SEA_Y 14.0f
#define NO_GROUND -10000.0f
#define DAY_START 60.0f    /* seconds of daylight at the start */
#define DAY_MAX 75.0f      /* the most the sun can hold */
#define STEP (1.0f / 240.0f)

#define TEX_U 256 /* hill texture: 512 world units across, repeating */
#define TEX_V 512 /* and 1024 deep */
#define MAX_KEYS 72
#define MAX_COINS 96
#define MAX_PARTS 96
#define MAX_POPS 4
#define TRAIL 24

enum { TITLE, PLAY, NIGHT, OVER };

/* ---------------------------------------------------------------------------------------------
 * Random numbers */

static uint32_t rng = 1;
static uint32_t rnd(void) {
    rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return (int)(rnd() % (uint32_t)n); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------------------------------------------
 * Islands */

typedef struct {
    float x, y;
    int taken;
} Coin;

typedef struct {
    int number, n;
    float kx[MAX_KEYS], ky[MAX_KEYS];
    float crest_x;  /* the launch ramp's top */
    float land_x;   /* where a flight from the island before comes down */
    int ncoins;
    Coin coins[MAX_COINS];
    int stripes, diagonal, tex_done; /* the texture: its stripes, and columns worked out so far */
    float cols[32][3];
    uint32_t tex[TEX_U * TEX_V]; /* column-major: tex[u * TEX_V + v] */
} Island;

static Island isl[3];
static int cur; /* isl[cur] is the island Pip is on (or flying from) */

static int nexti(void) { return (cur + 1) % 3; }

/* The remake's randomColor: bright, and not grey */
static void random_colour(float *c) {
    for (;;) {
        int r = irand(256), g = irand(256), b = irand(256);
        int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (hi - lo < 150 || r + g + b < 450) continue;
        c[0] = r / 255.0f, c[1] = g / 255.0f, c[2] = b / 255.0f;
        return;
    }
}

/* The remake's stripes texture: its stripes chosen now, its texels worked out by texture_step */
static void plan_texture(Island *is) {
    int n = irand(30 - 4) + 4;
    if (n & 1) ++n;
    is->stripes = n;
    is->diagonal = irand(2);
    for (int i = 0; i < (is->diagonal ? n / 2 : n); ++i) random_colour(is->cols[i]);
    is->tex_done = 0;
}

/* up to count more columns of the texture: an island is made two ahead of Pip, so its texture
 * can be spread over many frames instead of stalling one */
static void texture_step(Island *is, int count) {
    int n = is->stripes, diagonal = is->diagonal, ncol = diagonal ? n / 2 : n;
    float (*cols)[3] = is->cols;
    for (; count > 0 && is->tex_done < TEX_U; --count) {
        int u = is->tex_done++;
        uint32_t *col = is->tex + u * TEX_V;
        for (int v = 0; v < TEX_V; ++v) {
            int k = diagonal ? ((u + v) & (TEX_U - 1)) * ncol / TEX_U : v * n / TEX_V;
            float r = cols[k][0], g = cols[k][1], b = cols[k][2];
            float t = (float)v / TEX_V; /* gradient: black, 0 at the top to 0.5 at the bottom */
            float dk = 1.0f - 0.5f * t;
            r *= dk, g *= dk, b *= dk;
            if (v < TEX_V / 4) { /* highlight: pale yellow, fading over the top quarter */
                float h = 1.0f - (float)v / (TEX_V / 4);
                float a = 0.5f * h;
                r = r * (1 - a) + 1.0f * a * h, g = g * (1 - a) + 1.0f * a * h, b = b * (1 - a) + 0.5f * a * h;
            }
            if (v < 1) r *= 0.5f, g *= 0.5f, b *= 0.5f; /* the edge line */
            /* the grain, twice (the remake's noise is 205..246 of 255) */
            int nz = noise_px[v * NOISE_N + u * 2]; /* the grain once over the texture, as in the remake */
            float gr = (205.0f + nz * (41.0f / 255.0f)) / 255.0f;
            gr *= gr;
            gr *= 1.22f; /* back up to the brightness of the remake's screen */
            r *= gr, g *= gr, b *= gr;
            col[v] = (uint32_t)clampi((int)(r * 255), 0, 255) << 16 |
                     (uint32_t)clampi((int)(g * 255), 0, 255) << 8 | (uint32_t)clampi((int)(b * 255), 0, 255);
        }
    }
}

static float key_h(const Island *is, float x, float *slope) {
    if (x < is->kx[0] || x > is->kx[is->n - 1]) return NO_GROUND;
    int lo = 0, hi = is->n - 1;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (is->kx[m] <= x) lo = m;
        else hi = m;
    }
    float x0 = is->kx[lo], x1 = is->kx[hi], y0 = is->ky[lo], y1 = is->ky[hi];
    float k = (float)M_PI / (x1 - x0), a = (x - x0) * k;
    float mid = (y0 + y1) * 0.5f, amp = (y0 - y1) * 0.5f;
    if (slope) *slope = -amp * k * sinf(a);
    return mid + amp * cosf(a);
}

static const Island *island_at(float x) {
    for (int i = 0; i < 3; ++i) {
        const Island *is = &isl[(cur + i) % 3];
        if (is->n && x >= is->kx[0] && x <= is->kx[is->n - 1]) return is;
    }
    return 0;
}

/* The ground under x and its slope: NO_GROUND over the sea */
static float ground(float x, float *slope) {
    const Island *is = island_at(x);
    if (is) return key_h(is, x, slope);
    if (slope) *slope = 0;
    return NO_GROUND;
}

/* An island: the remake's hills (alternating up and down by random steps), getting longer and
 * steeper island by island, out of the sea at start_x and back into it after a launch ramp. */
static void make_island(Island *is, int number, float start_x) {
    is->number = number;
    is->n = 0;
    is->ncoins = 0;
    float x, y;
#define KEY(X, Y) (is->kx[is->n] = (X), is->ky[is->n] = (Y), ++is->n)
    if (number == 1) { /* the remake's start: Pip sleeps on a shelf at x = 0 */
        KEY(-440, -50);
        KEY(-210, 250);
        x = 0, y = 168;
        KEY(x, y);
    } else {
        KEY(start_x - 230, -50);
        x = start_x, y = 268;
        KEY(x, y);
    }
    int hills = 16 + 2 * number;
    if (hills > 34) hills = 34;
    int min_dx = 160 + 10 * (number - 1), range_dx = 80;
    int min_dy = 60 + 9 * (number - 1), range_dy = 60 + 6 * (number - 1);
    if (min_dx > 250) min_dx = 250;
    if (min_dy > 150) min_dy = 150;
    if (range_dy > 110) range_dy = 110;
    float sign = -1;
    for (int i = 0; i < hills; ++i) {
        x += (float)(irand(range_dx) + min_dx);
        float ny = y + (float)(irand(range_dy) + min_dy) * sign;
        y = clampf(ny, 22, 300);
        sign = -sign;
        KEY(x, y);
    }
    if (sign < 0) { /* end on a valley */
        x += (float)(irand(range_dx) + min_dx);
        y = clampf(y - (float)(irand(range_dy) + min_dy), 22, 120);
        KEY(x, y);
    }
    /* the launch ramp, then the drop into the sea */
    x += 300;
    KEY(x, y + 150 > 290 ? y + 150 : 290);
    is->crest_x = x;
    KEY(x + 230, -60);
#undef KEY
    is->land_x = number > 1 ? is->kx[1] + 0.3f * (is->kx[2] - is->kx[1]) : 0;

    /* coins: rows along some downslopes, at the height Pip's middle passes when sliding */
    for (int i = 2; i + 1 < is->n - 2 && is->ncoins < MAX_COINS - 8; ++i) {
        if (is->ky[i + 1] >= is->ky[i] || frand() > 0.45f) continue;
        int count = 5 + irand(4);
        float a = is->kx[i] + 0.22f * (is->kx[i + 1] - is->kx[i]);
        float b = is->kx[i] + 0.78f * (is->kx[i + 1] - is->kx[i]);
        for (int c = 0; c < count; ++c) {
            float cx = a + (b - a) * c / (count - 1);
            Coin *k = &is->coins[is->ncoins++];
            k->x = cx, k->y = key_h(is, cx, 0) + 22, k->taken = 0;
        }
    }
    plan_texture(is);
}

/* ---------------------------------------------------------------------------------------------
 * Pip and the run */

static struct {
    float x, y, vx, vy, angle;
    int grounded, asleep, jumping, smooth;
    float air;    /* seconds since leaving the ground */
    float wobble; /* after a bump */
    int mouth;    /* open: a coin is just ahead, or was just eaten */
    float chomp;  /* seconds the mouth stays open after a bite */
    float trail[TRAIL][2];
    int trail_n;
    float trail_t;
} bird;

typedef struct {
    float x, y, vx, vy, life, max, size;
    uint32_t colour;
    int add; /* drawn as a glow (sparkles) */
} Part;

typedef struct {
    char text[24];
    int big, seq, placed;
    float life, max;
    float y; /* its bottom edge on screen, easing towards its place in the stack */
    uint32_t colour;
} Pop;

static struct {
    int state, paused, held, best, score, shown_score, island, chain, perfects, coins, hits, pop_seq;
    int sel, confirm, wheel, quit; /* the pause menu: the chosen button, the question asked, wheel
                                    * travel, and Quit chosen */
    float time_left, fever, t, scale, lift, night, over_t, sleep_t;
    Part parts[MAX_PARTS];
    Pop pops[MAX_POPS];
} g;

int game_best(void) { return g.best; }

static void pop(const char *text, int big, uint32_t colour) {
    Pop *p = &g.pops[0]; /* the slot closest to its end */
    for (int i = 1; i < MAX_POPS; ++i)
        if (g.pops[i].life < p->life) p = &g.pops[i];
    int n = 0;
    while (text[n] && n < (int)sizeof p->text - 1) p->text[n] = text[n], ++n;
    p->text[n] = 0;
    p->big = big, p->max = p->life = big ? 1.6f : 1.2f, p->colour = colour;
    p->seq = ++g.pop_seq, p->placed = 0;
}

#define POP_BOTTOM (H - 26) /* the newest pop-up's bottom edge */

/* The stack of pop-ups: the newest at the bottom, each older one above the one after it. They
 * ease into place, so a new one slides the others up instead of making them jump. */
static void layout_pops(float dt) {
    int order[MAX_POPS], n = 0;
    for (int i = 0; i < MAX_POPS; ++i)
        if (g.pops[i].life > 0) order[n++] = i;
    for (int i = 1; i < n; ++i) /* newest first */
        for (int j = i; j > 0 && g.pops[order[j]].seq > g.pops[order[j - 1]].seq; --j) {
            int t = order[j];
            order[j] = order[j - 1], order[j - 1] = t;
        }
    float bottom = POP_BOTTOM;
    for (int k = 0; k < n; ++k) {
        Pop *p = &g.pops[order[k]];
        if (!p->placed) p->y = bottom + 10, p->placed = 1; /* comes in from a little below */
        p->y += (bottom - p->y) * clampf(dt * 30, 0, 1); /* about 50 ms to make room */
        bottom -= (float)((p->big ? FONT_BIG_H : FONT_MID_H) - 4);
    }
}

static void part(float x, float y, float vx, float vy, float life, float size, uint32_t colour, int add) {
    Part *p = 0;
    for (int i = 0; i < MAX_PARTS; ++i)
        if (g.parts[i].life <= 0) {
            p = &g.parts[i];
            break;
        }
    if (!p) p = &g.parts[irand(MAX_PARTS)];
    p->x = x, p->y = y, p->vx = vx, p->vy = vy, p->life = p->max = life, p->size = size;
    p->colour = colour, p->add = add;
}

static int itoa_(int v, char *out) {
    char tmp[12];
    int n = 0, k = 0;
    if (v < 0) out[k++] = '-', v = -v;
    do tmp[n++] = (char)('0' + v % 10), v /= 10;
    while (v);
    while (n) out[k++] = tmp[--n];
    out[k] = 0;
    return k;
}

static void add_score(int pts, const char *label, int big, uint32_t colour) {
    g.score += pts * (g.fever > 0 ? 2 : 1);
    if (label) pop(label, big, colour);
}

static void new_run(void) {
    memset(&bird, 0, sizeof bird);
    for (int i = 0; i < 3; ++i) isl[i].n = 0;
    cur = 0;
    make_island(&isl[0], 1, 0);
    make_island(&isl[1], 2, isl[0].crest_x + 760);
    texture_step(&isl[0], TEX_U);
    texture_step(&isl[1], TEX_U);
    bird.x = 0;
    bird.y = isl[0].ky[2] + RADIUS;
    bird.asleep = 1, bird.grounded = 1;
    g.state = TITLE;
    g.score = g.shown_score = 0;
    g.island = 1, g.chain = 0, g.perfects = 0, g.coins = 0, g.hits = 0;
    g.time_left = DAY_START, g.fever = 0, g.night = 0, g.over_t = 0, g.sleep_t = 0;
    g.scale = 1, g.lift = 0;
    memset(g.parts, 0, sizeof g.parts);
    memset(g.pops, 0, sizeof g.pops);
}

void game_init(uint32_t seed, int best) {
    memset(&g, 0, sizeof g);
    rng = seed ? seed : 0x9e3779b9u;
    g.best = best;
    new_run();
}

static void wake(void) {
    bird.asleep = 0;
    bird.grounded = 0;
    bird.vx = 140, bird.vy = 120; /* the remake's nudge awake */
    g.state = PLAY;
}

/* --- the pause menu: Resume, Restart and Quit. Restart and Quit ask first (Keep playing, or
 * go ahead), with Keep playing chosen, so a stray press can't end a run. --- */

#define BTN_W 170
#define BTN_H 34
#define BTN_X0 ((W - BTN_W) / 2)
static const int btn_y[3] = { 134, 178, 222 }; /* the buttons' tops */
enum { ASK_NONE, ASK_RESTART, ASK_QUIT };

static int buttons(void) { return g.confirm ? 2 : 3; }

/* Ending a run (restart or quit): its score counts for the best, as if Pip had fallen asleep */
static void end_run(void) {
    if (g.score > g.best) g.best = g.score;
}

static void restart(void) {
    end_run();
    new_run();
    g.paused = g.confirm = 0;
    g.held = 0;
}

static void choose(int button) {
    g.held = 0;
    if (g.confirm == ASK_NONE) {
        if (button == 0) g.paused = 0;                       /* Resume */
        else g.confirm = button == 1 ? ASK_RESTART : ASK_QUIT, g.sel = 0;
    } else if (button == 0) {
        g.confirm = ASK_NONE, g.sel = 0, g.paused = 0;       /* Keep playing: straight back in */
    } else if (g.confirm == ASK_RESTART) {
        restart();
    } else {
        end_run();
        g.quit = 1;                                          /* q2.c leaves at the end of the frame */
    }
}

int game_quit(void) { return g.quit; }

static int button_at(int x, int y) {
    for (int i = 0; i < buttons(); ++i)
        if (x >= BTN_X0 - 6 && x < BTN_X0 + BTN_W + 6 && y >= btn_y[i] - 4 && y < btn_y[i] + BTN_H + 6) return i;
    return -1;
}

/* The wheel moves the choice: anticlockwise (Q2 Pod's travel rising) up, clockwise down */
void game_wheel(int travel) {
    if (!g.paused) return;
    if ((travel > 0) != (g.wheel > 0)) g.wheel = 0; /* a reversal acts at once */
    g.wheel += travel;
    while (g.wheel >= 28) g.wheel -= 28, g.sel = g.sel > 0 ? g.sel - 1 : 0;
    while (g.wheel <= -28) g.wheel += 28, g.sel = g.sel < buttons() - 1 ? g.sel + 1 : g.sel;
}

void game_press(int down) {
    g.held = down;
    if (!down) return;
    if (g.paused) { /* Centre: press the chosen button */
        choose(g.sel);
        return;
    }
    if (g.state == TITLE) wake();
    else if (g.state == OVER && g.over_t > 1.2f) {
        new_run();
        g.held = 0;
    }
}

/* The glass's corners are rounded with an 80 px radius (Q2 Pod's docs): the score and the pause
 * button sit as far into their corners as the curve allows, 3 px clear of it. */
#define SCORE_X 44
#define SCORE_Y 9
#define PAUSE_CX (W - 37) /* the pause button: a disc */
#define PAUSE_CY 33
#define PAUSE_R 10
#define PAUSE_X0 (W - 78) /* where a touch counts as the pause button */
#define PAUSE_Y1 70

void game_touch(int down, int x, int y) {
    if (g.paused) { /* a tap on a button presses it; elsewhere does nothing */
        if (down) {
            int b = button_at(x, y);
            if (b >= 0) g.sel = b, choose(b);
        }
        g.held = 0;
        return;
    }
    if (down && x >= PAUSE_X0 && y < PAUSE_Y1 && (g.state == PLAY || g.state == NIGHT) && !g.paused) {
        g.paused = 1;
        g.held = 0;
        return;
    }
    game_press(down);
}

/* Play/Pause, a short Return: pause, resume, or back out of the restart question */
void game_pause(void) {
    if (g.state != PLAY && g.state != NIGHT) return;
    if (g.paused && g.confirm) g.sel = g.confirm == ASK_RESTART ? 1 : 2, g.confirm = ASK_NONE; /* back */
    else g.paused = !g.paused, g.sel = 0, g.confirm = 0;
    g.held = 0;
}

int game_state(void) { return g.paused ? 4 + g.confirm : g.state; }

/* --- events --- */

static void dust(int n, float spread) {
    for (int i = 0; i < n; ++i)
        part(bird.x - 4 + frand() * 8, bird.y - RADIUS + 2, -40 + frand() * 80 * spread,
             20 + frand() * 70, 0.5f + frand() * 0.4f, 2 + frand() * 2.5f, 0xf4ead2, 0);
}

static void landed(float vn, float slope) {
    float speed = sqrtf(bird.vx * bird.vx + bird.vy * bird.vy);
    float sin_a = speed > 1 ? -vn / speed : 0;
    DBG("land t=%.2f x=%.0f speed=%.0f sin=%.2f slope=%.2f air=%.2f held=%d\n", g.t, bird.x, speed, sin_a,
        slope, bird.air, g.held);
    if (bird.jumping) {
        bird.jumping = 0;
        bird.smooth = 0;
        dust(6, 1);
        return;
    }
    if (bird.air > 1.0f && g.state == PLAY)
        add_score((int)(bird.air * 30), bird.air > 2.4f ? "Awesome air!" : "Great air!", 0, 0x8fe3ff);
    if (sin_a > BUMP_SIN && speed > 160) { /* a bump: the chain is broken */
        bird.smooth = 0;
        g.chain = 0;
        ++g.hits;
        bird.wobble = 1;
        dust(10, 1.5f);
    } else {
        bird.smooth = sin_a < SMOOTH_SIN && slope < -0.08f;
        if (!bird.smooth) g.chain = 0;
    }
}

static void took_off(void) {
    DBG("  off t=%.2f x=%.0f vx=%.0f vy=%.0f smooth=%d\n", g.t, bird.x, bird.vx, bird.vy, bird.smooth);
    if (bird.jumping) return;
    if (bird.smooth && bird.vy > PERFECT_VY && g.state == PLAY) {
        ++g.chain;
        ++g.perfects;
        int pts = 30 * (g.chain < 5 ? g.chain : 5);
        if (g.chain == 3 && g.fever <= 0) {
            g.fever = 6;
            add_score(pts, "FEVER!", 1, 0xff8a3c);
        } else {
            if (g.fever > 0) g.fever = 6;
            add_score(pts, "Perfect slide!", 0, 0xfff07a);
        }
        bird.vx += g.fever > 0 ? 40 : 15;
        for (int i = 0; i < 8; ++i)
            part(bird.x, bird.y, -60 + frand() * 120, -20 + frand() * 120, 0.6f + frand() * 0.3f,
                 1.5f + frand() * 1.5f, 0xfff3b0, 1);
    }
    bird.smooth = 0;
    bird.air = 0;
}

/* Over the launch ramp: a flight that comes down on the next island's first slope */
static void launch(void) {
    Island *nx = &isl[nexti()];
    float lx = nx->land_x, ly = key_h(nx, lx, 0) + RADIUS * 1.2f;
    float d = lx - bird.x;
    float t = clampf(d / (bird.vx > 380 ? bird.vx : 380), 1.1f, 2.1f);
    bird.vx = d / t;
    bird.vy = (ly - bird.y) / t + 0.5f * GRAVITY * t;
    bird.jumping = 1;
    bird.grounded = 0;
    bird.smooth = 0;
    bird.air = 0;
    if (g.state == PLAY) {
        float bonus = 18.0f - 1.5f * (float)(g.island - 1);
        if (bonus < 5) bonus = 5;
        g.time_left = clampf(g.time_left + bonus, 0, DAY_MAX);
        char t1[24] = "Island ";
        itoa_(g.island + 1, t1 + 7);
        add_score(200 * g.island, t1, 1, 0xffffff);
        char t2[24] = "+";
        int k = 1 + itoa_((int)bonus, t2 + 1);
        t2[k] = ' ', t2[k + 1] = 's', t2[k + 2] = 0;
        pop(t2, 0, 0xffe08a);
    }
    ++g.island;
}

/* Pip is over the next island: the island two back makes way for the one after */
static void arrive(void) {
    cur = nexti();
    Island *spare = &isl[(cur + 1) % 3];
    make_island(spare, isl[cur].number + 1, isl[cur].crest_x + 760 + 30 * (float)isl[cur].number);
}

static void step(float dt) {
    int night = g.state == NIGHT;
    int dive = g.held && !bird.jumping && (g.state == PLAY || g.state == NIGHT);
    float ay = -GRAVITY - (dive ? DIVE_ACC : 0);
    bird.vy += ay * dt;
    bird.vx -= bird.vx * DAMPING * dt;
    bird.vy -= bird.vy * DAMPING * dt;
    if (!night && !bird.jumping && bird.vx < MIN_VX) bird.vx = MIN_VX;
    if (bird.vx > MAX_VX) bird.vx = MAX_VX;
    if (bird.vy < MIN_VY) bird.vy = MIN_VY;
    bird.x += bird.vx * dt;
    bird.y += bird.vy * dt;

    float slope, h = ground(bird.x, &slope);
    int contact = 0;
    if (h != NO_GROUND) {
        float lift = RADIUS * sqrtf(1 + slope * slope);
        if (bird.y < h + lift + (bird.grounded ? 0.6f : 0.0f)) {
            contact = 1;
            bird.y = h + lift;
            float len = sqrtf(1 + slope * slope), nx = -slope / len, ny = 1 / len;
            float vn = bird.vx * nx + bird.vy * ny;
            if (vn < 0) {
                if (!bird.grounded) landed(vn, slope);
                bird.vx -= vn * nx, bird.vy -= vn * ny;
            } else if (!bird.grounded && bird.jumping) {
                bird.jumping = 0;
            }
            if (night) { /* sleepy: Pip slows to a stop */
                float f = 1.0f - 0.9f * dt;
                bird.vx *= f, bird.vy *= f;
            }
        }
    }
    if (!contact && bird.grounded) took_off();
    if (!contact) bird.air += dt;
    bird.grounded = contact;

    /* over the sea, below the waves: a safety net that should never be needed */
    if (h == NO_GROUND && bird.y < SEA_Y && !bird.jumping) bird.y = SEA_Y, bird.vy = 260;

    /* the ramp and the next island */
    Island *is = &isl[cur];
    if (!bird.jumping && bird.x >= is->crest_x && bird.x < is->crest_x + 400 && g.island == is->number) launch();
    if (isl[nexti()].n && bird.x >= isl[nexti()].kx[0] + 200) arrive();

    /* coins */
    for (int k = 0; k < 2; ++k) {
        Island *c = &isl[(cur + k) % 3];
        for (int i = 0; i < c->ncoins; ++i) {
            Coin *o = &c->coins[i];
            if (o->taken) continue;
            float dx = o->x - bird.x, dy = o->y - bird.y;
            if (dx > 40 || dx < -40) continue;
            if (dx * dx + dy * dy < 21 * 21) {
                o->taken = 1;
                ++g.coins;
                bird.chomp = 0.12f;
                add_score(10, 0, 0, 0);
                for (int j = 0; j < 4; ++j)
                    part(o->x, o->y, -50 + frand() * 100, -30 + frand() * 90, 0.45f, 1.5f, 0xffe066, 1);
            }
        }
    }
}

void game_update(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    if (g.paused) return;
    g.t += dt;
    if (g.state == OVER) g.over_t += dt;

    if (g.state == PLAY) {
        g.time_left -= dt;
        if (g.time_left <= 0) {
            g.time_left = 0;
            g.state = NIGHT;
            g.chain = 0;
            g.fever = 0;
            pop("Night falls...", 1, 0xc8d4ff);
        }
    }
    if (g.state == NIGHT || g.state == OVER) g.night = clampf(g.night + dt / 2.5f, 0, 1);
    if (g.fever > 0) g.fever -= dt;
    for (int i = 0; i < 3; ++i) /* finish any island's texture, 8 columns a frame */
        if (isl[i].n && isl[i].tex_done < TEX_U) texture_step(&isl[i], isl[i].number == isl[cur].number ? TEX_U : 8);

    if (!bird.asleep) {
        int n = (int)(dt / STEP) + 1;
        float h = dt / (float)n;
        for (int i = 0; i < n; ++i) step(h);
        if (g.state == NIGHT) {
            float sp = sqrtf(bird.vx * bird.vx + bird.vy * bird.vy);
            g.sleep_t = bird.grounded && sp < 45 ? g.sleep_t + dt : 0;
            if (g.sleep_t > 0.6f || (g.night >= 1 && sp < 20)) {
                bird.asleep = 1;
                bird.vx = bird.vy = 0;
                g.state = OVER;
                if (g.score > g.best) g.best = g.score;
            }
        }
    }

    /* the angle Pip faces: the slope on the ground, the flight in the air */
    float target;
    if (bird.asleep) target = 0;
    else if (bird.grounded) {
        float sl;
        ground(bird.x, &sl);
        target = atanf(sl);
    } else target = atan2f(bird.vy, bird.vx > 1 ? bird.vx : 1);
    bird.angle += (target - bird.angle) * clampf(dt * 12, 0, 1);

    /* the mouth: open when a coin is close ahead (within reach in about a fifth of a second, at
     * least 70 units), shut a moment after the last bite */
    if (bird.chomp > 0) bird.chomp -= dt;
    int near = 0;
    if (!bird.asleep) {
        float reach = bird.vx * 0.2f > 70 ? bird.vx * 0.2f : 70;
        for (int k = 0; k < 2 && !near; ++k) {
            Island *c = &isl[(cur + k) % 3];
            for (int i = 0; i < c->ncoins && !near; ++i) {
                Coin *o = &c->coins[i];
                float dx = o->x - bird.x, dy = o->y - bird.y;
                near = !o->taken && dx > -6 && dx < reach && dy > -50 && dy < 50;
            }
        }
    }
    bird.mouth = near || bird.chomp > 0;
    if (bird.wobble > 0) bird.wobble -= dt * 2.5f;

    /* the camera: zoom out as Pip climbs */
    float s = bird.y > ZOOM_HEIGHT ? ZOOM_HEIGHT / bird.y : 1;
    if (s < MIN_SCALE) s = MIN_SCALE;
    g.scale += (s - g.scale) * clampf(dt * 5, 0, 1);
    /* past the furthest zoom, the view follows Pip up, keeping it 70 px below the top */
    float over = bird.y * g.scale - (H - 70);
    g.lift += ((over > 0 ? over : 0) - g.lift) * clampf(dt * 8, 0, 1);

    /* the fever trail */
    bird.trail_t += dt;
    if (bird.trail_t > 1.0f / 40) {
        bird.trail_t = 0;
        memmove(bird.trail[1], bird.trail[0], sizeof bird.trail[0] * (TRAIL - 1));
        bird.trail[0][0] = bird.x, bird.trail[0][1] = bird.y;
        if (bird.trail_n < TRAIL) ++bird.trail_n;
    }
    if (g.fever > 0 && !bird.grounded && irand(3) == 0)
        part(bird.x - 10, bird.y + frand() * 10 - 5, -30, -10 + frand() * 20, 0.5f, 1.8f, 0xffd36b, 1);

    for (int i = 0; i < MAX_PARTS; ++i) {
        Part *p = &g.parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt, p->y += p->vy * dt;
        p->vy -= 160 * dt;
    }
    for (int i = 0; i < MAX_POPS; ++i)
        if (g.pops[i].life > 0) g.pops[i].life -= dt;
    layout_pops(dt);

    /* the score counts up */
    if (g.shown_score < g.score) {
        g.shown_score += (g.score - g.shown_score) / 6 + 1;
        if (g.shown_score > g.score) g.shown_score = g.score;
    } else g.shown_score = g.score;
}

/* ---------------------------------------------------------------------------------------------
 * Drawing */

static uint32_t *fb;
static float cam_x, cam_s, cam_y; /* cam_y: px the world is moved down when Pip is very high */
static unsigned char lut_r[256], lut_g[256], lut_b[256]; /* the light on the hills */

static inline uint32_t blend(uint32_t d, uint32_t c, int a) { /* a: 0..256 */
    uint32_t rb = d & 0xff00ff, gg = d & 0x00ff00;
    rb += (((c & 0xff00ff) - rb) * (uint32_t)a) >> 8;
    gg += (((c & 0x00ff00) - gg) * (uint32_t)a) >> 8;
    return (rb & 0xff00ff) | (gg & 0x00ff00);
}

static inline uint32_t add_px(uint32_t d, uint32_t c, int a) {
    int r = (int)(d >> 16 & 255) + ((int)(c >> 16 & 255) * a >> 8);
    int gg = (int)(d >> 8 & 255) + ((int)(c >> 8 & 255) * a >> 8);
    int b = (int)(d & 255) + ((int)(c & 255) * a >> 8);
    return (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(gg > 255 ? 255 : gg) << 8 | (uint32_t)(b > 255 ? 255 : b);
}

static uint32_t rgb(float r, float gg, float b) {
    return (uint32_t)clampi((int)r, 0, 255) << 16 | (uint32_t)clampi((int)gg, 0, 255) << 8 | (uint32_t)clampi((int)b, 0, 255);
}

static float wsx(float x) { return (x - cam_x) * cam_s + BIRD_X; }
static float wsy(float y) { return H - y * cam_s + cam_y; }

/* Sky colours through the day, by the fraction of daylight left: top and horizon */
static const float sky_keys[][7] = {
    /* left   top r g b        horizon r g b */
    { 0.00f, 52, 46, 104, 214, 112, 108 },
    { 0.10f, 82, 82, 158, 246, 146, 110 },
    { 0.22f, 96, 136, 196, 250, 202, 140 },
    { 0.40f, 92, 168, 214, 196, 230, 232 },
    { 1.00f, 86, 170, 222, 186, 230, 240 },
};
static const float night_top[3] = { 14, 20, 52 }, night_hor[3] = { 42, 52, 102 };

static void sky_colours(float *top, float *hor) {
    float f = g.state == TITLE ? 0.75f : g.time_left / DAY_MAX;
    int n = sizeof sky_keys / sizeof *sky_keys, i = 0;
    while (i < n - 2 && f > sky_keys[i + 1][0]) ++i;
    float t = clampf((f - sky_keys[i][0]) / (sky_keys[i + 1][0] - sky_keys[i][0]), 0, 1);
    for (int c = 0; c < 3; ++c) {
        top[c] = mixf(sky_keys[i][1 + c], sky_keys[i + 1][1 + c], t);
        hor[c] = mixf(sky_keys[i][4 + c], sky_keys[i + 1][4 + c], t);
        top[c] = mixf(top[c], night_top[c], g.night);
        hor[c] = mixf(hor[c], night_hor[c], g.night);
    }
}

static void set_light(void) {
    float f = g.state == TITLE ? 1 : clampf(g.time_left / (DAY_MAX * 0.4f), 0, 1);
    /* day white -> sunset warm -> night blue-dark */
    float r = mixf(1.02f, 1.0f, f), gg = mixf(0.80f, 1.0f, f), b = mixf(0.70f, 1.0f, f);
    r = mixf(r, 0.30f, g.night), gg = mixf(gg, 0.34f, g.night), b = mixf(b, 0.55f, g.night);
    for (int i = 0; i < 256; ++i) {
        lut_r[i] = (unsigned char)clampi((int)(i * r), 0, 255);
        lut_g[i] = (unsigned char)clampi((int)(i * gg), 0, 255);
        lut_b[i] = (unsigned char)clampi((int)(i * b), 0, 255);
    }
}

static inline uint32_t lit(uint32_t c) {
    return (uint32_t)lut_r[c >> 16 & 255] << 16 | (uint32_t)lut_g[c >> 8 & 255] << 8 | lut_b[c & 255];
}

static void draw_sky(void) {
    float top[3], hor[3];
    sky_colours(top, hor);
    int off = (int)(cam_x * 0.2f) & (NOISE_N - 1);
    static short grain[256];
    for (int i = 0; i < 256; ++i) grain[i] = (short)(256 + (i - 128) / 5);
    for (int y = 0; y < H; ++y) {
        float t = (float)y / (H - 1);
        t = t * t * (3 - 2 * t);
        int r = (int)mixf(top[0], hor[0], t), gg = (int)mixf(top[1], hor[1], t), b = (int)mixf(top[2], hor[2], t);
        const unsigned char *nrow = noise_px + ((y + 64) & (NOISE_N - 1)) * NOISE_N;
        uint32_t *row = fb + y * W;
        for (int x = 0; x < W; ++x) {
            int k = grain[nrow[(x + off) & (NOISE_N - 1)]];
            int rr = r * k >> 8, g2 = gg * k >> 8, bb = b * k >> 8;
            row[x] = (uint32_t)(rr > 255 ? 255 : rr) << 16 | (uint32_t)(g2 > 255 ? 255 : g2) << 8 | (uint32_t)(bb > 255 ? 255 : bb);
        }
    }
}

/* A soft-edged disc: colour inside r, fading over edge px */
static void disc(float cx, float cy, float r, uint32_t colour, int alpha, float edge) {
    int x0 = clampi((int)(cx - r - edge - 1), 0, W), x1 = clampi((int)(cx + r + edge + 2), 0, W);
    int y0 = clampi((int)(cy - r - edge - 1), 0, H), y1 = clampi((int)(cy + r + edge + 2), 0, H);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float c = clampf((r + edge * 0.5f - d) / edge, 0, 1);
            if (c > 0) fb[y * W + x] = blend(fb[y * W + x], colour, (int)(c * alpha));
        }
}

/* An additive halo out to r */
static void glow(float cx, float cy, float r, uint32_t colour, int alpha) {
    int x0 = clampi((int)(cx - r), 0, W), x1 = clampi((int)(cx + r + 1), 0, W);
    int y0 = clampi((int)(cy - r), 0, H), y1 = clampi((int)(cy + r + 1), 0, H);
    float inv = 1.0f / (r * r);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x - cx, dy = y - cy, q = 1 - (dx * dx + dy * dy) * inv;
            if (q > 0) fb[y * W + x] = add_px(fb[y * W + x], colour, (int)(q * q * alpha));
        }
}

/* A soft blob blended over the frame, strongest in the middle */
static void soft(float cx, float cy, float r, uint32_t colour, int alpha) {
    int x0 = clampi((int)(cx - r), 0, W), x1 = clampi((int)(cx + r + 1), 0, W);
    int y0 = clampi((int)(cy - r), 0, H), y1 = clampi((int)(cy + r + 1), 0, H);
    float inv = 1.0f / (r * r);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x - cx, dy = y - cy, q = 1 - (dx * dx + dy * dy) * inv;
            if (q > 0) fb[y * W + x] = blend(fb[y * W + x], colour, (int)(q * alpha));
        }
}

static void draw_sun_moon_stars(void) {
    if (g.night > 0) { /* stars */
        uint32_t s = 12345;
        for (int i = 0; i < 70; ++i) {
            s = s * 1103515245u + 12345u;
            int x = (int)((s >> 8) % W);
            s = s * 1103515245u + 12345u;
            int y = (int)((s >> 8) % (H * 2 / 3));
            float tw = 0.6f + 0.4f * sinf(g.t * 2.3f + (float)i);
            int a = (int)(g.night * tw * 220);
            fb[y * W + x] = add_px(fb[y * W + x], 0xffffff, a);
            if (i % 5 == 0 && x + 1 < W) fb[y * W + x + 1] = add_px(fb[y * W + x + 1], 0xffffff, a / 2);
        }
        float mx = W * 0.72f, my = 70 + (1 - g.night) * 120;
        glow(mx, my, 48, 0x405080, (int)(g.night * 160));
        disc(mx, my, 15, 0xf4f0dc, (int)(g.night * 255), 1.5f);
        disc(mx + 6, my - 4, 13, 0x1a2250, (int)(g.night * 235), 1.5f);
    }
    if (g.night < 1) { /* the sun: high with plenty of daylight left, setting as it runs out */
        float f = g.state == TITLE ? 0.8f : clampf(g.time_left / DAY_MAX, 0, 1);
        float sx = W * 0.60f + (1 - f) * W * 0.24f, sy = 46 + (1 - f) * (H * 0.62f);
        int a = (int)((1 - g.night) * 255);
        uint32_t c = f > 0.3f ? 0xfff6c8 : rgb(255, 170 + 200 * f, 110 + 200 * f);
        glow(sx, sy, 70, f > 0.3f ? 0x605838 : 0x803818, a * 3 / 4);
        disc(sx, sy, 19, c, a, 1.5f);
    }
}

static void draw_clouds(void) {
    static const float cl[][3] = { { 40, 52, 0 },    { 330, 96, 1 },   { 640, 38, 2 },
                                   { 900, 120, 0 },  { 1180, 70, 1 },  { 1450, 30, 2 } };
    float top[3], hor[3];
    sky_colours(top, hor);
    uint32_t tint = rgb(mixf(255, hor[0] + 30, 0.35f), mixf(255, hor[1] + 30, 0.35f), mixf(255, hor[2] + 30, 0.35f));
    if (g.night > 0) tint = blend(tint, 0x50608c, (int)(g.night * 200));
    for (unsigned i = 0; i < sizeof cl / sizeof *cl; ++i) {
        float x = cl[i][0] - cam_x * 0.12f - g.t * 4;
        x = x - 1600 * floorf((x + 200) / 1600);
        int ox = (int)x, oy = (int)(cl[i][1] + cam_y * 0.3f);
        const unsigned char *src = cloud_px + (int)cl[i][2] * CLOUD_W * CLOUD_H;
        for (int y = 0; y < CLOUD_H; ++y) {
            int sy = oy + y;
            if (sy < 0 || sy >= H) continue;
            for (int xx = 0; xx < CLOUD_W; ++xx) {
                int sx = ox + xx;
                if (sx < 0 || sx >= W) continue;
                int a = src[y * CLOUD_W + xx];
                if (a) fb[sy * W + sx] = blend(fb[sy * W + sx], tint, a * 210 >> 8);
            }
        }
    }
}

/* Far hills: a pale, slow ridge between the sky and the islands */
static void draw_far_hills(void) {
    float top[3], hor[3];
    sky_colours(top, hor);
    float s2 = 0.55f + 0.45f * cam_s;
    uint32_t c = rgb(hor[0] * 0.78f + 20, hor[1] * 0.86f + 18, hor[2] * 0.9f + 10);
    uint32_t c2 = rgb(hor[0] * 0.66f + 18, hor[1] * 0.76f + 18, hor[2] * 0.84f + 12);
    for (int x = 0; x < W; ++x) {
        float wx = cam_x * 0.35f + (x - BIRD_X) / s2;
        float h = 70 + 28 * sinf(wx * 0.0042f) + 16 * sinf(wx * 0.011f + 1.3f) + 7 * sinf(wx * 0.027f + 2.1f);
        float h2 = 40 + 22 * sinf(wx * 0.006f + 2.5f) + 12 * sinf(wx * 0.017f + 0.4f);
        int y0 = clampi((int)(H - h * s2 + cam_y * 0.5f), 0, H), y2 = clampi((int)(H - h2 * s2 + cam_y * 0.5f), 0, H);
        for (int y = y0; y < H; ++y) fb[y * W + x] = blend(fb[y * W + x], y >= y2 ? c2 : c, 200);
    }
}

static void draw_terrain(void) {
    float sea_sy = wsy(SEA_Y);
    uint32_t sea_top = lit(0x5cb4d8), sea_deep = lit(0x2a6fa8), foam = lit(0xe8f6fb);
    float inv = 1.0f / cam_s;
    for (int x = 0; x < W; ++x) {
        float wx = cam_x + (x + 0.5f - BIRD_X) * inv;
        const Island *is = island_at(wx);
        float h = is ? key_h(is, wx, 0) : NO_GROUND;
        uint32_t *col = fb + x;
        if (h > SEA_Y) {
            float sy = wsy(h);
            int y = (int)ceilf(sy);
            if (y < 0) y = 0;
            int u = (int)floorf(wx * 0.5f) & (TEX_U - 1);
            const uint32_t *tex = is->tex + u * TEX_V;
            float v = (y - sy) * inv * 0.5f, dv = inv * 0.5f;
            if (sy > 0 && sy < H) { /* the edge pixel, partly covered */
                int ey = (int)floorf(sy);
                float cov = (float)y - sy;
                col[ey * W] = blend(col[ey * W], lit(tex[0]) & 0x7f7f7f, (int)(cov * 256));
            }
            for (; y < H; ++y, v += dv) {
                int vi = (int)v;
                if (vi > TEX_V - 1) vi = TEX_V - 1;
                col[y * W] = lit(tex[vi]);
            }
        } else {
            float wave = 1.6f * sinf(wx * 0.05f + g.t * 2.2f) + 1.0f * sinf(wx * 0.13f - g.t * 3.1f);
            float sy = sea_sy + wave * cam_s;
            int y = (int)ceilf(sy);
            if (y < 0) y = 0;
            for (; y < H; ++y) {
                float t = clampf((y - sy) / 30.0f, 0, 1);
                uint32_t c = blend(sea_top, sea_deep, (int)(t * 256));
                if (y - sy < 2.2f) c = blend(c, foam, 170);
                col[y * W] = c;
            }
        }
    }
}

static void draw_coins(void) {
    for (int k = 0; k < 3; ++k) {
        Island *c = &isl[(cur + k) % 3];
        for (int i = 0; i < c->ncoins; ++i) {
            Coin *o = &c->coins[i];
            if (o->taken) continue;
            float sx = wsx(o->x), sy = wsy(o->y);
            if (sx < -10 || sx > W + 10 || sy < -10 || sy > H + 10) continue;
            float r = 6.0f * cam_s + 0.6f;
            disc(sx, sy, r + 0.8f, lit(0x9a6410), 255, 1.2f);
            disc(sx, sy, r - 0.4f, lit(0xffcf3a), 255, 1.2f);
            disc(sx, sy, r * 0.55f, lit(0xffe48a), 200, 1.2f);
            disc(sx - r * 0.35f, sy - r * 0.35f, r * 0.22f + 0.4f, 0xffffff, 200, 1.0f);
        }
    }
}

static void draw_parts(void) {
    for (int i = 0; i < MAX_PARTS; ++i) {
        Part *p = &g.parts[i];
        if (p->life <= 0) continue;
        float a = p->life / p->max;
        float sx = wsx(p->x), sy = wsy(p->y), r = p->size * (0.4f + 0.6f * cam_s);
        if (p->add) glow(sx, sy, r * 2.2f, p->colour, (int)(a * 255));
        else disc(sx, sy, r, lit(p->colour), (int)(a * 220), 1.2f);
    }
}

/* Pip: a pose from the sheet, turned by angle and scaled, premultiplied over the frame */
static void draw_bird(void) {
    int pose;
    if (bird.asleep) pose = POSE_SLEEP;
    else if (g.held && !bird.jumping) pose = POSE_DIVE;
    else if (!bird.grounded && (bird.vy > 0 || bird.jumping)) {
        int f = (int)(g.t * 10) % 4;
        pose = f == 0 ? POSE_FLAP_UP : f == 2 ? POSE_FLAP_DOWN : POSE_GLIDE;
    } else pose = POSE_GLIDE;
    if (bird.mouth && pose != POSE_SLEEP) pose += POSE_OPEN;
    const unsigned char *src = bird_px + pose * BIRD_SIZE * BIRD_SIZE * 4;
    float s = 0.55f + 0.45f * cam_s; /* Pip shrinks less than the hills, so stays readable */
    float a = bird.angle + (bird.wobble > 0 ? sinf(g.t * 40) * 0.25f * bird.wobble : 0);
    float ca = cosf(a) / s, sa = sinf(a) / s;
    float cx = wsx(bird.x), cy = wsy(bird.y);
    if (bird.grounded && !bird.asleep) cy += RADIUS * (1 - s) * 0.5f;
    float half = BIRD_SIZE * 0.5f, reach = half * 1.42f * s;
    int x0 = clampi((int)(cx - reach), 0, W), x1 = clampi((int)(cx + reach + 1), 0, W);
    int y0 = clampi((int)(cy - reach), 0, H), y1 = clampi((int)(cy + reach + 1), 0, H);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float u = dx * ca - dy * sa + half - 0.5f, v = dx * sa + dy * ca + half - 0.5f;
            if (u < 0 || v < 0 || u >= BIRD_SIZE - 1 || v >= BIRD_SIZE - 1) continue;
            int iu = (int)u, iv = (int)v;
            int fu = (int)((u - iu) * 256), fv = (int)((v - iv) * 256);
            const unsigned char *p = src + (iv * BIRD_SIZE + iu) * 4;
            int ch[4];
            for (int k = 0; k < 4; ++k) {
                int t0 = p[k] * (256 - fu) + p[4 + k] * fu;
                int t1 = p[BIRD_SIZE * 4 + k] * (256 - fu) + p[BIRD_SIZE * 4 + 4 + k] * fu;
                ch[k] = (t0 * (256 - fv) + t1 * fv) >> 16;
            }
            if (!ch[3]) continue;
            /* Pip takes the light too: its colour through the hills' light, then over the frame */
            uint32_t c = lit((uint32_t)ch[2] << 16 | (uint32_t)ch[1] << 8 | (uint32_t)ch[0]);
            uint32_t d = fb[y * W + x];
            int ia = 255 - ch[3];
            int r = (int)(c >> 16 & 255) + ((int)(d >> 16 & 255) * ia >> 8);
            int gg = (int)(c >> 8 & 255) + ((int)(d >> 8 & 255) * ia >> 8);
            int b = (int)(c & 255) + ((int)(d & 255) * ia >> 8);
            fb[y * W + x] = (uint32_t)clampi(r, 0, 255) << 16 | (uint32_t)clampi(gg, 0, 255) << 8 | (uint32_t)clampi(b, 0, 255);
        }
}

static void draw_trail(void) {
    if (g.fever <= 0) return;
    int a = g.fever > 1 ? 255 : (int)(g.fever * 255);
    for (int i = bird.trail_n - 1; i >= 1; --i) { /* oldest first, so the newest is on top */
        float t = 1.0f - (float)i / TRAIL;
        float sx = wsx(bird.trail[i][0]), sy = wsy(bird.trail[i][1]), k = 0.5f + 0.5f * cam_s;
        soft(sx, sy, (5 + 9 * t) * k, i & 1 ? 0xff7a2a : 0xffb43c, (int)(a * t * 0.85f));
        soft(sx, sy, (2 + 4 * t) * k, 0xfff0a0, (int)(a * t));
    }
}

/* --- text --- */

typedef struct {
    const unsigned char *alpha, *outline;
    const unsigned int (*glyphs)[3];
    int h, grow;
} Font;

static const Font fonts[3] = {
    { font_small_alpha, font_small_outline, font_small_glyphs, FONT_SMALL_H, FONT_SMALL_GROW },
    { font_mid_alpha, font_mid_outline, font_mid_glyphs, FONT_MID_H, FONT_MID_GROW },
    { font_big_alpha, font_big_outline, font_big_glyphs, FONT_BIG_H, FONT_BIG_GROW },
};
enum { SMALL, MID, BIG };

static int text_w(int f, const char *s) {
    int w = 0;
    for (; *s; ++s)
        if (*s >= 32 && *s < 127) w += (int)fonts[f].glyphs[*s - 32][2];
    return w;
}

/* One pass of a string: its outlines (map = the outline maps) or its letters */
static void text_pass(const Font *ft, int x, int y, const char *s, const unsigned char *map, uint32_t c, int alpha) {
    for (; *s; ++s) {
        if (*s < 32 || *s >= 127) continue;
        const unsigned int *gl = ft->glyphs[*s - 32];
        int off = (int)gl[0], gw = (int)gl[1], gx = x - ft->grow - 1;
        for (int yy = 0; yy < ft->h; ++yy) {
            int sy = y - ft->grow + yy;
            if (sy < 0 || sy >= H) continue;
            const unsigned char *row = map + off + yy * gw;
            for (int xx = 0; xx < gw; ++xx) {
                int sx = gx + xx, a = row[xx];
                if (a && sx >= 0 && sx < W) fb[sy * W + sx] = blend(fb[sy * W + sx], c, a * alpha >> 8);
            }
        }
        x += (int)gl[2];
    }
}

/* text with its top-left at x, y, at alpha 0..256: every outline in oc first, then the letters
 * in fc over them, so a letter's outline never covers its neighbour */
static void text(int f, int x, int y, const char *s, uint32_t fc, uint32_t oc, int alpha) {
    const Font *ft = &fonts[f];
    text_pass(ft, x, y, s, ft->outline, oc, alpha);
    text_pass(ft, x, y, s, ft->alpha, fc, alpha);
}

static void text_c(int f, int cx, int y, const char *s, uint32_t fc, uint32_t oc, int alpha) {
    text(f, cx - text_w(f, s) / 2, y, s, fc, oc, alpha);
}

static void dim(int alpha, uint32_t c) {
    for (int i = 0; i < W * H; ++i) fb[i] = blend(fb[i], c, alpha);
}

/* A rounded rectangle (corners of radius 14, or half its height if less), smooth-edged: each
 * pixel is covered by how far its centre is inside the shape's edge */
static void panel(int x0, int y0, int x1, int y1, uint32_t c, int alpha) {
    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f, cx = x0 + hw, cy = y0 + hh;
    float r = hh < 14 ? hh : 14;
    for (int y = clampi(y0 - 1, 0, H); y < clampi(y1 + 1, 0, H); ++y)
        for (int x = clampi(x0 - 1, 0, W); x < clampi(x1 + 1, 0, W); ++x) {
            float qx = fabsf(x + 0.5f - cx) - (hw - r), qy = fabsf(y + 0.5f - cy) - (hh - r);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float dist = sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
            float cov = clampf(0.5f - dist, 0, 1);
            if (cov > 0) fb[y * W + x] = blend(fb[y * W + x], c, (int)(cov * alpha));
        }
}

#define INK 0x3a2418

static void draw_hud(void) {
    char s[32];
    if (g.state == PLAY || g.state == NIGHT) {
        itoa_(g.shown_score, s);
        text(MID, SCORE_X, SCORE_Y, s, 0xffffff, INK, 256);
        if (!g.paused) { /* the pause button: two white bars on a dark disc */
            disc(PAUSE_CX, PAUSE_CY, PAUSE_R, INK, 150, 1.5f);
            for (int y = PAUSE_CY - 5; y < PAUSE_CY + 5; ++y)
                for (int x = 0; x < 3; ++x) {
                    fb[y * W + PAUSE_CX - 4 + x] = blend(fb[y * W + PAUSE_CX - 4 + x], 0xffffff, 240);
                    fb[y * W + PAUSE_CX + 2 + x] = blend(fb[y * W + PAUSE_CX + 2 + x], 0xffffff, 240);
                }
        }
        if (g.fever > 0) {
            int a = g.fever < 1 ? (int)(g.fever * 256) : 256;
            text(SMALL, SCORE_X - 6, SCORE_Y + 30, "FEVER x2", 0xffb050, INK, a);
        } else if (g.chain > 1 && g.state == PLAY) {
            char c[16] = "Perfect x";
            itoa_(g.chain, c + 9);
            text(SMALL, SCORE_X - 6, SCORE_Y + 30, c, 0xfff2a0, INK, 230);
        }
    }
    /* pop-ups, where layout_pops put them */
    for (int i = 0; i < MAX_POPS; ++i) {
        Pop *p = &g.pops[i];
        if (p->life <= 0 || !p->placed) continue;
        float age = p->max - p->life;
        /* fades in once the older ones have moved up out of its way */
        int alpha = (int)(clampf(p->life / 0.4f, 0, 1) * clampf((age - 0.05f) / 0.12f, 0, 1) * 256);
        int f = p->big ? BIG : MID;
        text_c(f, W / 2, (int)(p->y + 0.5f) - fonts[f].h + fonts[f].grow, p->text, p->colour, INK, alpha);
    }
}

static void draw_screens(void) {
    if (g.state == TITLE) {
        text_c(BIG, W / 2, 34, "Wings", 0xffffff, INK, 256);
        text_c(SMALL, W / 2, 76, "Pip's island hop", 0xfff2c8, INK, 256);
        float b = 0.8f + 0.2f * sinf(g.t * 3);
        text_c(MID, W / 2, H - 92, "Tap or press Centre", 0xffffff, INK, (int)(b * 256));
        text_c(SMALL, W / 2, H - 62, "Hold to dive down hills, let go to fly", 0xffffff, INK, 230);
        if (g.best > 0) {
            char t[32] = "Best ";
            itoa_(g.best, t + 5);
            text_c(SMALL, W / 2, H - 40, t, 0xffe08a, INK, 230);
        }
    }
    if (bird.asleep && g.state != PLAY) { /* z z z */
        float cx = wsx(bird.x) + 12, cy = wsy(bird.y) - 22;
        for (int i = 0; i < 3; ++i) {
            float ph = fmodf(g.t * 0.6f + i / 3.0f, 1.0f);
            text(SMALL, (int)(cx + ph * 18 + i), (int)(cy - ph * 26), "z", 0xffffff, INK, (int)((1 - ph) * 256));
        }
    }
    if (g.paused) {
        dim(120, 0x101828);
        static const char *const labels[3][3] = { { "Resume", "Restart", "Quit" },
                                                   { "Keep playing", "Restart" },
                                                   { "Keep playing", "Quit" } };
        if (g.confirm == ASK_NONE) {
            char b[32] = "Best ";
            itoa_(g.score > g.best ? g.score : g.best, b + 5); /* this run counts once it's ahead */
            text_c(BIG, W / 2, 50, "Paused", 0xffffff, INK, 256);
            text_c(MID, W / 2, 96, b, 0xffe08a, INK, 256);
        } else {
            text_c(BIG, W / 2, 50, g.confirm == ASK_RESTART ? "Restart?" : "Quit?", 0xffffff, INK, 256);
            text_c(SMALL, W / 2, 98, "This run ends, and its score counts for Best", 0xffffff, INK, 240);
        }
        for (int i = 0; i < buttons(); ++i) { /* the chosen button gold, the others dark */
            int on = g.sel == i;
            panel(BTN_X0 - 2, btn_y[i] - 2, BTN_X0 + BTN_W + 2, btn_y[i] + BTN_H + 2, INK, on ? 230 : 150);
            panel(BTN_X0, btn_y[i], BTN_X0 + BTN_W, btn_y[i] + BTN_H, on ? 0xffc94a : 0x2a3456, on ? 250 : 200);
            text_c(MID, W / 2, btn_y[i] + (BTN_H - 24) / 2 - 1, labels[g.confirm][i], 0xffffff, INK, 256);
        }
    }
    if (g.state == OVER && g.over_t > 0.6f) {
        int a = (int)(clampf((g.over_t - 0.6f) / 0.4f, 0, 1) * 256);
        char s[24], t[40] = "Best ", u[48] = "Islands ";
        panel(64, 64, W - 64, H - 58, 0x1a2240, a * 150 >> 8);
        text_c(MID, W / 2, 76, "Pip fell asleep", 0xc8d4ff, INK, a);
        itoa_(g.score, s);
        text_c(BIG, W / 2, 104, s, 0xffffff, INK, a);
        itoa_(g.best, t + 5);
        text_c(SMALL, W / 2, 148, t, 0xffe08a, INK, a);
        int k = 8 + itoa_(g.island, u + 8);
        const char *mid = "   Perfect ";
        for (int i = 0; mid[i]; ++i) u[k++] = mid[i];
        itoa_(g.perfects, u + k);
        text_c(SMALL, W / 2, 170, u, 0xffffff, INK, a);
        if (g.over_t > 1.2f) {
            float b = 0.8f + 0.2f * sinf(g.t * 3);
            text_c(SMALL, W / 2, 210, "Tap to fly again", 0xffffff, INK, (int)(b * a));
        }
    }
}

void game_render(uint32_t *px) {
    fb = px;
    cam_s = g.scale;
    cam_x = bird.x;
    cam_y = g.lift;
    set_light();
    draw_sky();
    draw_sun_moon_stars();
    draw_clouds();
    draw_far_hills();
    draw_terrain();
    draw_coins();
    draw_trail();
    draw_parts();
    draw_bird();
    draw_hud();
    draw_screens();
}

/* ---------------------------------------------------------------------------------------------
 * Host test helpers */

void game_info(GameInfo *o) {
    o->state = g.state, o->paused = g.paused, o->score = g.score, o->island = g.island;
    o->chain = g.chain, o->fever = g.fever > 0, o->perfects = g.perfects, o->coins = g.coins;
    o->hits = g.hits, o->grounded = bird.grounded, o->jumping = bird.jumping, o->asleep = bird.asleep;
    o->mouth = bird.mouth;
    o->x = bird.x, o->y = bird.y, o->vx = bird.vx, o->vy = bird.vy, o->time_left = g.time_left;
    o->scale = g.scale;
}

#ifdef GAME_TEST
void game_test_fever(void) { g.fever = 6, g.chain = 3; } /* to see the fever drawn */
#endif

/* Dive on downslopes; in the air, dive when the ground a moment ahead slopes down */
int game_autopilot(void) {
    if (bird.jumping) return 0;
    float sl;
    float h = ground(bird.x + 18, &sl);
    if (h == NO_GROUND) return 0;
    if (bird.grounded) return sl < -0.05f;
    float ahead = ground(bird.x + bird.vx * 0.25f, &sl);
    return bird.vy < 120 && ahead != NO_GROUND && sl < -0.15f;
}
