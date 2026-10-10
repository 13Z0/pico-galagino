/*
 * ROM set loader, generalised from pico-mooncresta's romload.c.
 *
 * Finds a game's files in a directory, inside zips (merged, split or
 * non-merged MAME sets, clone subfolders included) or as loose files. Files are
 * identified by size and CRC32 only, so their names and their place in a zip do
 * not matter. That is what makes MAME's merged puckman.zip work for Pac-Man:
 * the Midway files sit in clone subfolders there, and the PROMs under the
 * parent's names.
 *
 * Without region buffers the loader only probes: zip entries are taken from the
 * central directory, without inflating anything.
 *
 * The file system is reached through gal_io_t, so the same code runs on FatFs
 * on the board and on stdio in the host harness. Large buffers come from the
 * allocator of gal_io_t, never the stack: the inflate state alone is about
 * 11 KB.
 */
#include "romload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

/* Index of the still-missing set file with this size and CRC, or -1. With
 * region buffers given, only files of the regions that have one count. */
static uint8_t *const *cur_regions;

static int wanted(const gal_game_info_t *g, uint32_t found, uint32_t size, uint32_t crc)
{
    for (int i = 0; i < g->nfiles; i++)
        if (!(found & (1u << i)) && g->files[i].size == size && g->files[i].crc == crc &&
            (!cur_regions || cur_regions[g->files[i].region]))
            return i;
    return -1;
}

static int size_wanted(const gal_game_info_t *g, uint32_t size)
{
    for (int i = 0; i < g->nfiles; i++)
        if (g->files[i].size == size)
            return 1;
    return 0;
}

static uint32_t all_files(const gal_game_info_t *g)
{
    return (g->nfiles >= 32) ? 0xffffffffu : ((1u << g->nfiles) - 1);
}

static uint8_t *dest_of(const gal_game_info_t *g, uint8_t *const *regions, int idx)
{
    return regions[g->files[idx].region] + g->files[idx].offset;
}

static void *tmp_alloc(const gal_io_t *io, uint32_t size)
{
    if (!size)
        size = 1;
    return io->alloc ? io->alloc(io->ctx, size) : malloc(size);
}

