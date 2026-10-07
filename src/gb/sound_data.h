/* sound_data.h - songs, instruments, drums and sfx scripts (SPEEDY PURPLE PETER).
 * Included by sound.c only (and by tests/test_sound.c, which walks the data).
 * All melodies are original.
 *
 * ---- song streams ----------------------------------------------------------------------
 * One byte stream per channel; a row is one 16th note (the song's rate sets its length).
 *   0x00..0x7F  note (sound_notes.h: C2 = 0) plus the channel's transpose; on CH4 a drum D_*
 *   0x80        end of this channel (jingles: the song is done when every channel ended)
 *   0x81        loop: jump to the song's loop point for this channel
 *   0x82 p      call pattern p (SP_*; one level deep)
 *   0x83        return from a pattern
 *   0x84 t      transpose the following notes by t semitones (signed)
 *   0x85 i..    instrument I_* (2 bytes on the pulses, 3 on the wave channel)
 *   0x86        rest (the pulses ring out, the bass fades)
 *   0xC0+n-1    length: the following notes / rests last n rows (1..64)
 * Every channel of a looping song spans the same number of rows (tests/test_sound.c checks it,
 * and that every note is in the song's key and inside the note table).
 *
 * ---- sfx scripts ----------------------------------------------------------------------
 * Register writes for one channel, run once per frame:
 *   reg, value   (reg = 0x10..0x23, the low byte of the I/O address)
 *   0x40+n-1     wait n frames (1..192)
 *   0x00         end: the channel goes back to the song
 * Every note starts with a falling envelope, so it dies away by itself (no DAC off, no
 * zombie-mode writes); a CH1 script stops its sweep with NR10 = 0x08 (keeping the negate
 * bit: clearing it after a falling sweep cuts the channel off with a click).
 */
#ifndef SOUND_DATA_H
#define SOUND_DATA_H

#include <stdint.h>
#include "sound_notes.h"

/* ------------------------------------------------------------------ stream bytes */
#define REST     0x86
#define SEND_CH  0x80
#define LOOP     0x81
#define CALL(p)  0x82, (p)
#define RET      0x83
#define TR(t)    0x84, (uint8_t)(t)
#define INS(i)   0x85, i         /* no parentheses: i is a list of bytes */
#define LEN(n)   (uint8_t)(0xC0 + (n) - 1)

#define SB_NOTES 0x80            /* below: a note */
#define SB_REST  0x86
#define SB_END   0x80
#define SB_LOOP  0x81
#define SB_CALL  0x82
#define SB_RET   0x83
#define SB_TR    0x84
#define SB_INS   0x85
#define SB_LEN   0xC0

/* ------------------------------------------------------------------ instruments */
/* INS(I_x) puts the instrument's bytes in the stream:
   pulse (CH1, CH2): NRx1 (duty), NRx2 (envelope, always falling, volume >= 1)
   wave (CH3):       peak level, frames at the peak (0 = none), sustain level
                     (levels: 0 = mute, 1 = 25%, 2 = 50%, 3 = 100%) */
#define I_LEAD   0x40, 0xB2      /* 25% duty, bright pluck */
#define I_STAB   0x80, 0x61      /* short offbeat chord stab */
#define I_PAD    0x80, 0x66      /* soft long tone */
#define I_ARP    0x00, 0x51      /* 12.5% sparkle, very short */
#define I_NLEAD  0x40, 0xB3      /* invincible lead */
#define I_TLEAD  0x80, 0xA5      /* title lead: round, slow fade */
#define I_ECHO   0x00, 0x34      /* title echo, an octave up, quiet */
#define I_JLEAD  0x80, 0xB4      /* jingle lead */
#define I_JHARM  0x80, 0x74      /* jingle harmony */
#define I_BASS   3, 4, 2         /* plucky: 100% for 4 frames, then 50% */
#define I_BSOFT  2, 0, 2         /* steady 50% */
#define I_BLONG  3, 30, 2        /* jingle bass */

