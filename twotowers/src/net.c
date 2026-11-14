/* (2.1) Online play: two to four machines -- Windows and Linux alike -- playing one co-op (or Versus) game together.
 *
 * Lockstep: every machine runs the whole game, all players' characters included, and they stay the same game because
 * each one is given exactly the same input at every step of the world. The game's own clock is a fixed step online
 * (twotowers_manual.c, tt_frametime and tt_sim_tick) and its math our own (rc_fastmath.h), so a step's input is all that
 * moves it: a few bytes per player per step, where the game's memory changes by over a megabyte a step.
 *
 * The host is the hub (only its port needs to be reachable): each joiner sends it his own pad, a few steps ahead of
 * when it is used (TT_NET_DELAY, 4 steps), and the host sends every machine all the players' pads for each step once
 * it has them. Each packet carries the last several steps again, so a lost one costs nothing. Each machine also sends
 * a hash of its world now and then: if two differ, the machines have parted ("[NET] desync" in the log, a note on the
 * screen) -- the players can vote to restart the level.
 *
 * In play (screens 6 and 8) the machines keep in step; each entry into play is a meeting point (all wait there for all).
 * Out of play -- the menus, a level's loading -- each machine goes its own way with its own pad.
 *
 * Nobody pauses an online game: Start is kept from the game and opens the online menu over the running game instead
 * (vote to restart the level, or to leave). A vote carried by every player takes effect on every machine at the same step.
 *
 * Set up: TT_NET=host (port TT_NET_PORT, 27960) or TT_NET=join:<address>[:<port>]; or online.ini beside the game
 * (role=host|join, address=, port=, players=, delay=). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tt_env.h"
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_sock;
#define NET_BAD INVALID_SOCKET
static void net_sleep_ms(int ms) { Sleep((DWORD)ms); }
/* (2.1) a short wait for the other machines' pads: Sleep(1) is a whole timer tick on Windows (15.6 ms where the period
 * isn't granted -- Windows 11 grants it per process or not at all): a frame waited for one was a frame lost, under 40 a
 * second online. The runtime's high-resolution timer instead (about half a millisecond) */
static void net_sleep_short(void) { extern unsigned long xbox_hr_sleep_100ns(long long t, int alertable); xbox_hr_sleep_100ns(2500, 0); }
static uint32_t net_ms(void) { return (uint32_t)GetTickCount(); }
static double net_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1.0e6 / (double)f.QuadPart; }
static int net_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
typedef int net_sock;
#define NET_BAD (-1)
#define closesocket close
static void net_sleep_ms(int ms) { struct timespec t; t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&t, NULL); }
static void net_sleep_short(void) { struct timespec t; t.tv_sec = 0; t.tv_nsec = 250000L; nanosleep(&t, NULL); }
static uint32_t net_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u); }
static double net_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1.0e6 + (double)t.tv_nsec / 1000.0; }
static int net_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

extern void tt_dbg(const char *fmt, ...);
extern void xbox_BannerSet(const char *line1, const char *line2);

/* the input layer's pad (src/input/xinput_xbox.h) */
typedef struct { uint32_t dwPacketNumber; struct { uint16_t wButtons; uint8_t bAnalogButtons[8]; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } Gamepad; } NET_PAD_STATE;
extern unsigned long xbox_InputGetStateLocal(unsigned long port, NET_PAD_STATE *st);
extern int (*volatile xbox_pad_net)(unsigned long port, NET_PAD_STATE *st);

#define NET_MAGIC 0x54544E31u   /* "TTN1" */
#define NET_VER 1u
#define NET_MAXP 4
#define NET_RING 256            /* steps of pads kept */
#define NET_REC 20              /* bytes per player per step */
#define NET_RESEND 12           /* steps sent again in each packet */
static uint32_t s_host_oop_t;   /* (2.1, joiner) when the host last said he was out of play (his WELCOME, sent only then) */
enum { PK_HELLO = 1, PK_WELCOME, PK_READY, PK_GO, PK_INPUT, PK_FRAME, PK_BYE, PK_PING, PK_LOBBY, PK_SAVE, PK_CHAT, PK_P2UPG };
enum { NB_VOTE_RESTART = 1, NB_VOTE_QUIT = 2, NB_MENU = 4, NB_BRING = 8 };   /* (NB_BRING: the host's "bring the others to me", held a few steps) */   /* a record's flags byte */

typedef struct { uint8_t b[NET_REC]; } net_rec;

static int s_role;              /* 0 off, 1 host, 2 joiner */
static net_sock s_sock = NET_BAD;
static struct sockaddr_storage s_peer[NET_MAXP]; static socklen_t s_peer_len[NET_MAXP];   /* (host: each joiner's; joiner: [0] the host) */
static int s_have_peer[NET_MAXP];
static uint32_t s_peer_seen[NET_MAXP];
static int s_me, s_np = 2, s_delay = 3, s_welcomed;   /* (3 steps: 50 ms at 60 a second) */
static uint32_t s_seed;
static uint8_t s_settings[64];  /* the host's choices, for every machine (net_settings_*) */
static int s_post_wait;   /* (2.1) player 2 done with his upgrades: the Online menu says so */
static void net_setenv(const char *k, const char *v);
static int s_active;            /* in step: the world is in play and every machine is here */
static uint32_t s_entry;        /* meetings so far (each entry into play) */
static uint32_t s_step;         /* the step of this meeting */
static net_rec s_in[NET_RING][NET_MAXP];
static uint8_t s_have[NET_RING][NET_MAXP];   /* (the step's number + 1 in the low byte check: s_have_step) */
static uint32_t s_have_step[NET_RING][NET_MAXP];
static net_rec s_cur[NET_MAXP]; static int s_cur_ok;   /* the pads of the step being run */
static uint32_t s_hash[NET_RING]; static uint32_t s_hash_step[NET_RING];
static uint32_t s_ready_mask;   /* (host) who is at the meeting */
static int s_desync_said;
#define NET_CLKW 24
static uint32_t s_clk[NET_CLKW];   /* the game's clocks (0x4B8B70-0x4B8BCF) at the meeting: the host's, for every machine */
static uint8_t s_myflags;       /* this machine's flags for its next record (votes, its menu open) */
static uint8_t s_flags_seen[NET_MAXP];
static volatile int s_menu_open;
static uint32_t s_ban_until;   /* (a passing note: cleared then) */
static void note(const char *a, const char *b) { xbox_BannerSet(a, b); s_ban_until = net_ms() + 5000u; if (!s_ban_until) s_ban_until = 1; }

static int s_lobby;             /* the Online menu's lobby: connected, no game yet */
static volatile int s_host_gone;
static uint32_t s_gone_mask;   /* (host, in a game: the players gone -- their pads at rest) */   /* (a joiner: the host said goodbye) */
static int s_lobby_page;        /* (back from a game: the Online menu to open at the main menu) */
static int s_want_slot;         /* (a joiner back after a relaunch: his place) */
static uint8_t s_lb_hero[NET_MAXP];   /* (host) each player's hero in the lobby */
static uint8_t s_my_hero;
static uint8_t s_my_ready, s_lb_ready[NET_MAXP], s_lb_saveok[NET_MAXP];   /* (the lobby: each player's Ready; a joiner has the host's save) */
#define NET_SAVE_A 0x4AB598u   /* the save's progress: every hero's record (levels, experience, upgrades), as Versus keeps it */
#define NET_SAVE_N 0x11A8u
static uint8_t s_save[NET_SAVE_N]; static uint8_t s_save_have[(NET_SAVE_N + 1023) / 1024]; static int s_save_ok, s_save_apply;
static char s_p1key[32] = "aragorn";
int tt_net_on(void) { return s_role != 0 && !s_lobby; }
int tt_net_me(void) { return s_me; }
int tt_net_players(void) { return s_np; }
int tt_net_active(void) { return s_active; }
uint32_t tt_net_step_now(void) { return s_step; }
uint32_t tt_net_entry(void) { return s_entry; }

/* ---- the socket ---- */
static int net_open(int port)
{
    struct sockaddr_in a;
#if defined(_WIN32)
    static int wsa; WSADATA wd; u_long nb = 1;
    if (!wsa) { if (WSAStartup(MAKEWORD(2, 2), &wd)) return 0; wsa = 1; }
#endif
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock == NET_BAD) return 0;
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)port);
    if (bind(s_sock, (struct sockaddr *)&a, sizeof a) != 0) { tt_dbg("[NET] can't use port %d\n", port); closesocket(s_sock); s_sock = NET_BAD; return 0; }
#if defined(_WIN32)
    ioctlsocket(s_sock, FIONBIO, &nb);
    {   /* (2.1) Windows: an "unreachable" answer to one packet (the host not up yet, a player gone) made every later read fail at
         * once with WSAECONNRESET -- the read loop stopped there each frame and the other side's packets were never read: off */
        DWORD no = 0, ret = 0;
        WSAIoctl(s_sock, _WSAIOW(IOC_VENDOR, 12), &no, sizeof no, NULL, 0, &ret, NULL, NULL); }
#else
    fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    return 1;
}
static int net_resolve(const char *host, int port, struct sockaddr_storage *out, socklen_t *len)
{
    struct addrinfo hint, *res = NULL; char ps[16];
    memset(&hint, 0, sizeof hint); hint.ai_family = AF_INET; hint.ai_socktype = SOCK_DGRAM;
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hint, &res) != 0 || !res) return 0;
    memcpy(out, res->ai_addr, res->ai_addrlen); *len = (socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    return 1;
}
static void net_send_to(int peer, const uint8_t *b, int n)
{
    if (s_sock == NET_BAD || !s_have_peer[peer]) return;
    sendto(s_sock, (const char *)b, n, 0, (struct sockaddr *)&s_peer[peer], s_peer_len[peer]);
}
static int same_addr(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
    return x->sin_family == y->sin_family && x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
}

/* ---- packets: little-endian, a header of magic, version, type, entry ---- */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int head(uint8_t *b, int type) { put32(b, NET_MAGIC); b[4] = (uint8_t)NET_VER; b[5] = (uint8_t)type; b[6] = (uint8_t)s_me; b[7] = 0; put32(b + 8, s_entry); return 12; }

static void rec_from_pad(net_rec *r, const NET_PAD_STATE *s, uint8_t flags)
{
    int k;
    r->b[0] = (uint8_t)s->Gamepad.wButtons; r->b[1] = (uint8_t)(s->Gamepad.wButtons >> 8);
    for (k = 0; k < 8; k++) r->b[2 + k] = s->Gamepad.bAnalogButtons[k];
    r->b[10] = (uint8_t)s->Gamepad.sThumbLX; r->b[11] = (uint8_t)((uint16_t)s->Gamepad.sThumbLX >> 8);
    r->b[12] = (uint8_t)s->Gamepad.sThumbLY; r->b[13] = (uint8_t)((uint16_t)s->Gamepad.sThumbLY >> 8);
    r->b[14] = (uint8_t)s->Gamepad.sThumbRX; r->b[15] = (uint8_t)((uint16_t)s->Gamepad.sThumbRX >> 8);
    r->b[16] = (uint8_t)s->Gamepad.sThumbRY; r->b[17] = (uint8_t)((uint16_t)s->Gamepad.sThumbRY >> 8);
    r->b[18] = flags; r->b[19] = 1;   /* (present) */
}
static void pad_from_rec(NET_PAD_STATE *s, const net_rec *r, uint32_t packet)
{
    int k;
    s->dwPacketNumber = packet;
    s->Gamepad.wButtons = (uint16_t)r->b[0];   /* (2.1: b[1], the pad's unused high byte, is the host's time for the step) */
    for (k = 0; k < 8; k++) s->Gamepad.bAnalogButtons[k] = r->b[2 + k];
    s->Gamepad.sThumbLX = (int16_t)(r->b[10] | r->b[11] << 8); s->Gamepad.sThumbLY = (int16_t)(r->b[12] | r->b[13] << 8);
    s->Gamepad.sThumbRX = (int16_t)(r->b[14] | r->b[15] << 8); s->Gamepad.sThumbRY = (int16_t)(r->b[16] | r->b[17] << 8);
}
static void store(uint32_t step, int p, const net_rec *r)
{
    int i = (int)(step % NET_RING);
    if (p < 0 || p >= NET_MAXP) return;
    if (s_have_step[i][p] == step + 1u) return;
    s_in[i][p] = *r; s_have_step[i][p] = step + 1u;
}
static int have(uint32_t step, int p) { return s_have_step[step % NET_RING][p] == step + 1u; }
static int have_all(uint32_t step) { int p; for (p = 0; p < s_np; p++) if (!have(step, p)) return 0; return 1; }

