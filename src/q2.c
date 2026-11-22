/* q2-wings: Wings on the Shanling Q2 under Q2 Pod, as a card game.
 *
 * Q2 Pod (github.com/DiamondBond/q2-pod, docs/internals.md "Card games") forks this binary from
 * Library -> Games when /mnt/mmc/.wings/q2app holds game=1. While it runs, Q2 Pod stops painting
 * and the game owns the screen and input:
 *
 *   screen  /dev/fb0: the portrait panel, 320 x 375, 32 bpp, red at bit 16, two pages (320 x 750
 *           virtual). Q2 Pod's view is landscape, 375 x 320, the panel turned anticlockwise, so a
 *           view pixel (X, Y) is the panel's row X, column 319 - Y. Frames go to the hidden page,
 *           then FBIOPAN_DISPLAY.
 *   input   text datagrams on /tmp/q2-wings.input.sock: "k d CODE" / "k u CODE" (AWTK key codes:
 *           Return 170, Play/Pause 171, Centre 218), "t d|m|u X Y" (a touch, in the view's
 *           375 x 320 pixels) and "w N" (the wheel). Return held 700 ms is Q2 Pod's: it ends
 *           the game with a TERM.
 *
 * Controls:
 *   touchscreen  touch and hold anywhere: dive; let go: fly. The button at the top right pauses.
 *   Centre       the same as a touch: hold to dive
 *   Play/Pause   pause
 *   pause menu   Resume, Restart and Quit (Restart and Quit ask first): tap one, or turn the
 *                wheel to choose and press Centre
 *   Return       a short press pauses (acting on release); held, Q2 Pod ends the game. While
 *                held, Q2 Pod repeats the key's down: repeats are ignored, so the pause screen
 *                doesn't flicker on and off on the way out.
 *
 * The best score is kept in best.txt and each run's log in wings.log, in the card folder. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "game.h"

#define WINGS_VERSION "0.8"
#define INPUT_SOCK "/tmp/q2-wings.input.sock"
#define APP_DIR "/mnt/mmc/.wings"
#define LOG_FILE "wings.log"
#define BEST_FILE "best.txt"

#define FB_W 320 /* the portrait panel: the view turned */
#define FB_H 375

#define Q2K_RETURN 170
#define Q2K_PLAY 171
#define Q2K_CENTRE 218

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY 0x4606

#define FRAME_US 16667 /* 60 fps at most */
#define QUIT_WAIT_US 300000  /* after Quit, before leaving */
#define QUIT_MAX_US 2000000

static int log_fd = -1;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof b - 1) n = sizeof b - 1;
    if (log_fd >= 0 && n > 0) write(log_fd, b, (size_t)n);
}

static uint32_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)t.tv_sec * 1000000u + (uint32_t)(t.tv_nsec / 1000);
}

/* ---------------------------------------------------------------------------------------------
 * Screen */

static struct {
    int fd;
    unsigned var[40], fix[17]; /* fb_var_screeninfo, fb_fix_screeninfo */
    unsigned char *mem;
    unsigned line;     /* bytes a panel row */
    unsigned y0, back; /* Q2 Pod's page when we started, the page we draw next */
    int open;
} fb = { .fd = -1 };

static int fb_open(void) {
    fb.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb.fd < 0 || ioctl(fb.fd, FBIOGET_VSCREENINFO, fb.var) || ioctl(fb.fd, FBIOGET_FSCREENINFO, fb.fix))
        return 0;
    fb.line = fb.fix[11];
    /* the Q2's panel, or nothing: xres, yres, yres_virtual, bpp, red's offset */
    if (fb.var[0] != FB_W || fb.var[1] != FB_H || fb.var[3] < 2 * FB_H || fb.var[6] != 32 || fb.var[8] != 16 ||
        fb.line < FB_W * 4 || 2 * FB_H * fb.line > fb.fix[5])
        return 0;
    fb.mem = mmap(0, fb.fix[5], PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    if (fb.mem == MAP_FAILED) return 0;
    fb.y0 = fb.var[5];
    fb.back = fb.y0 ? 0 : FB_H;
    fb.open = 1;
    return 1;
}

