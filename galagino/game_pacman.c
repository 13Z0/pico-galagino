/*
 * Pac-Man (Midway, 1980), from upstream Galagino's pacman.h.
 *
 * One Z80 at 3.072 MHz, 50688 cycles per 60.6 Hz frame. Galagino's RAM buffer
 * holds 4000-4fff (video, colour, work and sprite RAM) at 0000-0fff and the
 * sprite coordinates written to 5060-506f at 1060-106f. A15 is not decoded.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#define CYCLES_PER_FRAME 50688

static struct
{
    const uint8_t *rom;
    uint16_t *tiles;     /* [256][8] */
    uint32_t *sprites;   /* [64][16] */
    uint32_t *sprites_x; /* [64][16], flipped in x */
    uint16_t *colormap;  /* [64][4] */
    int8_t *waves;       /* [16][32] */
    uint8_t irq_enable;
    uint8_t irq_vector;
} pm;

/* --- Memory map ------------------------------------------------------------ */

static uint8_t GAL_HOT(pm_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    addr &= 0x7fff;
    if ((addr & 0xff00) == 0x5000)
    {
        const uint16_t keys = gal.buttons;
        switch (addr & 0xc0)
        {
        case 0x00: /* IN0 */
        {
            uint8_t v = 0xff;
            if (keys & GAL_BTN_UP)
                v &= ~0x01;
            if (keys & GAL_BTN_LEFT)
                v &= ~0x02;
            if (keys & GAL_BTN_RIGHT)
                v &= ~0x04;
            if (keys & GAL_BTN_DOWN)
                v &= ~0x08;
            if (keys & GAL_BTN_COIN)
                v &= ~0x20;
            return v;
        }
        case 0x40: /* IN1 */
        {
            uint8_t v = 0xff; /* 0xef for service */
            if (keys & GAL_BTN_START)
                v &= ~0x20;
            if (keys & GAL_BTN_START2)
                v &= ~0x40;
            return v;
        }
        case 0x80: /* DSW1 */
            return PACMAN_DIP;
        default: /* DSW2 */
            return 0xff;
        }
    }
    return 0xff;
}

static void GAL_HOT(pm_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    addr &= 0x7fff;
    if ((addr & 0xff00) != 0x5000)
        return;
    if ((addr & 0xfff0) == 0x5060) /* sprite coordinates */
        gal.ram[addr - 0x4000] = value;
    else if (addr == 0x5000)
    {
        pm.irq_enable = value & 1;
        if (!pm.irq_enable)
            z80_set_irq_line(cpu, 0);
    }
    else if ((addr & 0xffe0) == 0x5040)
        gal.soundregs[addr - 0x5040] = value & 0x0f;
}

static void pm_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)port;
    /* the interrupt vector; writing it acknowledges a pending interrupt */
    pm.irq_vector = value;
    z80_set_irq_line(cpu, 0);
}

static uint8_t pm_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0;
}

/* --- Life cycle ---------------------------------------------------------------- */

static int pm_init(uint8_t *const *regions)
{
    pm.rom = regions[0];
    pm.tiles = (uint16_t *)gal_alloc(256 * 8 * 2);
    pm.sprites = (uint32_t *)gal_alloc(64 * 16 * 4);
    pm.sprites_x = (uint32_t *)gal_alloc(64 * 16 * 4);
    pm.colormap = (uint16_t *)gal_alloc(64 * 4 * 2);
    pm.waves = (int8_t *)gal_alloc(16 * 32);
    if (!pm.tiles || !pm.sprites || !pm.sprites_x || !pm.colormap || !pm.waves)
        return 0;

    gal_conv_chars_namco(regions[1], 256, pm.tiles);
    gal_conv_sprites_namco(regions[2], 64, 1, pm.sprites, pm.sprites_x);

    /* cmapconv.py parse_colormap: 64 groups of four entries of the first 16
     * palette colours */
    uint16_t palette[16];
    for (int i = 0; i < 16; i++)
        palette[i] = gal_color(gal_prom_bgr233(regions[3][i]));
    for (int i = 0; i < 64 * 4; i++)
        pm.colormap[i] = palette[regions[4][i] & 0x0f];

    gal_conv_waves(regions[5], 16, pm.waves);

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[p] = cpu->rd_page[p | 0x80] = pm.rom + p * 256;
    for (int p = 0; p < 0x10; p++)
    {
        uint8_t *ram = gal.ram + p * 256;
        cpu->rd_page[0x40 | p] = cpu->rd_page[0xc0 | p] = ram;
        cpu->wr_page[0x40 | p] = cpu->wr_page[0xc0 | p] = ram;
    }
    cpu->read = pm_read;
    cpu->write = pm_write;
    cpu->in = pm_in;
    cpu->out = pm_out;
    return 1;
}