/* ---- the hashes: the host's world against each joiner's ---- */
static void note_hash(uint32_t step, uint32_t h) { s_hash[step % NET_RING] = h; s_hash_step[step % NET_RING] = step + 1u; }
static void check_hash(int who, uint32_t step, uint32_t h)
{
    int i = (int)(step % NET_RING);
    if (!step || s_hash_step[i] != step + 1u || s_hash[i] == h || s_desync_said) return;
    s_desync_said = 1;
    {   /* (2.1) the desync's own file too (online_desync.txt beside the game): the log is moved aside when the game starts again */
        extern FILE *volatile tt_dbg_tee; FILE *f = fopen("online_desync.txt", "w"); tt_dbg_tee = f;
        tt_dbg("[NET] desync: player %d's world and this one's parted by step %u of meeting %u (%08X here, %08X there) -- I am player %d\n", who + 1, step, s_entry, s_hash[i], h, s_me + 1);
        { extern void tt_net_dump_recent(void); tt_net_dump_recent(); }
        tt_dbg_tee = NULL; if (f) fclose(f); }
    xbox_BannerSet("ONLINE: the games have drifted apart", "Press Start and vote to restart the level"); s_ban_until = net_ms() + 15000u;
}
static uint32_t s_last_hash_step, s_last_hash;   /* (the newest step whose hash this machine knows) */

/* ---- sending ---- */
static void send_input(void)   /* (joiner) his own pads for the steps ahead, the newest hash */
{
    uint8_t b[12 + 16 + NET_RESEND * NET_REC]; int n = head(b, PK_INPUT), k, cnt = 0;
    uint32_t last = s_step + (uint32_t)s_delay, first = last + 1u >= NET_RESEND ? last + 1u - NET_RESEND : 0;
    put32(b + n, first); n += 4; n += 4;
    put32(b + n, s_last_hash_step); put32(b + n + 4, s_last_hash); n += 8;
    for (k = 0; first + (uint32_t)k <= last; k++) { if (!have(first + (uint32_t)k, s_me)) break; memcpy(b + n, s_in[(first + k) % NET_RING][s_me].b, NET_REC); n += NET_REC; cnt++; }
    put32(b + 16, (uint32_t)cnt);
    net_send_to(0, b, n);
}
static void send_frames(void)  /* (host) every player's pads, for the newest steps that all have */
{
    uint8_t b[12 + 20 + NET_RESEND * NET_REC * NET_MAXP]; int n, k, p, cnt = 0, q;
    uint32_t last = s_step + (uint32_t)s_delay, first, low = s_step + 1u > NET_RESEND ? s_step + 1u - NET_RESEND : 0;
    /* (2.1) down past this step too: a host waiting here for a player still sends the steps before it -- the ones he may have
     * lost and waits for himself (it sent nothing: both waited for good, a game on a LAN after a few steps) */
    while (last > low && !have_all(last)) last--;
    if (!have_all(last)) return;
    first = last + 1u >= NET_RESEND ? last + 1u - NET_RESEND : 0;
    while (first < last && !have_all(first)) first++;
    n = head(b, PK_FRAME); put32(b + n, first); n += 4; n += 4; b[n++] = (uint8_t)s_np; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    put32(b + n, s_last_hash_step); put32(b + n + 4, s_last_hash); n += 8;
    for (k = 0; first + (uint32_t)k <= last; k++) { for (p = 0; p < s_np; p++) { memcpy(b + n, s_in[(first + k) % NET_RING][p].b, NET_REC); n += NET_REC; } cnt++; }
    put32(b + 16, (uint32_t)cnt);
    for (q = 1; q < NET_MAXP; q++) net_send_to(q, b, n);
}
static void send_simple(int to, int type)
{
    uint8_t b[12 + 4 * NET_CLKW + 80]; int n = head(b, type);
    if (type == PK_GO) { int k; for (k = 0; k < NET_CLKW; k++) { put32(b + n, s_clk[k]); n += 4; } }
    if (type == PK_HELLO || type == PK_LOBBY) { b[n++] = (uint8_t)s_want_slot; b[n++] = s_my_hero; b[n++] = s_my_ready; b[n++] = (uint8_t)s_save_ok; }
    if (type == PK_WELCOME) { b[n++] = (uint8_t)to; b[n++] = (uint8_t)s_np; b[n++] = (uint8_t)s_delay; b[n++] = 0; put32(b + n, s_seed); n += 4; memcpy(b + n, s_settings, sizeof s_settings); n += (int)sizeof s_settings; }
    if (to < 0) { int q; for (q = 1; q < NET_MAXP; q++) net_send_to(q, b, n); } else net_send_to(to, b, n);
}

/* ---- receiving ---- */
static void chat_recv(const uint8_t *b, int n, int from_host);
static void on_packet(const uint8_t *b, int n, const struct sockaddr_storage *from, socklen_t flen)
{
    int type, who, k, p; uint32_t entry;
    if (n < 12 || get32(b) != NET_MAGIC || b[4] != NET_VER) return;
    type = b[5]; who = b[6]; entry = get32(b + 8);
    if (s_role == 1) {   /* host */
        if (type == PK_HELLO) {
            int slot = -1;
            for (p = 1; p < NET_MAXP; p++) if (s_have_peer[p] && same_addr(&s_peer[p], from)) slot = p;
            if (slot < 0 && n >= 14 && b[12] >= 1 && b[12] < s_np && (!s_have_peer[b[12]] || net_ms() - s_peer_seen[b[12]] > 1500u)) {   /* (or his game started afresh: a new port) */ p = b[12]; slot = p; memcpy(&s_peer[p], from, flen); s_peer_len[p] = flen; s_have_peer[p] = 1; s_gone_mask &= ~(1u << p); tt_dbg("[NET] player %d back\n", p + 1); }
            for (p = 1; slot < 0 && p < s_np; p++) if (!s_have_peer[p]) { slot = p; memcpy(&s_peer[p], from, flen); s_peer_len[p] = flen; s_have_peer[p] = 1;
                tt_dbg("[NET] player %d joined\n", p + 1); { char l[64]; snprintf(l, sizeof l, "ONLINE: player %d joined", p + 1); note(l, ""); } }
            { static int nl; if (slot <= 0 && nl++ < 5) tt_dbg("[NET] a HELLO not taken (n %d, slot asked %d, players %d, have %d, seen %u ms ago)\n", n, n >= 14 ? b[12] : -1, s_np, n >= 14 && b[12] < NET_MAXP ? s_have_peer[b[12]] : -1, n >= 14 && b[12] < NET_MAXP ? net_ms() - s_peer_seen[b[12]] : 0); }
            if (slot > 0) { s_peer_seen[slot] = net_ms(); if (n >= 14) s_lb_hero[slot] = b[13]; send_simple(slot, PK_WELCOME); }
            return;
        }
        if (who <= 0 || who >= NET_MAXP || !s_have_peer[who] || !same_addr(&s_peer[who], from)) return;
        s_peer_seen[who] = net_ms();
        if (type == PK_CHAT) { chat_recv(b, n, 0); return; }
        if (type == PK_P2UPG && n >= 13 + 0x348 + 0x50) {   /* (2.1) player 2's upgrades from his machine's shop */
            static uint32_t last_id; uint32_t id = get32(b + 8);
            if (id != last_id) { extern void tt_net_p2_upg_apply(int b, const uint8_t *rec, const uint8_t *blk); last_id = id; tt_net_p2_upg_apply(b[12], b + 13, b + 13 + 0x348); }
            return; }
        if (type == PK_READY && entry == s_entry) s_ready_mask |= 1u << who;
        if (type == PK_READY && entry == s_entry && s_active) send_simple(who, PK_GO);   /* (his GO was lost) */
        if (type == PK_INPUT && entry == s_entry && n >= 28) {
            uint32_t first = get32(b + 12), cnt = get32(b + 16); const uint8_t *r = b + 28;
            check_hash(who, get32(b + 20), get32(b + 24));
            for (k = 0; k < (int)cnt && r + NET_REC <= b + n; k++, r += NET_REC) store(first + (uint32_t)k, who, (const net_rec *)r);
        }
        if (type == PK_LOBBY && n >= 14) s_lb_hero[who] = b[13];
        if (type == PK_LOBBY && n >= 16) { s_lb_ready[who] = b[14]; s_lb_saveok[who] = b[15]; }
        if (type == PK_BYE) { char l[64]; tt_dbg("[NET] player %d left\n", who + 1); s_have_peer[who] = 0; s_gone_mask |= 1u << who; snprintf(l, sizeof l, "ONLINE: player %d left the game", who + 1); note(l, ""); }
        return;
    }
    /* joiner: from the host only */
    if (!s_have_peer[0] || !same_addr(&s_peer[0], from)) return;
    s_peer_seen[0] = net_ms();
    if (type == PK_WELCOME && n >= 12 + 8 + (int)sizeof s_settings) {
        s_host_oop_t = net_ms() | 1u;
        memcpy(s_settings, b + 20, sizeof s_settings); s_np = b[13];
        if (!s_welcomed) { s_me = b[12]; s_np = b[13]; s_delay = b[14]; s_seed = get32(b + 16); s_welcomed = 1;
            tt_dbg("[NET] in the host's game: player %d of %d, %d steps of delay\n", s_me + 1, s_np, s_delay);
            { char l[64]; snprintf(l, sizeof l, "ONLINE: connected -- you are player %d of %d", s_me + 1, s_np); note(l, ""); }
            { extern uint32_t tt_net_seed_v; tt_net_seed_v = s_seed; } }
        return;
    }
    if (type == PK_BYE) { tt_dbg("[NET] the host closed the game\n"); s_host_gone = 1; return; }
    if (type == PK_CHAT) { chat_recv(b, n, 1); return; }
    if (type == PK_SAVE && n >= 22) {   /* (the host's save, in pieces of 1 KB) */
        uint32_t off = get32(b + 12), len = (uint32_t)(b[16] | b[17] << 8); int k;
        if (off + len <= NET_SAVE_N && n >= 22 + (int)len && off % 1024u == 0) { memcpy(s_save + off, b + 22, len); s_save_have[off / 1024u] = 1;
            for (k = 0; k < (int)sizeof s_save_have && s_save_have[k]; k++) ; if (k == (int)sizeof s_save_have && !s_save_ok) { s_save_ok = 1; tt_dbg("[NET] the host's save is here\n"); } }
        return;
    }
    if (type == PK_GO && entry == s_entry && n >= 12 + 4 * NET_CLKW) { int k; for (k = 0; k < NET_CLKW; k++) s_clk[k] = get32(b + 12 + 4 * k); s_ready_mask |= 2u; }   /* (go, with the host's clocks) */
    if (type == PK_FRAME && entry == s_entry && n >= 36) {
        uint32_t first = get32(b + 12), cnt = get32(b + 16); int np = b[20]; const uint8_t *r = b + 32;
        check_hash(0, get32(b + 24), get32(b + 28));
        for (k = 0; k < (int)cnt; k++) for (p = 0; p < np && p < NET_MAXP && r + NET_REC <= b + n; p++, r += NET_REC) store(first + (uint32_t)k, p, (const net_rec *)r);
    }
}
static void net_pump(void)
{
    uint8_t b[2048]; int i;
    if (s_sock == NET_BAD) return;
    for (i = 0; i < 256; i++) {
        struct sockaddr_storage from; socklen_t fl = sizeof from; int n;
        n = (int)recvfrom(s_sock, (char *)b, sizeof b, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) { if (n < 0 && !net_would_block() && i < 255) continue; break; }   /* (2.1) an error on one packet: read on */
        {   /* (test) TT_NET_DROP=percent: packets lost on the way, as on a busy network */
            static int drop = -1; static uint32_t r = 12345u; if (drop < 0) drop = getenv("TT_NET_DROP") ? atoi(getenv("TT_NET_DROP")) : 0;
            if (drop > 0) { r = r * 1103515245u + 12345u; if ((int)((r >> 16) % 100u) < drop) continue; } }
        if (getenv("TT_NET_PKTLOG")) { static uint32_t cnt[16], lt; if (n > 5 && b[5] < 16) cnt[b[5]]++; if (net_ms() - lt > 3000u) { int q; lt = net_ms(); tt_dbg("[NETPKT] role %d lobby %d screen in:", s_role, s_lobby); for (q = 0; q < 12; q++) tt_dbg(" %u", cnt[q]); tt_dbg("\n"); } }
        on_packet(b, n, &from, fl);
    }
}

