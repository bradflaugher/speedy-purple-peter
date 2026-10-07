/* sound.c - SPEEDY PURPLE PETER sound engine: a small 4-channel song player + sfx scripts.
 *
 * Bank 0, data const, 135 bytes of RAM.  snd_tick() runs once per frame from VBlank; every
 * other call only posts a request (the game loop and the ISR share nothing else).  Host
 * build: -DHOST_TEST sends register writes to host_snd_write() (tests/test_sound.c is a
 * fake APU that checks them).
 *
 * Songs (sound_data.h): one byte stream per channel with pattern calls, transpose and
 * inline instruments.  Tempo is an 8-bit accumulator (a row on each carry), so hurry is
 * just a bigger rate (1.5x).  Between rows a frame costs a few compares and the effects.
 *
 *   CH1  chords as a fast arpeggio (root, third, fifth: a frame each), or an echo of the
 *        lead (the sfx channel: skipped while an sfx owns it)
 *   CH2  lead: slides up into notes (SL) and a delayed vibrato on held notes
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
 * registers; the effects take a new note's values on the frame after its row and the lead's
 * wait for their next step on a countdown; a song start takes a frame of its own; new sfx
 * wait out a row frame (and a frame that parsed two channels).  Measured in PyBoy
 * (tools/render_audio.py --bench, the game's compiler flags): ~75 M-cycles idle, ~300
 * average with music (the arpeggio and vibrato cost ~50 a frame), ~900 worst.
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
static volatile uint8_t rq;               /* 1: a song or hurry request is waiting */
static volatile uint8_t sq[4];
static volatile uint8_t sq_head, sq_tail;

/* song player */
static uint8_t song, hurry, rate, acc, live, delay;
static uint8_t busy;                      /* this frame's music work: 0, 1 (parsed one), 2 (row, or parsed two) */
static uint8_t need;                      /* channels whose next event is still to be parsed */

/* Per channel: the stream position, and the next event, parsed ahead of time (see row()):
   c<N>_ev is the event's stream byte (a note < SB_NOTES, SB_REST or SB_END). */
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

/* CH1 arpeggio ("chords"): with ARP xy, a note plays root, +x, +y semitones, a frame each */
static uint8_t c0_arp, c0_parp, c0_aon, c0_ap;
static uint16_t c0_pa, c0_pb, c0_x0, c0_x1, c0_x2;
/* CH2 lead: SL scoops up into the next note; the instrument's delayed vibrato.  The p*
   values are parsed ahead for the next note, the others belong to the note playing. */
static uint8_t c1_slf, c1_psl, c1_pfx, c1_pdel;   /* slide flag, slide depth, effects (bit 0 slide,
                                                     1 vibrato), vibrato delay */
static uint16_t c1_pvu, c1_pvd;           /* vibrato: the next note's register +- depth */
static uint8_t c1_vdel;                   /* the instrument's vibrato delay (frames, 0 = none) */
static uint8_t c1_fx, c1_wake, c1_ph, c1_sl, c1_del;   /* effects (0x80: take the note's values),
                                     frames to the next step, vibrato phase, slide, delay */
static uint16_t c1_cx, c1_vu, c1_vd;

/* bass */
static uint8_t b_lvl, b_tgt, b_hold, b_peak, b_phold, b_sus;
static uint8_t w_on, w_cur, w_want;
static uint8_t b_work;                    /* the bass level / wave has something to do */
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
    if (s < NUM_MUS) {
        rq_song = s;
        rq = 1;
    }
}