/* ------------------------------------------------------------------ drums (CH4) */
/* { NR41 length, NR42 envelope (falling), NR43 noise, NR44 (0x80 trigger | 0x40 length) } */
enum { D_KICK, D_SNARE, D_HAT, D_OHAT, D_THUD, D_CRASH, D_SOFT, NUM_DRUMS };
static const uint8_t snd_drum[NUM_DRUMS][4] = {
    { 0x00, 0xB1, 0x62, 0x80 },   /* KICK  low thump */
    { 0x00, 0xA1, 0x32, 0x80 },   /* SNARE */
    { 0x30, 0x61, 0x00, 0xC0 },   /* HAT   closed: cut by the length counter */
    { 0x00, 0x52, 0x01, 0x80 },   /* OHAT  open */
    { 0x00, 0xC2, 0x74, 0x80 },   /* THUD  jingle accent */
    { 0x00, 0xA4, 0x11, 0x80 },   /* CRASH */
    { 0x38, 0x31, 0x00, 0xC0 },   /* SOFT  title shaker */
};

/* ------------------------------------------------------------------ waves (CH3) */
enum { WV_PUNCH, WV_SOFT, NUM_WAVES };
static const uint8_t snd_wave[NUM_WAVES][16] = {
    { 0xFE, 0xED, 0xDC, 0xCB, 0xBA, 0x99, 0x88, 0x77, 0x66, 0x54, 0x43, 0x32, 0x21, 0x10, 0x37, 0xBF },
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 },
};

/* ================================================================== patterns */
/* ---- MAIN: "Purple Streak" - D major, 150 BPM, A A' B B' C A(up an octave) = 24 bars ---- */
/* lead, bars 1-3 (D | Bm | G) */
static const uint8_t p_ma1[] = {
    LEN(3), Fs5, E5, LEN(2), D5, LEN(4), A5, LEN(2), Fs5, E5,
    LEN(3), D5, Cs5, LEN(2), B4, LEN(4), Fs5, LEN(2), D5, Cs5,
    LEN(2), B4, D5, G5, Fs5, G5, LEN(4), B5, LEN(2), A5,
    RET };
/* bar 4 (A), first ending */
static const uint8_t p_mae[] = { LEN(3), A5, G5, LEN(2), Fs5, LEN(6), E5, LEN(2), REST, RET };
/* bar 4 (A), second ending: climbs into B */
static const uint8_t p_ma2e[] = { LEN(2), E5, Fs5, A5, B5, LEN(3), Cs6, D6, LEN(2), E6, RET };
/* B, bars 5-6 (G | A) */
static const uint8_t p_mb1[] = {
    LEN(6), D6, LEN(2), B5, LEN(4), A5, G5,
    LEN(6), A5, LEN(2), Cs6, LEN(4), E6, Cs6,
    RET };
/* B, bars 7-8 (F#m | Bm) */
static const uint8_t p_mbe[] = {
    LEN(6), Cs6, LEN(2), A5, LEN(4), Fs5, LEN(2), A5, Cs6,
    LEN(6), B5, LEN(2), Fs5, B5, Cs6, LEN(4), D6,
    RET };
/* B', bars 7-8 (G | A) */
static const uint8_t p_mb2e[] = {
    LEN(4), B5, D6, G6, LEN(2), Fs6, E6,
    LEN(8), E6, LEN(4), Cs6, A5,
    RET };
/* C, the bridge (Bm | G | D | A) */
static const uint8_t p_mc[] = {
    LEN(2), Fs5, LEN(1), Fs5, REST, LEN(2), B5, D6, Cs6, B5, A5, Fs5,
    LEN(2), G5, LEN(1), G5, REST, LEN(2), B5, D6, LEN(4), E6, D6,
    LEN(2), Fs5, A5, D6, A5, LEN(4), Fs6, E6,
    LEN(2), E6, D6, Cs6, A5, B5, Cs6, LEN(4), E6,
    RET };
/* offbeat stabs, one bar each */
static const uint8_t p_hd[]  = { LEN(2), REST, Fs4, REST, A4, REST, Fs4, REST, A4, RET };
static const uint8_t p_hbm[] = { LEN(2), REST, D4, REST, Fs4, REST, D4, REST, Fs4, RET };
static const uint8_t p_hg[]  = { LEN(2), REST, G4, REST, B4, REST, G4, REST, B4, RET };
static const uint8_t p_ha[]  = { LEN(2), REST, A4, REST, Cs5, REST, A4, REST, Cs5, RET };
/* B section pads (two half notes per bar) */
static const uint8_t p_pg[]  = { LEN(8), B4, D5, RET };
static const uint8_t p_pa[]  = { LEN(8), Cs5, E5, RET };
static const uint8_t p_pfm[] = { LEN(8), Cs5, A4, RET };
static const uint8_t p_pbm[] = { LEN(8), D5, B4, RET };
/* bass in D, transposed per chord: bouncing octaves, and a syncopated bridge figure */
static const uint8_t p_bo[] = { LEN(2), D2, D3, D2, D3, D2, D3, A2, D3, RET };
static const uint8_t p_bs[] = { LEN(3), D2, D2, LEN(2), D3, LEN(3), D2, D2, LEN(2), A2, RET };
/* drums */
static const uint8_t p_da[] = { LEN(2), D_KICK, D_HAT, D_SNARE, D_HAT, D_KICK, D_KICK, D_SNARE,
                                LEN(1), D_HAT, D_HAT, RET };
