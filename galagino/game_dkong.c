/*
 * Donkey Kong (Nintendo, 1981), from upstream Galagino's dkong.h.
 *
 * A Z80 at 3.072 MHz and an MB8884 (an 8048) sound CPU at 6 MHz / 15 =
 * 400 kHz machine cycles, writing an 8-bit DAC; walk, jump and stomp were
 * discrete circuits and are Galagino's recorded samples. The raster is 256
 * lines: the game uses the upright rows 16..271 (tile rows 2..33).
 *
 * Galagino's RAM buffer holds 6000-77ff at 0000: work RAM, the sprite RAM the
 * DMA copies to at 7000 (1000), the video RAM at 7400 (1400).
 *
 * Galagino runs the 8048 as fast as its audio queue drains and plays the DAC at
 * 11765 Hz (6 MHz / 15 / 34 cycles per sample). Here it runs by its clock in
 * step with the Z80, and the DAC is sampled as the level it holds: each write
 * is stamped with its cycle within the quarter frame, and the output samples
 * read the level at their time.
 */
#include "gal_internal.h"

#include "dip_switches.h"
#include "i8048.h"

#include "data/dkong_sample_jump.h"
#include "data/dkong_sample_stomp.h"
#include "data/dkong_sample_walk0.h"
#include "data/dkong_sample_walk1.h"
#include "data/dkong_sample_walk2.h"

#define MAIN_CYCLES_PER_FRAME 50688
#define SOUND_CYCLES_PER_FRAME 6600 /* 400 kHz / 60.606 Hz */
#define STEPS_PER_QUARTER 16
#define SAMPLE_RATE 11765 /* of the recorded samples */
#define MAX_DAC_EVENTS 512

const unsigned char *dkong_rom_cpu2; /* i8048.h reads its ROM through this */

static struct
{
    const uint8_t *rom;
    uint16_t *tiles;     /* [256][8] */
    uint32_t *sprites;   /* [128][16] */
    uint32_t *sprites_x; /* the same flipped in x */
    uint16_t *cmap;      /* [4][256][4]: per screen colour table and tile position */
    uint16_t cmap_sprite[4][16][4];

    uint8_t nmi_enable;
    uint8_t colortable_select;
    uint8_t sfx_index;

    struct i8048_state_S snd;
    unsigned long quarter_start; /* snd.cycles at the start of the quarter */
    struct
    {
        uint16_t t;
        uint8_t v;
    } dac[MAX_DAC_EVENTS];
    int ndac;
    uint8_t dac_level;
    int32_t dc_x, dc_y; /* DC blocker state */

    gal_voice_t voice[3]; /* walk, jump, stomp */
    uint32_t rng;
} dk;

/* --- The 8048's ports, called by i8048.c --------------------------------------- */

void i8048_port_write(struct i8048_state_S *state, unsigned char port, unsigned char pos)
{
    if (port == 1)
    {
        if (dk.ndac < MAX_DAC_EVENTS)
        {
            dk.dac[dk.ndac].t = (uint16_t)(state->cycles - dk.quarter_start);
            dk.dac[dk.ndac].v = pos;
            dk.ndac++;
        }
    }
    else if (port == 2)
        state->p2_state = pos;
}

unsigned char i8048_port_read(struct i8048_state_S *state, unsigned char port)
{
    if (port == 2)
        return (unsigned char)state->p2_state;
    return 0;
}

void i8048_xdm_write(struct i8048_state_S *state, unsigned char addr, unsigned char data)
{
    (void)state;
    (void)addr;
    (void)data;
}

unsigned char i8048_xdm_read(struct i8048_state_S *state, unsigned char addr)
{
    /* inverted Z80 MUS register */
    if (state->p2_state & 0x40)
        return dk.sfx_index ^ 0x0f;
    return dkong_rom_cpu2[2048 + addr + 256 * (state->p2_state & 7)];
}

/* --- Main CPU ---------------------------------------------------------------- */

