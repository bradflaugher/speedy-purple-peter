/* sound_data.h - songs, instruments, drums and sfx scripts (SPEEDY PURPLE PETER).
 * Included by sound.c only (and by tests/test_sound.c, which walks the data).
 * All melodies are original: one theme ("Starlight Sprint") in three moods, for running
 * (MAIN), the title (TITLE: slow and wide) and invincibility (NOVA: at a gallop), quoted by
 * the jingles.
 *
 * ---- song streams ----------------------------------------------------------------------
 * One byte stream per channel; a row is one 16th note (the song's rate sets its length).
 *   0x00..0x7F  note (sound_notes.h: C2 = 0) plus the channel's transpose; on CH4 a drum D_*
 *   0x80        end of this channel (jingles: the song is done when every channel ended)
 *   0x81        loop: jump to the song's loop point for this channel
 *   0x82 p      call pattern p (SP_*; one level deep)
 *   0x83        return from a pattern
 *   0x84 t      transpose the following notes by t semitones (signed)
 *   0x85 i..    instrument I_* (3 bytes: see the instruments below)
 *   0x86        rest (the pulses ring out, the bass fades)
 *   0x87        slide: the next note scoops up into place (CH2 lead; CH1 ignores it)
 *   0x88 xy     arpeggio (CH1): the notes play as root, +x, +y semitones, a frame each (0: off)
 *   0x89 t p    transpose by t, then call pattern p (the chord changes of bass and harmony)
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
#define SL       0x87            /* the next note slides up into place (CH2) */
#define ARP(xy)  0x88, (xy)      /* CH1: notes play as root, +x, +y (0x00: off) */
#define TCALL(t, p) 0x89, (uint8_t)(t), (p)   /* transpose by t, then call pattern p */
#define SEND_CH  0x80
#define LOOP     0x81
#define CALL(p)  0x82, (p)
#define RET      0x83
#define TR(t)    0x84, (uint8_t)(t)
#define INS(i)   0x85, i         /* no parentheses: i is a list of bytes */
#define LEN(n)   (uint8_t)(0xC0 + (n) - 1)

#define SB_NOTES 0x80            /* below: a note */
#define SB_REST  0x86
#define SB_SLIDE 0x87
#define SB_ARP   0x88
#define SB_TCALL 0x89
#define SB_END   0x80
#define SB_LOOP  0x81
#define SB_CALL  0x82
#define SB_RET   0x83
#define SB_TR    0x84
#define SB_INS   0x85
#define SB_LEN   0xC0

/* ------------------------------------------------------------------ instruments */
/* INS(I_x) puts the instrument's bytes in the stream (3 bytes):
   pulse (CH1, CH2): NRx1 (duty), NRx2 (envelope, always falling, volume >= 1),
                     vibrato delay in frames (CH2 only; 0 = no vibrato)
   wave (CH3):       peak level, frames at the peak (0 = none), sustain level
                     (levels: 0 = mute, 1 = 25%, 2 = 50%, 3 = 100%) */
/* MAIN */
#define I_LV     0x80, 0xC7, 14  /* verse lead: 50% duty, warm and long, vibrato after ~1/4 s */
#define I_LC     0x40, 0xF7, 10  /* chorus lead: 25% duty, bright, soaring */
#define I_LB     0x40, 0xB2, 0   /* bridge lead: staccato, no vibrato */
#define I_AV     0x80, 0x63, 0   /* verse chords: soft 50% arps */
#define I_AC     0x40, 0xA2, 0   /* chorus chords: bright 25% arps, eighths */
#define I_AB     0x00, 0x82, 0   /* bridge chords: thin 12.5%, choppy */
#define I_ECHO   0x00, 0x66, 0   /* the lead's echo, 3 rows late, quiet */
/* TITLE */
#define I_TL     0x80, 0xD7, 20 /* round lead, slow fade, lazy vibrato */
#define I_TLC    0x40, 0xE7, 16  /* chorus: brighter */
#define I_TPAD   0x80, 0x77, 0   /* shimmering chord pads */
/* NOVA */
#define I_NL     0x40, 0xD5, 8   /* frantic lead */
#define I_NA     0x00, 0x72, 0   /* sparkle arps */
/* jingles */
#define I_JL     0x80, 0xC6, 12
#define I_JH     0x80, 0x75, 0
#define I_HL     0x40, 0xC3, 0
/* bass */
#define I_BASS   3, 4, 2         /* plucky: 100% for 4 frames, then 50% */
#define I_BDRV   3, 2, 2         /* sixteenths: a short peak */
#define I_BSOFT  3, 0, 3         /* steady 100% */
#define I_BLONG  3, 30, 2        /* jingle bass */

