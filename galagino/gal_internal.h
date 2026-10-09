/*
 * Galagino core internals: the machine state shared by the games, the game
 * arena, and the band renderer's drawing helpers.
 *
 * Galagino keeps its state in globals (one machine runs at a time); this keeps
 * the same shape in a single struct, `gal`.
 */
#ifndef GAL_INTERNAL_H
#define GAL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gal_port.h"
#include "galagino.h"
#include "z80.h"

/* The upright picture every game draws. */
#define GAL_W 224
#define GAL_H 288

/* Galagino's RAM buffer, remapped per game; 1942 needs the extra bytes (its
 * 128 bytes of sprite RAM at 2400 are mapped as a whole 256-byte page). */
#define GAL_RAMSIZE (8192 + 1024 + 256)

/* Converted game data lives in a fixed arena in .bss, so the cost is visible at
 * link time and nothing fragments the heap. Sized for the largest game that is
 * kept in SRAM (Dig Dug); see gal_arena_peak(). */
#ifndef GAL_ARENA_SIZE
#define GAL_ARENA_SIZE (100 * 1024)
#endif

typedef struct
{
    uint8_t code, color, flags;
    int16_t x, y;
} gal_sprite_t;

#define GAL_MAX_SPRITES 128

/* Tables a game keeps in the flash store besides its ROM regions. */
#define GAL_MAX_DERIVED 2
#define GAL_MAX_PARTS (GAL_MAX_REGIONS + GAL_MAX_DERIVED)

/* A rectangle of the upright picture being drawn. Pixel (x, y) of the picture
 * is buf[(y - y0) * w + (x - x0)]; pixels are already in the canvas format. */
typedef struct
{
    uint16_t *buf;
    int x0, y0, w, h;
} gal_band_t;

typedef struct
{
    /* Converts the loaded ROM regions into the game's data (arena) and sets up
     * the memory map. Returns 0 when out of memory. */
    int (*init)(uint8_t *const *regions);
    /* Power-on reset. */
    void (*reset)(void);
    /* A quarter (q = 0..3) of a 60 Hz frame of the CPUs. The core renders the
     * sound after each quarter, so register changes within a frame are heard;
     * the frame's interrupts belong at the end of quarter 3. */
    void (*run_slice)(int q);
    /* Builds the sprite list for the following render_band calls. */
    void (*prepare_frame)(void);
    /* Clears the band to the background and draws the picture into it. */
    void (*render_band)(const gal_band_t *b);
    /* Renders n samples of the game's sound (n is about a quarter frame). */
    void (*sound)(int16_t *out, int n);
    /* The rows of the upright 224x288 picture the game uses: all of them on
     * the Namco boards; 16..271 on the Galaxian-class boards with a 256-line
     * raster, which then lose only one row in 16 when upright. */
    int16_t view_top, view_h;
    /* The game's data does not fit in SRAM: it only runs with a PSRAM block,
     * or from the flash store. */
    uint8_t needs_psram;
    /* Tables built from the ROMs that the flash store keeps as well (1942's
     * playfield tiles and sprites): their number, the region each is built
     * from, their sizes (multiples of 4 KB), and building bytes ofs..ofs+len
     * (len a multiple of 4 KB) of table i from that region. init() finds them
     * at regions[GAL_MAX_REGIONS + i] when the game starts from flash, NULL
     * otherwise. */
    uint8_t nderived;
    uint8_t derived_src[GAL_MAX_DERIVED];
    uint32_t derived_size[GAL_MAX_DERIVED];
    void (*derive)(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len);
} gal_game_ops_t;

typedef struct
{
    int game; /* gal_game_id_t, or -1 */
    const gal_game_ops_t *ops;
    gal_fmt_t fmt;
    int samplerate;

    uint16_t buttons; /* GAL_BTN_* of the current frame */
    uint8_t game_started;

    z80_t cpu[3];
    uint8_t ram[GAL_RAMSIZE];
    uint8_t soundregs[32];

    gal_sprite_t sprite[GAL_MAX_SPRITES];
    int active_sprites;
} gal_t;

extern gal_t gal;