static void pm_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    pm.irq_enable = 0;
    pm.irq_vector = 0;
    gal.game_started = 0;
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    gal_wsg_reset();
}

static void GAL_HOT(pm_run_slice)(int q)
{
    z80_run(&gal.cpu[0], CYCLES_PER_FRAME / 4);
    if (q == 3)
    {
        /* writing 85 ('U' of "1UP") to the top left corner tells the game has
         * booted, as in Galagino */
        if (gal.ram[985] == 85)
            gal.game_started = 1;
        if (pm.irq_enable)
            z80_hold_irq(&gal.cpu[0], pm.irq_vector);
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(pm_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 8; idx++)
    {
        const uint8_t *base = gal.ram + 2 * (7 - idx);
        gal_sprite_t spr;
        spr.code = base[0x0ff0] >> 2;
        spr.color = base[0x0ff1] & 63;
        spr.flags = base[0x0ff0] & 3;
        /* the position on the upright screen */
        spr.x = 255 - 16 - base[0x1060];
        spr.y = 16 + 256 - base[0x1061];
        if (spr.code < 64 && spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
            gal.sprite[gal.active_sprites++] = spr;
    }
}

static void GAL_HOT(pm_render_band)(const gal_band_t *b)
{
    gal_fill_band(b, 0);

    for (int row = b->y0 / 8; row <= (b->y0 + b->h - 1) / 8; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[row][col];
            gal_draw_tile(b, 8 * col, 8 * row, pm.tiles + 8 * gal.ram[addr],
                          pm.colormap + 4 * (gal.ram[0x400 + addr] & 63));
        }
    }

    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint32_t *rows = ((spr->flags & 2) ? pm.sprites_x : pm.sprites) + 16 * spr->code;
        gal_draw_sprite(b, spr->x, spr->y, rows, spr->flags & 1, pm.colormap + 4 * spr->color);
    }
}

static void GAL_HOT(pm_sound)(int16_t *out, int n)
{
    gal_wsg_render(pm.waves, 0x0f, out, n);
}

const gal_game_ops_t gal_pacman_ops = {
    .init = pm_init,
    .reset = pm_reset,
    .run_slice = pm_run_slice,
    .prepare_frame = pm_prepare_frame,
    .render_band = pm_render_band,
    .sound = pm_sound,
    .view_top = 0,
    .view_h = GAL_H,
};

#ifdef GAL_CONV_CHECK
int gal_tables_pacman(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"pacman_rom", pm.rom, 0x4000};
    t[n++] = (gal_table_t){"pacman_5e", pm.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"pacman_sprites", pm.sprites, 64 * 16 * 4};
    t[n++] = (gal_table_t){"pacman_sprites_x", pm.sprites_x, 64 * 16 * 4};
    t[n++] = (gal_table_t){"pacman_colormap", pm.colormap, 64 * 4 * 2};
    t[n++] = (gal_table_t){"pacman_wavetable", pm.waves, 16 * 32};
    return n;
}
#endif