static void tmp_free(const gal_io_t *io, void *p)
{
    if (!p)
        return;
    if (io->free)
        io->free(io->ctx, p);
    else
        free(p);
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, p, n);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    if (m > n)
        return 0;
    s += n - m;
    for (size_t i = 0; i < m; i++)
    {
        char a = s[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z')
            b += 'a' - 'A';
        if (a != b)
            return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * Zip
 * ------------------------------------------------------------------------ */

#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CDH_SIG 0x02014b50u
#define ZIP_LFH_SIG 0x04034b50u
#define ZIP_EOCD_SEARCH 1024 /* covers a TorrentZip comment and then some */

/* Extracts one entry to dst (exactly `size` bytes), verifying the CRC. */
static int zip_extract(const gal_io_t *io, void *fh, uint32_t lfh_ofs, int method, uint32_t comp_size, uint8_t *dst,
                       uint32_t size, uint32_t crc)
{
    uint8_t lfh[30];
    if (io->read_at(io->ctx, fh, lfh_ofs, lfh, sizeof(lfh)) != (int)sizeof(lfh) || le32(lfh) != ZIP_LFH_SIG)
        return 0;
    uint32_t data_ofs = lfh_ofs + 30 + le16(lfh + 26) + le16(lfh + 28);

    uint8_t *comp = (uint8_t *)tmp_alloc(io, comp_size);
    if (!comp)
        return 0;
    int ok = 0;
    if (io->read_at(io->ctx, fh, data_ofs, comp, comp_size) == (int)comp_size)
    {
        if (method == 0 && comp_size == size)
        {
            memcpy(dst, comp, size);
            ok = 1;
        }
        else if (method == 8)
        {
            tinfl_decompressor *d = (tinfl_decompressor *)tmp_alloc(io, sizeof(tinfl_decompressor));
            if (d)
            {
                tinfl_init(d);
                size_t in_sz = comp_size, out_sz = size;
                tinfl_status st =
                    tinfl_decompress(d, comp, &in_sz, dst, dst, &out_sz, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                ok = (st == TINFL_STATUS_DONE && out_sz == size);
                tmp_free(io, d);
            }
        }
    }
    tmp_free(io, comp);
    return ok && crc32_of(dst, size) == crc;
}

static uint32_t load_zip(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *path,
                         uint32_t found)
{
    void *fh = io->open(io->ctx, path);
    if (!fh)
        return found;

    uint32_t fsize = io->size(io->ctx, fh);
    uint32_t tail = fsize < ZIP_EOCD_SEARCH ? fsize : ZIP_EOCD_SEARCH;
    uint8_t *buf = (uint8_t *)tmp_alloc(io, tail);
    if (!buf || tail < 22 || io->read_at(io->ctx, fh, fsize - tail, buf, tail) != (int)tail)
    {
        tmp_free(io, buf);
        io->close(io->ctx, fh);
        return found;
    }

    /* find the end of central directory record, last one wins */
    int eocd = -1;
    for (int i = (int)tail - 22; i >= 0; i--)
    {
        if (le32(buf + i) == ZIP_EOCD_SIG)
        {
            eocd = i;
            break;
        }
    }
    uint32_t entries = 0, cd_ofs = 0;
    if (eocd >= 0)
    {
        entries = le16(buf + eocd + 10);
        cd_ofs = le32(buf + eocd + 16);
    }
    tmp_free(io, buf);

    const uint32_t all = all_files(g);
    uint32_t ofs = cd_ofs;
    for (uint32_t e = 0; e < entries && found != all; e++)
    {
        uint8_t cdh[46];
        if (io->read_at(io->ctx, fh, ofs, cdh, sizeof(cdh)) != (int)sizeof(cdh) || le32(cdh) != ZIP_CDH_SIG)
            break;
        int method = le16(cdh + 10);
        uint32_t crc = le32(cdh + 16);
        uint32_t comp_size = le32(cdh + 20);
        uint32_t size = le32(cdh + 24);
        uint32_t lfh_ofs = le32(cdh + 42);
        ofs += 46 + le16(cdh + 28) + le16(cdh + 30) + le16(cdh + 32);

        int idx = wanted(g, found, size, crc);
        if (idx < 0)
            continue;
        if (!regions)
            found |= 1u << idx; /* probe: the directory's CRC is enough */
        else if (zip_extract(io, fh, lfh_ofs, method, comp_size, dest_of(g, regions, idx), size, crc))
            found |= 1u << idx;
    }

    io->close(io->ctx, fh);
    return found;
}

/* ---------------------------------------------------------------------------
 * Loose files
 * ------------------------------------------------------------------------ */

static uint32_t load_loose(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *path,
                           uint32_t size, uint32_t found)
{
    void *fh = io->open(io->ctx, path);
    if (!fh)
        return found;
    uint8_t *buf = (uint8_t *)tmp_alloc(io, size);
    if (buf && io->read_at(io->ctx, fh, 0, buf, size) == (int)size)
    {
        int idx = wanted(g, found, size, crc32_of(buf, size));
        if (idx >= 0)
        {
            if (regions)
                memcpy(dest_of(g, regions, idx), buf, size);
            found |= 1u << idx;
        }
    }
    tmp_free(io, buf);
    io->close(io->ctx, fh);
    return found;
}

/* ---------------------------------------------------------------------------
 * Directory scan
 * ------------------------------------------------------------------------ */

#define MAX_CANDIDATES 64
#define MAX_NAME 96

typedef struct
{
    char name[MAX_NAME];
    uint32_t size;
    uint8_t is_zip;
} candidate_t;

typedef struct
{
    const gal_game_info_t *g;
    candidate_t *list;
    int count;
} scan_t;

static void scan_cb(void *arg, const char *name, uint32_t size, int is_dir)
{
    scan_t *s = (scan_t *)arg;
    if (is_dir || s->count >= MAX_CANDIDATES || strlen(name) >= MAX_NAME)
        return;
    int is_zip = ends_with_ci(name, ".zip");
    if (!is_zip && !size_wanted(s->g, size))
        return; /* not one of the game's ROMs by size */
    candidate_t *c = &s->list[s->count++];
    strcpy(c->name, name);
    c->size = size;
    c->is_zip = (uint8_t)is_zip;
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    if (dl && dir[dl - 1] == '/')
        dl--;
    if (dl + 1 + strlen(name) + 1 > n)
    {
        out[0] = 0;
        return;
    }
    memcpy(out, dir, dl);
    out[dl] = '/';
    strcpy(out + dl + 1, name);
}

static uint32_t scan_dir(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *dir,
                         uint32_t found)
{
    const uint32_t all = all_files(g);
    scan_t s;
    s.g = g;
    s.count = 0;
    s.list = (candidate_t *)tmp_alloc(io, sizeof(candidate_t) * MAX_CANDIDATES);
    if (!s.list)
        return found;
    if (!io->list_dir(io->ctx, dir, scan_cb, &s))
    {
        tmp_free(io, s.list);
        return found;
    }

    char setzip[32];
    snprintf(setzip, sizeof(setzip), "%s.zip", g->mameset);
    char path[MAX_NAME * 2 + 64];

    /* pass 0: the set's own zip, pass 1: other zips, pass 2: loose files */
    for (int pass = 0; pass < 3 && found != all; pass++)
    {
        for (int i = 0; i < s.count && found != all; i++)
        {
            candidate_t *c = &s.list[i];
            int is_set_zip = c->is_zip && ends_with_ci(c->name, setzip) && strlen(c->name) == strlen(setzip);
            if ((pass == 0 && !is_set_zip) || (pass == 1 && (!c->is_zip || is_set_zip)) || (pass == 2 && c->is_zip))
                continue;
            join(path, sizeof(path), dir, c->name);
            if (!path[0])
                continue;
            found = c->is_zip ? load_zip(g, regions, io, path, found)
                              : load_loose(g, regions, io, path, c->size, found);
        }
    }

    tmp_free(io, s.list);
    return found;
}

uint32_t gal_romload(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *dir,
                     uint32_t found)
{
    cur_regions = regions;
    found = scan_dir(g, regions, io, dir, found);
    if (found != all_files(g))
    {
        /* unzipping a set commonly puts its files in a folder named after it */
        char sub[160];
        join(sub, sizeof(sub), dir, g->mameset);
        if (sub[0])
            found = scan_dir(g, regions, io, sub, found);
    }
    return found;
}
