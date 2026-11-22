/* Wings: the game, apart from the Shanling Q2. It draws 375 x 320 frames (Q2 Pod's landscape
 * view, XRGB with red at bit 16) and takes one button: held, Pip dives. q2.c supplies the screen,
 * the input and the clock; test/sim.c drives it on a PC. */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define VIEW_W 375
#define VIEW_H 320

void game_init(uint32_t seed, int best);
/* the dive button (Centre, or a finger anywhere but the pause button) */
void game_press(int down);
/* a touch in view pixels: the pause button, else the dive button */
void game_touch(int down, int x, int y);
void game_pause(void); /* Play/Pause, Return: pause or resume (or back out of the restart question) */
void game_wheel(int travel); /* the wheel's travel, 200 a turn, rising anticlockwise */
int game_state(void);  /* 0 title, 1 playing, 2 night, 3 over, 4 paused, 5 asked to restart, 6 to quit */
int game_quit(void);   /* Quit chosen in the pause menu: the program should end */
void game_update(float dt);
void game_render(uint32_t *px);
int game_best(void);

/* for the host tests */
typedef struct {
    int state; /* 0 title, 1 playing, 2 night, 3 over */
    int paused, score, island, chain, fever, perfects, coins, hits, grounded, jumping, asleep, mouth;
    float x, y, vx, vy, time_left, scale;
} GameInfo;
void game_info(GameInfo *g);
/* autopilot: dive on the way down, let go on the way up (the sim's player) */
int game_autopilot(void);
void game_test_fever(void); /* GAME_TEST builds only */
#endif