static const uint8_t p_df[] = { LEN(2), D_KICK, D_HAT, D_SNARE, D_HAT, LEN(1), D_SNARE, D_SNARE,
                                LEN(2), D_SNARE, LEN(1), D_SNARE, D_SNARE, LEN(2), D_CRASH, RET };
static const uint8_t p_db[] = { LEN(4), D_KICK, LEN(2), D_HAT, D_HAT, LEN(4), D_SNARE, LEN(2), D_HAT,
                                D_OHAT, RET };
static const uint8_t p_dc[] = { LEN(2), D_KICK, D_OHAT, D_SNARE, D_OHAT, D_KICK, D_OHAT, D_SNARE,
                                D_OHAT, RET };

/* ---- NOVA: "Supernova" - E major, 180 BPM, 8 bars (E | C#m | A | B) x2 ---- */
static const uint8_t p_arpmaj[] = { LEN(1), E4, Gs4, B4, E5, Gs5, E5, B4, Gs4,
                                    E4, Gs4, B4, E5, Gs5, B5, Gs5, E5, RET };
static const uint8_t p_arpmin[] = { LEN(1), E4, G4, B4, E5, G5, E5, B4, G4,
                                    E4, G4, B4, E5, G5, B5, G5, E5, RET };
static const uint8_t p_nv[] = {
    LEN(6), E6, LEN(2), Ds6, LEN(4), E6, B5,
    LEN(6), Cs6, LEN(2), B5, LEN(4), Cs6, Gs5,
    LEN(4), A5, B5, Cs6, E6,
    LEN(8), Ds6, Fs6,
    LEN(6), E6, LEN(2), Fs6, LEN(4), Gs6, E6,
    LEN(6), E6, LEN(2), Ds6, LEN(4), Cs6, Gs5,
    LEN(4), A5, Cs6, E6, A6,
    LEN(4), Gs6, Fs6, Ds6, B5,
    RET };
static const uint8_t p_dn[]  = { LEN(2), D_KICK, D_HAT, D_SNARE, D_HAT, D_KICK, D_HAT, D_SNARE, D_HAT, RET };
static const uint8_t p_dnf[] = { LEN(2), D_KICK, D_HAT, D_SNARE, D_HAT,
                                 LEN(1), D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE,
                                 LEN(2), D_CRASH, RET };

/* ---- TITLE: the main theme, slow and spacey, with an echo an octave up ---- */
static const uint8_t p_bw[] = { LEN(8), D3, A2, RET };
static const uint8_t p_dt[]  = { LEN(4), D_SOFT, D_SOFT, D_SOFT, D_SOFT, RET };
static const uint8_t p_dt2[] = { LEN(4), D_SOFT, D_SOFT, D_SOFT, D_OHAT, RET };

/* ---- HURRY: three rising stabs ---- */
static const uint8_t p_hur[] = { LEN(1), E5, Gs5, B5, LEN(3), E6,
                                 LEN(1), F5, A5, C6, LEN(3), F6,
                                 LEN(1), Fs5, As5, Cs6, LEN(8), Fs6, RET };