/* ------------------------------------------------------------------ drums (CH4) */
/* { NR41 length, NR42 envelope (falling), NR43 noise, NR44 (0x80 trigger | 0x40 length) } */
enum { D_KICK, D_SNARE, D_HAT, D_OHAT, D_THUD, D_CRASH, D_SOFT, D_TOM, NUM_DRUMS };
static const uint8_t snd_drum[NUM_DRUMS][4] = {
    { 0x00, 0xB1, 0x62, 0x80 },   /* KICK  low thump */
    { 0x00, 0xA1, 0x32, 0x80 },   /* SNARE */
    { 0x30, 0x61, 0x00, 0xC0 },   /* HAT   closed: cut by the length counter */
    { 0x00, 0x52, 0x01, 0x80 },   /* OHAT  open */
    { 0x00, 0xC2, 0x74, 0x80 },   /* THUD  jingle accent */
    { 0x00, 0xA4, 0x11, 0x80 },   /* CRASH */
    { 0x38, 0x31, 0x00, 0xC0 },   /* SOFT  title shaker */
    { 0x00, 0xB2, 0x55, 0x80 },   /* TOM   for fills */
};
#define K D_KICK
#define S D_SNARE
#define H D_HAT
#define O D_OHAT
#define X D_CRASH
#define T D_TOM

/* ------------------------------------------------------------------ waves (CH3) */
enum { WV_PUNCH, WV_SOFT, NUM_WAVES };
static const uint8_t snd_wave[NUM_WAVES][16] = {
    { 0xFE, 0xED, 0xDC, 0xCB, 0xBA, 0x99, 0x88, 0x77, 0x66, 0x54, 0x43, 0x32, 0x21, 0x10, 0x37, 0xBF },
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 },
};

/* ================================================================== patterns */
/* ---- MAIN: "Starlight Sprint" - E minor verses, a G major chorus, 150 BPM ----
   intro (2 bars, once) | verse 1 (8) | verse 2 (8, climbs) | chorus (8) | bridge (4, fill) | loop
   verse:  Em  C  G  D | Em  C  Am  B     (i VI III VII | i VI iv V)
   chorus: C   D  G  Em | C  D  G   G     (the relative major: IV V I vi | IV V I I)
   bridge: Am  Em C  B                    (one bar = 16 rows = 1.6 s)
   The hook: a rising minor arpeggio onto a held fifth, answered by a falling 3-3-2 line.
   Patterns are two bars each (fewer calls to parse). */
/* verse bars 1-2 (Em | C): the hook */
static const uint8_t p_v12[] = { LEN(2), B4, E5, G5, LEN(10), SL, B5,
                                 LEN(3), C6, B5, LEN(2), A5, LEN(4), G5, E5, RET };
/* bars 3-4 (G | D): the hook a third higher, an open ending */
static const uint8_t p_v34[] = { LEN(2), D5, G5, B5, LEN(10), SL, D6,
                                 LEN(3), E6, D6, LEN(2), A5, LEN(4), Fs5, A5, RET };
