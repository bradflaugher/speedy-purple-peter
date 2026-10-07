/* sound.h - SPEEDY PURPLE PETER sound engine: songs + sound effects (GBDK-2020, host-testable).
 *
 * Every call below only posts a request; all APU register work happens in snd_tick(), which
 * the game calls exactly once per frame from the VBlank handler.  Bank 0, no banking needed.
 *
 *   CH1 pulse  sfx channel; the songs put a quiet harmony / sparkle on it while it is free
 *   CH2 pulse  song lead (a few big sfx borrow it)
 *   CH3 wave   song bass (never used by sfx)
 *   CH4 noise  song drums; sfx borrow it
 *
 * A new sfx takes its channels if nothing more important is playing on them (equal priority
 * replaces, so coins and ticks retrigger every frame), and hands them back to the song when
 * it ends.  Build on the host with -DHOST_TEST: register writes then go to host_snd_write().
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* songs */
enum {
    MUS_NONE,       /* stop the music (notes fade out by themselves) */
    MUS_MAIN,       /* the endless run (loops) */
    MUS_NOVA,       /* invincible (loops) */
    MUS_TITLE,      /* title screen (loops) */
    MUS_DEATH,      /* jingle */
    MUS_GAMEOVER,   /* jingle */
    MUS_HURRY,      /* warning jingle; then MUS_MAIN starts over in hurry tempo by itself */
    NUM_MUS
};

/* sound effects */
enum {
    SFX_JUMP, SFX_JUMP_BIG, SFX_STOMP, SFX_KICK, SFX_BUMP, SFX_BREAK, SFX_COIN,
    SFX_SPROUT, SFX_POWERUP, SFX_POWERDOWN, SFX_1UP, SFX_SHOT, SFX_FLAG, SFX_TICK,
    SFX_PAUSE, SFX_LAUNCH, SFX_SELECT,
    NUM_SFX
};

void snd_init(void);              /* APU on, master volume, silence */
void snd_tick(void);              /* once per frame, from the VBlank handler */
void sfx_play(uint8_t id);        /* SFX_* */
void music_play(uint8_t song);    /* MUS_*; MUS_NONE stops.  Playing the current loop again
                                     restarts it.  MUS_TITLE, MUS_DEATH and MUS_GAMEOVER
                                     clear the hurry tempo; MUS_HURRY sets it. */
void music_hurry(uint8_t on);     /* 1: the "time is running out" tempo (1.5x); applies to the
                                     looping songs (MAIN, NOVA) until music_hurry(0) */
uint8_t music_done(void);         /* 1 when a non-looping song (jingle) has finished, or no
                                     song is playing; 0 while a song (or a request) is pending */

#ifdef HOST_TEST
/* test hooks */
uint8_t snd_debug_owned(void);    /* channels (bit 0 = CH1 .. bit 3 = CH4) held by sfx */
uint8_t snd_debug_song(void);     /* the song playing (MUS_NONE when stopped or done) */
uint8_t snd_debug_hurry(void);
unsigned snd_debug_late(void);   /* rows whose event was not parsed ahead in time (0) */
#endif

#endif