/* The view (375 x 320) onto a portrait page, turned: view (X, Y) is panel row X, column 319 - Y.
 * Done in bands of 8 view rows so the reads stay in cache. */
void q2_blit(unsigned char *page, unsigned line, const uint32_t *view) {
    for (int y0 = 0; y0 < VIEW_H; y0 += 8)
        for (int x = 0; x < VIEW_W; ++x) {
            uint32_t *out = (uint32_t *)(page + (unsigned)x * line) + (FB_W - 1 - y0);
            const uint32_t *in = view + y0 * VIEW_W + x;
            for (int k = 0; k < 8; ++k) out[-k] = in[k * VIEW_W] | 0xff000000u;
        }
}

static void fb_show(const uint32_t *view) {
    if (!fb.open) return;
    q2_blit(fb.mem + fb.back * fb.line, fb.line, view);
    fb.var[5] = fb.back;
    ioctl(fb.fd, FBIOPAN_DISPLAY, fb.var);
    fb.back = fb.back ? 0 : FB_H;
}

static void fb_close(void) {
    if (!fb.open) return;
    if (fb.var[5] != fb.y0) fb.var[5] = fb.y0, ioctl(fb.fd, FBIOPAN_DISPLAY, fb.var);
    fb.open = 0; /* Q2 Pod repaints the whole screen once we have gone */
}

/* ---------------------------------------------------------------------------------------------
 * Input */

static int sock = -1;
static volatile sig_atomic_t quit_asked;
static int centre_down, touch_down, play_down, return_down;
static uint32_t return_at;
#define RETURN_TAP_US 500000 /* shorter than Q2 Pod's 700 ms hold to leave */

static void pause_key(const char *which) {
    say("%s: pause/resume\n", which);
    game_pause();
}

static int input_open(void) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, INPUT_SOCK);
    sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    unlink(INPUT_SOCK);
    return sock >= 0 && !bind(sock, (const struct sockaddr *)&addr, sizeof addr);
}

static int num(const char **p) {
    int v = 0, neg = 0;
    while (**p == ' ') ++*p;
    if (**p == '-') neg = 1, ++*p;
    while (**p >= '0' && **p <= '9') v = v * 10 + (*(*p)++ - '0');
    return neg ? -v : v;
}

/* one datagram from Q2 Pod */
void q2_input(const char *m) {
    if (m[0] == 'k' && m[1] == ' ' && (m[2] == 'd' || m[2] == 'u')) {
        const char *p = m + 3;
        int code = num(&p), down = m[2] == 'd';
        if (code == Q2K_CENTRE) {
            if (down != centre_down) centre_down = down, game_press(down || touch_down);
        } else if (code == Q2K_PLAY) {
            if (down && !play_down) pause_key("Play/Pause");
            play_down = down;
        } else if (code == Q2K_RETURN) {
            if (down && !return_down) return_at = now_us();
            if (!down && return_down && now_us() - return_at < RETURN_TAP_US) pause_key("Return");
            return_down = down;
        }
    } else if (m[0] == 'w' && m[1] == ' ') {
        const char *p = m + 2;
        game_wheel(num(&p));
    } else if (m[0] == 't' && m[1] == ' ' && (m[2] == 'd' || m[2] == 'm' || m[2] == 'u')) {
        const char *p = m + 3;
        int x = num(&p), y = num(&p);
        if (m[2] == 'd' && !touch_down) touch_down = 1, game_touch(1, x, y);
        else if (m[2] == 'u' && touch_down) touch_down = 0, game_touch(centre_down, x, y);
    }
}

static void pump(void) {
    char m[64];
    ssize_t n;
    while (sock >= 0 && (n = recv(sock, m, sizeof m - 1, MSG_DONTWAIT)) > 0) {
        m[n] = 0;
        q2_input(m);
    }
}