enum {
    SP_MA1, SP_MAE, SP_MA2E, SP_MB1, SP_MBE, SP_MB2E, SP_MC,
    SP_HD, SP_HBM, SP_HG, SP_HA, SP_PG, SP_PA, SP_PFM, SP_PBM,
    SP_BO, SP_BS, SP_DA, SP_DF, SP_DB, SP_DC,
    SP_ARPMAJ, SP_ARPMIN, SP_NV, SP_DN, SP_DNF,
    SP_BW, SP_DT, SP_DT2, SP_HUR,
    NUM_SP
};
static const uint8_t * const snd_pat[NUM_SP] = {
    p_ma1, p_mae, p_ma2e, p_mb1, p_mbe, p_mb2e, p_mc,
    p_hd, p_hbm, p_hg, p_ha, p_pg, p_pa, p_pfm, p_pbm,
    p_bo, p_bs, p_da, p_df, p_db, p_dc,
    p_arpmaj, p_arpmin, p_nv, p_dn, p_dnf,
    p_bw, p_dt, p_dt2, p_hur,
};

/* ================================================================== songs (orders) */
/* chord transposes for the D bass figures */
#define B_D   TR(0)
#define B_BM  TR(9)
#define B_G   TR(5)
#define B_A   TR(7)
#define B_FSM TR(4)

/* MAIN */
static const uint8_t s_main_lead[] = {
    INS(I_LEAD), TR(0),
    CALL(SP_MA1), CALL(SP_MAE), CALL(SP_MA1), CALL(SP_MA2E),
    CALL(SP_MB1), CALL(SP_MBE), CALL(SP_MB1), CALL(SP_MB2E),
    CALL(SP_MC),
    TR(12), CALL(SP_MA1), CALL(SP_MAE),
    LOOP };
static const uint8_t s_main_harm[] = {
    INS(I_STAB),
    CALL(SP_HD), CALL(SP_HBM), CALL(SP_HG), CALL(SP_HA),
    CALL(SP_HD), CALL(SP_HBM), CALL(SP_HG), CALL(SP_HA),
    INS(I_PAD),
    CALL(SP_PG), CALL(SP_PA), CALL(SP_PFM), CALL(SP_PBM),
    CALL(SP_PG), CALL(SP_PA), CALL(SP_PG), CALL(SP_PA),
    INS(I_STAB),
    CALL(SP_HBM), CALL(SP_HG), CALL(SP_HD), CALL(SP_HA),
    CALL(SP_HD), CALL(SP_HBM), CALL(SP_HG), CALL(SP_HA),
    LOOP };
static const uint8_t s_main_bass[] = {
    INS(I_BASS),
    B_D, CALL(SP_BO), B_BM, CALL(SP_BO), B_G, CALL(SP_BO), B_A, CALL(SP_BO),
    B_D, CALL(SP_BO), B_BM, CALL(SP_BO), B_G, CALL(SP_BO), B_A, CALL(SP_BO),
    B_G, CALL(SP_BO), B_A, CALL(SP_BO), B_FSM, CALL(SP_BO), B_BM, CALL(SP_BO),
    B_G, CALL(SP_BO), B_A, CALL(SP_BO), B_G, CALL(SP_BO), B_A, CALL(SP_BO),
    B_BM, CALL(SP_BS), B_G, CALL(SP_BS), B_D, CALL(SP_BS), B_A, CALL(SP_BS),
    B_D, CALL(SP_BO), B_BM, CALL(SP_BO), B_G, CALL(SP_BO), B_A, CALL(SP_BO),
    LOOP };
static const uint8_t s_main_drum[] = {
    CALL(SP_DA), CALL(SP_DA), CALL(SP_DA), CALL(SP_DF),
    CALL(SP_DA), CALL(SP_DA), CALL(SP_DA), CALL(SP_DF),
    CALL(SP_DB), CALL(SP_DB), CALL(SP_DB), CALL(SP_DB),
    CALL(SP_DB), CALL(SP_DB), CALL(SP_DB), CALL(SP_DF),
    CALL(SP_DC), CALL(SP_DC), CALL(SP_DC), CALL(SP_DF),
    CALL(SP_DA), CALL(SP_DA), CALL(SP_DA), CALL(SP_DF),
    LOOP };

/* NOVA */
static const uint8_t s_nova_arp[] = {
    INS(I_ARP),
    TR(0), CALL(SP_ARPMAJ), TR(-3), CALL(SP_ARPMIN), TR(5), CALL(SP_ARPMAJ), TR(7), CALL(SP_ARPMAJ),
    TR(12), CALL(SP_ARPMAJ), TR(9), CALL(SP_ARPMIN), TR(17), CALL(SP_ARPMAJ), TR(7), CALL(SP_ARPMAJ),
    LOOP };