/* bars 5-6 (Em | C): the hook, its answer turns upwards */
static const uint8_t p_v56[] = { LEN(2), B4, E5, G5, LEN(10), SL, B5,
                                 LEN(3), C6, B5, LEN(2), A5, G5, A5, B5, C6, RET };
/* bars 7-8 (Am | B), verse 1: up to the high E, down the dominant seventh */
static const uint8_t p_v78[] = { LEN(8), SL, E6, LEN(2), D6, C6, B5, A5,
                                 LEN(6), B5, LEN(2), A5, LEN(4), Fs5, Ds5, RET };
/* bars 7-8, verse 2: the climb into the chorus (A minor in quarters, a held leading tone) */
static const uint8_t p_v78b[] = { LEN(4), A5, C6, E6, A6,
                                  LEN(6), SL, Fs6, LEN(2), E6, LEN(8), SL, Ds6, RET };
/* chorus bars 1-2 (C | D): long-short-turn, then a step down */
static const uint8_t p_c12[] = { LEN(6), SL, E6, LEN(2), D6, LEN(4), E6, G6,
                                 LEN(6), SL, Fs6, LEN(2), E6, LEN(4), D6, A5, RET };
/* bars 3-4 (G | Em): the climb to the held G, and back */
static const uint8_t p_c34[] = { LEN(4), B5, D6, LEN(8), SL, G6,
                                 LEN(4), Fs6, E6, LEN(8), B5, RET };
/* bars 5-6 (C | D): again, a step higher */
static const uint8_t p_c56[] = { LEN(6), SL, E6, LEN(2), D6, LEN(4), E6, G6,
                                 LEN(6), SL, A6, LEN(2), G6, LEN(4), Fs6, D6, RET };
/* bars 7-8 (G): the summit, held, then a run down into the bridge */
static const uint8_t p_c78[] = { LEN(12), SL, B6, LEN(2), A6, G6,
                                 LEN(2), D6, E6, LEN(4), G6, LEN(2), Fs6, E6, D6, B5, RET };
/* bridge (Am | Em | C | B): staccato, the hook's rhythm in eighths, down to the D# */
static const uint8_t p_br[] = {
    LEN(2), A5, LEN(1), A5, REST, LEN(2), C6, A5, LEN(4), E6, LEN(2), D6, C6,
    LEN(2), B5, LEN(1), B5, REST, LEN(2), G5, B5, LEN(4), E6, LEN(2), D6, B5,
    LEN(2), C6, LEN(1), C6, REST, LEN(2), E6, G6, E6, C6, G5, E5,
    LEN(2), Ds6, B5, Fs5, B5, A5, Fs5, LEN(4), Ds5,
    RET };
/* CH1 chords (written in E: transpose per chord).  Verse: 3+3+2 */
static const uint8_t p_armin[] = { ARP(0x37), LEN(6), E4, E4, LEN(4), E4, RET };
static const uint8_t p_armaj[] = { ARP(0x47), LEN(6), E4, E4, LEN(4), E4, RET };
/* chorus / bridge / nova: eighths (the first chorus bar is 13 rows: the echo before it
   started 3 rows late) */