static void dk_trigger_sound(int snd)
{
    static const struct
    {
        const unsigned char *data;
        uint16_t length;
    } samples[] = {
        {dkong_sample_walk0, sizeof(dkong_sample_walk0)}, {dkong_sample_walk1, sizeof(dkong_sample_walk1)},
        {dkong_sample_walk2, sizeof(dkong_sample_walk2)}, {dkong_sample_jump, sizeof(dkong_sample_jump)},
        {dkong_sample_stomp, sizeof(dkong_sample_stomp)},
    };
    /* 0 walk (one of three variants), 1 jump, 2 stomp; at 6.25, 12.5 and 25% volume */
    int idx = snd + 2;
    if (snd == 0)
    {
        dk.rng = dk.rng * 1664525u + 1013904223u;
        idx = (int)((dk.rng >> 16) % 3);
    }
    gal_voice_start(&dk.voice[snd], (const int8_t *)samples[idx].data, samples[idx].length, SAMPLE_RATE, 2 - snd);
}

static uint8_t GAL_HOT(dk_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    switch (addr & 0xfff0)
    {
    case 0x7c00: /* IN0 */
    {
        uint8_t v = 0;
        if (keys & GAL_BTN_RIGHT)
            v |= 0x01;
        if (keys & GAL_BTN_LEFT)
            v |= 0x02;
        if (keys & GAL_BTN_UP)
            v |= 0x04;
        if (keys & GAL_BTN_DOWN)
            v |= 0x08;
        if (keys & GAL_BTN_FIRE)
            v |= 0x10;
        return v;
    }
    case 0x7c80: /* IN1 */
        return 0x00;
    case 0x7d00: /* IN2 */
    {
        uint8_t v = 0;
        if (keys & GAL_BTN_COIN)
            v |= 0x80;
        if (keys & GAL_BTN_START)
            v |= 0x04;
        if (keys & GAL_BTN_START2)
            v |= 0x08;
        return v;
    }
    case 0x7d80:
        return DKONG_DIP;
    }
    return 0xff;
}

static void GAL_HOT(dk_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xfe00) != 0x7c00) /* 7800 DMA registers and the rest */
        return;

    if (addr == 0x7c00) /* music */
        dk.sfx_index = value;

    if ((addr & 0xfff0) == 0x7d00)
    {
        const int reg = addr & 0x0f;
        if (reg <= 2 && value) /* samples: walk, jump, stomp */
            dk_trigger_sound(reg);
        else if (reg == 3)
        {
            if (value & 1)
                dk.snd.p2_state &= ~0x20;
            else
                dk.snd.p2_state |= 0x20;
        }
        else if (reg == 4)
            dk.snd.T1 = !(value & 1);
        else if (reg == 5)
            dk.snd.T0 = !(value & 1);
    }

    if (addr == 0x7d80)
        dk.snd.notINT = !(value & 1);
    else if (addr == 0x7d84)
    {
        dk.nmi_enable = value & 1;
        if (!dk.nmi_enable)
            z80_set_nmi_line(cpu, 0);
    }
    else if (addr == 0x7d85 && (value & 1))
    {
        /* the DMA: Donkey Kong only uses it to copy the sprites from 6900 to 7000 */
        memcpy(gal.ram + 0x1000, gal.ram + 0x900, 384);
    }
    else if (addr == 0x7d86)
        dk.colortable_select = (uint8_t)((dk.colortable_select & ~1) | (value & 1));
    else if (addr == 0x7d87)
        dk.colortable_select = (uint8_t)((dk.colortable_select & ~2) | ((value << 1) & 2));
}