/* ---- start ---- */
static void net_ini(char *role, int rn, char *addr, int an, int *port, int *players, int *delay)
{
    FILE *f = fopen("online.ini", "r"); char l[256];
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        char *e = strchr(l, '='), *v; size_t k;
        if (!e || l[0] == '#' || l[0] == ';') continue;
        *e = 0; v = e + 1; k = strlen(v); while (k && (v[k - 1] == '\n' || v[k - 1] == '\r' || v[k - 1] == ' ')) v[--k] = 0;
        while (*v == ' ') v++;
        { char *t = l + strlen(l); while (t > l && t[-1] == ' ') *--t = 0; }
        if (!strcmp(l, "role")) snprintf(role, (size_t)rn, "%s", v);
        else if (!strcmp(l, "address")) snprintf(addr, (size_t)an, "%s", v);
        else if (!strcmp(l, "port") && atoi(v) > 0) *port = atoi(v);
        else if (!strcmp(l, "players") && atoi(v) >= 2) *players = atoi(v);
        else if (!strcmp(l, "delay") && atoi(v) >= 1) *delay = atoi(v);
    }
    fclose(f);
}
static int net_pad_hook(unsigned long port, NET_PAD_STATE *st);
void tt_net_bye(void);
static char s_host_addr[160] = "127.0.0.1"; static int s_port = 27960;
static int net_start_host(int port, int players)
{
    extern int tt_coop_nplayers(void);
    if (s_sock != NET_BAD) { closesocket(s_sock); s_sock = NET_BAD; }
    memset(s_have_peer, 0, sizeof s_have_peer);
    if (!net_open(port)) { note("ONLINE: that port is in use", ""); return 0; }
    s_role = 1; s_me = 0; s_np = players >= 2 && players <= 4 ? players : tt_coop_nplayers(); s_welcomed = 1;
    { extern uint32_t tt_net_seed_v; s_seed = net_ms() * 2654435761u ^ 0x5EEDu; if (getenv("TT_NET_SEED")) s_seed = (uint32_t)strtoul(getenv("TT_NET_SEED"), NULL, 0); tt_net_seed_v = s_seed; }
    tt_dbg("[NET] hosting on port %d for %d players (%d steps of delay)%s\n", port, s_np, s_delay, s_lobby ? " -- the lobby" : "");
    return 1;
}
static int net_start_join(const char *addr, int port)
{
    if (s_sock != NET_BAD) { closesocket(s_sock); s_sock = NET_BAD; }
    memset(s_have_peer, 0, sizeof s_have_peer);
    if (!net_open(0)) return 0;
    if (!net_resolve(addr, port, &s_peer[0], &s_peer_len[0])) { tt_dbg("[NET] can't find the host '%s'\n", addr); note("ONLINE: can't find that address", addr); closesocket(s_sock); s_sock = NET_BAD; return 0; }
    s_have_peer[0] = 1; s_role = 2; s_welcomed = 0;
    tt_dbg("[NET] joining %s port %d%s\n", addr, port, s_lobby ? " -- the lobby" : "");
    return 1;
}
static void net_missing_file(const char *path)   /* (a file of the game missing on this machine: the cause of a "dirty disc", named) */
{
    const char *g = strstr(path, "game_files"); char l[200];
    if (!g || strstr(g, "FMV") || strstr(g, ".bik")) return;   /* (a movie: skipped, nothing waits on it) */
    tt_dbg("[FILES] this machine is missing a game file: %s\n", g);
    snprintf(l, sizeof l, "%s", g + 11); xbox_BannerSet("MISSING GAME FILE in game_files:", l);
}
__attribute__((constructor)) static void net_missing_hook(void) { extern void (*volatile xbox_on_missing_file)(const char *path); xbox_on_missing_file = net_missing_file; }
void tt_net_init(void)
{
    char role[32] = "", addr[160] = "", *c; int port = 27960, players = 0, delay = 0;
    const char *e = getenv("TT_NET");
    static int done; if (done) return; done = 1;
    atexit(tt_net_bye);   /* (the window closed: the others told) */
    net_ini(role, sizeof role, addr, sizeof addr, &port, &players, &delay);
    if (addr[0]) snprintf(s_host_addr, sizeof s_host_addr, "%s", addr);
    s_port = port;
    if (getenv("TT_NET_ME")) s_want_slot = atoi(getenv("TT_NET_ME"));
    if (getenv("TT_NET_HERO")) s_my_hero = (uint8_t)atoi(getenv("TT_NET_HERO"));
    if (getenv("TT_NET_SETTINGS")) { const char *h = getenv("TT_NET_SETTINGS"); int k; for (k = 0; k < (int)sizeof s_settings && h[2 * k] && h[2 * k + 1]; k++) { char t[3] = { h[2 * k], h[2 * k + 1], 0 }; s_settings[k] = (uint8_t)strtoul(t, NULL, 16); } }
    if (getenv("TT_NET_LOBBY")) { e = getenv("TT_NET_LOBBY"); s_lobby = 1; s_lobby_page = 1; }
    if (getenv("TT_NET_POSTWAIT")) s_post_wait = 1;   /* (2.1) player 2 back from his upgrades: waiting for the host */
    if (getenv("TT_NET_NOTE") && *getenv("TT_NET_NOTE")) { note(getenv("TT_NET_NOTE"), ""); s_ban_until = net_ms() + 8000u; }
    if (getenv("TT_NET_SAVE")) { FILE *f = fopen(getenv("TT_NET_SAVE"), "rb"); if (f) { s_save_apply = fread(s_save, 1, NET_SAVE_N, f) == NET_SAVE_N; fclose(f); } tt_dbg("[NET] the host's save: %s\n", s_save_apply ? "read, put in place until the level begins" : "missing"); }
    if (getenv("TT_NET_PORT")) port = atoi(getenv("TT_NET_PORT"));
    if (getenv("TT_NET_PLAYERS")) players = atoi(getenv("TT_NET_PLAYERS"));
    if (getenv("TT_NET_DELAY")) delay = atoi(getenv("TT_NET_DELAY"));
    if (delay < 1) {   /* (2.1) the delay in steps from a time, about 90 ms, at this machine's frame rate: 3 steps were 100 ms at 30 fps but
        * 18 ms at 165 -- under a round trip, so each step waited on the network and the game held near 30 fps online. The host's
        * goes to every player (PK_WELCOME) */
        const char *f = getenv("TT_FPS"); int fps = 30;
        if (f && (f[0] == 'u' || f[0] == 'U' || f[0] == 'r' || f[0] == 'R')) fps = 240; else if (f && f[0] == '6') fps = 60; else if (f && atoi(f) > 30) fps = atoi(f);
        delay = (fps * 90 + 999) / 1000; if (delay < 3) delay = 3;
        tt_dbg("[NET] %d steps of delay (about 90 ms at %d fps)\n", delay > 30 ? 30 : delay, fps); }
    if (delay > 30) delay = 30;
    s_delay = delay;
    if (!e || !*e) return;
    if (!strncmp(e, "host", 4)) { if (e[4] == ':') port = atoi(e + 5); s_port = port; if (!net_start_host(port, players)) return;
        if (!s_lobby) { char l[80]; snprintf(l, sizeof l, "ONLINE: waiting for the other %d player%s", s_np - 1, s_np > 2 ? "s" : ""); note(l, ""); } }
    else if (!strncmp(e, "join:", 5)) { snprintf(addr, sizeof addr, "%s", e + 5); c = strrchr(addr, ':'); if (c) { *c = 0; port = atoi(c + 1); }
        snprintf(s_host_addr, sizeof s_host_addr, "%s", addr); s_port = port; if (!net_start_join(addr, port)) return; }
    else return;
    if (!s_lobby) { extern volatile int xbox_gfxmenu_locked; xbox_pad_net = net_pad_hook; xbox_gfxmenu_locked = 1; }   /* (an online game: no F10 settings -- they are set before) */
}

/* ---- (2.1) the chat: "/" opens it, Enter sends, Escape closes -- in the Online menu once connected and in play; never in a
 * loading screen or another menu. Lines go to the host, who passes them on; each is sent three times (UDP), seen once ---- */