static const uint8_t p_abmin[] = { ARP(0x37), LEN(2), E4, E4, E4, E4, E4, E4, E4, E4, RET };
static const uint8_t p_abmaj[] = { ARP(0x47), LEN(2), E4, E4, E4, E4, E4, E4, E4, E4, RET };
static const uint8_t p_ab13[]  = { ARP(0x47), LEN(3), E4, LEN(2), E4, E4, E4, E4, E4, RET };
/* bass in E (transpose per chord): octave eighths, driving sixteenths, a syncopated bridge */
static const uint8_t p_b8[]  = { LEN(2), E2, E3, E2, E3, E2, E3, B2, E3, RET };
static const uint8_t p_b16[] = { LEN(1), E2, E2, E3, E2, E2, E3, E2, E3, E2, E2, E3, E2, E2, E3, B2, E3, RET };
static const uint8_t p_bsy[] = { LEN(3), E2, E2, LEN(2), E3, LEN(3), E2, E2, LEN(2), B2, RET };
/* drums */
static const uint8_t p_dv[]  = { LEN(2), K, H, S, H, K, K, S, LEN(1), H, H, RET };       /* verse */
static const uint8_t p_dvx[] = { LEN(2), X, H, S, H, K, K, S, LEN(1), H, H, RET };       /* + crash */
static const uint8_t p_dc[]  = { LEN(2), K, O, S, O, K, K, S, O, RET };                  /* chorus */
static const uint8_t p_dcx[] = { LEN(2), X, O, S, O, K, K, S, O, RET };
static const uint8_t p_df[]  = { LEN(2), K, H, S, H, LEN(1), S, S, S, S, LEN(2), S, S, RET };
static const uint8_t p_df2[] = { LEN(2), K, S, K, S, LEN(1), S, S, S, S, S, S, S, S, RET };
static const uint8_t p_db[]  = { LEN(4), K, LEN(2), H, H, LEN(4), S, LEN(2), H, H, RET };  /* bridge */
static const uint8_t p_dbf[] = { LEN(2), S, T, S, T, LEN(1), S, S, T, T, S, S, T, T, RET }; /* big fill */
static const uint8_t p_di[]  = { LEN(4), K, H, K, LEN(2), S, S, RET };                   /* intro */

/* ---- TITLE: the same theme, slow and wide: chorus first, then the verse ---- */
static const uint8_t p_padmin[] = { ARP(0x37), LEN(8), E4, E4, RET };
static const uint8_t p_padmaj[] = { ARP(0x47), LEN(8), E4, E4, RET };
static const uint8_t p_bw[]  = { LEN(8), E3, B2, RET };
static const uint8_t p_dt[]  = { LEN(4), D_SOFT, D_SOFT, D_SOFT, D_SOFT, RET };
static const uint8_t p_dt2[] = { LEN(4), K, D_SOFT, D_SOFT, O, RET };

/* ---- NOVA: "Supernova" - G major, 180 BPM: the chorus hook at a gallop ----
   G  D  Em  C | G  D  C  D */
static const uint8_t p_nv[] = {
    LEN(3), B5, LEN(1), A5, LEN(4), B5, D6, G6,
    LEN(3), A5, LEN(1), G5, LEN(4), A5, D6, Fs6,
    LEN(3), G6, LEN(1), Fs6, LEN(4), E6, B5, E6,
    LEN(3), E6, LEN(1), D6, LEN(4), C6, LEN(6), SL, G6, LEN(2), E6,
    LEN(3), B5, LEN(1), A5, LEN(4), B5, D6, G6,
    LEN(3), A5, LEN(1), G5, LEN(4), A5, D6, A6,
    LEN(3), G6, LEN(1), E6, LEN(2), C6, E6, G6, E6, G6, A6,
    LEN(8), SL, Fs6, LEN(2), D6, E6, Fs6, A6,
    RET };
static const uint8_t p_dn[]  = { LEN(2), K, H, S, H, K, H, S, H, RET };
static const uint8_t p_dnf[] = { LEN(2), K, H, S, H, LEN(1), S, S, S, S, S, S, LEN(2), X, RET };

/* ---- HURRY: VI - VII - i, rising stabs onto the hook's chord ---- */
static const uint8_t p_hur[] = { LEN(1), C5, E5, G5, LEN(3), C6,
                                 LEN(1), D5, Fs5, A5, LEN(3), D6,
                                 LEN(1), E5, G5, B5, LEN(8), SL, E6, RET };

