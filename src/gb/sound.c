/* sound.c - SPEEDY PURPLE PETER sound engine: a small 4-channel song player + sfx scripts.
 *
 * Bank 0, data const, 100 bytes of RAM.  snd_tick() runs once per frame from VBlank; every
 * other call only posts a request (the game loop and the ISR share nothing else).  Host
 * build: -DHOST_TEST sends register writes to host_snd_write() (tests/test_sound.c is a
 * fake APU that checks them).
 *
 * Songs (sound_data.h): one byte stream per channel with pattern calls, transpose and
 * inline instruments.  Tempo is an 8-bit accumulator (a row on each carry), so hurry is
 * just a bigger rate (1.5x).  Between rows a frame costs a few compares.
 *
 *   CH1  harmony / sparkle (the sfx channel: skipped while an sfx owns it)
 *   CH2  lead
 *   CH3  bass: triggered once and never retriggered (a retrigger while playing corrupts wave
 *        RAM on the DMG); notes only change the frequency, and the level follows a small
 *        software envelope (NR32, one step per frame).  A new wave is loaded only after the
 *        level reached 0% and the length counter stopped the channel.
 *   CH4  drums
 *
 * SFX: register scripts, one slot each for CH1, CH2 and CH4, with a priority.  A channel an
 * sfx owns is left alone by the song (which keeps its place) and comes back at its next note.
 *
 * No pops on real hardware (a DAC that is on sits at full DC offset even at volume 0):
 *   - every DAC is switched on once in snd_init() and never off (NRx2 is never written with
 *     volume 0 + decreasing, NR30 never cleared); silence is an envelope running out;
 *   - NR51 (panning) and NR50 are written once, in snd_init();
 *   - every envelope falls (no zombie-mode volume jumps); the CH3 level moves one step a frame;
 *   - wave RAM is written only while CH3 is stopped.
 *
 * Speed (SDCC on the SM83 is slow with arrays and stack locals): the per-channel code is
 * written out once per channel with fixed-address state (the CH_* / S_* macros); each
 * channel's next event is parsed ahead on the frames between rows, so a row only writes
 * registers; a song start takes a frame of its own; new sfx wait out a row frame.  Measured
 * in PyBoy (tools/render_audio.py): ~100 M-cycles idle, ~250 average with music, ~800 worst.
 */
#include <stdint.h>
#include "sound.h"
#include "hw_sound.h"
#define SND_FREQ_TABLE
#include "sound_notes.h"
#include "sound_data.h"

#define RQ_NONE 0xFF

/* ------------------------------------------------------------------ state */
/* requests (game loop -> snd_tick) */
static volatile uint8_t rq_song = RQ_NONE, rq_hurry = RQ_NONE;
static volatile uint8_t sq[4];
static volatile uint8_t sq_head, sq_tail;
static uint8_t sq_waited;                 /* the queue head waited out a busy frame */

/* song player */
static uint8_t song, hurry, rate, acc, live, delay;
static uint8_t busy;                      /* this frame's music work: 0, 1 (parsed two), 2 (row) */
static uint8_t need;                      /* channels whose next event is still to be parsed */

/* Per channel: the stream position, and the next event, parsed ahead of time (see row()). */
#define EV_NOTE 0
#define EV_REST 1
#define EV_END  2
#define CH_STATE(N) \
    static const uint8_t *c##N##_ptr, *c##N##_ret, *c##N##_loop; \
    static uint8_t c##N##_wait, c##N##_len, c##N##_tr, c##N##_ev, c##N##_plen
CH_STATE(0);
CH_STATE(1);
CH_STATE(2);
CH_STATE(3);
static uint16_t c0_px, c1_px, c2_px;      /* the next note's frequency register */
static const uint8_t *c3_pd;              /* the next drum */
static uint8_t c0_duty, c0_env, c1_duty, c1_env;

/* bass */
static uint8_t b_lvl, b_tgt, b_hold, b_peak, b_phold, b_sus;
static uint8_t w_on, w_cur, w_want;
static uint16_t b_x;

/* sfx */
#define S_STATE(N) static const uint8_t *s##N##_ptr; static uint8_t s##N##_wait, s##N##_prio
S_STATE(0);
S_STATE(1);
S_STATE(2);
static uint8_t owned;                     /* bit 0 = CH1 .. bit 3 = CH4 */

static const uint8_t nr32_lvl[4] = { 0x00, 0x60, 0x40, 0x20 };   /* mute, 25%, 50%, 100% */