#define CHAT_MAX 60
static int s_chat_open, s_chat_len; static char s_chat_in[CHAT_MAX + 1];
static char s_chat_log[3][CHAT_MAX + 16]; static uint32_t s_chat_until, s_chat_id, s_chat_last[NET_MAXP]; static int s_chat_dirty;
static unsigned char s_chat_was[256];
int tt_net_chat_open(void) { return s_chat_open; }
static void chat_show(void)
{
    char l2[CHAT_MAX + 24];
    if (s_chat_open) { snprintf(l2, sizeof l2, "Say: %s_", s_chat_in); xbox_BannerSet(s_chat_log[0][0] ? s_chat_log[0] : "CHAT -- Enter: send, /: close", l2); s_ban_until = 0; return; }
    if (s_menu_open) return;   /* (the online menu's banner has the place) */
    if (s_chat_until && (int32_t)(net_ms() - s_chat_until) < 0) { xbox_BannerSet(s_chat_log[0], s_chat_log[1]); return; }
    if (s_chat_until) { s_chat_until = 0; xbox_BannerSet("", ""); }
}
static void chat_add(int who, const char *t)
{
    memmove(s_chat_log[1], s_chat_log[0], sizeof s_chat_log[0] * 2);
    snprintf(s_chat_log[0], sizeof s_chat_log[0], "P%d: %s", who + 1, t);
    tt_dbg("[CHAT] P%d: %s\n", who + 1, t);
    s_chat_until = net_ms() + 7000u; if (!s_chat_until) s_chat_until = 1; s_chat_dirty = 1;
}
static void chat_send_line(int who, uint32_t id, const char *t, int to_host)
{
    uint8_t b[12 + 6 + CHAT_MAX + 1]; int n = head(b, PK_CHAT), len = (int)strlen(t), k, q;
    if (len > CHAT_MAX) len = CHAT_MAX;
    b[n++] = (uint8_t)who; b[n++] = (uint8_t)len; put32(b + n, id); n += 4; memcpy(b + n, t, (size_t)len); n += len;
    for (k = 0; k < 3; k++) { if (to_host) net_send_to(0, b, n); else for (q = 1; q < NET_MAXP; q++) if (s_have_peer[q] && q != who) net_send_to(q, b, n); }
}
static void chat_recv(const uint8_t *b, int n, int from_host)
{
    int who, len; uint32_t id; char t[CHAT_MAX + 1];
    if (n < 18) return;
    who = b[12]; len = b[13]; id = get32(b + 14);
    if (who < 0 || who >= NET_MAXP || len > CHAT_MAX || n < 18 + len) return;
    if (id <= s_chat_last[who]) return;   /* (a copy already seen) */
    s_chat_last[who] = id;
    memcpy(t, b + 18, (size_t)len); t[len] = 0;
    { int k; for (k = 0; k < len; k++) if ((unsigned char)t[k] < 32 || (unsigned char)t[k] > 126) t[k] = '?'; }
    chat_add(who, t);
    if (!from_host && s_role == 1) chat_send_line(who, id, t, 0);   /* (the host passes it on) */
}
static int chat_allowed(void);
static void chat_tick(void)
{
    extern int xbox_InputKeyDown(int vk); extern volatile int xbox_pad_blocked; int vk, sh, allowed = chat_allowed();
    if (!allowed) { if (s_chat_open) { s_chat_open = 0; xbox_pad_blocked = 0; xbox_BannerSet("", ""); } for (vk = 0; vk < 256; vk++) s_chat_was[vk] = 0; return; }
    {   /* (test) TT_NET_CHATTEST=text: said once, 100 steps into play */
        static int said; const char *ct = getenv("TT_NET_CHATTEST");
        if (ct && !said && !s_lobby && s_step > 100) { uint32_t id = ++s_chat_id + (net_ms() << 8); said = 1; s_chat_last[s_me] = id; chat_add(s_me, ct); chat_send_line(s_me, id, ct, s_role == 2); } }
    sh = xbox_InputKeyDown(0x10) || xbox_InputKeyDown(0xA0) || xbox_InputKeyDown(0xA1);
    for (vk = 8; vk < 256; vk++) {
        int d = xbox_InputKeyDown(vk), edge = d && !s_chat_was[vk]; s_chat_was[vk] = (unsigned char)d;
        if (!edge) continue;
        if (!s_chat_open) { if ((vk == 0xBF && !sh) || vk == 0x6F) { s_chat_open = 1; s_chat_len = 0; s_chat_in[0] = 0; s_menu_open = 0; xbox_pad_blocked = 1; } continue; }
        if (vk == 0x1B || (vk == 0xBF && !sh) || vk == 0x6F) { s_chat_open = 0; xbox_pad_blocked = 0; xbox_BannerSet("", ""); s_chat_until = net_ms() + 3000u; continue; }   /* (2.1: "/" again, or Escape: closed) */
        if (vk == 0x0D) {
            if (s_chat_len) { uint32_t id = ++s_chat_id + (net_ms() << 8); if (id <= s_chat_last[s_me]) id = s_chat_last[s_me] + 1; s_chat_last[s_me] = id;
                chat_add(s_me, s_chat_in); chat_send_line(s_me, id, s_chat_in, s_role == 2); }
            s_chat_open = 0; xbox_pad_blocked = 0; continue; }
        if (vk == 0x08) { if (s_chat_len) s_chat_in[--s_chat_len] = 0; continue; }
        if (s_chat_len >= CHAT_MAX) continue;
        {   char c = 0;
            if (vk >= 'A' && vk <= 'Z') c = (char)(sh ? vk : vk + 32);
            else if (vk >= '0' && vk <= '9') c = sh ? ")!@#$%^&*("[vk - '0'] : (char)vk;
            else if (vk >= 0x60 && vk <= 0x69) c = (char)('0' + vk - 0x60);
            else if (vk == 0x20) c = ' ';
            else if (vk == 0xBE) c = sh ? '>' : '.'; else if (vk == 0xBC) c = sh ? '<' : ','; else if (vk == 0xBD) c = sh ? '_' : '-';
            else if (vk == 0xDE) c = sh ? '"' : '\''; else if (vk == 0xBF && sh) c = '?'; else if (vk == 0xBA) c = sh ? ':' : ';';
            else if (vk == 0xBB) c = sh ? '+' : '=';
            if (c) { s_chat_in[s_chat_len++] = c; s_chat_in[s_chat_len] = 0; } }
    }
    chat_show();
}
/* ---- the game's side: each step ---- */
/* in play: screens 6 and 8 with the level loaded -- [0x4AD020] is 0 through the loading screen (its tips, while the level's
 * files come in: a while that differs from machine to machine) and 1 from the level's first frame. Each machine meets the
 * others there; before, the steps ran through the loading screen and the level began at a different step on each machine. */
/* (once in step, a level's own loads in play -- its closing cutscene's -- keep the steps going: leaving the steps there on one
 * machine and not the other left each waiting for the other, frozen, the joiner sent home: a playtest at Balin's Tomb's end) */
