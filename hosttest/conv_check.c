/*
 * Dumps the converted data of every game whose ROM set is found, for
 * hosttest/conv_check.py, which compares it with the output of upstream
 * Galagino's romconv scripts.
 *
 *   ./hosttest/conv_check <romroot> <dumpdir>
 *
 * Each table goes to <dumpdir>/<name>.bin, named after the array upstream
 * writes. Built by hosttest/build.sh with -DGAL_CONV_CHECK.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gal_internal.h"

static int host_list_dir(void *ctx, const char *dir, gal_dir_cb cb, void *arg)
{
    (void)ctx;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        cb(arg, e->d_name, (uint32_t)st.st_size, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return 1;
}

static void *host_open(void *ctx, const char *path)
{
    (void)ctx;
    return fopen(path, "rb");
}

static uint32_t host_size(void *ctx, void *f)
{
    (void)ctx;
    long cur = ftell((FILE *)f);
    fseek((FILE *)f, 0, SEEK_END);
    long n = ftell((FILE *)f);
    fseek((FILE *)f, cur, SEEK_SET);
    return (uint32_t)n;
}

static int host_read_at(void *ctx, void *f, uint32_t ofs, void *buf, uint32_t len)
{
    (void)ctx;
    if (fseek((FILE *)f, ofs, SEEK_SET) != 0)
        return -1;
    return (int)fread(buf, 1, len, (FILE *)f);
}

static void host_close(void *ctx, void *f)
{
    (void)ctx;
    fclose((FILE *)f);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <romroot> <dumpdir>\n", argv[0]);
        return 1;
    }
    const gal_io_t io = {NULL, host_list_dir, host_open, host_size, host_read_at, host_close};
    gal_init(GAL_FMT_RGB565, 44100);
    gal_set_psram(malloc(GAL_PSRAM_SIZE), GAL_PSRAM_SIZE);
    mkdir(argv[2], 0755);

    int failed = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/%s", argv[1], gal_games[g].folder);
        gal_start((gal_game_id_t)g, &io, dir);
        if (gal_running() != g)
        {
            printf("%-12s not started, skipped\n", gal_games[g].title);
            continue;
        }
        gal_table_t t[GAL_MAX_TABLES];
        const int n = gal_tables(t);
        for (int i = 0; i < n; i++)
        {
            char path[1100];
            snprintf(path, sizeof(path), "%s/%s.bin", argv[2], t[i].name);
            FILE *f = fopen(path, "wb");
            if (!f || fwrite(t[i].data, 1, t[i].bytes, f) != t[i].bytes)
                failed = 1;
            if (f)
                fclose(f);
        }
        printf("%-12s %d tables\n", gal_games[g].title, n);
        gal_stop();
    }
    return failed;
}
