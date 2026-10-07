# Design notes

## 1. Physics (src/core/sim.c)

Peter's speeds are kept in 1/4096 px per frame, the resolution of the 1985 original, so its
constants are used as they are (the original writes `0x01900` for 1.5625 px/frame; here it is
`0x1900`):

| constant | value | px/frame |
| --- | --- | --- |
| minimum walking speed | `0x0130` | 0.074 |
| walking acceleration | `0x0098` | 0.037 per frame |
| running acceleration | `0x00E4` | 0.056 per frame |
| release deceleration | `0x00D0` | 0.051 per frame |
| skid deceleration | `0x01A0` | 0.102 per frame |
| skid turnaround below | `0x0900` | 0.563 |
| max walking speed | `0x1900` | 1.56 |
| max running speed | `0x2900` | 2.56 |
| run memory after B is released | 10 frames | |
| max fall speed | `0x4800` (then reset to `0x4000`) | 4.5 |

Jumps take their strength from the speed at takeoff:

| takeoff speed | initial speed | gravity while A is held and rising | gravity otherwise |
| --- | --- | --- | --- |
| < `0x1000` | `-0x4000` | `0x0200` | `0x0700` |
| < `0x2500` | `-0x4000` | `0x01E0` | `0x0600` |
| ≥ `0x2500` | `-0x5000` | `0x0280` | `0x0900` |

No gravity is applied on the takeoff frame, so a standing jump with A held rises 66 px: four blocks
with two pixels to spare. In the air Peter's facing is locked; pressing forward accelerates (run
rate above walking speed), pressing back brakes (strongly if the takeoff was fast), and the air
speed limit is set by the takeoff speed. Stomping sets the vertical speed to `-0x4000` and keeps
the jump's gravities, so holding A on a stomp bounces higher.

Collision: two foot points (x+3, x+12), a single head point in the middle (x+8), side points at
x+2 / x+13. The single head point gives the classic feel: you can jump past a block's corner, and
you only bump the block above the middle of your head.

## 2. The level (src/core/level.c)

A sector is a pure function of `(seed, sector)`: a start stretch (16 flat columns and a power-up
pod in sector 0, 6 later), 12 + min(sector, 8) body segments, then the checkpoint: one column of
plain ground that nothing marks (the run never visibly ends). Segments: flat ground (maybe with
enemies), pits, capsule rows (`B?B?B`, spaced pods, bricks hiding things, star-bit rows), a high
brick row reached from a low one, tubes (with a Chomper later on), stair pyramids (with a gap
later on), floating platforms over wide pits, star bit arcs and comet launchers. Every hazard is
followed by at least two flat columns.

Rules that keep every jump makeable (enforced in `tests/test_core.c` over 200 seeds):

- pits: at most 2 wide in sector 0, 3 until sector 4, 4 until sector 8, then 5 (a running jump
  covers 6+);
- never a one-column island between two pits;
- floating platforms leave three clear columns on each side, so you land on them from above
  instead of bumping their underside;
- a 4-high tube needs a run-up of at least three columns (a standing jump only just clears it).

The search bot then plays the real simulation to prove it: 12 sectors of terrain on 40 seeds, 8
sectors with enemies on 12 seeds.

## 3. The Game Boy front end (src/gb/)

- **Screen.** The HUD is the window over lines 0-15. A STAT interrupt at LY=15 turns the window
  off and the sprites on; VBlank turns the window back on and the sprites off, so no sprite ever
  covers the HUD.
- **The BG ring.** 16 metatile columns (32 tiles). A new column is built in the frame loop (never
  on a frame the generator ran) and written by the VBlank handler, half a column per VBlank. The
  HUD's text and the animated tiles (the pods' pulse, the star bits' twinkle) are written in
  VBlank too, from small queues.
- **Banks.** Bank 0: boot, interrupts, the frame loop, sound. The simulation, entities, generator,
  renderer, screens and art each sit in switchable banks (autobanked); the art tables the
  renderer needs every frame are copied to RAM at load.
- **Save.** MBC5 SRAM keeps the best score and distance per mode, the last seed and the last
  mode, with a magic, a version and a checksum (a version 1 save's best becomes the classic one).

## 4. Performance

The goal is no slow frames at all. SDCC's code for 16-bit, struct-heavy C is several times slower
than hand-written SM83, and the DMG has about 17,500 cycles (154 lines) a frame, so the busy paths
were measured (LY stamps and cycle-counting PyBoy hooks on a `-DSPP_PROFILE -debug` build) and
rewritten one by one:

- the tile lookup the physics uses most is a few lines of assembly (`cell_px` in sim_int.h), and
  the level ring is the first field of the world so it sits at `_W`;
- so is the sprites' on-screen test and OAM position (`at`), the two-object writer (`put2a`) and
  the sliding shell's "who is near me" scan (`near_x`);
- the entity being updated is copied into a plain global (`E`), which SDCC reaches directly
  instead of through a pointer; walkers strolling between cell boundaries and Moon Chompers are
  updated in place without the copy; a walker only probes the level when it enters a new cell;
- the score is awarded in fifties and kept as decimal digits for the HUD (no 32-bit compare or
  division per award); effects loops stop after the last live effect;
- loops walk pointers instead of indexing arrays of structs, and the hottest functions keep
  their locals in static RAM (`SST`), which SDCC addresses directly;
- level generation keeps 16 to 22 columns of slack and waits for a frame with at most one enemy
  about; starting a new segment gets a frame of its own;
- the BG map is a 16-column ring drawn up to 5 columns ahead, so the next column waits for a
  quiet frame; it is written by the VBlank handler over two VBlanks;
- the HUD updates one field per frame, and waits (up to three frames) when a frame runs long;
- only the OAM entries left over from the previous frame are cleared.

`test_rom.py`'s frame budget test plays 8 sectors on 8 seeds (66,000 frames) on both machines,
each run identical to the host's. On a Game Boy Color (double speed): no slow frames. On the
original DMG: 2 slow frames in 66,336 (0.003%; the test allows one in 20,000), where a block bump,
a crowd and the time bonus all land on the same frame. The simulation is frame-locked, so a slow
frame never changes the outcome: the run timer counts game frames, and a seed plays identically
on every machine.

## 5. A compiler bug, and how it was caught

`tests/test_rom.py` feeds the host bot's winning inputs to the ROM and compares the end state.
The first run diverged at step 515: on the Game Boy the sector checkpoint (then a beacon pole)
fired 16 columns early. The cause
was SDCC (GBDK 4.3's 4.3.x and also 4.5.1) compiling `if ((int16_t)(px + 12 - pole) < 0)` into a
sign test of the result's **low** byte. The line is now an unsigned comparison
(`(uint16_t)(...) >= 0x8000`), with a comment, and the end-to-end test stays as the guard.

## 6. Sound

A 4-channel engine (`src/gb/sound.c`): CH2 lead, CH3 bass, CH4 drums, CH1 harmony when no effect
needs it. All melodies are original. It never switches a DAC off, never rewrites NR51 after boot,
only writes wave RAM with CH3 stopped, and moves volumes a step at a time, so it is click-free on
real hardware. `tests/test_sound.c` checks every song and effect against a fake APU.

The run's theme, "Starlight Sprint", is an E-minor chase at 150 BPM (a 45 s loop) with pitch
slides, delayed vibrato and arpeggiated chords, in the bright, bouncy spirit of the classic 8-bit
moon-stage themes; the title, invincibility, hurry, death and game-over tunes share its motifs.
The engine averages about 300 cycles a frame and runs from the VBlank handler.