static const uint8_t s_nova_lead[] = { INS(I_NLEAD), TR(0), CALL(SP_NV), LOOP };
static const uint8_t s_nova_bass[] = {
    INS(I_BASS),
    TR(2), CALL(SP_BO), TR(-1), CALL(SP_BO), TR(7), CALL(SP_BO), TR(9), CALL(SP_BO),
    TR(2), CALL(SP_BO), TR(-1), CALL(SP_BO), TR(7), CALL(SP_BO), TR(9), CALL(SP_BO),
    LOOP };
static const uint8_t s_nova_drum[] = {
    CALL(SP_DN), CALL(SP_DN), CALL(SP_DN), CALL(SP_DNF),
    CALL(SP_DN), CALL(SP_DN), CALL(SP_DN), CALL(SP_DNF),
    LOOP };

/* TITLE */
#define TITLE_TUNE \
    CALL(SP_MA1), CALL(SP_MAE), CALL(SP_MA1), CALL(SP_MA2E), \
    CALL(SP_MB1), CALL(SP_MBE), CALL(SP_MB1), CALL(SP_MB2E)
static const uint8_t s_title_lead[] = { INS(I_TLEAD), TR(0), TITLE_TUNE, LOOP };
static const uint8_t s_title_echo[] = { INS(I_ECHO), LEN(3), REST, TR(12), TITLE_TUNE, LOOP };
#define TITLE_ECHO_LOOP 5                      /* offset of the loop point (after the rest) */
static const uint8_t s_title_bass[] = {
    INS(I_BSOFT),
    TR(0), CALL(SP_BW), TR(-3), CALL(SP_BW), TR(-7), CALL(SP_BW), TR(-5), CALL(SP_BW),
    TR(0), CALL(SP_BW), TR(-3), CALL(SP_BW), TR(-7), CALL(SP_BW), TR(-5), CALL(SP_BW),
    TR(-7), CALL(SP_BW), TR(-5), CALL(SP_BW), TR(4), CALL(SP_BW), TR(-3), CALL(SP_BW),
    TR(-7), CALL(SP_BW), TR(-5), CALL(SP_BW), TR(-7), CALL(SP_BW), TR(-5), CALL(SP_BW),
    LOOP };
static const uint8_t s_title_drum[] = {
    CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2),
    CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2),
    CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2),
    CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2),
    LOOP };

/* DEATH: a chromatic slump in thirds, ending on a thud (18 rows, ~1.8 s) */
static const uint8_t s_death_lead[] = { INS(I_JLEAD), LEN(3), D5, Cs5, C5, LEN(9), B4, SEND_CH };
static const uint8_t s_death_harm[] = { INS(I_JHARM), LEN(3), B4, As4, A4, LEN(9), Gs4, SEND_CH };
static const uint8_t s_death_bass[] = { INS(I_BLONG), LEN(3), G2, Fs2, F2, LEN(9), E2, SEND_CH };
static const uint8_t s_death_drum[] = { LEN(12), REST, LEN(1), D_THUD, LEN(5), REST, SEND_CH };

/* GAME OVER: D minor, Dm | Bb | A | Dm (32 rows, ~3.2 s) */
static const uint8_t s_over_lead[] = { INS(I_JLEAD), LEN(4), A4, F4, F4, D4, E4, Cs4, LEN(8), D4, SEND_CH };
static const uint8_t s_over_harm[] = { INS(I_JHARM), LEN(8), D4, As3, A3, A3, SEND_CH };
static const uint8_t s_over_bass[] = { INS(I_BLONG), LEN(8), D3, As2, A2, D2, SEND_CH };
static const uint8_t s_over_drum[] = { LEN(24), REST, LEN(1), D_THUD, LEN(7), REST, SEND_CH };

/* HURRY: rising stabs over a snare roll (23 rows at 4 frames, ~1.5 s) */
static const uint8_t s_hur_lead[] = { INS(I_LEAD), TR(0), CALL(SP_HUR), SEND_CH };
static const uint8_t s_hur_harm[] = { INS(I_JHARM), TR(-12), CALL(SP_HUR), SEND_CH };
static const uint8_t s_hur_bass[] = { INS(I_BASS), LEN(6), E3, F3, LEN(11), Fs3, SEND_CH };
static const uint8_t s_hur_drum[] = { LEN(1), D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE,
                                      D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE, D_SNARE,
                                      LEN(4), D_SNARE, LEN(7), D_CRASH, SEND_CH };