static uint8_t dk_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void dk_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* cmapconv.py parse_palette with two PROMs: inverted rrrgggbb */
static uint16_t dk_palette(uint8_t lo, uint8_t hi)
{
    const unsigned c = ((unsigned)hi << 4) + lo;
    const unsigned r = 31 - 31 * ((c >> 5) & 0x7) / 7;
    const unsigned g = 63 - 63 * ((c >> 2) & 0x7) / 7;
    const unsigned b = 31 - 31 * ((c >> 0) & 0x3) / 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* spriteconv.py parse_sprite_dkong: each sprite spread over all four ROMs */
static void dk_conv_sprites(const uint8_t *roms, uint32_t *out_normal, uint32_t *out_xflip)
{
    for (int spr = 0; spr < 128; spr++)
    {
        uint32_t *dst = out_normal + 16 * spr;
        for (int y = 0; y < 16; y++)
        {
            const uint8_t *d0 = roms + 0x800 * (y / 8) + 16 * spr;
            const uint8_t *d1 = roms + 0x800 * (y / 8 + 2) + 16 * spr;
            uint32_t val = 0;
            for (int x = 0; x < 16; x++)
            {
                const int c0 = (d0[15 - x] & (0x80 >> (y & 7))) ? 1 : 0;
                const int c1 = (d1[15 - x] & (0x80 >> (y & 7))) ? 2 : 0;
                val = (val >> 2) | ((uint32_t)(c0 + c1) << 30);
            }
            dst[y] = val;
        }
        gal_sprite_xflip(dst, out_xflip + 16 * spr);
    }
}

static int dk_init(uint8_t *const *regions)
{
    dk.rom = regions[0];
    dkong_rom_cpu2 = regions[1];
    dk.tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    dk.sprites = (uint32_t *)gal_alloc(128 * 16 * 4);
    dk.sprites_x = (uint32_t *)gal_alloc(128 * 16 * 4);
    dk.cmap = (uint16_t *)gal_alloc(4 * 256 * 4 * 2);
    if (!dk.tiles || !dk.sprites || !dk.sprites_x || !dk.cmap)
        return 0;

    gal_conv_chars_2(regions[2], regions[2] + 0x800, dk.tiles);
    dk_conv_sprites(regions[3], dk.sprites, dk.sprites_x);

    /* cmapconv.py parse_colormap_dkong: four screen colour tables */
    uint16_t palette[256];
    for (int i = 0; i < 256; i++)
        palette[i] = gal_color(dk_palette(regions[4][i] & 0x0f, regions[5][i] & 0x0f));
    for (int s = 0; s < 4; s++)
    {
        for (int i = 0; i < 256; i++)
        {
            const int idx = (regions[6][i] & 0x0f) + 16 * s;
            for (int k = 0; k < 4; k++)
                dk.cmap[(s * 256 + i) * 4 + k] = palette[4 * idx + k];
        }
        for (int i = 0; i < 16; i++)
            for (int k = 0; k < 4; k++)
                dk.cmap_sprite[s][i][k] = palette[4 * (i + 16 * s) + k];
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[p] = dk.rom + p * 256;
    for (int p = 0; p < 0x18; p++) /* 6000-77ff RAM */
        cpu->rd_page[0x60 + p] = cpu->wr_page[0x60 + p] = gal.ram + p * 256;
    cpu->read = dk_read;
    cpu->write = dk_write;
    cpu->in = dk_in;
    cpu->out = dk_out;
    return 1;
}

static void dk_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    dk.nmi_enable = 0;
    dk.colortable_select = 0;
    dk.sfx_index = 0;
    dk.ndac = 0;
    dk.dac_level = 0;
    dk.dc_x = dk.dc_y = 0;
    for (int i = 0; i < 3; i++)
        dk.voice[i].data = NULL;
    if (!dk.rng)
        dk.rng = 12345;
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    i8048_reset(&dk.snd);
    dk.quarter_start = 0;
    gal.game_started = 1; /* as in Galagino */
}

static void GAL_HOT(dk_run_slice)(int q)
{
    dk.quarter_start = dk.snd.cycles;
    dk.ndac = 0;
    for (int i = 0; i < STEPS_PER_QUARTER; i++)
    {
        const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
        const int main_cycles =
            (step + 1) * MAIN_CYCLES_PER_FRAME / steps - step * MAIN_CYCLES_PER_FRAME / steps;
        const unsigned long snd_end =
            dk.quarter_start + (unsigned long)((i + 1) * (SOUND_CYCLES_PER_FRAME / 4) / STEPS_PER_QUARTER);

        z80_run(&gal.cpu[0], main_cycles);
        while ((long)(dk.snd.cycles - snd_end) < 0)
            i8048_step(&dk.snd);
    }

    /* VBLANK: NMI while enabled */
    if (q == 3 && dk.nmi_enable)
    {
        z80_set_nmi_line(&gal.cpu[0], 1);
        z80_set_nmi_line(&gal.cpu[0], 0);
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(dk_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 96 && gal.active_sprites < GAL_MAX_SPRITES; idx++)
    {
        const uint8_t *base = gal.ram + 0x1000 + 4 * idx;
        gal_sprite_t spr;
        /* the position on the upright screen */
        spr.x = base[0] - 23;
        spr.y = base[3] + 8;
        spr.code = base[1] & 0x7f;
        spr.color = base[2] & 0x0f;
        spr.flags = (uint8_t)(((base[2] & 0x80) ? 1 : 0) | ((base[1] & 0x80) ? 2 : 0));
        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
            gal.sprite[gal.active_sprites++] = spr;
    }
}

static void GAL_HOT(dk_render_band)(const gal_band_t *b)
{
    gal_fill_band(b, 0);

    const int row_first = b->y0 / 8 < 2 ? 2 : b->y0 / 8;
    const int row_last = (b->y0 + b->h - 1) / 8 > 33 ? 33 : (b->y0 + b->h - 1) / 8;
    const uint16_t *cmap = dk.cmap + dk.colortable_select * 256 * 4;
    for (int row = row_first; row <= row_last; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[row][col];
            const uint8_t code = gal.ram[0x1400 + addr];
            if (code == 0x10) /* blank */
                continue;
            /* a colour per tile position: 32 rows by 8 groups of four columns */
            gal_draw_tile(b, 8 * col, 8 * row, dk.tiles + 8 * code, cmap + 4 * (row - 2 + 32 * (col / 4)));
        }
    }

    /* black sprite pixels are not transparent here: they mask */
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint32_t *rows = ((spr->flags & 2) ? dk.sprites_x : dk.sprites) + 16 * spr->code;
        gal_draw_sprite_ex(b, spr->x, spr->y, rows, spr->flags & 1, dk.cmap_sprite[dk.colortable_select][spr->color],
                           1);
    }
}