void music_hurry(uint8_t on)
{
    rq_hurry = on ? 1 : 0;
    rq = 1;
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
#define INS1(p) do { c1_duty = (p)[0]; c1_env = (p)[1]; c1_vdel = (p)[2]; } while (0)
#define INS2(p) do { b_peak = (p)[0]; b_phold = (p)[1]; b_sus = (p)[2]; } while (0)
#define INS3(p) ((void)0)
#define SLIDE0() ((void)0)                /* (an echo of the lead on CH1 plays it plain) */
#define SLIDE1() (c1_slf = 1)
#define SLIDE2() ((void)0)
#define SLIDE3() ((void)0)
#define ARP0(v) (c0_arp = (v))
#define ARP1(v) ((void)0)
#define ARP2(v) ((void)0)
#define ARP3(v) ((void)0)

/* the next note's frequency (and its effects), ready for row() */
static void prep0(uint8_t n)
{
    uint8_t a = c0_arp;
    n += c0_tr;
    c0_px = snd_freq[n];
    c0_parp = a;
    if (a) {
        c0_pa = snd_freq[(uint8_t)(n + (a >> 4))];
        c0_pb = snd_freq[(uint8_t)(n + (a & 15))];
    }
}

static void prep1(uint8_t n)
{
    uint16_t x;
    uint8_t d, f = 0;
    n += c1_tr;
    c1_px = x = snd_freq[n];
    c1_psl = 0;
    if (c1_slf) {
        c1_psl = snd_sld[n];              /* slide: from about a tone below */
        c1_slf = 0;
        f = 1;
    }
    d = snd_vib[n];                       /* vibrato: about +-1/3 semitone, on notes of a */
    if (d && c1_vdel && c1_len >= 4) {    /* quarter note or longer */
        c1_pdel = c1_vdel;
        c1_pvu = x + d;
        c1_pvd = x - d;
        f |= 2;
    }
    c1_pfx = f;
}

static void prep2(uint8_t n)
{
    c2_px = snd_freq[(uint8_t)(n + c2_tr + 12)];   /* CH3: an octave down */
}

static void prep3(uint8_t n)
{
    c3_pd = snd_drum[n];
}

/* (functions, so the parsers below keep the event byte in a register) */
#define PREP0(b) prep0(b)
#define PREP1(b) prep1(b)
#define PREP2(b) prep2(b)
#define PREP3(b) prep3(b)

/* Read channel N's stream up to its next note / rest / end and keep it ready for row().
   One copy per channel: fixed addresses are much cheaper than indexing on the SM83.  The
   common bytes stay in the loop (where SDCC keeps the position in a register); the rare
   ones (TR, INS, LOOP) go to a helper that moves the position through cp. */
static const uint8_t *cp;                 /* the stream position, for the ch<N>_cmd() helpers */

#define CH_PARSE(N, INSLEN) \
static void ch##N##_cmd(uint8_t b) \
{ \
    if (b == SB_TR) { \
        c##N##_tr = *cp++; \
    } else if (b == SB_INS) { \
        INS##N(cp); \
        cp += INSLEN; \
    } else { /* SB_LOOP */ \
        cp = c##N##_loop; \
    } \
} \
static void ch##N##_parse(void) \
{ \
    const uint8_t *p = c##N##_ptr; \
    uint8_t b; \
    for (;;) { \
        b = *p++; \
        if (b <= SB_END) /* a note, or the end */ \
            break; \
        if (b >= SB_LEN) { \
            c##N##_len = (uint8_t)(b - (SB_LEN - 1)); \
            continue; \
        } \
        if (b == SB_REST) \
            break; \
        if (b == SB_RET) { \
            p = c##N##_ret + 1; \
            continue; \
        } \
        if (b == SB_TCALL) { \
            c##N##_tr = *p++; \
            goto call; \
        } \
        if (b == SB_CALL) { \
call: \
            c##N##_ret = p;               /* (at the pattern number) */ \
            p = snd_pat[*p]; \
            continue; \
        } \
        if (b == SB_SLIDE) { \
            SLIDE##N(); \
            continue; \
        } \
        if (b == SB_ARP) { \
            ARP##N(*p); \
            p++; \
            continue; \
        } \
        cp = p;                           /* the rare ones: TR, INS, LOOP */ \
        ch##N##_cmd(b); \
        p = cp; \
    } \
    c##N##_ptr = p; \
    c##N##_ev = b; \
    c##N##_plen = c##N##_len; \
    need &= (uint8_t)~(1 << N); \
    if (b < SB_NOTES) \
        PREP##N(b); \
}
CH_PARSE(0, 3)
CH_PARSE(1, 3)
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
    c0_arp = c1_slf = c0_aon = c1_fx = 0;
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
    b_work = 1;
    f = snd_song_wave[s];
    if (f != WV_KEEP)
        w_want = f;
    set_rate();
    delay = 1;
}