enum {
    SP_V12, SP_V34, SP_V56, SP_V78, SP_V78B, SP_C12, SP_C34, SP_C56, SP_C78, SP_BR,
    SP_ARMIN, SP_ARMAJ, SP_ABMIN, SP_ABMAJ, SP_AB13, SP_B8, SP_B16, SP_BSY,
    SP_DV, SP_DVX, SP_DC, SP_DCX, SP_DF, SP_DF2, SP_DB, SP_DBF, SP_DI,
    SP_PADMIN, SP_PADMAJ, SP_BW, SP_DT, SP_DT2, SP_NV, SP_DN, SP_DNF, SP_HUR,
    NUM_SP
};
static const uint8_t * const snd_pat[NUM_SP] = {
    p_v12, p_v34, p_v56, p_v78, p_v78b, p_c12, p_c34, p_c56, p_c78, p_br,
    p_armin, p_armaj, p_abmin, p_abmaj, p_ab13, p_b8, p_b16, p_bsy,
    p_dv, p_dvx, p_dc, p_dcx, p_df, p_df2, p_db, p_dbf, p_di,
    p_padmin, p_padmaj, p_bw, p_dt, p_dt2, p_nv, p_dn, p_dnf, p_hur,
};

/* ================================================================== songs (orders) */
/* chords: transposes from E for the bass figures and arps, which are written in E */
#define C_EM 0
#define C_C  (-4)
#define C_D  (-2)
#define C_G  3
#define C_AM 5
#define C_B  7
#define AMIN(c)  TCALL(c, SP_ARMIN)
#define AMAJ(c)  TCALL(c, SP_ARMAJ)
#define ABMAJ(c) TCALL(c, SP_ABMAJ)
#define ABMIN(c) TCALL(c, SP_ABMIN)

#define VERSE1 CALL(SP_V12), CALL(SP_V34), CALL(SP_V56), CALL(SP_V78)
#define VERSE2 CALL(SP_V12), CALL(SP_V34), CALL(SP_V56), CALL(SP_V78B)
#define CHORUS CALL(SP_C12), CALL(SP_C34), CALL(SP_C56), CALL(SP_C78)

/* MAIN: each channel starts with the 2-bar intro, then loops its body */
static const uint8_t s_main_lead_in[] = { INS(I_LV), LEN(32), REST, LOOP };
static const uint8_t s_main_lead[] = {
    INS(I_LV), TR(0), VERSE1, VERSE2,
    INS(I_LC), CHORUS,
    INS(I_LB), CALL(SP_BR),
    LOOP };
static const uint8_t s_main_harm_in[] = { INS(I_AV), AMIN(C_EM), AMIN(C_EM), LOOP };
static const uint8_t s_main_harm[] = {
    INS(I_AV),                                                    /* verse 1: chords */
    AMIN(C_EM), AMAJ(C_C), AMAJ(C_G), AMAJ(C_D), AMIN(C_EM), AMAJ(C_C), AMIN(C_AM), AMAJ(C_B),
    INS(I_ECHO), ARP(0), TR(0), LEN(3), REST, VERSE2,             /* verse 2: the echo */
    INS(I_AC), TCALL(C_C, SP_AB13), ABMAJ(C_D), ABMAJ(C_G), ABMIN(C_EM),   /* chorus: chords */
    ABMAJ(C_C), ABMAJ(C_D), ABMAJ(C_G), ABMAJ(C_G),
    INS(I_AB), ABMIN(C_AM), ABMIN(C_EM), ABMAJ(C_C), ABMAJ(C_B),  /* bridge */
    LOOP };
#define B8(c) TCALL(c, SP_B8)
#define B16(c) TCALL(c, SP_B16)
#define BSY(c) TCALL(c, SP_BSY)
static const uint8_t s_main_bass_in[] = { INS(I_BASS), B8(C_EM), B8(C_EM), LOOP };
static const uint8_t s_main_bass[] = {
    INS(I_BASS),
    B8(C_EM), B8(C_C), B8(C_G), B8(C_D), B8(C_EM), B8(C_C), B8(C_AM), B8(C_B),
    B8(C_EM), B8(C_C), B8(C_G), B8(C_D), B8(C_EM), B8(C_C), B8(C_AM), B8(C_B),
    INS(I_BDRV),
    B16(C_C), B16(C_D), B16(C_G), B16(C_EM), B16(C_C), B16(C_D), B16(C_G), B16(C_G),
    INS(I_BASS),
    BSY(C_AM), BSY(C_EM), BSY(C_C), B8(C_B),
    LOOP };