static int in_play(void) { extern uint32_t tt_net_screen(void); uint32_t s = tt_net_screen(); extern int tt_net_level_loaded(void); return (s == 6u || s == 8u) && (s_active || tt_net_level_loaded()); }
static uint32_t s_pkt;
static int chat_allowed(void)   /* (the Online menu once connected; play, not a loading screen nor another menu) */
{
    int p, any = 0;
    if (!s_role) return 0;
    for (p = 1; p < NET_MAXP; p++) if (s_have_peer[p]) any = 1;
    if (s_lobby) { extern int tt_opt_up(void); return tt_opt_up() && (s_role == 1 ? any : s_welcomed); }
    return in_play();
}
static float s_step_dt = 1000.0f / 60.0f;   /* (this step's time, ms: the host's) */
float tt_net_step_dt(void) { return s_step_dt; }
static void net_relaunch(int game);
static int net_pad_hook(unsigned long port, NET_PAD_STATE *st)
{
    if (!s_active || !s_cur_ok) {   /* out of step: this machine's own player on port 0, the others' ports empty */
        if (!st) return port == 0 ? -1 : (port < (unsigned long)s_np ? 1 : 0);
        if (port == 0 && s_role == 2 && !s_lobby) {   /* (2.1) player 2's machine after a level: the stats go on by themselves; his hands only on his own shop */
            extern uint32_t tt_net_screen(void); extern int tt_coop_post_ph; uint32_t sc = tt_net_screen();
            if ((sc == 9u || sc == 10u) && !(sc == 10u && tt_coop_post_ph == 3)) {
                uint32_t t = net_ms(); memset(st, 0, sizeof *st); st->dwPacketNumber = t / 16u;
                if (sc == 9u && (t % 900u) < 120u) st->Gamepad.bAnalogButtons[0] = 255;   /* (A, now and then: on through the stats) */
                return 1; }
        }
        if (port == 0) return -1;
        if (port < (unsigned long)s_np) { memset(st, 0, sizeof *st); st->dwPacketNumber = 1; return 1; }
        return 0;
    }
    if (port >= (unsigned long)s_np) return 0;
    if (!st) return 1;
    pad_from_rec(st, &s_cur[port], s_pkt);
    st->Gamepad.wButtons &= (uint16_t)~0x0010u;   /* (Start: nobody pauses an online game -- the online menu instead) */
    return 1;
}
/* the local player's pad for a step: his device (port 0 here), Start taken for the online menu */
static void sample_local(net_rec *r)
{
    NET_PAD_STATE st; static uint16_t lastb;
    memset(&st, 0, sizeof st);
    if (xbox_InputGetStateLocal(0, &st) != 0) memset(&st, 0, sizeof st);
    {   /* (2.1) the title's changes to this player's pad (mouse aiming and steering, ...) made here, before it is sent: on the
         * pads that come from the network they are not made (xinput_device.c) -- they were made with each machine's own mouse
         * and settings, to every player's pad there, and the worlds parted */
        extern void (*volatile xbox_pad_filter)(unsigned long port, NET_PAD_STATE *st); if (xbox_pad_filter) xbox_pad_filter(0, &st); }
    static int was;
    extern int tt_net_scene_now(void);
    {   /* (2.1) player 1's Start pauses the game, as off line (it goes to the game, the pause the same on every machine); the online menu
         * is his from the pause, Y (menus.c says so there). The others' Start opens it for them (a playtest) */
        extern int tt_game_paused_now(void); static int ylast; int y = st.Gamepad.bAnalogButtons[3] > 30, toggled = 0;
        int p1pause = s_me == 0 && !getenv("TT_NET_NOP1PAUSE");
        if ((st.Gamepad.wButtons & 0x0010u) && !(lastb & 0x0010u) && (s_menu_open || (!tt_net_scene_now() && !p1pause))) { s_menu_open = !s_menu_open; toggled = 1; tt_dbg("[NET] the online menu %s\n", s_menu_open ? "opened" : "closed"); }
        if (p1pause && y && !ylast && !s_menu_open && tt_game_paused_now()) { s_menu_open = 1; toggled = 1; st.Gamepad.bAnalogButtons[3] = 0; tt_dbg("[NET] the online menu opened (from the pause)\n"); }
        ylast = y;
        lastb = st.Gamepad.wButtons;
        if (p1pause && !toggled && !s_menu_open && !was && !tt_net_scene_now()) goto keep_start;   /* (Start to the game: its pause) */
    }
    if (s_me == 0 && (st.Gamepad.wButtons & 0x0010u) && !s_menu_open && tt_net_scene_now()) st.Gamepad.wButtons |= 0x0020u;   /* (in a cutscene the host's Start skips it: as Back, which does too and pauses nothing) */
    if (s_me != 0 && tt_net_scene_now()) st.Gamepad.wButtons &= (uint16_t)~0x0020u;   /* (2.1: the others neither skip nor pause anything) */
    st.Gamepad.wButtons &= (uint16_t)~0x0010u;
keep_start:
    if (s_menu_open || was) {   /* the menu has the pad: the player stands */
        extern void tt_net_menu_pad(const NET_PAD_STATE *st, uint8_t *flags, volatile int *open);
        tt_net_menu_pad(&st, &s_myflags, &s_menu_open);
        memset(&st.Gamepad, 0, sizeof st.Gamepad);
    }
    was = s_menu_open;
    if (s_chat_open) memset(&st.Gamepad, 0, sizeof st.Gamepad);   /* (typing in the chat: the player stands) */
    rec_from_pad(r, &st, (uint8_t)(s_myflags | (s_menu_open ? NB_MENU : 0)));
    if (s_me == 0) {   /* (2.1) the host's real time for a step, in half milliseconds, for every machine's step (tt_net_step_dt): the
                        * game runs at its own speed whatever the frame rate the players' machines keep together */
        static double last, carry; double now = net_us(), d = last > 0 ? (now - last) / 500.0 + carry : 33.333 * 2.0; int u;
        last = now; if (d < 2.0) d = 2.0; if (d > 200.0) { d = 200.0; carry = 0; }
        u = (int)d; carry = d - u; if (u < 1) u = 1; if (u > 200) u = 200;
        r->b[1] = (uint8_t)u;
    }
}
static void meet(void)   /* every machine at this entry into play, then the steps from 0 */
{
    uint32_t t0 = net_ms(), lr = 0; int p, said = 0;
    s_entry++; s_ready_mask = 1u; s_step = 0; s_cur_ok = 0; s_desync_said = 0; s_last_hash_step = 0;
    memset(s_have_step, 0, sizeof s_have_step); memset(s_hash_step, 0, sizeof s_hash_step);
    tt_dbg("[NET] meeting %u: waiting for every player\n", s_entry);
    { extern void tt_net_clocks(uint32_t *w, int n, int set); if (s_role == 1) tt_net_clocks(s_clk, NET_CLKW, 0); }
    for (;;) {
        uint32_t now = net_ms(), want = (1u << s_np) - 1u;
        net_pump();
        if (s_role == 1) {
            if ((s_ready_mask & want) == want) { send_simple(-1, PK_GO); break; }
            if (now - lr > 200) { lr = now; for (p = 1; p < s_np; p++) if (s_have_peer[p]) send_simple(p, PK_WELCOME); }
        } else {
            if (now - lr > 100) { lr = now; send_simple(0, s_welcomed ? PK_READY : PK_HELLO); }
            if (s_welcomed && (s_ready_mask & 2u)) break;
        }
        if (!said && now - t0 > 1500) { said = 1; xbox_BannerSet("ONLINE: waiting for the other players...", ""); }
        net_sleep_ms(2);
    }
    if (s_role == 2) { extern void tt_net_clocks(uint32_t *w, int n, int set); s_ready_mask = 0; tt_net_clocks(s_clk, NET_CLKW, 1); }   /* (the host's clocks here too) */
    for (p = 0; p < s_np; p++) { uint32_t k; net_rec z; memset(&z, 0, sizeof z); z.b[19] = 1; for (k = 0; k < (uint32_t)s_delay; k++) store(k, p, &z); }   /* (the first steps: at rest) */
    s_active = 1;
    tt_dbg("[NET] meeting %u: all %d here after %u ms -- in step\n", s_entry, s_np, net_ms() - t0);
    { extern void tt_net_clock_log(void); tt_net_clock_log(); }
    if (said) xbox_BannerSet("", "");
}
/* once a step, before the world moves (sub_000E3350): this step's pads for every player, waited for if need be */
void tt_net_step(void)
{
    net_rec mine; uint32_t t0, lr = 0; int said = 0, p;
    if (!s_role) { if (s_ban_until && (int32_t)(net_ms() - s_ban_until) > 0) { s_ban_until = 0; xbox_BannerSet("", ""); } return; }
    net_pump();
    chat_tick();
    if (s_ban_until && (int32_t)(net_ms() - s_ban_until) > 0 && !s_menu_open) { s_ban_until = 0; xbox_BannerSet("", ""); }
    if (s_save_apply && !s_active) { extern void tt_net_save_put(const uint8_t *in, uint32_t a, uint32_t n); tt_net_save_put(s_save, NET_SAVE_A, NET_SAVE_N); }   /* (until the meeting: the level loads with it) */
    if (s_lobby) { static void (*volatile lt)(void); extern void tt_net_lobby_tick_(void); tt_net_lobby_tick_(); (void)lt; return; }
    if (!in_play()) {
        { extern void tt_net_game_over_tick_(void); tt_net_game_over_tick_(); }
        static uint32_t s_left_t, s_left_send;
        if (s_active) { tt_dbg("[NET] out of play after %u steps of meeting %u\n", s_step, s_entry); s_active = 0; s_cur_ok = 0; s_left_t = net_ms() | 1u; }
        if (s_role == 1 && s_left_t && net_ms() - s_left_t < 4000u && net_ms() - s_left_send >= 15u) { s_left_send = net_ms(); send_frames(); }   /* (2.1) the last steps again for a while: the others still need the
             * step the level ended in (it was sent only when the next step began, and there was none -- player 2 froze waiting for it) */
        else if (s_left_t && net_ms() - s_left_t >= 4000u) s_left_t = 0;
        if (s_menu_open) { s_menu_open = 0; xbox_BannerSet("", ""); tt_dbg("[NET] the online menu closed (out of play)\n"); }
        if (s_role == 2 && !s_welcomed) { static uint32_t lh; if (net_ms() - lh > 300) { lh = net_ms(); send_simple(0, PK_HELLO); } }
        net_pump();
        if (s_role == 2 && s_entry && (s_host_gone || net_ms() - s_peer_seen[0] > 20000u)) {   /* (2.1) the host gone while out of play (a loading screen, the level's end): to the Online menu */
            tt_dbg("[NET] the host is gone (out of play): back to the Online menu\n"); net_relaunch(3); return; }
        return;
    }
    if (!s_active) meet();
    else s_step++;
    sample_local(&mine);
    store(s_step + (uint32_t)s_delay, s_me, &mine);
    if (s_role == 1 && s_gone_mask) { uint32_t k; net_rec z; memset(&z, 0, sizeof z); z.b[19] = 1; for (p = 1; p < s_np; p++) if (s_gone_mask & (1u << p)) for (k = s_step; k <= s_step + (uint32_t)s_delay; k++) store(k, p, &z); }
    t0 = net_ms();
    for (;;) {
        uint32_t now;
        {   /* (2.1) at once for a new step, then again every 15 ms while waiting: every 1 ms it flooded the other machine's
             * socket and its packets were dropped */
            static uint32_t last_send, last_step = ~0u; uint32_t tn = net_ms();
            if (last_step != s_step || tn - last_send >= 15u) { last_step = s_step; last_send = tn; if (s_role == 1) send_frames(); else send_input(); } }
        if (have_all(s_step)) break;
        net_pump();
        if (have_all(s_step)) { if (s_role == 1) send_frames(); break; }   /* (2.1) the host: this step's pads to the others at once, not a step later */
        now = net_ms();
        if (!said && now - t0 > 2000) { said = 1; xbox_BannerSet("ONLINE: waiting for a player's connection...", ""); tt_dbg("[NET] step %u: waiting for players\n", s_step); }
        if (s_role == 1) for (p = 1; p < s_np; p++) if (!(s_gone_mask & (1u << p)) && !have(s_step, p) && now - s_peer_seen[p] > 8000u && now - t0 > 2000u) {   /* (a player silent 8 s: gone) */
            char l[64]; s_gone_mask |= 1u << p; s_have_peer[p] = 0; tt_dbg("[NET] player %d lost: his hero stands from now on\n", p + 1); snprintf(l, sizeof l, "ONLINE: player %d disconnected", p + 1); note(l, ""); }
        if (s_role == 1 && s_gone_mask) { uint32_t k; net_rec z; memset(&z, 0, sizeof z); z.b[19] = 1;   /* (the gone: at rest, for this step and the ones ahead) */
            for (p = 1; p < s_np; p++) if (s_gone_mask & (1u << p)) for (k = s_step; k <= s_step + (uint32_t)s_delay; k++) store(k, p, &z); }
        if (s_role == 2 && now - t0 > 8000u && s_host_oop_t && now - s_host_oop_t < 1500u) {   /* (2.1) a failsafe: the host has left play and this step will
             * never come -- not frozen for good: to the Online menu, waiting for him there */
            extern void tt_net_post_wait(void);
            tt_dbg("[NET] step %u: the host left play without it -- to the Online menu\n", s_step); xbox_BannerSet("", ""); s_active = 0; tt_net_post_wait(); return; }
        if (s_role == 2 && (s_host_gone || now - s_peer_seen[0] > 10000u)) {   /* the host gone: this machine back to its own main menu */
            tt_dbg("[NET] the host is gone: back to the Online menu\n"); xbox_BannerSet("ONLINE: the host left the game", ""); net_relaunch(3); return; }
        (void)lr;
        net_sleep_short();
    }
    if (said) xbox_BannerSet("", "");
    for (p = 0; p < NET_MAXP; p++) s_cur[p] = s_in[s_step % NET_RING][p];
    s_step_dt = s_cur[0].b[1] ? (float)s_cur[0].b[1] * 0.5f : 1000.0f / 60.0f;
    {   /* (2.1) the host's call to bring the others beside him: at the step his flag comes on, on every machine */
        static uint8_t was_bring; uint8_t nb = (uint8_t)(s_cur[0].b[18] & NB_BRING); extern void tt_net_bring(void);
        if (nb && !was_bring) tt_net_bring();
        was_bring = nb;
        if (s_me == 0 && (s_myflags & NB_BRING)) { static int held; if (++held > 20) { held = 0; s_myflags &= (uint8_t)~NB_BRING; } } }
    s_cur_ok = 1; s_pkt = s_step + 1u;
    for (p = 0; p < s_np; p++) s_flags_seen[p] = s_cur[p].b[18];
    {   /* the votes: one carried by every player happens now, on every machine */
        extern void tt_net_vote_do(int what);
        int all_r = 1, all_q = 1;
        for (p = 0; p < s_np; p++) { if (!(s_flags_seen[p] & NB_VOTE_RESTART)) all_r = 0; if (!(s_flags_seen[p] & NB_VOTE_QUIT)) all_q = 0; }
        if (all_r || all_q) { s_myflags &= (uint8_t)~(NB_VOTE_RESTART | NB_VOTE_QUIT); tt_dbg("[NET] step %u: every player voted to %s\n", s_step, all_r ? "restart the level" : "leave"); tt_net_vote_do(all_r ? 1 : 2); }
    }
}
/* the world's hash of this step (tt_simhash_frame) */
void tt_net_hash(uint32_t h)
{
    if (!s_active) return;
    note_hash(s_step, h); s_last_hash_step = s_step; s_last_hash = h;
}
/* who has voted for what, for the online menu: bit p of *restart, *quit */
void tt_net_votes(int *restart, int *quit, int *menus)
{
    int p; *restart = *quit = *menus = 0;
    for (p = 0; p < s_np; p++) { if (s_flags_seen[p] & NB_VOTE_RESTART) *restart |= 1 << p; if (s_flags_seen[p] & NB_VOTE_QUIT) *quit |= 1 << p; if (s_flags_seen[p] & NB_MENU) *menus |= 1 << p; }
}
int tt_net_menu_is_open(void) { return s_menu_open; }
uint8_t tt_net_my_flags(void) { return s_myflags; }
void tt_net_bye(void)
{
    uint8_t b[16]; int n;
    if (!s_role) return;
    n = head(b, PK_BYE);
    if (s_role == 1) { int q; for (q = 1; q < NET_MAXP; q++) net_send_to(q, b, n); } else net_send_to(0, b, n);
}

/* ======================================================================
 * (2.1) The Online menu (options.c draws it, menus.c opens it from the main menu): host or join, the game to play --
 * Co-op or Versus, the level, the players, each one's hero -- and Start. A game starts by every machine starting the
 * game afresh straight into that level (the developer switches, as the test runs do), the same on all of them: the same
 * level, heroes, settings and seed. After the level (or a vote to leave) each goes back to this menu, still connected.
 * ====================================================================== */
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
enum { LB_MODE, LB_PLAYERS, LB_LEVEL, LB_HERO, LB_DIFF, LB_ADDR, LB_PORT, LB_N };
enum { ST_MODE = 0, ST_NP = 1, ST_LEVEL = 2, ST_DIFF = 3, ST_HERO = 4, ST_CONN = 8, ST_START = 9, ST_COOPSET = 10, ST_NCOOP = 11, ST_READY = 21, ST_FLOW = 22, ST_DMGT = 23, ST_DMGE = 24, ST_HEROMOD = 25, ST_VSKIND = 26 };   /* (ST_VSKIND: Versus with 4: 1 Team Deathmatch, 2 Free-for-all) */
static const struct { const char *code, *name; } s_levels[] = {
    { "00ISUL1", "Prologue: Mount Doom" }, { "01WEAT1", "Weathertop" }, { "02GATE2", "Gates of Moria" }, { "04BALIN1", "Balin's Tomb" },
    { "13AMON1", "Amon Hen" }, { "14FANG2", "Fangorn Forest" }, { "16ROHAN2", "Plains of Rohan" }, { "16ROHAN1", "Westfold" },
    { "16ROHAN3", "Road to Helm's Deep" }, { "17TOP1", "The Deeping Wall" }, { "18HOLE1", "The Breached Wall" },
    { "20YARD1", "Hornburg Courtyard" }, { "21ORTH1", "Orthanc" }, { "21ORTH2", "Orthanc Summit" } };
#define NET_NLEVEL ((int)(sizeof s_levels / sizeof s_levels[0]))
static const struct { const char *key, *name; } s_heroes[] = {
    { "aragorn", "Aragorn" }, { "legolas", "Legolas" }, { "gimli", "Gimli" }, { "isildur", "Isildur" }, { "boromir", "Boromir" },
    { "gandalf", "Gandalf" }, { "frodo", "Frodo" }, { "lurtz", "Lurtz" }, { "elrond", "Elrond" }, { "sauron", "Sauron" },
    { "uruk", "Berserker" }, { "urukcap", "Uruk Captain" }, { "saruman", "Saruman" } };