/* rate: added to an 8-bit accumulator every frame, a row on each carry (256/rate frames) */
#define SF_HURRY_ON  0x01            /* playing it sets the hurry tempo */
#define SF_HURRY_OFF 0x02            /* playing it clears the hurry tempo */
#define SF_LOOPS     0x04            /* a loop (for the tests) */
#define WV_KEEP      0xFF
/* one row per song: rate, hurry rate, wave, flags, next song,
   streams for CH1 harmony, CH2 lead, CH3 bass, CH4 drums, then their loop points (just after
   the first instrument: 3 bytes on the pulses, 4 on the bass; the instrument then in force
   must be the one the stream starts with - tests/test_sound.c checks it) */
#define SONG_LIST(X) \
    /* MUS_NONE */ \
    X(0, 0, WV_KEEP, 0, MUS_NONE, 0, 0, 0, 0, 0, 0, 0, 0) \
    /* MUS_MAIN: 150 BPM (5.95 frames a row); hurry 225 BPM (4) */ \
    X(43, 64, WV_PUNCH, SF_LOOPS, MUS_NONE, \
      s_main_harm, s_main_lead, s_main_bass, s_main_drum, \
      s_main_harm + 3, s_main_lead + 3, s_main_bass + 4, s_main_drum) \
    /* MUS_NOVA: 180 BPM (5.02); hurry 270 BPM (3.3) */ \
    X(51, 77, WV_PUNCH, SF_LOOPS, MUS_NONE, \
      s_nova_arp, s_nova_lead, s_nova_bass, s_nova_drum, \
      s_nova_arp + 3, s_nova_lead + 3, s_nova_bass + 4, s_nova_drum) \
    /* MUS_TITLE: ~98 BPM (9.14) */ \
    X(28, 28, WV_SOFT, SF_LOOPS | SF_HURRY_OFF, MUS_NONE, \
      s_title_echo, s_title_lead, s_title_bass, s_title_drum, \
      s_title_echo + TITLE_ECHO_LOOP, s_title_lead + 3, s_title_bass + 4, s_title_drum) \
    /* MUS_DEATH */ \
    X(43, 43, WV_PUNCH, SF_HURRY_OFF, MUS_NONE, \
      s_death_harm, s_death_lead, s_death_bass, s_death_drum, 0, 0, 0, 0) \
    /* MUS_GAMEOVER */ \
    X(43, 43, WV_PUNCH, SF_HURRY_OFF, MUS_NONE, \
      s_over_harm, s_over_lead, s_over_bass, s_over_drum, 0, 0, 0, 0) \
    /* MUS_HURRY: then MAIN in hurry tempo */ \
    X(64, 64, WV_PUNCH, SF_HURRY_ON, MUS_MAIN, \
      s_hur_harm, s_hur_lead, s_hur_bass, s_hur_drum, 0, 0, 0, 0)

/* separate arrays: much cheaper to index on the SM83 than an array of structs */
#define SG_RATE(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) r,
#define SG_RATEH(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) rh,
#define SG_WAVE(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) w,
#define SG_FLAGS(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) f,
#define SG_NEXT(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) n,
#define SG_CH(r, rh, w, f, n, a, b, c, d, la, lb, lc, ld) { a, b, c, d, la, lb, lc, ld },
static const uint8_t snd_song_rate[NUM_MUS] = { SONG_LIST(SG_RATE) };
static const uint8_t snd_song_rate_h[NUM_MUS] = { SONG_LIST(SG_RATEH) };
static const uint8_t snd_song_wave[NUM_MUS] = { SONG_LIST(SG_WAVE) };
static const uint8_t snd_song_flags[NUM_MUS] = { SONG_LIST(SG_FLAGS) };
static const uint8_t snd_song_next[NUM_MUS] = { SONG_LIST(SG_NEXT) };
static const uint8_t * const snd_song_ch[NUM_MUS][8] = { SONG_LIST(SG_CH) };