/* ------------------------------------------------------------------ public API */
void sfx_play(uint8_t id)
{
    uint8_t h;
    if (id >= NUM_SFX)
        return;
    h = sq_head;
    if (((h + 1) & 3) == sq_tail)
        return;                           /* 3 already queued this frame: drop */
    sq[h] = id;
    sq_head = (uint8_t)((h + 1) & 3);
}

void music_play(uint8_t s)
{
    if (s < NUM_MUS)
        rq_song = s;
}

void music_hurry(uint8_t on)
{
    rq_hurry = on ? 1 : 0;
}

uint8_t music_done(void)
{
    return (uint8_t)(rq_song == RQ_NONE && song == MUS_NONE);
}

#ifdef HOST_TEST
uint8_t snd_debug_owned(void) { return owned; }
uint8_t snd_debug_song(void) { return song; }
uint8_t snd_debug_hurry(void) { return hurry; }
#endif

/* ------------------------------------------------------------------ parsing */
#define INS0(p) do { c0_duty = (p)[0]; c0_env = (p)[1]; } while (0)
#define INS1(p) do { c1_duty = (p)[0]; c1_env = (p)[1]; } while (0)
#define INS2(p) do { b_peak = (p)[0]; b_phold = (p)[1]; b_sus = (p)[2]; } while (0)
#define INS3(p) ((void)0)

#define PREP0(b) c0_px = snd_freq[(uint8_t)((b) + c0_tr)]
#define PREP1(b) c1_px = snd_freq[(uint8_t)((b) + c1_tr)]
#define PREP2(b) c2_px = snd_freq[(uint8_t)((b) + c2_tr + 12)]   /* CH3: an octave down */
#define PREP3(b) c3_pd = snd_drum[b]

/* Read channel N's stream up to its next note / rest / end and keep it ready for row().
   One copy per channel: fixed addresses are much cheaper than indexing on the SM83. */
#define CH_PARSE(N, INSLEN) \
static void ch##N##_parse(void) \
{ \
    const uint8_t *p = c##N##_ptr; \
    uint8_t b; \
    for (;;) { \
        b = *p++; \
        if (b < SB_NOTES) { \
            c##N##_ev = EV_NOTE; \
            PREP##N(b); \
            break; \
        } \
        if (b >= SB_LEN) { \
            c##N##_len = (uint8_t)(b - (SB_LEN - 1)); \
            continue; \
        } \
        switch (b) { /* 0x80..0x86: a jump table */ \
        case SB_END: \
            c##N##_ev = EV_END; \
            goto out; \
        case SB_LOOP: \
            p = c##N##_loop; \
            break; \
        case SB_CALL: \
            c##N##_ret = p + 1; \
            p = snd_pat[*p]; \
            break; \
        case SB_RET: \
            p = c##N##_ret; \
            break; \
        case SB_TR: \
            c##N##_tr = *p++; \
            break; \
        case SB_INS: \
            INS##N(p); \
            p += INSLEN; \
            break; \
        default: /* SB_REST */ \
            c##N##_ev = EV_REST; \
            goto out; \
        } \
    } \
out: \
    c##N##_plen = c##N##_len; \
    c##N##_ptr = p; \
    need &= (uint8_t)~(1 << N); \
}
CH_PARSE(0, 2)
CH_PARSE(1, 2)
CH_PARSE(2, 3)
CH_PARSE(3, 0)

#ifdef HOST_TEST
static unsigned late_parses;
unsigned snd_debug_late(void) { return late_parses; }
#define LATE() late_parses++
#else
#define LATE() ((void)0)
#endif

/* ------------------------------------------------------------------ song control */
static void set_rate(void)
{
    rate = hurry ? snd_song_rate_h[song] : snd_song_rate[song];
}

/* A new song plays nothing until the channels' first events are parsed, one a frame (after
   any wave change), so a song start never shares a frame with a row. */