#define NET_NHERO ((int)(sizeof s_heroes / sizeof s_heroes[0]))
static const char *const s_diffs[4] = { "Easy", "Normal", "Expert", "Impossible" };
static int s_lb_init;
static void lb_defaults(void)
{
    if (!getenv("TT_NET_4P") && s_settings[ST_NP] && s_settings[ST_NP] != 2) s_settings[ST_NP] = 2;   /* (2.1: online is 2 players -- Co-op, and Versus' Deathmatch or Troll Battle) */
    if (!getenv("TT_NET_4P") && s_settings[ST_VSKIND] != 0 && s_settings[ST_VSKIND] != 3) s_settings[ST_VSKIND] = 0;
    extern int tt_coop_setting(int i); int k;
    if (s_lb_init) return; s_lb_init = 1;
    if (!s_settings[ST_NP]) { s_settings[ST_MODE] = 0; s_settings[ST_NP] = 2; s_settings[ST_LEVEL] = 1; s_settings[ST_DIFF] = (uint8_t)tt_coop_setting(0);
        for (k = 0; k < NET_MAXP; k++) s_settings[ST_HERO + k] = (uint8_t)(k < 3 ? k : 3); }
}
static int lb_hero_ok(int p, int h) { (void)p; return h >= 0 && h < NET_NHERO; }   /* (2.1: every hero for both -- player 1's extras as hero 4 wearing the pick, TT_GUEST) */
static int lb_troll(void) { return s_settings[ST_MODE] && s_settings[ST_VSKIND] == 3; }   /* (2.1) online Troll Battle: both the cave troll */
static int lb_level_ok(int lv) { return !lb_troll() || !strcmp(s_levels[lv].code, "04BALIN1") || !strcmp(s_levels[lv].code, "13AMON1"); }
int tt_net_lobby_state(void) { return !s_role ? 0 : s_role == 1 ? 1 : s_welcomed ? 3 : 2; }   /* 0 not connected, 1 hosting, 2 joining, 3 joined */
int tt_net_lobby_ready(void) { return s_my_ready; }
void tt_net_lobby_toggle_ready(void) { s_my_ready ^= 1; if (s_role == 2) send_simple(0, PK_LOBBY); }
int tt_net_joiner_waiting(void) { return s_role == 2 && s_welcomed && s_settings[ST_FLOW] && !s_settings[ST_MODE]; }   /* (2.1: the host on his way to the mission -- the joiner may only wait) */
int tt_net_coop_flow(void) { return s_role == 1 && s_lobby && s_settings[ST_FLOW]; }   /* (the host on his way through the save, the select and the map) */
int tt_net_lobby_page_wanted(void) { int v = s_lobby_page; s_lobby_page = 0; return v; }
static uint8_t lb_conn(void) { int p; uint8_t m = 1; for (p = 1; p < NET_MAXP; p++) if (s_have_peer[p]) m |= (uint8_t)(1u << p); return m; }
void tt_net_lobby_host(void)
{
    lb_defaults(); s_lobby = 1; s_my_hero = s_settings[ST_HERO];
    if (s_port < 1) s_port = 27960;   /* (2.1: the port row left empty) */
    if (net_start_host(s_port, s_settings[ST_NP])) { char l[80]; snprintf(l, sizeof l, "ONLINE: hosting on port %d", s_port); note(l, "The others choose Join Game with this PC's address"); }
}
void tt_net_lobby_join(void)
{
    lb_defaults(); s_lobby = 1; s_want_slot = 0; if (s_my_hero == 0) s_my_hero = 1;
    if (s_port < 1) s_port = 27960;
    if (net_start_join(s_host_addr, s_port)) { char l[200]; snprintf(l, sizeof l, "ONLINE: joining %s ...", s_host_addr); note(l, ""); }
}
void tt_net_lobby_leave(void)
{
    s_post_wait = 0; net_setenv("TT_NET_POSTWAIT", NULL);
    if (!s_role) return;
    tt_net_bye();
    if (s_sock != NET_BAD) { closesocket(s_sock); s_sock = NET_BAD; }
    s_role = 0; s_welcomed = 0; memset(s_have_peer, 0, sizeof s_have_peer); xbox_pad_net = NULL;
    s_my_ready = 0; memset(s_lb_ready, 0, sizeof s_lb_ready); memset(s_lb_saveok, 0, sizeof s_lb_saveok); s_settings[ST_FLOW] = 0; s_save_ok = 0; memset(s_save_have, 0, sizeof s_save_have);
    tt_dbg("[NET] left the online game\n");
}
/* the menu's rows: their words, and left / right */
const char *tt_net_lobby_label(int row)
{
    static const char *const l[LB_N] = { "Mode:", "Players:", "Level:", "Your Hero:", "Difficulty:", "Host Address:", "Port:" };
    if (row == LB_PLAYERS && s_settings[ST_MODE]) return "Match:";
    if (row == LB_LEVEL) return s_settings[ST_MODE] ? "Arena:" : "Mission:";
    return row >= 0 && row < LB_N ? l[row] : "";
}
int tt_net_lobby_rows(void) { return LB_N; }
void tt_net_lobby_value(int row, char *out, int n)
{
    lb_defaults();
    switch (row) {
    case LB_MODE: snprintf(out, (size_t)n, "%s", s_settings[ST_MODE] ? "Versus" : "Co-op"); break;
    case LB_PLAYERS: if (!s_settings[ST_MODE]) snprintf(out, (size_t)n, "2");
        else snprintf(out, (size_t)n, "%s", lb_troll() ? "1 vs 1 Troll Battle" : "1 vs 1 Deathmatch"); break;
    case LB_LEVEL: if (!s_settings[ST_MODE]) snprintf(out, (size_t)n, "The host's save and mission"); else snprintf(out, (size_t)n, "%s", s_levels[s_settings[ST_LEVEL] % NET_NLEVEL].name); break;
    case LB_HERO: if (!s_settings[ST_MODE]) { snprintf(out, (size_t)n, "%s", "Picked at the character select"); break; }   /* (2.1, Co-op: the host picks every hero there) */
        if (lb_troll()) { snprintf(out, (size_t)n, "Cave Troll"); break; }
        snprintf(out, (size_t)n, "%s", s_heroes[(s_role == 1 || !s_role ? s_settings[ST_HERO] : s_my_hero) % NET_NHERO].name); break;
    case LB_DIFF: snprintf(out, (size_t)n, "%s", s_diffs[s_settings[ST_DIFF] & 3]); break;
    case LB_ADDR: snprintf(out, (size_t)n, "%s", s_role == 1 ? "(you are the host)" : s_host_addr); break;
    case LB_PORT: if (s_port > 0) snprintf(out, (size_t)n, "%d", s_port); else out[0] = 0; break;   /* (2.1: empty while typed over) */
    default: out[0] = 0;
    }
}
static void lb_save_ini(void)
{
    FILE *f = fopen("online.ini", "w");
    if (!f) return;
    fprintf(f, "# the Online menu's last address and port (2.1)\naddress=%s\nport=%d\n", s_host_addr, s_port);
    fclose(f);
}
int tt_net_lobby_change(int row, int d)   /* 1 changed */
{
    int v;
    lb_defaults();
    if (s_role == 2 && row != LB_HERO && row != LB_ADDR && row != LB_PORT) return 0;   /* (a joiner: the host's choices) */
    if (row == LB_HERO && !s_settings[ST_MODE]) return 0;   /* (Co-op: picked at the character select, by the host) */
    if (s_role && (row == LB_ADDR || row == LB_PORT)) return 0;
    switch (row) {
    case LB_MODE: s_settings[ST_MODE] ^= 1; s_settings[ST_NP] = 2; s_settings[ST_VSKIND] = 0; if (s_role == 1) s_np = 2; break;
    case LB_PLAYERS: if (!s_settings[ST_MODE]) return 0;   /* (2.1: online Co-op is 2 players) */
        if (!getenv("TT_NET_4P")) {   /* (2.1) online Versus: 1 vs 1 -- Deathmatch or Troll Battle (3 and 4 players taken out) */
            int k; s_settings[ST_VSKIND] = (uint8_t)(lb_troll() ? 0 : 3); s_settings[ST_NP] = 2; if (s_role == 1) s_np = 2;
            for (k = 0; k < NET_NLEVEL && !lb_level_ok(s_settings[ST_LEVEL] % NET_NLEVEL); k++) s_settings[ST_LEVEL] = (uint8_t)((s_settings[ST_LEVEL] + 1) % NET_NLEVEL);
            break; }
        {   /* Versus: Deathmatch (2), Team Deathmatch (4), Free-for-all (4) */
            /* (kinds: 0 Deathmatch 2, 1 Team Deathmatch 4, 2 Free-for-all 4, 3 Free-for-all 3 -- player 4 off) */
            int kind = s_settings[ST_NP] == 2 ? 0 : s_settings[ST_NP] == 3 ? 3 : s_settings[ST_VSKIND] == 2 ? 2 : 1; kind = (kind + 4 + (d > 0 ? 1 : -1)) % 4;
            v = kind == 0 ? 2 : kind == 3 ? 3 : 4; if (kind == 3) kind = 2; if (s_role == 1 && v < s_np) { int p; for (p = v; p < NET_MAXP; p++) if (s_have_peer[p]) return 0; }
            s_settings[ST_VSKIND] = (uint8_t)kind; s_settings[ST_NP] = (uint8_t)v; if (s_role == 1) s_np = v; break; }
        v = s_settings[ST_NP] + d;
        if (v < 2 || v > 4) return 0; if (s_role == 1 && v < s_np) { int p; for (p = v; p < NET_MAXP; p++) if (s_have_peer[p]) return 0; }
        s_settings[ST_NP] = (uint8_t)v; if (s_role == 1) s_np = v; break;
    case LB_LEVEL: if (!s_settings[ST_MODE]) return 0;
        { int k; for (k = 0; k < NET_NLEVEL; k++) { s_settings[ST_LEVEL] = (uint8_t)((s_settings[ST_LEVEL] + NET_NLEVEL + d) % NET_NLEVEL); if (lb_level_ok(s_settings[ST_LEVEL])) break; } } break;
    case LB_HERO: { if (lb_troll()) return 0;
        int p = s_role == 2 ? s_me : 0, h = s_role == 2 ? s_my_hero : s_settings[ST_HERO], k;
        for (k = 0; k < NET_NHERO; k++) { h = (h + NET_NHERO + d) % NET_NHERO; if (lb_hero_ok(p, h)) break; }
        if (s_role == 2) s_my_hero = (uint8_t)h; else { s_settings[ST_HERO] = (uint8_t)h; s_my_hero = (uint8_t)h; }
        if (s_role == 2) send_simple(0, PK_LOBBY);
        break; }
    case LB_DIFF: s_settings[ST_DIFF] = (uint8_t)((s_settings[ST_DIFF] + 4 + d) & 3); break;
    case LB_ADDR: {   /* left: the last character off; right: the next of the usual addresses */
        static const char *const pre[] = { "127.0.0.1", "192.168.1.", "192.168.0.", "10.0.0." }; static int pi;
        size_t l = strlen(s_host_addr);
        if (d < 0) { if (l) s_host_addr[l - 1] = 0; } else { pi = (pi + 1) % 4; snprintf(s_host_addr, sizeof s_host_addr, "%s", pre[pi]); }
        lb_save_ini(); break; }
    case LB_PORT: v = s_port + d; if (v < 1024) v = 1024; if (v > 65535) v = 65535; s_port = v; lb_save_ini(); break;
    default: return 0;
    }
    return 1;
}
/* (2.1) the port typed on its row: digits, 8 to take one off (empty: the usual 27960 when used) */
void tt_net_lobby_type_port(int c)
{
    if (s_role) return;
    if (c == 8) s_port /= 10;
    else if (c >= '0' && c <= '9' && s_port * 10 + (c - '0') <= 65535) s_port = s_port * 10 + (c - '0');
    if (getenv("TT_NET_TYPELOG")) tt_dbg("[NETTYPE] port now %d\n", s_port);
    lb_save_ini();
}
/* (2.1) this PC's address on its network (what the others type to join it): the address a socket "connected" toward the internet
 * would send from -- nothing is sent. "" if none */
