/* Terminal platform for rdgeneric.
 *
 *   ./rd-term                 play in this terminal
 *   ./rd-term --serve 2323    host a shared world over telnet
 *
 * Each character cell carries two pixels using the upper half block, with
 * the foreground colour as the top pixel and the background as the bottom.
 * The m key cycles truecolor, 256 colours and plain ASCII, so it works on
 * anything from a modern terminal to a serial console.
 *
 * Terminals do not normally report key releases, so a key counts as held
 * for a short while after each press. Terminals that speak the kitty
 * keyboard protocol report releases, and when one answers the query the
 * hold timer is switched off.
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../../src/rd.h"
#include "../../src/rd_demo.h"

#define MAX_CLIENTS RD_MAX_PLAYERS
#define FPS 30.0

enum { K_W, K_A, K_S, K_D, K_SPACE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_COUNT };
enum { MODE_TRUE, MODE_256, MODE_ASCII, MODE_COUNT };

static const char *block_names[RD_NUM_BLOCKS] = {"Air",    "Stone", "Grass", "Dirt",
                                                 "Cobble", "Planks", "Log",  "Leaves"};

typedef struct {
    int used, in_fd, out_fd, telnet, player;
    int cols, rows, mode;
    unsigned char ib[512];
    int ilen;
    int iac, sblen;
    unsigned char sb[32];
    double hold_until[K_COUNT];
    int kitty;
    int mx, my, mouse_seen;
    rd_input pending;
    char *out;
    size_t olen, ocap, opos;
    rd_u32 *fb, *prev;
    int fbw, fbh, cells;
    int full;
    double last_frame;
    int quit;
} client;

static client clients[MAX_CLIENTS];
static volatile sig_atomic_t winch = 1, stop_flag = 0;
static struct termios saved_tio;
static int local_raw = 0;
static const char *level_path = NULL;
/* With --demo the scripted walk advances two ticks on each "." key and the
 * screen is redrawn once per step, so a recorder gets matching frames. */
static int demo_mode = 0, demo_steps = 0;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- output buffer ---- */