static void song_start(uint8_t s)
{
    const uint8_t * const *q = snd_song_ch[s];
    uint8_t f = snd_song_flags[s];
    song = s;
    if (f & SF_HURRY_ON)
        hurry = 1;
    if (f & SF_HURRY_OFF)
        hurry = 0;
    c0_ptr = *q++;
    c1_ptr = *q++;
    c2_ptr = *q++;
    c3_ptr = *q++;
    c0_loop = *q++;
    c1_loop = *q++;
    c2_loop = *q++;
    c3_loop = *q;
    c0_wait = c1_wait = c2_wait = c3_wait = 1;
    c0_len = c1_len = c2_len = c3_len = 1;
    c0_tr = c1_tr = c2_tr = c3_tr = 0;
    f = 0;
    if (c0_ptr)
        f = 1;
    if (c1_ptr)
        f |= 2;
    if (c2_ptr)
        f |= 4;
    if (c3_ptr)
        f |= 8;
    live = need = f;
    b_tgt = 0;
    b_hold = 0;
    f = snd_song_wave[s];
    if (f != WV_KEEP)
        w_want = f;
    set_rate();
    delay = 1;
}

static void rest2(void) { b_tgt = 0; b_hold = 0; }

/* A row only plays events parsed on the frames before (parse_ahead), so it costs about
   four sets of register writes.  If a parse is somehow late, the channel parses now. */
static void row(void)
{
    uint8_t l = live, e;
    if ((l & 1) && !--c0_wait) {
        if (need & 1) {
            LATE();
            ch0_parse();
        }
        e = c0_ev;
        if (e == EV_NOTE) {
            if (!(owned & 1)) {
                SND_W(SND_NR11, c0_duty);
                SND_W(SND_NR12, c0_env);
                SND_W(SND_NR13, (uint8_t)c0_px);
                SND_W(SND_NR14, (uint8_t)(0x80 | (c0_px >> 8)));
            }
        } else if (e == EV_END) {
            live &= (uint8_t)~1;
            goto ch1;
        }
        c0_wait = c0_plen;
        need |= 1;
    }
ch1:
    if ((l & 2) && !--c1_wait) {
        if (need & 2) {
            LATE();
            ch1_parse();
        }
        e = c1_ev;
        if (e == EV_NOTE) {
            if (!(owned & 2)) {
                SND_W(SND_NR21, c1_duty);
                SND_W(SND_NR22, c1_env);
                SND_W(SND_NR23, (uint8_t)c1_px);
                SND_W(SND_NR24, (uint8_t)(0x80 | (c1_px >> 8)));
            }
        } else if (e == EV_END) {
            live &= (uint8_t)~2;
            goto ch2;
        }
        c1_wait = c1_plen;
        need |= 2;
    }
ch2:
    if ((l & 4) && !--c2_wait) {
        if (need & 4) {
            LATE();
            ch2_parse();
        }
        e = c2_ev;
        if (e == EV_NOTE) {               /* bass: new pitch, no retrigger */
            b_x = c2_px;
            if (w_on) {
                SND_W(SND_NR33, (uint8_t)c2_px);
                SND_W(SND_NR34, (uint8_t)(c2_px >> 8));
            }
            b_hold = b_phold;
            b_tgt = b_phold ? b_peak : b_sus;
        } else {
            rest2();
            if (e == EV_END) {
                live &= (uint8_t)~4;
                goto ch3;
            }
        }
        c2_wait = c2_plen;
        need |= 4;
    }
ch3:
    if ((l & 8) && !--c3_wait) {
        if (need & 8) {
            LATE();
            ch3_parse();
        }
        e = c3_ev;
        if (e == EV_NOTE) {               /* drum */
            if (!(owned & 8)) {
                const uint8_t *d = c3_pd;
                SND_W(SND_NR41, d[0]);
                SND_W(SND_NR42, d[1]);
                SND_W(SND_NR43, d[2]);
                SND_W(SND_NR44, d[3]);
            }
        } else if (e == EV_END) {
            live &= (uint8_t)~8;
            goto done;
        }
        c3_wait = c3_plen;
        need |= 8;
    }
done:
    if (!live) {                          /* a jingle ended */
        l = snd_song_next[song];
        if (l != MUS_NONE)
            song_start(l);
        else
            song = MUS_NONE;
    }
}

static void parse_one(void)
{
    uint8_t n = need;
    if (n & 1)
        ch0_parse();
    else if (n & 2)
        ch1_parse();
    else if (n & 4)
        ch2_parse();
    else
        ch3_parse();
}

/* Parse the channels that played on the last row, spread over the frames before the next
   one (rows are >= 3 frames apart): ceil(waiting / frames left), so at most two a frame. */
