/*
 * The flash store: the ROM sets copied into flash on boards without PSRAM.
 *
 * Every supported game has a slot at a fixed place, sized from its regions:
 *
 *   +0      header page (256 bytes), written last
 *   +256    region 0, region 1, ... each starting on a 256-byte page
 *           derived table 0, 1, ... (1942: its converted tiles and sprites)
 *
 * The slots follow each other in game order, each a whole number of 4 KB
 * sectors. A slot is in use when its header carries the expected layout and
 * the CRC32 of its data; anything else (an empty slot, a copy cut short, a
 * layout from an older build) is copied again from the SD card.
 *
 * Copying loads one region at a time from the zips into the SRAM arena, which
 * is free while no game runs (1942's sprite ROMs, 64 KB, are the largest), and
 * verifies every page programmed against the RAM it came from.
 */
#include "gal_internal.h"

#include <stdio.h>

#include "miniz.h"
#include "romload.h"

#define MAGIC 0x464c4147u /* "GALF" */
#define VERSION 1
#define HDR_SIZE 256
#define PAGE 256
#define SECTOR 4096
#define CHUNK 4096

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t game, nparts;
    uint32_t data_size; /* from the end of the header page */
    uint32_t crc;       /* CRC32 of the data */
    uint32_t part_ofs[GAL_MAX_PARTS]; /* from the start of the slot */
    uint32_t part_size[GAL_MAX_PARTS];
} hdr_t;

static gal_flash_t fl;
static uint32_t in_flash;           /* games whose slot is in use */
static hdr_t layout[GAL_GAME_COUNT]; /* the expected headers, crc 0 */
static uint32_t slot_ofs[GAL_GAME_COUNT], slot_size[GAL_GAME_COUNT];

static uint32_t round_up(uint32_t v, uint32_t to)
{
    return (v + to - 1) / to * to;
}

static void make_layout(void)
{
    uint32_t ofs = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        const gal_game_ops_t *ops = gal_ops(g);
        hdr_t *h = &layout[g];
        memset(h, 0, sizeof(*h));
        slot_ofs[g] = ofs;
        slot_size[g] = 0;
        if (!ops)
            continue;
        const gal_game_info_t *info = &gal_games[g];
        h->magic = MAGIC;
        h->version = VERSION;
        h->game = (uint8_t)g;
        h->nparts = (uint8_t)(info->nregions + ops->nderived);
        uint32_t d = HDR_SIZE;
        for (int r = 0; r < info->nregions; r++)
        {
            h->part_ofs[r] = d;
            h->part_size[r] = info->region_size[r];
            d += round_up(info->region_size[r], PAGE);
        }
        for (int i = 0; i < ops->nderived; i++)
        {
            h->part_ofs[GAL_MAX_REGIONS + i] = d;
            h->part_size[GAL_MAX_REGIONS + i] = ops->derived_size[i];
            d += round_up(ops->derived_size[i], PAGE);
        }
        h->data_size = d - HDR_SIZE;
        slot_size[g] = round_up(d, SECTOR);
        ofs += slot_size[g];
    }
}

uint32_t gal_flash_size_needed(void)
{
    make_layout();
    return slot_ofs[GAL_GAME_COUNT - 1] + slot_size[GAL_GAME_COUNT - 1];
}

static int fits(int g)
{
    return slot_size[g] && slot_ofs[g] + slot_size[g] <= fl.size;
}

static int slot_valid(int g)
{
    if (!fits(g))
        return 0;
    const uint8_t *slot = fl.base + slot_ofs[g];
    hdr_t h;
    memcpy(&h, slot, sizeof(h));
    hdr_t want = layout[g];
    want.crc = h.crc;
    if (memcmp(&h, &want, sizeof(h)) != 0)
        return 0;
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, slot + HDR_SIZE, h.data_size) == h.crc;
}

/* Programs len bytes (a multiple of PAGE) at ofs in the slot, and checks them. */
static int put(int g, uint32_t ofs, const uint8_t *data, uint32_t len)
{
    fl.program(fl.offset + slot_ofs[g] + ofs, data, len);
    return memcmp(fl.base + slot_ofs[g] + ofs, data, len) == 0;
}

static int copy_game(int g, const gal_io_t *io, const char *dir)
{
    const gal_game_info_t *info = &gal_games[g];
    const gal_game_ops_t *ops = gal_ops(g);
    const hdr_t *h = &layout[g];

    if (fl.copying)
        fl.copying(g);
    fl.erase(fl.offset + slot_ofs[g], slot_size[g]);

    gal_arena_reset();
    uint8_t *chunk = (uint8_t *)gal_alloc(CHUNK);
    int ok = chunk != NULL;
    for (int r = 0; ok && r < info->nregions; r++)
    {
        const uint32_t size = round_up(info->region_size[r], PAGE);
        uint8_t *buf = (uint8_t *)gal_temp_alloc(size);
        if (!buf)
        {
            ok = 0;
            break;
        }
        memset(buf, 0, size);

        /* this region's files only */
        uint8_t *regions[GAL_MAX_REGIONS] = {0};
        regions[r] = buf;
        const uint32_t found = gal_romload(info, regions, io, dir, 0);
        for (int i = 0; i < info->nfiles; i++)
            if (info->files[i].region == r && !(found & (1u << i)))
                ok = 0;
        ok = ok && put(g, h->part_ofs[r], buf, size);

        /* the tables built from this region */
        for (int d = 0; ok && d < ops->nderived; d++)
        {
            if (ops->derived_src[d] != r)
                continue;
            for (uint32_t ofs = 0; ok && ofs < ops->derived_size[d]; ofs += CHUNK)
            {
                ops->derive(d, buf, ofs, chunk, CHUNK);
                ok = put(g, h->part_ofs[GAL_MAX_REGIONS + d] + ofs, chunk, CHUNK);
            }
        }
        gal_temp_release();
    }

    if (ok)
    {
        /* the header last: until it is there, the slot is not in use */
        memset(chunk, 0xff, HDR_SIZE);
        hdr_t hdr = *h;
        hdr.crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, fl.base + slot_ofs[g] + HDR_SIZE, h->data_size);
        memcpy(chunk, &hdr, sizeof(hdr));
        ok = put(g, 0, chunk, HDR_SIZE);
    }
    gal_arena_reset();
    return ok && slot_valid(g);
}

uint32_t gal_flash_sync(const gal_flash_t *flash, const gal_io_t *io, const char *root)
{
    fl = *flash;
    make_layout();
    in_flash = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        if (!fits(g))
            continue;
        if (slot_valid(g))
        {
            in_flash |= 1u << g;
            continue;
        }
        if (!io)
            continue;
        char dir[160];
        snprintf(dir, sizeof(dir), "%s/%s", root, gal_games[g].folder);
        if (!gal_set_complete((gal_game_id_t)g, gal_probe((gal_game_id_t)g, io, dir)))
            continue;
        if (copy_game(g, io, dir))
            in_flash |= 1u << g;
    }
    return in_flash;
}

int gal_flash_parts(int game, const uint8_t **parts)
{
    if (game < 0 || game >= GAL_GAME_COUNT || !(in_flash & (1u << game)))
        return 0;
    const uint8_t *slot = fl.base + slot_ofs[game];
    for (int p = 0; p < GAL_MAX_PARTS; p++)
        parts[p] = layout[game].part_size[p] ? slot + layout[game].part_ofs[p] : NULL;
    return 1;
}