static void put(client *c, const char *s, size_t n) {
    if (c->olen + n > c->ocap) {
        c->ocap = (c->olen + n) * 2;
        c->out = realloc(c->out, c->ocap);
    }
    memcpy(c->out + c->olen, s, n);
    c->olen += n;
}
static void puts_(client *c, const char *s) { put(c, s, strlen(s)); }
static void putf(client *c, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    put(c, tmp, n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

static int flush_out(client *c) {
    while (c->opos < c->olen) {
        ssize_t n = write(c->out_fd, c->out + c->opos, c->olen - c->opos);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            if (errno == EINTR) continue;
            return -1;
        }
        c->opos += (size_t)n;
    }
    c->olen = c->opos = 0;
    return 0;
}

/* ---- screen setup ---- */

static const char *ENTER = "\x1b[?1049h\x1b[?25l\x1b[2J"
                           "\x1b[?1003h\x1b[?1006h" /* mouse motion, SGR coordinates */
                           "\x1b[>11u\x1b[?u";      /* kitty keyboard, then ask */
static const char *LEAVE = "\x1b[<u\x1b[?1003l\x1b[?1006l\x1b[0m\x1b[?25h\x1b[?1049l";

static void resize_buffers(client *c) {
    int w = c->cols < 8 ? 8 : c->cols, rows = c->rows - 1 < 4 ? 4 : c->rows - 1;
    c->fbw = w;
    c->fbh = rows * 2;
    c->cells = w * rows;
    c->fb = realloc(c->fb, sizeof(rd_u32) * c->fbw * c->fbh);
    c->prev = realloc(c->prev, sizeof(rd_u32) * c->cells * 2);
    c->full = 1;
}

static void open_client(client *c, int in_fd, int out_fd, int telnet) {
    memset(c, 0, sizeof *c);
    c->used = 1;
    c->in_fd = in_fd;
    c->out_fd = out_fd;
    c->telnet = telnet;
    c->cols = 80;
    c->rows = 24;
    const char *ct = getenv("COLORTERM");
    c->mode = telnet || (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) ? MODE_TRUE : MODE_256;
    c->player = rd_player_add();
    if (telnet) {
        /* WILL ECHO, WILL SUPPRESS-GO-AHEAD, DO NAWS: character mode and
         * window size reports. */
        static const unsigned char neg[] = {255, 251, 1, 255, 251, 3, 255, 253, 31};
        put(c, (const char *)neg, sizeof neg);
    }
    puts_(c, ENTER);
    resize_buffers(c);
}

static void close_client(client *c) {
    if (!c->used) return;
    puts_(c, LEAVE);
    flush_out(c);
    rd_player_remove(c->player);
    if (c->telnet) close(c->in_fd);
    free(c->out);
    free(c->fb);
    free(c->prev);
    c->used = 0;
}

/* ---- input ---- */

static void key_down(client *c, int k, double t, int repeat) {
    if (c->kitty) c->hold_until[k] = 1e300;
    else c->hold_until[k] = t + (c->hold_until[k] > t || repeat ? 0.12 : 0.6);
}
static void key_up(client *c, int k) { c->hold_until[k] = 0; }

/* Handles one key, given as a character code. Returns nothing; actions go
 * into c->pending. */
static void key(client *c, int ch, int event, double t) {
    int k = -1;
    switch (ch) {
    case 'w': case 'W': k = K_W; break;
    case 'a': case 'A': k = K_A; break;
    case 's': case 'S': k = K_S; break;
    case 'd': case 'D': k = K_D; break;
    case ' ': k = K_SPACE; break;
    }
    if (k >= 0) {
        if (event == 3) key_up(c, k);
        else key_down(c, k, t, event == 2);
        return;
    }
    if (event == 3) return;
    if (demo_mode && ch == '.') {
        rd_demo_step(2);
        demo_steps++;
        return;
    }
    if (ch >= '1' && ch <= '0' + RD_NUM_BLOCKS - 1) c->pending.select = ch - '0';
    else if (ch == 'r' || ch == 'R') c->pending.reset = 1;
    else if (ch == 'e' || ch == 'E') c->pending.place = 1;
    else if (ch == 'f' || ch == 'F') c->pending.dig = 1;
    else if (ch == 'm' || ch == 'M') c->mode = (c->mode + 1) % MODE_COUNT, c->full = 1;
    else if (ch == 'q' || ch == 'Q' || ch == 3 || ch == 4) c->quit = 1;
}

static int arrow(char f) {
    return f == 'A' ? K_UP : f == 'B' ? K_DOWN : f == 'C' ? K_RIGHT : f == 'D' ? K_LEFT : -1;
}

/* Parses one input sequence at the start of buf. Returns the bytes used,
 * or 0 when the sequence is incomplete. */
static int parse(client *c, const unsigned char *b, int n, double t) {
    if (b[0] != 27) {
        key(c, b[0], 1, t);
        return 1;
    }
    if (n < 2) return 0;
    if (b[1] == 'O') { /* SS3 arrows */
        if (n < 3) return 0;
        int k = arrow((char)b[2]);
        if (k >= 0) key_down(c, k, t, 0);
        return 3;
    }
    if (b[1] != '[') return 1; /* a lone Escape */
    int i = 2;
    while (i < n && (b[i] < 0x40 || b[i] > 0x7e)) i++;
    if (i >= n) return n > 64 ? n : 0;
    char final = (char)b[i];
    char p[64];
    int plen = i - 2 < 63 ? i - 2 : 63;
    memcpy(p, b + 2, plen);
    p[plen] = 0;

    if (p[0] == '<' && (final == 'M' || final == 'm')) { /* SGR mouse */
        int btn, x, y;
        if (sscanf(p + 1, "%d;%d;%d", &btn, &x, &y) == 3) {
            if (c->mouse_seen && (btn & 32)) {
                c->pending.yaw += (x - c->mx) * 0.05f;
                c->pending.pitch -= (y - c->my) * 0.1f;
            }
            c->mx = x, c->my = y, c->mouse_seen = 1;
            if (final == 'M' && !(btn & 32)) {
                if ((btn & 67) == 0) c->pending.dig = 1;
                else if ((btn & 67) == 2) c->pending.place = 1;
                else if ((btn & 67) == 64 || (btn & 67) == 65) {
                    int s = rd_selected(c->player) + ((btn & 1) ? 1 : -1);
                    if (s < 1) s = RD_NUM_BLOCKS - 1;
                    if (s >= RD_NUM_BLOCKS) s = 1;
                    c->pending.select = s;
                }
            }
        }
        return i + 1;
    }
    if (p[0] == '?' && final == 'u') { /* kitty protocol is supported */
        c->kitty = 1;
        return i + 1;
    }
    /* Event type is the sub-parameter after the modifiers: 1 press, 2
     * repeat, 3 release. */
    int event = 1;
    char *colon = strchr(p, ':');
    if (colon) event = atoi(colon + 1);
    if (final == 'u') {
        int code = atoi(p), mods = 1;
        char *semi = strchr(p, ';');
        if (semi) mods = atoi(semi + 1);
        if (code == 27) return i + 1;
        if (((mods - 1) & 4) && (code == 'c' || code == 'd')) code = 3;
        key(c, code, event, t);
        return i + 1;
    }
    int k = arrow(final);
    if (k >= 0) {
        if (event == 3) key_up(c, k);
        else key_down(c, k, t, event == 2);
    }
    return i + 1;
}

/* Strips telnet commands and keeps window size reports. */
static int telnet_filter(client *c, unsigned char *b, int n) {
    int o = 0;
    for (int i = 0; i < n; i++) {
        unsigned char x = b[i];
        switch (c->iac) {
        case 0:
            if (x == 255) c->iac = 1;
            else if (x != 0) b[o++] = x;
            break;
        case 1:
            if (x == 250) c->iac = 3, c->sblen = 0;
            else if (x >= 251 && x <= 254) c->iac = 2;
            else c->iac = 0;
            break;
        case 2: c->iac = 0; break;
        case 3:
            if (x == 255) c->iac = 4;
            else if (c->sblen < (int)sizeof c->sb) c->sb[c->sblen++] = x;
            break;
        case 4:
            if (x == 240) {
                if (c->sblen >= 5 && c->sb[0] == 31) {
                    c->cols = c->sb[1] << 8 | c->sb[2];
                    c->rows = c->sb[3] << 8 | c->sb[4];
                    resize_buffers(c);
                }
                c->iac = 0;
            } else {
                if (c->sblen < (int)sizeof c->sb) c->sb[c->sblen++] = x;
                c->iac = 3;
            }
            break;
        }
    }
    return o;
}

static int read_input(client *c, double t) {
    unsigned char tmp[512];
    ssize_t n = read(c->in_fd, tmp, sizeof tmp);
    if (n == 0) return -1;
    if (n < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    int m = c->telnet ? telnet_filter(c, tmp, (int)n) : (int)n;
    for (int i = 0; i < m; i++) {
        if (c->ilen < (int)sizeof c->ib) c->ib[c->ilen++] = tmp[i];
    }
    int pos = 0;
    while (pos < c->ilen) {
        int used = parse(c, c->ib + pos, c->ilen - pos, t);
        if (!used) break;
        pos += used;
    }
    memmove(c->ib, c->ib + pos, c->ilen - pos);
    c->ilen -= pos;
    return 0;
}

static void feed_player(client *c, double t) {
    rd_input in = c->pending;
#define H(k) (c->hold_until[k] > t ? 1.0f : 0.0f)
    in.forward = H(K_W) - H(K_S);
    in.strafe = H(K_D) - H(K_A);
    in.jump = c->hold_until[K_SPACE] > t;
    in.yaw += (H(K_RIGHT) - H(K_LEFT)) * 0.045f;
    in.pitch += (H(K_UP) - H(K_DOWN)) * 0.035f;
#undef H
    rd_player_input(c->player, &in);
    memset(&c->pending, 0, sizeof c->pending);
}

/* ---- drawing ---- */

static int to256(rd_u32 p) {
    int r = ((p >> 16 & 255) * 5 + 127) / 255, g = ((p >> 8 & 255) * 5 + 127) / 255,
        b = ((p & 255) * 5 + 127) / 255;
    return 16 + 36 * r + 6 * g + b;
}

static void draw(client *c, int nplayers) {
    rd_render(c->player, c->fb, c->fbw, c->fbh, c->fbw);
    int w = c->fbw, rows = c->fbh / 2;
    if (c->full) {
        puts_(c, "\x1b[0m\x1b[2J");
        memset(c->prev, 0xff, sizeof(rd_u32) * c->cells * 2);
        c->full = 0;
    }
    int cx = -1, cy = -1;
    long fg = -1, bg = -1;
    static const char ramp[] = " .:-=+*#%@";
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < w; x++) {
            rd_u32 top = c->fb[(2 * y) * w + x], bot = c->fb[(2 * y + 1) * w + x];
            if (c->mode == MODE_256) top = (rd_u32)to256(top), bot = (rd_u32)to256(bot);
            if (c->mode == MODE_ASCII) {
                int l = 0;
                for (int k = 0; k < 2; k++) {
                    rd_u32 q = k ? bot : top;
                    l += (int)((q >> 16 & 255) * 3 + (q >> 8 & 255) * 6 + (q & 255)) / 10;
                }
                top = (rd_u32)ramp[l * 9 / 510];
                bot = 0;
            }
            rd_u32 *pv = &c->prev[(y * w + x) * 2];
            if (pv[0] == top && pv[1] == bot) continue;
            pv[0] = top, pv[1] = bot;
            if (cx != x || cy != y) putf(c, "\x1b[%d;%dH", y + 1, x + 1);
            if (c->mode == MODE_ASCII) {
                char ch = (char)top;
                put(c, &ch, 1);
            } else {
                if ((long)top != fg) {
                    if (c->mode == MODE_TRUE)
                        putf(c, "\x1b[38;2;%u;%u;%um", top >> 16 & 255, top >> 8 & 255, top & 255);
                    else
                        putf(c, "\x1b[38;5;%um", top);
                    fg = (long)top;
                }
                if ((long)bot != bg) {
                    if (c->mode == MODE_TRUE)
                        putf(c, "\x1b[48;2;%u;%u;%um", bot >> 16 & 255, bot >> 8 & 255, bot & 255);
                    else
                        putf(c, "\x1b[48;5;%um", bot);
                    bg = (long)bot;
                }
                puts_(c, "\xe2\x96\x80");
            }
            cx = x + 1, cy = y;
        }
    float px, py, pz;
    rd_player_pos(c->player, &px, &py, &pz);
    static const char *mode_names[] = {"truecolor", "256", "ascii"};
    char status[256];
    snprintf(status, sizeof status,
             " rdgeneric  %.0f %.0f %.0f  block %s  players %d  %s  %s",
             px, py, pz, block_names[rd_selected(c->player)], nplayers, mode_names[c->mode],
             c->kitty ? "keys exact" : "keys timed");
    putf(c, "\x1b[0m\x1b[%d;1H\x1b[7m%-*.*s\x1b[0m", rows + 1, w, w, status);
}

/* ---- level file, raw blocks ---- */

static void load_level(void) {
    if (!level_path) return;
    FILE *f = fopen(level_path, "rb");
    if (!f) return;
    if (fread(rd_blocks(), 1, (size_t)rd_world_bytes(), f) == (size_t)rd_world_bytes()) rd_relight();
    fclose(f);
}
static void save_level(void) {
    if (!level_path) return;
    FILE *f = fopen(level_path, "wb");
    if (!f) return;
    fwrite(rd_blocks(), 1, (size_t)rd_world_bytes(), f);
    fclose(f);
}

/* ---- main loop ---- */

static void on_winch(int s) { (void)s; winch = 1; }
static void on_stop(int s) { (void)s; stop_flag = 1; }

static void restore_tty(void) {
    if (local_raw) tcsetattr(0, TCSAFLUSH, &saved_tio);
}

static void usage(void) {
    puts("usage: rd-term [--serve PORT] [--flat] [--seed N] [--level FILE] [--view N] [--demo]\n"
         "keys: WASD move, arrows or mouse look, space jump, click or F dig,\n"
         "      right click or E place, 1-7 pick block, R respawn, M colour mode, Q quit");
}

int main(int argc, char **argv) {
    int port = 0, style = RD_HILLS, view = 40;
    unsigned seed = (unsigned)time(NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--serve") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--flat")) style = RD_FLAT;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level_path = argv[++i];
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) view = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--demo")) demo_mode = 1;
        else { usage(); return 1; }
    }
    rd_init(seed, style);
    rd_set_view_distance(view);
    load_level();
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    signal(SIGPIPE, SIG_IGN);

    int lfd = -1;
    if (port) {
        lfd = socket(AF_INET6, SOCK_STREAM, 0);
        int one = 1, zero = 0;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        struct sockaddr_in6 a = {0};
        a.sin6_family = AF_INET6;
        a.sin6_port = htons((unsigned short)port);
        a.sin6_addr = in6addr_any;
        if (bind(lfd, (struct sockaddr *)&a, sizeof a) || listen(lfd, 8)) {
            perror("listen");
            return 1;
        }
        fcntl(lfd, F_SETFL, O_NONBLOCK);
        fprintf(stderr, "rdgeneric: serving on port %d, connect with: telnet localhost %d\n", port, port);
    } else {
        if (!isatty(0)) { fputs("rd-term needs a terminal\n", stderr); return 1; }
        tcgetattr(0, &saved_tio);
        struct termios raw = saved_tio;
        cfmakeraw(&raw);
        tcsetattr(0, TCSAFLUSH, &raw);
        local_raw = 1;
        atexit(restore_tty);
        signal(SIGWINCH, on_winch);
        fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
        open_client(&clients[0], 0, 1, 0);
        if (demo_mode) clients[0].player = rd_demo_start();
    }

    double last_tick = now(), last_save = now();
    while (!stop_flag) {
        struct pollfd pf[MAX_CLIENTS + 1];
        int map[MAX_CLIENTS + 1], n = 0;
        if (lfd >= 0) pf[n].fd = lfd, pf[n].events = POLLIN, map[n++] = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].used) {
                pf[n].fd = clients[i].in_fd;
                pf[n].events = POLLIN;
                map[n++] = i;
            }
        poll(pf, (nfds_t)n, 5);
        double t = now();

        for (int k = 0; k < n; k++) {
            if (map[k] < 0) {
                if (pf[k].revents & POLLIN) {
                    int fd = accept(lfd, NULL, NULL);
                    if (fd < 0) continue;
                    int slot = -1;
                    for (int i = 0; i < MAX_CLIENTS; i++)
                        if (!clients[i].used) { slot = i; break; }
                    if (slot < 0) {
                        const char *full = "rdgeneric: the world is full\r\n";
                        if (write(fd, full, strlen(full)) < 0) {}
                        close(fd);
                        continue;
                    }
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    open_client(&clients[slot], fd, fd, 1);
                }
                continue;
            }
            client *c = &clients[map[k]];
            if ((pf[k].revents & (POLLIN | POLLHUP | POLLERR)) && read_input(c, t) < 0) c->quit = 1;
        }

        if (winch && !port) {
            struct winsize ws;
            if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) {
                clients[0].cols = ws.ws_col;
                clients[0].rows = ws.ws_row;
                resize_buffers(&clients[0]);
            }
            winch = 0;
        }

        int active = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client *c = &clients[i];
            if (!c->used) continue;
            if (c->quit) {
                close_client(c);
                if (!port) stop_flag = 1;
                continue;
            }
            active++;
        }

        /* Fixed 60 Hz simulation, as rd-132211's Timer(60). */
        int steps = 0;
        while (!demo_mode && t - last_tick >= 1.0 / 60 && steps < 10) {
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (clients[i].used) feed_player(&clients[i], last_tick);
            rd_tick();
            last_tick += 1.0 / 60;
            steps++;
        }
        if (t - last_tick > 0.5) last_tick = t;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            client *c = &clients[i];
            if (!c->used) continue;
            static int drawn_steps = -1;
            if (demo_mode) {
                if (demo_steps != drawn_steps && !c->olen) {
                    draw(c, active);
                    drawn_steps = demo_steps;
                    /* An invisible title change marks the end of each
                     * frame, so a recorder knows which step it has. */
                    putf(c, "\x1b]0;rdgeneric frame %d\x07", drawn_steps);
                }
            } else if (!c->olen && t - c->last_frame >= 1.0 / FPS) {
                draw(c, active);
                c->last_frame = t;
            }
            if (flush_out(c) < 0) c->quit = 1;
        }
        if (port && t - last_save > 60) save_level(), last_save = t;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) close_client(&clients[i]);
    save_level();
    return 0;
}