/* --- Arenas ---------------------------------------------------------------
 * Two-ended: the game's data from the bottom, the raw ROM regions that are
 * only needed while converting from the top. A game is given the SRAM arena,
 * or the PSRAM block the board provides (gal_set_psram) when it needs_psram;
 * gal_fast_alloc takes from the SRAM arena either way, for data such a game
 * reads all the time. */
void *gal_alloc(uint32_t size);      /* NULL when full */
void *gal_fast_alloc(uint32_t size); /* NULL when full */
void *gal_temp_alloc(uint32_t size); /* NULL when full */
void gal_temp_release(void);
void gal_arena_reset(void);

/* --- The flash store (gal_flash.c) ----------------------------------------- */

/* The game's parts (regions, then derived tables) in the store, or 0. */
int gal_flash_parts(int game, const uint8_t **parts);

/* The ops of a game, NULL when it is not part of this build. */
const gal_game_ops_t *gal_ops(int game);

/* --- Colours --------------------------------------------------------------- */

/* Galagino's colour PROM decodes produce RGB565; this is the canvas format.
 * Black stays 0, and only black: sprites treat colour 0 as transparent, as in
 * Galagino, so a dark colour that would round to 0 becomes 1. */
uint16_t gal_color(uint16_t rgb565);

/* Galagino's bbgggrrr colour PROM decode (Galaga, Pac-Man, Frogger, Dig Dug). */
uint16_t gal_prom_bgr233(uint8_t c);

/* --- Graphics converters (gal_gfx.c), ports of upstream romconv scripts ------- */

/* tileconv.py parse_charmap, 4 KB of Pac-Man/Galaga characters: 256 tiles of
 * 8 rows, two bits per pixel, leftmost pixel in the low bits. */
void gal_conv_chars_namco(const uint8_t *rom, int count, uint16_t *out);

/* spriteconv.py parse_sprite, 64 sprites per 4 KB ROM: 16 rows of 32 bits, two
 * bits per pixel, leftmost pixel in the low bits. `out` gets two variants,
 * normal and flipped in x, each count*16 words; a flip in y reads the rows
 * backwards. */
void gal_conv_sprites_namco(const uint8_t *rom, int count, int pacman_fmt, uint32_t *out_normal, uint32_t *out_xflip);

/* Frogger: tileconv.py parse_charmap_2 (256 characters, a bitplane per ROM)
 * and spriteconv.py parse_sprite_frogger (64 sprites from the same ROMs). */
void gal_conv_chars_2(const uint8_t *rom0, const uint8_t *rom1, uint16_t *out);
void gal_conv_sprites_frogger(const uint8_t *rom0, const uint8_t *rom1, uint32_t *out_normal, uint32_t *out_xflip);

/* One 16x16 sprite flipped in x. */
void gal_sprite_xflip(const uint32_t *in, uint32_t *out);

/* audioconv.py: the Namco WSG waveform PROMs, 8 waves of 32 steps per 256
 * bytes, as signed values -7..8. */
void gal_conv_waves(const uint8_t *prom, int count, int8_t *out);

/* --- Sound (gal_sound.c) ------------------------------------------------- */

/* Namco WSG (Pac-Man, Galaga, Dig Dug), as Galagino renders it. */
void gal_wsg_reset(void);
void gal_wsg_render(const int8_t *waves, uint8_t wave_mask, int16_t *out, int n);

/* AY-3-8910 tone and noise of `chips` chips (registers at soundregs[16 * k]),
 * clocked at `clock` Hz, Galagino's per-channel level ay_vol. */
void gal_ay_reset(void);
void gal_ay_render(int chips, int clock, int ay_vol, int16_t *out, int n);

/* A recorded sample played once at its own rate, added to the output. */
typedef struct
{
    const int8_t *data; /* NULL when silent */
    uint32_t len, pos, step; /* 16.16 */
    uint8_t shift;
} gal_voice_t;

void gal_voice_start(gal_voice_t *v, const int8_t *data, uint32_t len, int rate, int shift);
void gal_voice_mix(gal_voice_t *v, int16_t *out, int n);

/* --- Games ----------------------------------------------------------------- */

/* Upstream's tileaddr.h: the canvas tile (row, column) of the upright 36x28
 * tile picture to its offset in the Namco video RAM. */
extern const unsigned short tileaddr[36][28];