static const uint8_t s_main_drum_in[] = { CALL(SP_DI), CALL(SP_DF2), LOOP };
static const uint8_t s_main_drum[] = {
    CALL(SP_DVX), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DF),
    CALL(SP_DVX), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DV), CALL(SP_DF2),
    CALL(SP_DCX), CALL(SP_DC), CALL(SP_DC), CALL(SP_DC), CALL(SP_DCX), CALL(SP_DC), CALL(SP_DC), CALL(SP_DF),
    CALL(SP_DB), CALL(SP_DB), CALL(SP_DB), CALL(SP_DBF),
    LOOP };

/* TITLE: the chorus, then verse 1 (its B leads back into the chorus's C), slowly */
#define PMIN(c) TCALL(c, SP_PADMIN)
#define PMAJ(c) TCALL(c, SP_PADMAJ)
static const uint8_t s_title_lead[] = { INS(I_TLC), TR(0), CHORUS, INS(I_TL), VERSE1, LOOP };
static const uint8_t s_title_harm[] = {
    INS(I_TPAD),
    PMAJ(C_C), PMAJ(C_D), PMAJ(C_G), PMIN(C_EM), PMAJ(C_C), PMAJ(C_D), PMAJ(C_G), PMAJ(C_G),
    PMIN(C_EM), PMAJ(C_C), PMAJ(C_G), PMAJ(C_D), PMIN(C_EM), PMAJ(C_C), PMIN(C_AM), PMAJ(C_B),
    LOOP };
#define BW(c) TCALL(c, SP_BW)
static const uint8_t s_title_bass[] = {
    INS(I_BSOFT),
    BW(C_C), BW(C_D), BW(C_G), BW(C_EM), BW(C_C), BW(C_D), BW(C_G), BW(C_G),
    BW(C_EM), BW(C_C), BW(C_G), BW(C_D), BW(C_EM), BW(C_C), BW(C_AM), BW(C_B),
    LOOP };
static const uint8_t s_title_drum[] = {
    CALL(SP_DT2), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT),
    CALL(SP_DT2), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT2), CALL(SP_DT), CALL(SP_DT), CALL(SP_DT),
    LOOP };

/* NOVA */
static const uint8_t s_nova_arp[] = {
    INS(I_NA),
    ABMAJ(C_G), ABMAJ(C_D), ABMIN(C_EM), ABMAJ(C_C), ABMAJ(C_G), ABMAJ(C_D), ABMAJ(C_C), ABMAJ(C_D),
    LOOP };
static const uint8_t s_nova_lead[] = { INS(I_NL), TR(0), CALL(SP_NV), LOOP };
static const uint8_t s_nova_bass[] = {
    INS(I_BDRV),
    B16(C_G), B16(C_D), B16(C_EM), B16(C_C), B16(C_G), B16(C_D), B16(C_C), B16(C_D),
    LOOP };
static const uint8_t s_nova_drum[] = {
    CALL(SP_DN), CALL(SP_DN), CALL(SP_DN), CALL(SP_DNF),
    CALL(SP_DN), CALL(SP_DN), CALL(SP_DN), CALL(SP_DBF),
    LOOP };

/* DEATH: the hook turned upside down, falling onto the D# and home (20 rows, ~2 s) */
static const uint8_t s_death_lead[] = { INS(I_JL), LEN(3), B5, G5, E5, Ds5, LEN(8), E5, SEND_CH };
static const uint8_t s_death_harm[] = { INS(I_JH), LEN(3), E5, B4, G4, Fs4, LEN(8), B4, SEND_CH };
static const uint8_t s_death_bass[] = { INS(I_BLONG), LEN(9), E3, LEN(3), B2, LEN(8), E2, SEND_CH };
static const uint8_t s_death_drum[] = { LEN(12), REST, LEN(1), D_THUD, LEN(7), REST, SEND_CH };