/* ================================================================== sfx scripts */
#define W(n)    (uint8_t)(0x40 + (n) - 1)
#define SEND    0x00
#define SWP(v)  0x10, (v)
#define SWP_OFF 0x10, 0x08
#define SQ1(sw, duty, env, x) 0x10, (sw), 0x11, (duty), 0x12, (env), 0x13, (uint8_t)((x) & 0xFF), 0x14, (uint8_t)(0x80 | ((x) >> 8))
#define N1(env, x)            0x12, (env), 0x13, (uint8_t)((x) & 0xFF), 0x14, (uint8_t)(0x80 | ((x) >> 8))
#define DUTY1(d)              0x11, (d)
#define SQ2(duty, env, x)     0x16, (duty), 0x17, (env), 0x18, (uint8_t)((x) & 0xFF), 0x19, (uint8_t)(0x80 | ((x) >> 8))
#define N2(env, x)            0x17, (env), 0x18, (uint8_t)((x) & 0xFF), 0x19, (uint8_t)(0x80 | ((x) >> 8))
#define NZ(env, poly)         0x20, 0x00, 0x21, (env), 0x22, (poly), 0x23, 0x80
#define NZP(poly)             0x22, (poly)
/* NR10 sweep: pace << 4 | 0x08 (falling) | shift */

static const uint8_t x_jump[]    = { SQ1(0x26, 0x40, 0xE1, 1650), W(8), SWP_OFF, W(3), SEND };
static const uint8_t x_jumpbig[] = { SQ1(0x36, 0x80, 0xF1, 1550), W(11), SWP_OFF, W(2), SEND };
static const uint8_t x_stomp1[]  = { SQ1(0x1B, 0x80, 0xD1, 1900), W(8), SWP_OFF, W(4), SEND };
static const uint8_t x_stomp4[]  = { NZ(0xA1, 0x44), W(4), SEND };
static const uint8_t x_kick1[]   = { SQ1(0x08, 0x40, 0xA1, X_A5), W(2), N1(0x81, X_E6), W(6), SEND };
static const uint8_t x_kick4[]   = { NZ(0xC1, 0x21), W(3), SEND };
static const uint8_t x_bump1[]   = { SQ1(0x2F, 0x80, 0xC1, 1100), W(6), SWP_OFF, W(4), SEND };
static const uint8_t x_bump4[]   = { NZ(0xB2, 0x74), W(10), SEND };
static const uint8_t x_break4[]  = { NZ(0xF2, 0x55), W(3), NZ(0xD2, 0x63), W(3), NZ(0xB2, 0x56), W(4),
                                     NZ(0x92, 0x64), W(4), NZ(0x72, 0x66), W(4), NZ(0x52, 0x67), W(8),
                                     SEND };
static const uint8_t x_coin[]    = { SQ1(0x08, 0x80, 0xD1, X_D6), W(4), N1(0xE3, X_A6), W(24), SEND };
static const uint8_t x_sprout[]  = { SQ1(0x75, 0x80, 0xA4, 1400), W(5), DUTY1(0x40), W(5), DUTY1(0x80),
                                     W(5), DUTY1(0x40), W(5), DUTY1(0x80), W(5), DUTY1(0x40), W(5),
                                     SWP_OFF, SEND };
static const uint8_t x_pwup1[]   = { SQ1(0x08, 0x80, 0xB1, X_D5), W(3), N1(0xB1, X_Fs5), W(3),
                                     N1(0xB1, X_A5), W(3), N1(0xB1, X_D6), W(3),
                                     N1(0xB1, X_E5), W(3), N1(0xB1, X_Gs5), W(3),
                                     N1(0xB1, X_B5), W(3), N1(0xB1, X_E6), W(3),
                                     N1(0xB1, X_Fs5), W(3), N1(0xB1, X_As5), W(3),
                                     N1(0xB1, X_Cs6), W(3), N1(0xC4, X_Fs6), W(24), SEND };
static const uint8_t x_pwup2[]   = { SQ2(0x40, 0x71, X_D4), W(6), N2(0x71, X_A4), W(6),
                                     N2(0x71, X_E4), W(6), N2(0x71, X_B4), W(6),
                                     N2(0x71, X_Fs4), W(6), N2(0x74, X_Cs5), W(24), SEND };
static const uint8_t x_pwdown[]  = { SQ1(0x1F, 0x80, 0xB2, X_D6), W(5), N1(0xB2, X_A5), W(5),
                                     N1(0xA2, X_Fs5), W(5), N1(0xA2, X_D5), W(5), N1(0x92, X_A4), W(5),
                                     N1(0x82, X_Fs4), W(5), N1(0x84, X_D4), W(20), SWP_OFF, SEND };
