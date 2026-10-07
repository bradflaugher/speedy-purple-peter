#!/usr/bin/env python3
"""render_audio.py - build a tiny sound-test ROM around src/gb/sound.c, run it headless in
PyBoy (real APU + CPU emulation), render the songs and sfx to WAV for listening, and measure
the cost of snd_tick() on the emulated CPU.

    python3 tools/render_audio.py                  # everything -> build/audio/*.wav
    python3 tools/render_audio.py --only main      # names containing 'main'
    python3 tools/render_audio.py --bench          # cycle stats only (no WAVs)

Needs /opt/gbdk (or $LCC) and pyboy + numpy.  WAVs are for QA: never commit them.
"""
import argparse
import os
import subprocess
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "sndtest")
LCC = os.environ.get("LCC", "/opt/gbdk/bin/lcc")

SONGS = ["none", "main", "nova", "title", "death", "gameover", "hurry"]
SFX = ["jump", "jump_big", "stomp", "kick", "bump", "break", "coin", "sprout", "powerup",
       "powerdown", "1up", "shot", "checkpoint", "tick", "pause", "launch", "select"]

MAIN_C = r"""
#include <gb/gb.h>
#include <stdint.h>
#include "sound.h"
/* mailbox at 0xD800:
   [0] song req (0xFF none)  [1] sfx req (0xFF none)  [2] hurry req (0xFF none)
   [3] music_done()  [19] frame counter.
   The cost of snd_tick() is measured from the host: PyBoy hooks on mark_a() / mark_b()
   read the emulated CPU's cycle counter (the timer registers are synced too lazily). */
#define MB ((volatile uint8_t *)0xD800)
volatile uint8_t sink;
void mark_a(void) { sink = 1; }
void mark_b(void) { sink = 2; }
void main(void)
{
    MB[0] = 0xFF; MB[1] = 0xFF; MB[2] = 0xFF;
    snd_init();
    mark_a();                          /* calibration: the empty measurement */
    mark_b();
    while (1) {
        wait_vbl_done();
        if (MB[2] != 0xFF) { music_hurry(MB[2]); MB[2] = 0xFF; }
        if (MB[0] != 0xFF) { music_play(MB[0]); MB[0] = 0xFF; }
        if (MB[1] != 0xFF) { sfx_play(MB[1]); MB[1] = 0xFF; }
        mark_a();
        snd_tick();
        mark_b();
        MB[3] = music_done();
        MB[19]++;
    }
}
"""


def build_rom():
    os.makedirs(BUILD, exist_ok=True)
    main = os.path.join(BUILD, "main.c")
    with open(main, "w") as f:
        f.write(MAIN_C)
    rom = os.path.join(BUILD, "sndtest.gb")
    src = os.path.join(ROOT, "src", "gb")
    obj = os.path.join(BUILD, "sound.o")
    # the game's compiler flags (Makefile CFLAGS_GB), so the cycle counts are the game's
    subprocess.check_call([LCC, "-Wf--max-allocs-per-node5000", "-Wf--opt-code-speed",
                           "-I" + src, "-I" + os.path.join(ROOT, "src", "core"), "-c", "-o",
                           obj, os.path.join(src, "sound.c")])
    subprocess.check_call([LCC, "-I" + src, "-Wl-m", "-Wl-j", "-o", rom, main, obj])
    return rom, obj


def code_size(obj):
    """bytes of code + const data in the sound object (from the .rel/.o area records)"""
    sizes = {}
    with open(obj) as f:
        for line in f:
            if line.startswith("A "):
                p = line.split()
                sizes[p[1]] = int(p[3], 16)
    return sizes


def map_symbol(rom, name):
    """address of a global symbol, from the linker's .noi file (-Wl-j)"""
    with open(rom[:-3] + ".noi") as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[0] == "DEF" and p[1] == name:
                return int(p[2], 16) & 0xFFFF
    raise KeyError(name)