/* --- Sound --------------------------------------------------------------------- */

static void GAL_HOT(dk_sound)(int16_t *out, int n)
{
    /* the DAC level at each output sample's time within the quarter */
    const uint32_t quarter = SOUND_CYCLES_PER_FRAME / 4;
    int e = 0;
    for (int i = 0; i < n; i++)
    {
        const uint32_t t = (uint32_t)i * quarter / (uint32_t)n;
        while (e < dk.ndac && dk.dac[e].t <= t)
            dk.dac_level = dk.dac[e++].v;
        /* Galagino plays the DAC at half the range; the DC blocker (a one-pole
         * high-pass at about 30 Hz) stands in for the output capacitor */
        const int32_t x = ((int32_t)dk.dac_level - 128) * 64;
        dk.dc_y = x - dk.dc_x + ((dk.dc_y * 255) >> 8);
        dk.dc_x = x;
        out[i] = (int16_t)(dk.dc_y > 32767 ? 32767 : (dk.dc_y < -32768 ? -32768 : dk.dc_y));
    }
    while (e < dk.ndac)
        dk.dac_level = dk.dac[e++].v;

    for (int v = 0; v < 3; v++)
        gal_voice_mix(&dk.voice[v], out, n);
}

const gal_game_ops_t gal_dkong_ops = {
    .init = dk_init,
    .reset = dk_reset,
    .run_slice = dk_run_slice,
    .prepare_frame = dk_prepare_frame,
    .render_band = dk_render_band,
    .sound = dk_sound,
    .view_top = 16,
    .view_h = 256,
};

#ifdef GAL_CONV_CHECK
int gal_tables_dkong(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"dkong_rom_cpu1", dk.rom, 0x4000};
    t[n++] = (gal_table_t){"dkong_rom_cpu2", dkong_rom_cpu2, 0x1000};
    t[n++] = (gal_table_t){"v_5h_b_bin", dk.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"dkong_sprites", dk.sprites, 128 * 16 * 4};
    t[n++] = (gal_table_t){"dkong_sprites_x", dk.sprites_x, 128 * 16 * 4};
    t[n++] = (gal_table_t){"dkong_colormap", dk.cmap, 4 * 256 * 4 * 2};
    t[n++] = (gal_table_t){"dkong_colormap_sprite", dk.cmap_sprite, sizeof(dk.cmap_sprite)};
    return n;
}
#endif