/* ---------------------------------------------------------------------------------------------
 * The best score */

static int best_load(void) {
    char b[16] = { 0 };
    int fd = open(BEST_FILE, O_RDONLY), v = 0;
    if (fd < 0) return 0;
    if (read(fd, b, sizeof b - 1) > 0)
        for (const char *p = b; *p >= '0' && *p <= '9'; ++p) v = v * 10 + (*p - '0');
    close(fd);
    return v;
}

static void best_save(int v) {
    char b[16];
    int n = snprintf(b, sizeof b, "%d\n", v);
    int fd = open(BEST_FILE ".new", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    int ok = write(fd, b, (size_t)n) == n;
    close(fd);
    if (ok) rename(BEST_FILE ".new", BEST_FILE);
}

/* ---------------------------------------------------------------------------------------------
 * Start and end */

static void on_term(int sig) {
    (void)sig;
    quit_asked = 1;
}

static uint32_t frame[VIEW_W * VIEW_H];

int main(int argc, char **argv) {
    (void)argc;
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGHUP, on_term);
    signal(SIGPIPE, SIG_IGN);

    /* the app's folder: the log and the best score */
    char dir[256];
    snprintf(dir, sizeof dir, "%s", argv[0]);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, "%s", APP_DIR);
    chdir(dir);
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    say("q2-wings %s: started in %s\n", WINGS_VERSION, dir);

    if (!fb_open()) {
        say("No Q2 framebuffer: fd %d, %u x %u, virtual %u, %u bpp, red at %u, line %u, %u bytes (errno %d)\n",
            fb.fd, fb.var[0], fb.var[1], fb.var[3], fb.var[6], fb.var[8], fb.fix[11], fb.fix[5], errno);
        return 1;
    }
    if (!input_open()) say("No input socket (errno %d)\n", errno);
    int best = best_load(), saved = best;
    say("Screen: page %u shown, drawing at page %s; best %d\n", fb.y0, fb.back ? "1" : "0", best);

    game_init(now_us() ^ 0x5eed1234u, best);
    uint32_t last = now_us(), stat_at = last, frames = 0, work = 0;
    while (!quit_asked) {
        uint32_t t0 = now_us();
        pump();
        game_update((float)(t0 - last) * 1e-6f);
        last = t0;
        game_render(frame);
        fb_show(frame);
        if (game_best() != saved) saved = game_best(), best_save(saved), say("Best: %d\n", saved);
        static int shown = -1;
        if (game_state() != shown) { /* the log follows the game: title, playing, paused... */
            static const char *const names[] = { "title", "playing", "night", "over", "paused", "restart?", "quit?" };
            shown = game_state();
            say("State: %s\n", names[shown]);
        }

        uint32_t used = now_us() - t0;
        work += used, ++frames;
        if (now_us() - stat_at > 10000000u) { /* every 10 s: the frame rate and the time a frame takes */
            say("%u fps, %u.%u ms a frame\n", frames / 10, work / frames / 1000, work / frames / 100 % 10);
            stat_at = now_us(), frames = work = 0;
        }
        /* Quit in the pause menu (the best is saved above). Leaving the moment Centre goes down
         * would hand its release to Q2 Pod's Games list, which opens the game again: so stay
         * QUIT_WAIT_US at least, and until Centre and the finger are up (QUIT_MAX_US at most) */
        if (game_quit()) {
            static uint32_t quit_at;
            uint32_t t = now_us();
            if (!quit_at) quit_at = t ? t : 1, say("Quit from the menu\n");
            if ((t - quit_at >= QUIT_WAIT_US && !centre_down && !touch_down) || t - quit_at >= QUIT_MAX_US) break;
        }
        if (used < FRAME_US) usleep(FRAME_US - used);
    }
    say("Leaving\n");
    fb_close();
    if (sock >= 0) close(sock), unlink(INPUT_SOCK);
    if (log_fd >= 0) close(log_fd);
    return 0;
}