class Rig:
    def __init__(self, rom):
        from pyboy import PyBoy
        self.pb = PyBoy(rom, window="null", sound_emulated=True, sound_sample_rate=48000)
        self.m = self.pb.memory
        self.t_a = 0
        self.last = 0
        self.calib = None
        self.pb.hook_register(0, map_symbol(rom, "_mark_a"), self._a, None)
        self.pb.hook_register(0, map_symbol(rom, "_mark_b"), self._b, None)
        self.samples = []
        self.costs = []
        self.pb.tick(200, False, True)

    def _a(self, ctx):
        self.t_a = self.pb._cycles()

    def _b(self, ctx):
        d = (self.pb._cycles() - self.t_a) // 4          # T-cycles -> M-cycles
        if self.calib is None:
            self.calib = d
        self.last = d - self.calib

    def frame(self, n=1, keep=True):
        import numpy as np
        for _ in range(n):
            self.pb.tick(1, False, True)
            if keep:
                self.samples.append(np.array(self.pb.sound.ndarray, copy=True))
            self.costs.append(self.last)

    def song(self, x):
        self.m[0xD800] = SONGS.index(x)

    def sfx(self, x):
        self.m[0xD801] = SFX.index(x)

    def hurry(self, on):
        self.m[0xD802] = on

    def done(self):
        return self.m[0xD803]

    def take(self):
        import numpy as np
        a = np.concatenate(self.samples) if self.samples else np.zeros((0, 2), dtype=np.int8)
        c = self.costs
        self.samples, self.costs = [], []
        return a, c


def write_wav(path, a, rate=48000):
    import numpy as np
    x = a.astype(np.float64)
    x -= x.mean(axis=0)          # remove DC (the GB mixer idles off-centre)
    pcm = np.clip(x * 256, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def stats(a):
    import numpy as np
    if len(a) == 0:
        return "empty"
    x = a.astype(np.float64)
    x -= x.mean(axis=0)
    return "rms %5.1f  peak %5.1f" % (np.sqrt((x ** 2).mean()), np.abs(x).max())


def cstats(c):
    c = sorted(c)
    n = len(c)
    return "tick avg %4d med %4d p99 %4d max %4d" % (sum(c) // n, c[n // 2], c[n * 99 // 100], c[-1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--bench", action="store_true")
    args = ap.parse_args()
    rom, obj = build_rom()
    out = os.path.join(ROOT, "build", "audio")
    os.makedirs(out, exist_ok=True)
    rig = Rig(rom)
    keep = not args.bench
    allc = []

    def want(name):
        return args.only is None or args.only in name

    def emit(name):
        a, c = rig.take()
        allc.extend(c)
        if keep:
            write_wav(os.path.join(out, name + ".wav"), a)
        print("%-22s %s  %s" % (name, cstats(c), "" if args.bench else stats(a)))

    def silence():
        rig.song("none")
        rig.frame(60, keep=False)
        rig.take()

    for name, secs, hurry in [("title", 80, 0), ("main", 80, 0), ("nova", 25, 0),
                              ("main_hurry", 55, 1), ("nova_hurry", 16, 1)]:
        if not want(name):
            continue
        rig.hurry(hurry)
        rig.frame(1, keep=False)
        rig.song(name.split("_")[0])
        rig.frame(int(60 * secs))
        emit(name)
        rig.hurry(0)
        silence()
    for name in ["death", "gameover", "hurry"]:
        if not want("jingle_" + name):
            continue
        rig.song("main")
        rig.frame(120, keep=False)
        rig.take()
        rig.song(name)
        rig.frame(2)
        n = 0
        while not rig.done() and n < 60 * (8 if name == "hurry" else 6):
            rig.frame(1)
            n += 1
        rig.frame(30)
        emit("jingle_" + name)
        rig.hurry(0)
        silence()
    for i, s in enumerate(SFX):
        name = "sfx_%02d_%s" % (i, s)
        if not want(name):
            continue
        reps = 6 if s in ("coin", "tick") else 1
        for _ in range(reps):
            rig.sfx(s)
            rig.frame(2 if s == "tick" else 8)
        rig.frame(90)
        emit(name)
    if want("gameplay"):
        # the run: jumps, coins, stomps over the main song, then hurry, nova and a death
        import random
        rnd = random.Random(7)
        rig.song("main")
        for f in range(60 * 40):
            if f == 60 * 20:
                rig.song("hurry")
            if f == 60 * 28:
                rig.song("nova")
            if f % 45 == 0:
                rig.sfx("jump")
            elif rnd.random() < 0.03:
                rig.sfx(rnd.choice(["coin", "coin", "stomp", "break", "kick", "shot"]))
            rig.frame(1)
        rig.song("death")
        rig.frame(150)
        emit("gameplay")
        rig.hurry(0)
        silence()
    if allc:
        allc.sort()
        print("ALL: %s  (%.1f%% of a 17556 M-cycle frame at max)" %
              (cstats(allc), allc[-1] * 100.0 / 17556))
    sizes = code_size(obj)
    print("sound.o areas: " + ", ".join("%s %d" % kv for kv in sorted(sizes.items()) if kv[1]))


if __name__ == "__main__":
    main()