extern const gal_game_ops_t gal_pacman_ops;
extern const gal_game_ops_t gal_galaga_ops;
extern const gal_game_ops_t gal_frogger_ops;
extern const gal_game_ops_t gal_dkong_ops;
extern const gal_game_ops_t gal_digdug_ops;
extern const gal_game_ops_t gal_1942_ops;

#ifdef GAL_CONV_CHECK
/* hosttest/conv_check: the running game's converted data under the names of
 * the arrays upstream's romconv scripts write. */
typedef struct
{
    const char *name;
    const void *data;
    uint32_t bytes;
} gal_table_t;
#define GAL_MAX_TABLES 16
int gal_tables(gal_table_t *t);
int gal_tables_pacman(gal_table_t *t);
int gal_tables_galaga(gal_table_t *t);
int gal_tables_dkong(gal_table_t *t);
int gal_tables_frogger(gal_table_t *t);
int gal_tables_digdug(gal_table_t *t);
int gal_tables_1942(gal_table_t *t);
#endif

/* --- Drawing helpers -------------------------------------------------------- */

/* An 8x8 tile, two bits per pixel; pen 0 is transparent. */
static inline void gal_draw_tile(const gal_band_t *b, int px, int py, const uint16_t *tile, const uint16_t *colors)
{
    int ys = py, ye = py + 8, xs = px, xe = px + 8;
    if (ys < b->y0)
        ys = b->y0;
    if (ye > b->y0 + b->h)
        ye = b->y0 + b->h;
    if (xs < b->x0)
        xs = b->x0;
    if (xe > b->x0 + b->w)
        xe = b->x0 + b->w;
    if (ys >= ye || xs >= xe)
        return;
    uint16_t *row = b->buf + (ys - b->y0) * b->w + (xs - b->x0);
    const int skip = 2 * (xs - px), cols = xe - xs;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        uint32_t pix = (uint32_t)tile[y - py] >> skip;
        if (!pix)
            continue;
        for (int c = 0; c < cols; c++, pix >>= 2)
            if (pix & 3)
                row[c] = colors[pix & 3];
    }
}

/* A 16x16 sprite, two bits per pixel (rows of 32 bits, leftmost pixel in the
 * low bits). Colour 0 is transparent (Pac-Man, Galaga, Dig Dug), or pen 0
 * (pen0 set: Frogger, Donkey Kong), as in Galagino. flipy reads the rows
 * bottom up. */
static inline void gal_draw_sprite_ex(const gal_band_t *b, int sx, int sy, const uint32_t *rows, int flipy,
                                      const uint16_t *colors, int pen0)
{
    int ys = sy, ye = sy + 16, xs = sx, xe = sx + 16;
    if (ys < b->y0)
        ys = b->y0;
    if (ye > b->y0 + b->h)
        ye = b->y0 + b->h;
    if (xs < b->x0)
        xs = b->x0;
    if (xe > b->x0 + b->w)
        xe = b->x0 + b->w;
    if (ys >= ye || xs >= xe)
        return;
    uint16_t *row = b->buf + (ys - b->y0) * b->w + (xs - b->x0);
    const int skip = 2 * (xs - sx), cols = xe - xs;
    for (int y = ys; y < ye; y++, row += b->w)
    {
        int r = y - sy;
        uint32_t pix = rows[flipy ? 15 - r : r] >> skip;
        if (!pix)
            continue;
        if (pen0)
        {
            for (int c = 0; c < cols; c++, pix >>= 2)
                if (pix & 3)
                    row[c] = colors[pix & 3];
        }
        else
        {
            for (int c = 0; c < cols; c++, pix >>= 2)
            {
                uint16_t col = colors[pix & 3];
                if (col)
                    row[c] = col;
            }
        }
    }
}

static inline void gal_draw_sprite(const gal_band_t *b, int sx, int sy, const uint32_t *rows, int flipy,
                                   const uint16_t *colors)
{
    gal_draw_sprite_ex(b, sx, sy, rows, flipy, colors, 0);
}

static inline void gal_fill_band(const gal_band_t *b, uint16_t color)
{
    uint16_t *p = b->buf;
    for (int i = b->w * b->h; i > 0; i--)
        *p++ = color;
}

#endif