/* GAME OVER: the hook, slowly, then iv - V - i (32 rows, ~3.2 s) */
static const uint8_t s_over_lead[] = { INS(I_JL), LEN(2), B4, E5, G5, LEN(10), SL, B5,
                                       LEN(4), A5, Fs5, LEN(8), SL, E5, SEND_CH };
static const uint8_t s_over_harm[] = { INS(I_JH), LEN(8), G4, LEN(8), B4, LEN(4), C5, Ds5, LEN(8), B4, SEND_CH };
static const uint8_t s_over_bass[] = { INS(I_BLONG), LEN(16), E2, LEN(4), A2, B2, LEN(8), E2, SEND_CH };
static const uint8_t s_over_drum[] = { LEN(24), REST, LEN(1), D_THUD, LEN(7), REST, SEND_CH };

/* HURRY: rising stabs over a snare roll (23 rows at 4 frames, ~1.5 s), into MAIN */
static const uint8_t s_hur_lead[] = { INS(I_HL), TR(0), CALL(SP_HUR), SEND_CH };
static const uint8_t s_hur_harm[] = { INS(I_JH), TR(-12), CALL(SP_HUR), SEND_CH };
static const uint8_t s_hur_bass[] = { INS(I_BASS), LEN(6), C3, D3, LEN(11), E3, SEND_CH };
static const uint8_t s_hur_drum[] = { LEN(1), S, S, S, S, S, S, S, S, S, S, S, S,
                                      LEN(4), S, LEN(7), X, SEND_CH };

#undef K
#undef S
#undef H
#undef O
#undef X
#undef T

/* rate: added to an 8-bit accumulator every frame, a row on each carry (256/rate frames) */
#define SF_HURRY_ON  0x01            /* playing it sets the hurry tempo */
#define SF_HURRY_OFF 0x02            /* playing it clears the hurry tempo */
#define SF_LOOPS     0x04            /* a loop (for the tests) */
#define WV_KEEP      0xFF
/* one row per song: rate, hurry rate, wave, flags, next song,
   streams for CH1 harmony, CH2 lead, CH3 bass, CH4 drums, then their loop points (LOOP jumps
   there: a stream may be an intro that LOOPs into a separate body).  Every body starts by
   setting its instrument (tests/test_sound.c checks that a loop sounds the same each time). */
#define SONG_LIST(X) \
    /* MUS_NONE */ \
    X(0, 0, WV_KEEP, 0, MUS_NONE, 0, 0, 0, 0, 0, 0, 0, 0) \
    /* MUS_MAIN: 150 BPM (5.95 frames a row); hurry 225 BPM (4) */ \
    X(43, 64, WV_PUNCH, SF_LOOPS, MUS_NONE, \
      s_main_harm_in, s_main_lead_in, s_main_bass_in, s_main_drum_in, \
      s_main_harm, s_main_lead, s_main_bass, s_main_drum) \
    /* MUS_NOVA: 180 BPM (5.02); hurry 270 BPM (3.3) */ \
    X(51, 77, WV_PUNCH, SF_LOOPS, MUS_NONE, \
      s_nova_arp, s_nova_lead, s_nova_bass, s_nova_drum, \
      s_nova_arp, s_nova_lead, s_nova_bass, s_nova_drum) \
    /* MUS_TITLE: ~100 BPM (9.14) */ \
    X(28, 28, WV_SOFT, SF_LOOPS | SF_HURRY_OFF, MUS_NONE, \
      s_title_harm, s_title_lead, s_title_bass, s_title_drum, \
      s_title_harm, s_title_lead, s_title_bass, s_title_drum) \
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
