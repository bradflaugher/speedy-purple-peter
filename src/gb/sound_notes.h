/* sound_notes.h - note names and pulse frequency registers (SPEEDY PURPLE PETER sound).
 * Included by sound.c / sound_data.h only.
 *
 * Note numbers: C2 = 0 .. B7 = 71 (MIDI 36..107), as the pitch you hear.  The pulse channels
 * play snd_freq[n]; the wave channel (an octave lower for the same register) plays
 * snd_freq[n + 12], so bass notes are written at their real pitch too (at most B6).
 * X_<note> is the 11-bit pulse register value, for the sfx scripts.
 * Register = round(2048 - 131072 / Hz), A4 = 440 Hz.
 */
#ifndef SOUND_NOTES_H
#define SOUND_NOTES_H

#include <stdint.h>

#define SND_NOTES 72
enum {
    C2, Cs2, D2, Ds2, E2, F2, Fs2, G2, Gs2, A2, As2, B2,
    C3, Cs3, D3, Ds3, E3, F3, Fs3, G3, Gs3, A3, As3, B3,
    C4, Cs4, D4, Ds4, E4, F4, Fs4, G4, Gs4, A4, As4, B4,
    C5, Cs5, D5, Ds5, E5, F5, Fs5, G5, Gs5, A5, As5, B5,
    C6, Cs6, D6, Ds6, E6, F6, Fs6, G6, Gs6, A6, As6, B6,
    C7, Cs7, D7, Ds7, E7, F7, Fs7, G7, Gs7, A7, As7, B7,
};

#define X_C2    44
#define X_Cs2  157
#define X_D2   263
#define X_Ds2  363
#define X_E2   457
#define X_F2   547
#define X_Fs2  631
#define X_G2   711
#define X_Gs2  786
#define X_A2   856
#define X_As2  923
#define X_B2   986
#define X_C3  1046
#define X_Cs3 1102
#define X_D3  1155
#define X_Ds3 1205
#define X_E3  1253
#define X_F3  1297
#define X_Fs3 1339
#define X_G3  1379
#define X_Gs3 1417
#define X_A3  1452
#define X_As3 1486
#define X_B3  1517
#define X_C4  1547
#define X_Cs4 1575
#define X_D4  1602
#define X_Ds4 1627
#define X_E4  1650
#define X_F4  1673
#define X_Fs4 1694
#define X_G4  1714
#define X_Gs4 1732
#define X_A4  1750
#define X_As4 1767
#define X_B4  1783
#define X_C5  1798
#define X_Cs5 1812
#define X_D5  1825
#define X_Ds5 1837
#define X_E5  1849
#define X_F5  1860
#define X_Fs5 1871
#define X_G5  1881
#define X_Gs5 1890
#define X_A5  1899
#define X_As5 1907
#define X_B5  1915
#define X_C6  1923
#define X_Cs6 1930
#define X_D6  1936
#define X_Ds6 1943
#define X_E6  1949
#define X_F6  1954
#define X_Fs6 1959
#define X_G6  1964
#define X_Gs6 1969
#define X_A6  1974
#define X_As6 1978
#define X_B6  1982
#define X_C7  1985
#define X_Cs7 1989
#define X_D7  1992
#define X_Ds7 1995
#define X_E7  1998
#define X_F7  2001
#define X_Fs7 2004
#define X_G7  2006
#define X_Gs7 2009
#define X_A7  2011
#define X_As7 2013
#define X_B7  2015

#ifdef SND_FREQ_TABLE
static const uint16_t snd_freq[SND_NOTES] = {
      44,  157,  263,  363,  457,  547,  631,  711,  786,  856,  923,  986,
    1046, 1102, 1155, 1205, 1253, 1297, 1339, 1379, 1417, 1452, 1486, 1517,
    1547, 1575, 1602, 1627, 1650, 1673, 1694, 1714, 1732, 1750, 1767, 1783,
    1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899, 1907, 1915,
    1923, 1930, 1936, 1943, 1949, 1954, 1959, 1964, 1969, 1974, 1978, 1982,
    1985, 1989, 1992, 1995, 1998, 2001, 2004, 2006, 2009, 2011, 2013, 2015,
};
#endif

#endif
