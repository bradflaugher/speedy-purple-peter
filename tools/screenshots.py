#!/usr/bin/env python3
"""Render the README screenshots from the real ROM (PyBoy, headless): docs/screens/*.png and
docs/screens/gameplay.gif. The gameplay is the host search bot's run (build/sppgen path), fed
to the ROM through its test hook, so every shot is a real frame of a real run.

Run: make screenshots
"""
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from test_rom import Game, SEED, SPPGEN  # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'screens')


def bot_keys(sectors):
    out = os.path.join(ROOT, 'build', 'shots_path.txt')
    subprocess.check_output([SPPGEN, 'path', str(SEED), str(sectors), out], text=True)
    with open(out) as f:
        return [int(x, 16) for x in f.read().split()]


def save(img, name, scale=1):
    img = img.convert('RGB')
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(os.path.join(OUT, name))


def run(cgb, keys, grab, gif=None):
    """feed the bot's keys; grab {step: name}; collect gif frames between gif=(a, b)"""
    g = Game(cgb)
    g.tick(150)
    save(g.pb.screen.image, '%s_title.png' % ('cgb' if cgb else 'dmg'))
    g.start_run()
    ring, sa = g.addr('_dbg_ring'), g.addr('_dbg_stop_at')

    def setstop(v):
        g.pb.memory[sa] = v & 255
        g.pb.memory[sa + 1] = v >> 8
    fed = 0
    setstop(0)
    g.pb.memory[g.addr('_dbg_feed')] = 1
    frames = []
    pending = sorted(grab)
    while pending or (gif and g.var('_dbg_steps') < gif[1]):
        s = g.var('_dbg_steps')
        while fed < len(keys) and fed < s + 200:
            g.pb.memory[ring + (fed & 255)] = keys[fed]
            fed += 1
        setstop(min(fed, pending[0] if pending else fed))
        g.tick()
        s = g.var('_dbg_steps')
        if pending and s >= pending[0]:
            save(g.pb.screen.image, grab[pending.pop(0)])
        if gif and gif[0] <= s < gif[1] and s % 2 == 0:
            frames.append(g.pb.screen.image.convert('RGB').resize((320, 288), Image.NEAREST))
    if gif:
        frames[0].save(os.path.join(OUT, 'gameplay.gif'), save_all=True, append_images=frames[1:],
                       duration=33, loop=0, optimize=True)
    # pause
    g.pb.memory[g.addr('_dbg_feed')] = 0
    g.press(0x80)
    g.tick(4)
    save(g.pb.screen.image, '%s_pause.png' % ('cgb' if cgb else 'dmg'))
    g.press(0x80)
    # game over: run out of lives and time
    g.poke('lives', 1)
    g.poke('time', 1)
    for _ in range(800):
        g.tick()
        if g.w('over'):
            break
    g.tick(420)
    save(g.pb.screen.image, '%s_game_over.png' % ('cgb' if cgb else 'dmg'))
    g.stop()


def main():
    os.makedirs(OUT, exist_ok=True)
    keys = bot_keys(3)
    run(True, keys, {140: 'cgb_start.png', 700: 'cgb_run.png',
                     2000: 'cgb_run2.png'}, gif=(300, 660))
    run(False, keys, {140: 'dmg_start.png', 700: 'dmg_run.png', 2000: 'dmg_run2.png'})
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
