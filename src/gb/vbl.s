; vbl.s - SPEEDY PURPLE PETER: every VRAM write made while the screen is on.
;
; The LCD controller ignores writes to VRAM while it is drawing a line (mode 3), so everything
; the game puts on screen during play goes through here, inside VBlank, where VRAM is open for
; ten lines (1140 machine cycles on a Game Boy, 2280 on a Game Boy Color at double speed). The
; main loop stages the work in RAM and publishes it with one byte; this routine writes it out.
;
; It has to fit, always, so it is written for speed and its worst case is counted. GBDK's own
; handler runs first (it copies the sprites to OAM): this routine starts 290 cycles in. Then:
;
;   half a column (13 rows)        13 * 14 + 40 = 222      (Color: twice, for the attributes)
;   the HUD queue (8 tiles)         8 * 12 + 20 = 116
;   the cell queue (4 cells)        4 * 25 + 20 = 120      (Color: twice)
;   or, when none of those is waiting, an animated group (64 bytes): 16 * 22 + 30 = 382
;
; so it ends at most 750 cycles into the 1140 on a Game Boy and 1090 into the 2280 on a Color.
; The stack pointer walks the staged data (pop is the fastest read there is); interrupts are off
; in here and nothing is pushed or called until it is put back.
;
; dbg_vbl_late counts the frames this ended outside VBlank (the tests want 0); dbg_vbl_ly is the
; latest line it ended on.

        .module vbl

        .globl  _is_cgb
        .globl  _col_pending, _col_src, _col_dst
        .globl  _anim_req, _anim_src, _anim_dst
        .globl  _hud_ready, _hud_q
        .globl  _cell_ready, _cell_q
        .globl  _dbg_vbl_late, _dbg_vbl_ly

        VBK  = 0x4F
        STAT = 0x41
        LY   = 0x44
        CELLQ_BYTES = 24                ; CELLQ (4) cells of 6 bytes: keep in step with front.h

        .area   _DATA
vbl_sp:
        .ds     2
vbl_vbk:
        .ds     1

        .area   _HOME

_vbl_vram::
        ldh     a, (VBK)
        ld      (vbl_vbk), a
        xor     a, a
        ldh     (VBK), a
        ld      (vbl_sp), sp

        ; ---- half of the next column: 13 rows of 2 tiles
        ld      hl, #_col_pending
        ld      a, (hl)
        or      a, a
        jr      z, anim
        dec     (hl)
        ld      hl, #_col_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      sp, hl
        ld      hl, #_col_dst
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      bc, #31
        ld      a, #13
col_tiles:
        pop     de
        ld      (hl), e
        inc     l                       ; (a column starts on an even tile: no carry)
        ld      (hl), d
        add     hl, bc
        dec     a
        jr      nz, col_tiles
        ld      (_col_src), sp          ; the second half goes on from here
        ld      a, l
        ld      (_col_dst), a
        ld      a, h
        ld      (_col_dst + 1), a
        ld      a, (_is_cgb)
        or      a, a
        jr      z, hud
        ld      de, #-416               ; back up the 13 rows; the attributes are 52 bytes on
        add     hl, de
        add     sp, #26
        ld      a, #1
        ldh     (VBK), a
        ld      a, #13
col_attrs:
        pop     de
        ld      (hl), e
        inc     l
        ld      (hl), d
        add     hl, bc
        dec     a
        jr      nz, col_attrs
        xor     a, a
        ldh     (VBK), a
        jr      hud

        ; ---- or, on a VBlank with no column: an animated group's 4 tiles (64 bytes)
anim:
        ld      hl, #_anim_req
        ld      a, (hl)
        or      a, a
        jr      z, hud
        ld      a, (_hud_ready)         ; (it is the longest job: it waits for an idle VBlank)
        ld      b, a
        ld      a, (_cell_ready)
        or      a, b
        jr      nz, hud
        ld      (hl), #0
        ld      hl, #_anim_src
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      sp, hl
        ld      hl, #_anim_dst
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      b, #16
anim_copy:
        pop     de
        ld      a, e
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        pop     de
        ld      a, e
        ld      (hl+), a
        ld      a, d
        ld      (hl+), a
        dec     b
        jr      nz, anim_copy

        ; ---- the HUD queue: (address, tile, -) each
hud:
        ld      hl, #_hud_ready
        ld      a, (hl)
        or      a, a
        jr      z, cells
        ld      (hl), #0
        ld      b, a
        ld      sp, #_hud_q
hud_copy:
        pop     hl
        pop     de
        ld      (hl), e
        dec     b
        jr      nz, hud_copy

        ; ---- the cell queue: (address, 4 tiles) each, then the same again for the attributes
cells:
        ld      a, (_cell_ready)
        or      a, a
        jr      z, done
        ld      sp, #_cell_q
        ld      bc, #31
cell_tiles:
        pop     hl
        pop     de
        ld      (hl), e
        inc     l
        ld      (hl), d
        add     hl, bc
        pop     de
        ld      (hl), e
        inc     l
        ld      (hl), d
        dec     a
        jr      nz, cell_tiles
        ld      a, (_is_cgb)
        or      a, a
        jr      z, cells_done
        ld      a, #1
        ldh     (VBK), a
        ld      sp, #_cell_q + CELLQ_BYTES
        ld      a, (_cell_ready)
cell_attrs:
        pop     hl
        pop     de
        ld      (hl), e
        inc     l
        ld      (hl), d
        add     hl, bc
        pop     de
        ld      (hl), e
        inc     l
        ld      (hl), d
        dec     a
        jr      nz, cell_attrs
cells_done:
        xor     a, a
        ld      (_cell_ready), a

done:
        ld      hl, #vbl_sp
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      sp, hl
        ld      a, (vbl_vbk)
        ldh     (VBK), a

        ; ---- did it fit? (the last line of VBlank reads LY = 0 for most of its length)
        ldh     a, (STAT)
        and     a, #3
        dec     a
        jr      z, in_time
        ld      hl, #_dbg_vbl_late
        inc     (hl)
        ret     nz
        dec     (hl)                    ; (stays at 255)
        ret
in_time:
        ldh     a, (LY)
        or      a, a
        jr      nz, stamp
        ld      a, #153
stamp:
        ld      hl, #_dbg_vbl_ly
        cp      a, (hl)
        ret     c
        ld      (hl), a
        ret