static void parse_ahead(uint8_t a)
{
    static const uint8_t bits[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
    uint8_t n = bits[need], r = rate, t;
    parse_one();
    if (n == 1)
        return;
    t = (uint8_t)(a + r);
    if (t < a) {                          /* the next frame is a row: finish now */
        while (need)
            parse_one();
        busy = 1;
        return;
    }
    if ((uint8_t)(t + r) < t) {           /* one more frame: half now (rounded up) */
        if (n >= 3) {
            parse_one();
            busy = 1;
        }
        return;
    }
    if (n == 4 && (uint8_t)(t + r + r) < (uint8_t)(t + r)) {
        parse_one();                      /* two more frames: two now, one, one */
        busy = 1;
    }
}

/* ------------------------------------------------------------------ bass level + wave */
/* The wave channel's rare state changes: fade out and stop for a new wave, then load it. */
static void wave_frame(void)
{
    if (w_on) {
        if (b_lvl) {                      /* fade out first, a step a frame */
            b_lvl--;
            SND_W(SND_NR32, nr32_lvl[b_lvl]);
            return;
        }
        SND_W(SND_NR31, 0xFF);
        SND_W(SND_NR34, (uint8_t)(0x40 | (b_x >> 8)));   /* length on: stops within */
        w_on = 0;                                        /* 1/256 s, before next frame */
        w_cur = WV_KEEP;                  /* (reload even if the old wave is wanted again) */
        return;
    }
    {
        const uint8_t *w = snd_wave[w_want];   /* CH3 stopped (its DAC on): load */
        SND_W(SND_WAVE + 0x0, w[0x0]);
        SND_W(SND_WAVE + 0x1, w[0x1]);
        SND_W(SND_WAVE + 0x2, w[0x2]);
        SND_W(SND_WAVE + 0x3, w[0x3]);
        SND_W(SND_WAVE + 0x4, w[0x4]);
        SND_W(SND_WAVE + 0x5, w[0x5]);
        SND_W(SND_WAVE + 0x6, w[0x6]);
        SND_W(SND_WAVE + 0x7, w[0x7]);
        SND_W(SND_WAVE + 0x8, w[0x8]);
        SND_W(SND_WAVE + 0x9, w[0x9]);
        SND_W(SND_WAVE + 0xA, w[0xA]);
        SND_W(SND_WAVE + 0xB, w[0xB]);
        SND_W(SND_WAVE + 0xC, w[0xC]);
        SND_W(SND_WAVE + 0xD, w[0xD]);
        SND_W(SND_WAVE + 0xE, w[0xE]);
        SND_W(SND_WAVE + 0xF, w[0xF]);
    }
    w_cur = w_want;
    SND_W(SND_NR33, (uint8_t)b_x);
    SND_W(SND_NR34, (uint8_t)(0x80 | (b_x >> 8)));   /* trigger, at 0% */
    w_on = 1;
    b_lvl = 0;
}

/* the bass level: one NR32 step a frame towards b_tgt */
static void level_step(void)
{
    if (b_lvl < b_tgt)
        b_lvl++;
    else
        b_lvl--;
    SND_W(SND_NR32, nr32_lvl[b_lvl]);
}

/* ------------------------------------------------------------------ sfx */
static uint8_t t_id, t_pr, t_m;           /* sfx_start() arguments / temporaries */

static void sfx_start(void)
{
    t_pr = snd_sfx_prio[t_id];
    t_m = snd_sfx_mask[t_id];
    if (t_m & owned) {
        if ((t_m & 1) && s0_ptr && s0_prio > t_pr)
            return;                       /* something more important is playing there */
        if ((t_m & 2) && s1_ptr && s1_prio > t_pr)
            return;
        if ((t_m & 8) && s2_ptr && s2_prio > t_pr)
            return;
    }
    if (t_m & 1) {
        s0_ptr = snd_sfx_s0[t_id];
        s0_wait = 0;
        s0_prio = t_pr;
    }
    if (t_m & 2) {
        s1_ptr = snd_sfx_s1[t_id];
        s1_wait = 0;
        s1_prio = t_pr;
    }
    if (t_m & 8) {
        s2_ptr = snd_sfx_s2[t_id];
        s2_wait = 0;
        s2_prio = t_pr;
    }
    owned |= t_m;
}

/* run slot N's script for this frame (one copy per slot, like the channels) */
#define S_RUN(N, BIT, RELEASE) \
static void s##N##_run(void) \
{ \
    const uint8_t *p = s##N##_ptr; \
    uint8_t b; \
    SND_SRC(SND_SRC_SFX); \
    for (;;) { \
        b = *p++; \
        if (b >= 0x40) { \
            s##N##_wait = (uint8_t)(b - 0x40); \
            break; \
        } \
        if (!b) { /* hand the channel back */ \
            RELEASE; \
            owned &= (uint8_t)~(BIT); \
            p = 0; \
            break; \
        } \
        SND_W(b, *p); \
        p++; \
    } \
    s##N##_ptr = p; \
    SND_SRC(SND_SRC_MUSIC); \
}
S_RUN(0, 1, SND_W(SND_NR10, 0x08))       /* sweep off (negate kept: see sound_data.h) */
S_RUN(1, 2, (void)0)
S_RUN(2, 8, (void)0)

/* ------------------------------------------------------------------ init + tick */
void snd_init(void)
{
    SND_SRC(SND_SRC_MUSIC);
    SND_W(SND_NR52, 0x80);                /* APU on */
    SND_W(SND_NR50, 0x77);                /* master volume: full, both sides */
    SND_W(SND_NR51, 0xFF);                /* every channel centred, for good */
    SND_W(SND_NR10, 0x08);
    SND_W(SND_NR12, 0x11);                /* DACs on (volume 1, falling: never 0x00) */
    SND_W(SND_NR22, 0x11);
    SND_W(SND_NR42, 0x11);
    SND_W(SND_NR32, 0x00);
    SND_W(SND_NR30, 0x80);
    rq_song = rq_hurry = RQ_NONE;
    sq_head = sq_tail = sq_waited = 0;
    song = MUS_NONE;
    hurry = live = need = acc = rate = delay = 0;
    s0_ptr = s1_ptr = s2_ptr = 0;
    owned = 0;
    b_lvl = b_tgt = b_hold = b_peak = b_phold = b_sus = 0;
    c0_duty = c1_duty = 0x80;
    c0_env = c1_env = 0x11;
    b_x = snd_freq[12];                   /* (CH3 is triggered before the first bass note) */
    w_on = 0;                             /* CH3 is stopped after power-on */
    w_cur = w_want = WV_KEEP;
}

void snd_tick(void)
{
    uint8_t a;

    /* requests */
    a = rq_hurry;
    if (a != RQ_NONE) {
        rq_hurry = RQ_NONE;
        hurry = a;
        set_rate();
    }
    a = rq_song;
    if (a != RQ_NONE) {
        rq_song = RQ_NONE;
        song_start(a);
        busy = 1;
        goto sfx;                         /* a frame of its own */
    }

    /* song: a row on each carry of the tempo accumulator; parse ahead in between */
    busy = 0;
    if (delay) {                          /* a new song: parse its first events, one a */
        if (need) {                       /* frame (not while a wave loads) */
            if (w_want == w_cur)
                parse_one();
        } else {
            delay = 0;
            acc = (uint8_t)(0u - rate);   /* the first row on the next frame */
        }
    } else if (live) {
        a = (uint8_t)(acc + rate);
        if (a < acc) {
            acc = a;
            row();
            busy = 2;
        } else {
            acc = a;
            if (need)
                parse_ahead(a);
        }
    }
    if (b_hold && !--b_hold)
        b_tgt = b_sus;                    /* the pluck: peak, then sustain */
    if (w_want != w_cur)
        wave_frame();
    else if (b_lvl != b_tgt)
        level_step();

sfx:
    /* new sfx: up to two a frame; one on a frame that parsed two channels; none on a row
       frame, unless they already waited a frame (a frame later is inaudible, and it keeps
       the worst frames cheap) */
    if (sq_tail != sq_head) {
        if (busy == 2 && !sq_waited) {
            sq_waited = 1;
        } else {
            sq_waited = 0;
            a = sq_tail;
            t_id = sq[a];
            sfx_start();
            a = (uint8_t)((a + 1) & 3);
            if (a != sq_head && !busy) {
                t_id = sq[a];
                sfx_start();
                a = (uint8_t)((a + 1) & 3);
            }
            sq_tail = a;
        }
    }
    if (!owned)
        return;
    if (s0_ptr) {
        if (s0_wait)
            s0_wait--;
        else
            s0_run();
    }
    if (s1_ptr) {
        if (s1_wait)
            s1_wait--;
        else
            s1_run();
    }
    if (s2_ptr) {
        if (s2_wait)
            s2_wait--;
        else
            s2_run();
    }
}