static void lb_my_ip(char *out, int n)
{
    struct sockaddr_in a, me; socklen_t ml = sizeof me; net_sock k;
    out[0] = 0;
#if defined(_WIN32)
    { static int wsa; WSADATA wd; if (!wsa) { if (WSAStartup(MAKEWORD(2, 2), &wd)) return; wsa = 1; } }
#endif
    k = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (k == NET_BAD) return;
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = htonl(0x08080808u);
    if (connect(k, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(k, (struct sockaddr *)&me, &ml) == 0) {
        uint32_t ip = ntohl(me.sin_addr.s_addr);
        if (ip) snprintf(out, (size_t)n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }
    closesocket(k);
}
void tt_net_lobby_type(int c)   /* a key typed on the address row: digits, '.', letters for a name, 8 to take one off */
{
    size_t l = strlen(s_host_addr);
    if (s_role) return;
    if (c == 8) { if (l) s_host_addr[l - 1] = 0; }
    else if (l + 1 < sizeof s_host_addr && ((c >= '0' && c <= '9') || c == '.' || c == '-' || (c >= 'a' && c <= 'z'))) { s_host_addr[l] = (char)c; s_host_addr[l + 1] = 0; }
    if (getenv("TT_NET_TYPELOG")) tt_dbg("[NETTYPE] address now \"%s\"\n", s_host_addr);
    lb_save_ini();
}
/* who is in: "P1 Aragorn (host)  P2 Legolas  P3 --" */
void tt_net_lobby_status(char *out, int n)
{
    int p, o = 0, np = s_settings[ST_NP] ? s_settings[ST_NP] : 2;
    uint8_t conn = s_role == 1 ? lb_conn() : s_settings[ST_CONN];
    out[0] = 0;
    if (!s_role) { snprintf(out, (size_t)n, "Host Game, or set the host's address and Join Game"); return; }
    if (s_role == 2 && !s_welcomed) { snprintf(out, (size_t)n, "Looking for the host at %s port %d ...", s_host_addr, s_port); return; }
    if (s_role == 1) {   /* (2.1) the host's own address, for the others to type: this PC's on its network */
        static char ip[48]; static unsigned when;
        if (!ip[0] || net_ms() - when > 10000u) { when = net_ms(); lb_my_ip(ip, sizeof ip); }
        if (ip[0]) o += snprintf(out + o, (size_t)n, "Your address %s:%d   ", ip, s_port); }
    if (s_role == 2 && (s_settings[ST_FLOW] || s_post_wait)) { snprintf(out, (size_t)n, s_save_ok ? "The host's save is here -- starting..." : "Waiting for player 1 to choose the level and your character..."); return; }
    for (p = 0; p < np && o < n - 1; p++) {
        if ((conn >> p) & 1) o += snprintf(out + o, (size_t)(n - o), "P%d %s%s%s   ", p + 1, s_heroes[s_settings[ST_HERO + p] % NET_NHERO].name, p == s_me ? " (you)" : "", p == 0 ? "" : ((s_role == 1 ? (s_lb_ready[p] ? 1 : 0) : (s_settings[ST_READY] >> p) & 1) ? " - ready" : " - not ready"));
        else o += snprintf(out + o, (size_t)(n - o), "P%d --   ", p + 1);
    }
}
static int lb_all_in(void) { int p, np = s_settings[ST_NP]; for (p = 1; p < np; p++) if (!s_have_peer[p]) return 0; return 1; }
static int lb_all_ready(void) { int p, np = s_settings[ST_NP]; for (p = 1; p < np; p++) if (!s_have_peer[p] || !s_lb_ready[p]) return 0; return 1; }
static void net_setenv(const char *k, const char *v)
{
#if defined(_WIN32)
    SetEnvironmentVariableA(k, v && *v ? v : NULL);
    { char b[600]; snprintf(b, sizeof b, "%s=%s", k, v ? v : ""); _putenv(b); }
#else
    if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
}
/* start the game afresh: into the chosen level (game 1), or back to the Online menu (game 0) */
static void lb_gameplay_settings(void)   /* (the host's: the F10 settings that change the game -- not the graphics, each machine's own) */
{
    extern float xbox_GfxGetF(int id); extern int xbox_GfxFind(const char *key); extern int tt_heroes_on(void);
    int it = xbox_GfxFind("damage_taken"), ie = xbox_GfxFind("enemy_damage"); float t = it >= 0 ? xbox_GfxGetF(it) : 100.0f, e = ie >= 0 ? xbox_GfxGetF(ie) : 100.0f;
    s_settings[ST_DMGT] = (uint8_t)(t > 0 && t < 1000 ? t / 4 : 25); s_settings[ST_DMGE] = (uint8_t)(e > 0 && e < 1000 ? e / 4 : 25); s_settings[ST_HEROMOD] = (uint8_t)(tt_heroes_on() ? 1 : 0);
}
static void net_relaunch(int game)
{
    extern const char *tt_coop_setting_env(int i);
    char v[400], hx[2 * sizeof s_settings + 1]; int k, np = s_settings[ST_NP] < 2 ? 2 : s_settings[ST_NP];
    for (k = 0; k < (int)sizeof s_settings; k++) snprintf(hx + 2 * k, 3, "%02X", s_settings[k]);
    net_setenv("TT_NET_SETTINGS", hx);
    snprintf(v, sizeof v, "%d", s_me); net_setenv("TT_NET_ME", v);
    snprintf(v, sizeof v, "%d", np); net_setenv("TT_NET_PLAYERS", v);
    snprintf(v, sizeof v, "%u", s_seed); net_setenv("TT_NET_SEED", v);
    snprintf(v, sizeof v, "%d", s_my_hero); net_setenv("TT_NET_HERO", v);
    if (s_role == 1) snprintf(v, sizeof v, "host:%d", s_port); else snprintf(v, sizeof v, "join:%s:%d", s_host_addr, s_port);
    net_setenv("TT_LAUNCHED", "1"); net_setenv("TT_SKIPMOVIES", "1");
    net_setenv("RECOMP_PAD_SCRIPT", getenv("TT_NET_NEXTSCRIPT"));   /* (tests: the next start's own presses) */
    net_setenv("TT_NET_NEXTSCRIPT", getenv("TT_NET_NEXTSCRIPT2"));
    net_setenv(game == 1 ? "TT_NET" : "TT_NET_LOBBY", game == 2 ? NULL : v); net_setenv(game == 1 ? "TT_NET_LOBBY" : "TT_NET", NULL);   /* (2: off line, the plain main menu) */
    if (game == 4) { net_setenv("TT_NET_LOBBY", "off"); net_setenv("TT_NET_NOTE", "ONLINE: the Extra heroes mod is on (unlocked for testing)"); game = 0; }   /* (4: the Online menu again, the mod turned on) */
    else if (game == 3) { net_setenv("TT_NET_LOBBY", "off"); net_setenv("TT_NET_NOTE", "ONLINE: the host left the game"); game = 0; }   /* (3: the Online menu, not connected: the host gone) */
    else net_setenv("TT_NET_NOTE", NULL);
    if (game == 1) {
        net_setenv("TT_NET_POSTWAIT", NULL);
        {   /* (2.1) Versus: player 1 any hero -- an extra hero is hero 4 (Isildur's place) wearing the pick (TT_GUEST); Troll Battle the cave troll */
            int h1 = s_settings[ST_HERO] % NET_NHERO, vs = s_settings[ST_MODE] && !s_settings[ST_FLOW], tr = vs && lb_troll();
            const char *p1 = s_settings[ST_FLOW] ? s_p1key : !vs ? s_heroes[h1 % 3].key : tr || h1 >= 3 ? "isildur" : s_heroes[h1].key;
            snprintf(v, sizeof v, "SKIPMAINMENU=1;HERO=%s;LEVEL=%s", p1, s_levels[s_settings[ST_LEVEL] % NET_NLEVEL].code);
            net_setenv("TT_GUEST", tr ? "troll" : vs && h1 >= 4 ? s_heroes[h1].key : NULL);
            net_setenv("TT_VS_TROLL", tr ? "1" : NULL); }
        net_setenv("TT_NET_SAVE", s_settings[ST_FLOW] && s_save_ok ? "online_save.bin" : NULL);
        net_setenv("TT_TWEAKS", v); net_setenv("TT_COOP", "clone");
        net_setenv("TT_VERSUS", s_settings[ST_MODE] ? "1" : NULL);
        net_setenv("TT_VS_TDM", s_settings[ST_MODE] && np == 4 && s_settings[ST_VSKIND] != 2 ? "1" : NULL);   /* (Versus with four: Team Deathmatch) */
        net_setenv("TT_VS_FFA", s_settings[ST_MODE] && np >= 3 && s_settings[ST_VSKIND] == 2 ? "1" : NULL);   /* (or Free-for-all) */
        net_setenv("TT_VS_FFAPLAYERS", s_settings[ST_MODE] && np == 3 ? "3" : NULL);
        net_setenv("TT_COOP_P2CHAR", lb_troll() ? "troll" : s_heroes[s_settings[ST_HERO + 1] % NET_NHERO].key);
        net_setenv("TT_COOP_P3CHAR", np > 2 ? s_heroes[s_settings[ST_HERO + 2] % NET_NHERO].key : NULL);
        net_setenv("TT_COOP_P4CHAR", np > 3 ? s_heroes[s_settings[ST_HERO + 3] % NET_NHERO].key : NULL);
        snprintf(v, sizeof v, "%d", np); net_setenv("TT_COOP_PLAYERS", v);
        if (s_settings[ST_DMGT]) { snprintf(v, sizeof v, "%d", s_settings[ST_DMGT] * 4); net_setenv("TT_NET_DMGT", v); snprintf(v, sizeof v, "%d", s_settings[ST_DMGE] * 4); net_setenv("TT_NET_DMGE", v); }
        net_setenv("TT_HEROES", "2");   /* (2.1) online: the Extra heroes mod on and unlocked for testing, on every machine -- any hero for either player */
        net_setenv("TT_FPS", NULL);   /* (2.1: each machine's own frame rate; the steps' time is the host's) */
        for (k = 0; k < ST_NCOOP; k++) { snprintf(v, sizeof v, "%d", s_settings[ST_COOPSET + k]); net_setenv(tt_coop_setting_env(k), v); }
        snprintf(v, sizeof v, "%d", s_settings[ST_DIFF] & 3); net_setenv(tt_coop_setting_env(0), v);
        net_setenv(tt_coop_setting_env(4), "0"); net_setenv(tt_coop_setting_env(9), "0"); net_setenv(tt_coop_setting_env(10), "0");   /* (no bots: every player is a person) */
        net_setenv(tt_coop_setting_env(3), "0");   /* (no teleport: each his own camera) */
    } else {
        const char *gone[] = { "TT_TWEAKS", "TT_COOP", "TT_VERSUS", "TT_COOP_P2CHAR", "TT_COOP_P3CHAR", "TT_COOP_P4CHAR", "TT_COOP_PLAYERS", "TT_VS_TDM", "TT_NET_DMGT", "TT_NET_DMGE", "TT_HEROES", "TT_FPS", "TT_VS_FFA", "TT_VS_FFAPLAYERS", "TT_GUEST", "TT_VS_TROLL" };
        for (k = 0; k < (int)(sizeof gone / sizeof gone[0]); k++) net_setenv(gone[k], NULL);
        net_setenv("TT_HEROES", "2");   /* (2.1) the Online menu: the Extra heroes mod on, unlocked for testing */
        net_setenv("TT_NET_SAVE", NULL); s_settings[ST_FLOW] = 0;
        for (k = 0; k < ST_NCOOP; k++) net_setenv(tt_coop_setting_env(k), NULL);
    }
    tt_dbg("[NET] starting the game again: %s\n", game == 1 ? "into the online game" : game == 2 ? "off line" : "back to the Online menu");
    if (s_sock != NET_BAD) { closesocket(s_sock); s_sock = NET_BAD; }
    net_sleep_ms(game ? 0 : 0);
#if defined(_WIN32)
    {
        STARTUPINFOA si; PROCESS_INFORMATION pi; char exe[MAX_PATH];
        memset(&si, 0, sizeof si); si.cb = sizeof si; memset(&pi, 0, sizeof pi);
        GetModuleFileNameA(NULL, exe, sizeof exe);
        if (CreateProcessA(exe, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); ExitProcess(0); }
        tt_dbg("[NET] could not start the game again (%lu)\n", (unsigned long)GetLastError());
    }
#else
    {
        static char cmd[8192], exe[4096], *argv[64]; int fd, len = 0, ac = 0; ssize_t r;
        fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) { while ((r = read(fd, cmd + len, sizeof cmd - 1 - (size_t)len)) > 0) len += (int)r; close(fd); }
        for (k = 0; k < len && ac < 63; ) { argv[ac++] = cmd + k; k += (int)strlen(cmd + k) + 1; }
        argv[ac] = NULL;
        r = readlink("/proc/self/exe", exe, sizeof exe - 1); if (r > 0) exe[r] = 0; else snprintf(exe, sizeof exe, "%s", ac ? argv[0] : "./twotowers_recomp");
        if (!ac) { argv[0] = exe; argv[1] = NULL; }
        fflush(stderr);
        /* (2.1) every file this process has open closed at the exec: execv keeps them in the new game (no close-on-exec), the game's
         * save images and disc files among them -- still held, the new game's reads of them failed: "There's a problem with the
         * disc" on Linux after an online game ended or the host left */
        { int q; for (q = 3; q < 4096; q++) { int fl = fcntl(q, F_GETFD); if (fl >= 0) fcntl(q, F_SETFD, fl | FD_CLOEXEC); } }   /* (marked close-on-exec, not closed here: closing them under the running threads -- the window's X connection among them -- ended the game before the new one started) */
        execv(exe, argv);
        tt_dbg("[NET] could not start the game again (%d)\n", errno);
    }
#endif
}
/* (2.1) the Online menu opened: the Extra heroes mod on and unlocked for testing on this machine (the ini too, so it stays) --
 * a game started with it otherwise starts again into the Online menu with it on (its files are chosen as the game starts) */
void tt_net_heroes_check(void)
{
    extern int tt_heroes_online_ready(void); extern void tt_heroes_online_set_ini(void);
    tt_heroes_online_set_ini();
    if (tt_heroes_online_ready() || getenv("TT_NET_NOHEROESRESTART")) return;
    tt_dbg("[NET] the Online menu: the Extra heroes mod turned on -- the game starts again with it\n");
    s_role = 0; net_relaunch(4);
}
int tt_net_lobby_start(void)   /* 2: the host goes on to the save and the map (Co-op) */
{
    extern int tt_coop_setting(int i); int k;
    if (s_role != 1) return 0;
    if (!lb_all_in()) { note("ONLINE: not everyone is here yet", ""); return 0; }
    if (!lb_all_ready()) { note("ONLINE: waiting for everyone to be Ready", ""); return 0; }
    if (!s_settings[ST_MODE]) {   /* (2.1) Co-op: the host's own way -- New or Load Game, the save, the select, the map -- the others wait */
        s_settings[ST_FLOW] = 1; s_settings[ST_CONN] = lb_conn(); send_simple(-1, PK_WELCOME);
        tt_dbg("[NET] the host chooses the save and the mission (%d players)\n", s_settings[ST_NP]);
        return 2;
    }
    for (k = 0; k < ST_NCOOP; k++) s_settings[ST_COOPSET + k] = (uint8_t)tt_coop_setting(k);
    lb_gameplay_settings();
    s_settings[ST_START]++; if (!s_settings[ST_START]) s_settings[ST_START] = 1;
    s_settings[ST_CONN] = lb_conn();
    tt_dbg("[NET] the host starts the game: %s, %s, %d players\n", s_settings[ST_MODE] ? "Versus" : "Co-op", s_levels[s_settings[ST_LEVEL] % NET_NLEVEL].name, s_settings[ST_NP]);
    { uint32_t t0 = net_ms(); while (net_ms() - t0 < 1200u) { int p; send_simple(-1, PK_WELCOME); for (p = 0; p < 20; p++) { net_pump(); net_sleep_ms(5); } } }   /* (every joiner told) */
    net_relaunch(1);
    return 1;
}
/* (2.1) Co-op: the host's mission begins (the first frame of play) -- his save and the level to every joiner, then every
 * machine starts it afresh, the save put in place before the level loads (tt_net_save_put) */
static void send_save(void)
{
    uint32_t off; uint8_t b[22 + 1024];
    for (off = 0; off < NET_SAVE_N; off += 1024u) { uint32_t len = NET_SAVE_N - off < 1024u ? NET_SAVE_N - off : 1024u; int n = head(b, PK_SAVE), q;
        put32(b + n, off); b[n + 4] = (uint8_t)len; b[n + 5] = (uint8_t)(len >> 8); n = 22; memcpy(b + n, s_save + off, len); n += (int)len;
        for (q = 1; q < NET_MAXP; q++) net_send_to(q, b, n); }
}
static void write_save_file(void)
{
    FILE *f = fopen("online_save.bin", "wb");
    if (f) { fwrite(s_save, 1, NET_SAVE_N, f); fclose(f); }
}
static void flow_go(void)
{
    extern uint32_t tt_net_level_now(void); extern const char *tt_net_p1_key(void); extern void tt_net_save_get(uint8_t *out, uint32_t a, uint32_t n);
    extern int tt_coop_setting(int i); uint32_t lv = tt_net_level_now(), t0; int k, p, all;
    s_settings[ST_LEVEL] = (uint8_t)(lv < (uint32_t)NET_NLEVEL ? lv : 1u);
    snprintf(s_p1key, sizeof s_p1key, "%s", tt_net_p1_key());
    tt_net_save_get(s_save, NET_SAVE_A, NET_SAVE_N); s_save_ok = 1;
    {   /* (2.1) the other players' heroes: the host's own picks at the character select (he chooses for everyone) */
        extern char tt_coop_p2_selected[32], tt_coop_px_selected[2][32]; int q, h;
        for (q = 1; q < 4; q++) { const char *nm = q == 1 ? tt_coop_p2_selected : tt_coop_px_selected[q - 2];
            for (h = 0; h < NET_NHERO; h++) if (nm[0] && !strcmp(nm, s_heroes[h].key)) { s_settings[ST_HERO + q] = (uint8_t)h; tt_dbg("[NET] player %d: %s (the host's pick)\n", q + 1, s_heroes[h].name); } } }
    for (k = 0; k < ST_NCOOP; k++) s_settings[ST_COOPSET + k] = (uint8_t)tt_coop_setting(k);
    lb_gameplay_settings();
    tt_dbg("[NET] the host's mission: %s, his hero %s -- the save to every player\n", s_levels[s_settings[ST_LEVEL]].name, s_p1key);
    xbox_BannerSet("ONLINE: sending the save to the other players...", "");
    t0 = net_ms();
    do { send_save(); send_simple(-1, PK_WELCOME); for (p = 0; p < 20; p++) { net_pump(); net_sleep_ms(5); }
        all = 1; for (p = 1; p < s_settings[ST_NP]; p++) if (s_have_peer[p] && !s_lb_saveok[p]) all = 0;
    } while (!all && net_ms() - t0 < 120000u);   /* (a joiner may still be at the end of the last level) */
    s_settings[ST_START]++; if (!s_settings[ST_START]) s_settings[ST_START] = 1; s_settings[ST_CONN] = lb_conn();
    { uint32_t t1 = net_ms(); while (net_ms() - t1 < 1200u) { send_simple(-1, PK_WELCOME); for (p = 0; p < 20; p++) { net_pump(); net_sleep_ms(5); } } }
    write_save_file();
    net_relaunch(1);
}
/* the lobby, each frame (tt_net_step out of play): the host's choices out to all, the joiners' heroes in */
static void lobby_tick(void)
{
    static uint32_t lt; static int start_base = -1; uint32_t now = net_ms();
    if (!s_lobby || !s_role) return;
    if (s_role == 1) {
        int p; s_settings[ST_HERO] = s_my_hero;
        for (p = 1; p < NET_MAXP; p++) if (s_have_peer[p]) { if (!s_settings[ST_FLOW] && lb_hero_ok(p, s_lb_hero[p])) s_settings[ST_HERO + p] = s_lb_hero[p]; if (now - s_peer_seen[p] > 8000u) { char l[64]; s_have_peer[p] = 0; snprintf(l, sizeof l, "ONLINE: player %d lost", p + 1); note(l, ""); } }
        s_settings[ST_CONN] = lb_conn(); s_np = s_settings[ST_NP];
        { uint8_t m = 1; for (p = 1; p < NET_MAXP; p++) if (s_have_peer[p] && s_lb_ready[p]) m |= (uint8_t)(1u << p); s_settings[ST_READY] = m; }
        if (now - lt > 250u) { lt = now; send_simple(-1, PK_WELCOME); }
        if (s_settings[ST_FLOW]) { extern uint32_t tt_net_screen(void); if (tt_net_screen() == 6u) flow_go(); }   /* (his mission begins) */
        return;
    }
    if (now - lt > 300u) { lt = now; send_simple(0, s_welcomed ? PK_LOBBY : PK_HELLO); }
    if (s_host_gone || (s_welcomed && now - s_peer_seen[0] > 8000u)) { extern void tt_net_lobby_leave(void); s_host_gone = 0; tt_net_lobby_leave(); note("ONLINE: the host left -- not connected", ""); return; }
    if (!s_welcomed) return;
    if (start_base < 0) start_base = s_settings[ST_START];   /* (the host's count of games started, when first heard) */
    else if (s_settings[ST_START] != start_base && (!s_settings[ST_FLOW] || s_save_ok)) { tt_dbg("[NET] the host started the game\n"); if (s_settings[ST_FLOW]) write_save_file(); net_relaunch(1); }
}
/* a game over: back to the Online menu once out of play at the map or the main menu; a vote: the level again or the menu */
/* (2.1) the level's end online: player 2's own upgrade screen on his machine, its result to the host, then his Continue
 * is his Ready -- the Online menu, waiting while the host chooses the next mission (flow_go starts it for everyone) */
void tt_net_p2_upg_send(int hb, const uint8_t *rec, const uint8_t *blk)
{
    uint8_t pk[12 + 1 + 0x348 + 0x50]; int n = head(pk, PK_P2UPG), k; uint32_t id = net_ms() | 1u;
    if (s_role != 2) return;
    put32(pk + 8, id); pk[n++] = (uint8_t)hb; memcpy(pk + n, rec, 0x348); n += 0x348; memcpy(pk + n, blk, 0x50); n += 0x50;
    for (k = 0; k < 4; k++) { net_send_to(0, pk, n); net_sleep_ms(5); }
    tt_dbg("[NET] player 2's upgrades sent to the host (hero %d)\n", hb);
}
void tt_net_post_wait(void)
{
    if (s_role != 2) return;
    s_settings[ST_FLOW] = 1;
    net_setenv("TT_NET_NOTE", NULL);
    tt_dbg("[NET] player 2 ready: waiting for player 1 to choose the mission\n");
    s_post_wait = 1; net_setenv("TT_NET_POSTWAIT", "1"); net_relaunch(0);
}
static void game_over_tick(void)
{
    extern uint32_t tt_net_screen(void); uint32_t sc = tt_net_screen();
    static int mapflow; static uint32_t lt;
    if (s_lobby || !s_role || !s_entry) return;
    /* (2.1) Co-op, a level done: back at the map the host chooses the next mission with his save as it is now (the level's
     * progress and upgrades in it); the others wait in the Online menu, then everyone starts that mission (flow_go) */
    if (s_role == 1 && net_ms() - lt > 250u) { lt = net_ms(); send_simple(-1, PK_WELCOME); }   /* (out of play: the joiners, waiting in the Online menu, hear from the host) */
    if (!s_settings[ST_MODE] && s_role == 1 && (sc == 1u || mapflow)) {
        if (!mapflow) { mapflow = 1; s_settings[ST_FLOW] = 1; memset(s_lb_saveok, 0, sizeof s_lb_saveok); s_save_ok = 0;
            tt_dbg("[NET] at the map: the host chooses the next mission\n"); note("ONLINE: choose the next mission -- the others are waiting", ""); }
        if (sc == 6u) { mapflow = 0; tt_dbg("[NET] the host's mission chosen: to every player\n"); flow_go(); return; }
        if (sc != 4u) return;
    }
    if (!s_settings[ST_MODE] && s_role == 2 && sc == 1u) { s_settings[ST_FLOW] = 1; net_relaunch(0); return; }   /* (2.1: the level's end screens are his own now -- tt_net_post_wait sends him on) */   /* (2.1: from the level's end screens too -- the upgrades are the host's, his save goes on) */   /* (a joiner: to the Online menu, to wait) */
    if (sc == 1u || sc == 4u) net_relaunch(0);
}
void tt_net_relaunch_vote(int what) { if (what == 1) net_relaunch(1); else net_relaunch(0); }
void tt_net_lobby_tick_(void) { lobby_tick(); }
void tt_net_game_over_tick_(void) { game_over_tick(); }