static void rest2(void) { b_tgt = 0; b_hold = 0; b_work = 1; }

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
        if (e < SB_NOTES) {
            if (!(owned & 1)) {
                SND_W(SND_NR11, c0_duty);
                SND_W(SND_NR12, c0_env);
                SND_W(SND_NR13, (uint8_t)c0_px);
                SND_W(SND_NR14, (uint8_t)(0x80 | (c0_px >> 8)));
                c0_aon = c0_parp;         /* (the arpeggio takes its notes next frame) */
                c0_ap = 3;
            }
        } else if (e == SB_END) {
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
        if (e < SB_NOTES) {
            if (!(owned & 2)) {
                uint16_t x = c1_px - c1_psl;   /* (a slide starts below) */
                uint8_t f = c1_pfx;
                if (f) {                  /* effects: fx_lead() takes the note's values */
                    f |= 0x80;            /* next frame */
                    c1_wake = 1;
                }
                c1_fx = f;
                SND_W(SND_NR21, c1_duty);
                SND_W(SND_NR22, c1_env);
                SND_W(SND_NR23, (uint8_t)x);
                SND_W(SND_NR24, (uint8_t)(0x80 | (x >> 8)));
            }
        } else if (e == SB_END) {
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
        if (e < SB_NOTES) {               /* bass: new pitch, no retrigger */
            b_x = c2_px;
            if (w_on) {
                SND_W(SND_NR33, (uint8_t)c2_px);
                SND_W(SND_NR34, (uint8_t)(c2_px >> 8));
            }
            b_hold = b_phold;
            b_tgt = b_phold ? b_peak : b_sus;
            b_work = 1;
        } else {
            rest2();
            if (e == SB_END) {
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
        if (e < SB_NOTES) {               /* drum */
            if (!(owned & 8)) {
                const uint8_t *d = c3_pd;
                SND_W(SND_NR41, d[0]);
                SND_W(SND_NR42, d[1]);
                SND_W(SND_NR43, d[2]);
                SND_W(SND_NR44, d[3]);
            }
        } else if (e == SB_END) {
            live &= (uint8_t)~8;
            goto done;
        }
        c3_wait = c3_plen;
        need |= 8;
    }
done:
    if (!live) {                          /* a jingle ended */
        l = snd_song_next[song];
        if (l != MUS_NONE) {
            song_start(l);
        } else {
            song = MUS_NONE;
            c1_fx = c0_aon = 0;           /* (the last notes ring out plain) */
        }
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
   one (rows are >= 3 frames apart): one a frame; two when only two frames are left and two
   or more wait; the rest on the last frame (so at most two a frame).  While a new sfx waits
   to start, what can wait for a later frame does, so the sfx gets a light frame.
   busy: 1 = parsed one, 2 = parsed two (no new sfx on this frame). */
static void parse_ahead(uint8_t a)
{
    uint8_t r = rate;
    a += r;
    if (a < r) {                          /* the next frame is a row: finish now */
        parse_one();
        busy = 1;
        if (need) {
            do
                parse_one();
            while (need);
            busy = 2;
        }
        return;
    }
    if ((uint8_t)(a + r) < r) {           /* one more frame after this one */
        a = need;
        if (sq_tail != sq_head && !(a & (uint8_t)(a - 1)))
            return;                       /* an sfx waits: one can wait for the next frame */
        parse_one();
        busy = 1;
        a = need;
        if (a & (uint8_t)(a - 1)) {       /* two or more still waiting */
            parse_one();
            busy = 2;
        }
        return;
    }
    if (sq_tail == sq_head) {             /* more frames left: one now (none if an sfx waits) */
        parse_one();
        busy = 1;
    }
}

/* ------------------------------------------------------------------ pitch effects */
/* Only a frequency is rewritten (NRx3 / NRx4 without a trigger), from values prepared ahead.
   The lead, every second frame: its slide (the gap halves each time), then, after the
   instrument's delay, its vibrato (+d, 0, -d, 0: 7.5 Hz); c1_wake counts the frames to the
   next step, so a note waiting for its vibrato costs a decrement a frame.  CH1's arpeggio: one chord note
   a frame. */
static void fx_lead(void)
{
    uint16_t x;
    uint8_t t;
    if (c1_fx & 0x80) {                   /* a new note (the frame after its row, before
                                             anything is parsed): take its values */
        c1_fx &= 3;
        c1_cx = c1_px;
        c1_vu = c1_pvu;
        c1_vd = c1_pvd;
        c1_del = t = c1_pdel;
        c1_ph = 0;
        c1_sl = c1_psl;
        if (!c1_psl) {
            c1_wake = t;                  /* the vibrato's delay */
            return;
        }
    }
    t = c1_sl;
    c1_wake = 2;                          /* a step every two frames */
    if (t) {
        t >>= 1;
        c1_sl = t;
        x = c1_cx - t;
        if (!t) {                         /* slide done: vibrato (after its delay) or nothing */
            c1_wake = c1_del;
            c1_fx &= 2;
        }
    } else {
        t = (uint8_t)(++c1_ph & 3);
        x = c1_cx;
        if (t == 1)
            x = c1_vu;
        else if (t == 3)
            x = c1_vd;
    }
    SND_W(SND_NR23, (uint8_t)x);
    SND_W(SND_NR24, (uint8_t)(x >> 8));
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

/* the bass's frame: a wave change; the level, one NR32 step a frame towards b_tgt; then the
   pluck (b_hold frames at the peak, then the sustain level).  b_work goes off once there is
   nothing left to do. */
static void bass_frame(void)
{
    uint8_t l;
    if (w_want != w_cur) {
        wave_frame();
        return;
    }
    l = b_lvl;
    if (l != b_tgt) {
        if (l < b_tgt)
            l++;
        else
            l--;
        b_lvl = l;
        SND_W(SND_NR32, nr32_lvl[l]);
        return;
    }
    if (b_hold) {
        if (!--b_hold)
            b_tgt = b_sus;
        return;
    }
    b_work = 0;
}

/* ------------------------------------------------------------------ sfx */
static uint8_t t_id, t_pr, t_m;           /* sfx_start() argument / temporaries */

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
        c0_aon = 0;                       /* no pitch effects on a borrowed channel */
        s0_ptr = snd_sfx_s0[t_id];
        s0_wait = 0;
        s0_prio = t_pr;
    }
    if (t_m & 2) {
        c1_fx = 0;
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
    rq = 0;
    sq_head = sq_tail = 0;
    song = MUS_NONE;
    hurry = live = need = acc = rate = delay = 0;
    s0_ptr = s1_ptr = s2_ptr = 0;
    owned = 0;
    b_lvl = b_tgt = b_hold = b_peak = b_phold = b_sus = b_work = 0;
    c0_duty = c1_duty = 0x80;
    c0_env = c1_env = 0x11;
    c0_arp = c0_aon = c1_fx = c1_slf = c1_vdel = 0;
    b_x = snd_freq[12];                   /* (CH3 is triggered before the first bass note) */
    w_on = 0;                             /* CH3 is stopped after power-on */
    w_cur = w_want = WV_KEEP;
}

void snd_tick(void)
{
    uint8_t a;

    /* requests */
    if (rq) {
        rq = 0;
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
            goto sfx;                     /* a frame of its own */
        }
    }

    /* pitch effects first: a note's effect values are taken the frame after its row,
       before the parse-ahead below replaces them with the next note's */
    if (c1_fx && !--c1_wake)
        fx_lead();
    if (c0_aon) {                         /* CH1's arpeggio: the next chord note */
        uint16_t x;
        a = c0_ap;
        if (a == 0) {
            c0_ap = 1;
            x = c0_x1;
        } else if (a == 1) {
            c0_ap = 2;
            x = c0_x2;
        } else if (a == 2) {
            c0_ap = 0;
            x = c0_x0;
        } else {                          /* a new note (its root just played): take its chord */
            c0_x0 = c0_px;
            c0_x2 = c0_pb;
            c0_ap = 1;
            x = c0_x1 = c0_pa;
        }
        SND_W(SND_NR13, (uint8_t)x);
        SND_W(SND_NR14, (uint8_t)(x >> 8));
    }

    /* song: a row on each carry of the tempo accumulator; parse ahead in between */
    busy = 0;
    if (live) {
        if (delay) {                      /* a new song: parse its first events, one a */
            if (need) {                   /* frame (not while a wave loads) */
                if (w_want == w_cur)
                    parse_one();
            } else {
                delay = 0;
                acc = (uint8_t)(0u - rate);   /* the first row on the next frame */
            }
        } else {
            a = (uint8_t)(acc + rate);
            acc = a;
            if (a < rate) {               /* (carry) */
                row();
                busy = 2;
            } else if (need) {
                parse_ahead(a);
            }
        }
    }
    if (b_work)
        bass_frame();

sfx:
    /* new sfx: up to two a frame; one on a frame that parsed a channel; none on a row frame
       or one that parsed more (the frame after a row is never a row, and parses make room
       for a waiting sfx, so it waits a frame, rarely two: inaudible, and it keeps the worst
       frames cheap) */
    if (sq_tail != sq_head) {
        if (busy != 2) {
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