static const uint8_t x_1up1[]    = { SQ1(0x08, 0x80, 0xC2, X_G5), W(5), N1(0xC2, X_C6), W(5),
                                     N1(0xC2, X_E6), W(5), N1(0xC2, X_D6), W(5), N1(0xC4, X_G6), W(30),
                                     SEND };
static const uint8_t x_1up2[]    = { SQ2(0x40, 0x82, X_E5), W(5), N2(0x82, X_G5), W(5),
                                     N2(0x82, X_C6), W(5), N2(0x82, X_B5), W(5), N2(0x84, X_D6), W(30),
                                     SEND };
static const uint8_t x_shot[]    = { 0x20, 0x00, 0x21, 0xD1, 0x22, 0x09, 0x23, 0x80, W(1), NZP(0x1A), W(1),
                                     NZP(0x2B), W(1), NZP(0x3C), W(1), NZP(0x4D), W(1), NZP(0x5E), W(2),
                                     SEND };
static const uint8_t x_flag[]    = { SQ1(0x4F, 0x80, 0xD5, 1985), W(52), SWP_OFF, W(4), SEND };
static const uint8_t x_tick[]    = { SQ1(0x08, 0x40, 0x51, 1985), W(2), SEND };
static const uint8_t x_pause[]   = { SQ1(0x08, 0x80, 0xC1, X_A6), W(4), N1(0xA1, X_E6), W(4),
                                     N1(0xC3, X_A6), W(20), SEND };
static const uint8_t x_launch1[] = { SQ1(0x2B, 0x80, 0xF2, 1200), W(10), SWP_OFF, W(10), SEND };
static const uint8_t x_launch4[] = { NZ(0xF4, 0x71), W(6), NZP(0x72), W(10), NZP(0x73), W(30), SEND };
static const uint8_t x_select[]  = { SQ1(0x08, 0x80, 0xC1, X_E6), W(3), N1(0xC1, X_B6), W(8), SEND };

/* priority, channel mask (1 = CH1, 2 = CH2, 8 = CH4), script on CH1, CH2, CH4.  A higher
   priority is more important; equal replaces (so coins and ticks retrigger). */
#define SFX_LIST(X) \
    /* JUMP      */ X(3, 1, x_jump, 0, 0) \
    /* JUMP_BIG  */ X(3, 1, x_jumpbig, 0, 0) \
    /* STOMP     */ X(4, 9, x_stomp1, 0, x_stomp4) \
    /* KICK      */ X(4, 9, x_kick1, 0, x_kick4) \
    /* BUMP      */ X(4, 9, x_bump1, 0, x_bump4) \
    /* BREAK     */ X(4, 8, 0, 0, x_break4) \
    /* COIN      */ X(3, 1, x_coin, 0, 0) \
    /* SPROUT    */ X(5, 1, x_sprout, 0, 0) \
    /* POWERUP   */ X(6, 3, x_pwup1, x_pwup2, 0) \
    /* POWERDOWN */ X(6, 1, x_pwdown, 0, 0) \
    /* 1UP       */ X(6, 3, x_1up1, x_1up2, 0) \
    /* SHOT      */ X(3, 8, 0, 0, x_shot) \
    /* FLAG      */ X(6, 1, x_flag, 0, 0) \
    /* TICK      */ X(1, 1, x_tick, 0, 0) \
    /* PAUSE     */ X(7, 1, x_pause, 0, 0) \
    /* LAUNCH    */ X(5, 9, x_launch1, 0, x_launch4) \
    /* SELECT    */ X(7, 1, x_select, 0, 0)

#define SX_PRIO(p, m, a, b, c) p,
#define SX_MASK(p, m, a, b, c) m,
#define SX_S0(p, m, a, b, c) a,
#define SX_S1(p, m, a, b, c) b,
#define SX_S2(p, m, a, b, c) c,
static const uint8_t snd_sfx_prio[NUM_SFX] = { SFX_LIST(SX_PRIO) };
static const uint8_t snd_sfx_mask[NUM_SFX] = { SFX_LIST(SX_MASK) };
static const uint8_t * const snd_sfx_s0[NUM_SFX] = { SFX_LIST(SX_S0) };
static const uint8_t * const snd_sfx_s1[NUM_SFX] = { SFX_LIST(SX_S1) };
static const uint8_t * const snd_sfx_s2[NUM_SFX] = { SFX_LIST(SX_S2) };

#endif
