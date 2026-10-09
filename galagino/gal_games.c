/*
 * The ROM sets of the six Galagino games: the files upstream's romconv reads
 * (roms/README.md there), identified by MAME's CRC32 and size. The names are
 * for messages only; the loader matches by CRC, so a set is found whatever the
 * files are called and wherever they sit in a zip (Pac-Man's PROMs are
 * pm1-*.* in MAME's puckman parent set, for instance).
 *
 * Each file goes to a region of the game at an offset. Regions in a game's
 * temp mask are only needed while its data is converted: Pac-Man keeps its
 * program ROM, Galaga its three, Donkey Kong both CPUs' ROMs, Frogger both,
 * Dig Dug its three and the playfield map, 1942 its banked program and sound
 * ROMs.
 */
#include "gal_internal.h"

/* MAME pacman (Midway), a clone of puckman. */
static const gal_romfile_t pacman_files[] = {
    {"pacman.6e", 0xc1e6ab10, 0x1000, 0, 0x0000},
    {"pacman.6f", 0x1a6fb2d4, 0x1000, 0, 0x1000},
    {"pacman.6h", 0xbcdd1beb, 0x1000, 0, 0x2000},
    {"pacman.6j", 0x817d94e3, 0x1000, 0, 0x3000},
    {"pacman.5e", 0x0c944964, 0x1000, 1, 0x0000}, /* characters */
    {"pacman.5f", 0x958fedf9, 0x1000, 2, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 3, 0x0000}, /* palette */
    {"82s126.4a", 0x3eb3a8e4, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME galaga (Namco rev. B). */
static const gal_romfile_t galaga_files[] = {
    {"gg1_1b.3p", 0xab036c9f, 0x1000, 0, 0x0000},
    {"gg1_2b.3m", 0xd9232240, 0x1000, 0, 0x1000},
    {"gg1_3.2m", 0x753ce503, 0x1000, 0, 0x2000},
    {"gg1_4b.2l", 0x499fcc76, 0x1000, 0, 0x3000},
    {"gg1_5b.3f", 0xbb5caae3, 0x1000, 1, 0x0000},
    {"gg1_7b.2c", 0xd016686b, 0x1000, 2, 0x0000},
    {"gg1_9.4l", 0x58b2f47c, 0x1000, 3, 0x0000},  /* characters */
    {"gg1_11.4d", 0xad447c80, 0x1000, 4, 0x0000}, /* sprites */
    {"gg1_10.4f", 0xdd6f1afc, 0x1000, 4, 0x1000},
    {"prom-5.5n", 0x54603c6b, 0x0020, 5, 0x0000}, /* palette */
    {"prom-4.2n", 0x59b6edab, 0x0100, 6, 0x0000}, /* character colour lookup */
    {"prom-3.1c", 0x4a04bb6b, 0x0100, 7, 0x0000}, /* sprite colour lookup */
    {"prom-1.1d", 0x7a2815b4, 0x0100, 8, 0x0000}, /* sound waveforms */
};

/* MAME dkong (US set 1). */
static const gal_romfile_t dkong_files[] = {
    {"c_5et_g.bin", 0xba70b88b, 0x1000, 0, 0x0000},
    {"c_5ct_g.bin", 0x5ec461ec, 0x1000, 0, 0x1000},
    {"c_5bt_g.bin", 0x1c97d324, 0x1000, 0, 0x2000},
    {"c_5at_g.bin", 0xb9005ac0, 0x1000, 0, 0x3000},
    {"s_3i_b.bin", 0x45a4ed06, 0x0800, 1, 0x0000}, /* sound CPU */
    {"s_3j_b.bin", 0x4743fe92, 0x0800, 1, 0x0800},
    {"v_5h_b.bin", 0x12c8c95d, 0x0800, 2, 0x0000}, /* characters */
    {"v_3pt.bin", 0x15e9c5e9, 0x0800, 2, 0x0800},
    {"l_4m_b.bin", 0x59f8054d, 0x0800, 3, 0x0000}, /* sprites */
    {"l_4n_b.bin", 0x672e4714, 0x0800, 3, 0x0800},
    {"l_4r_b.bin", 0xfeaa59ee, 0x0800, 3, 0x1000},
    {"l_4s_b.bin", 0x20f2ef7e, 0x0800, 3, 0x1800},
    {"c-2k.bpr", 0xe273ede5, 0x0100, 4, 0x0000}, /* palette, low nibble */
    {"c-2j.bpr", 0xd6412358, 0x0100, 5, 0x0000}, /* palette, high nibble */
    {"v-5e.bpr", 0xb869b8f5, 0x0100, 6, 0x0000}, /* character colour lookup */
};

/* MAME frogger. */
static const gal_romfile_t frogger_files[] = {
    {"frogger.26", 0x597696d6, 0x1000, 0, 0x0000},
    {"frogger.27", 0xb6e6fcc3, 0x1000, 0, 0x1000},
    {"frsm3.7", 0xaca22ae0, 0x1000, 0, 0x2000},
    {"frogger.608", 0xe8ab0256, 0x0800, 1, 0x0000}, /* sound CPU */
    {"frogger.609", 0x7380a48f, 0x0800, 1, 0x0800},
    {"frogger.610", 0x31d7eb27, 0x0800, 1, 0x1000},
    {"frogger.606", 0xf524ee30, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"frogger.607", 0x05f7d883, 0x0800, 2, 0x0800},
    {"pr-91.6l", 0x413703bf, 0x0020, 3, 0x0000},    /* palette */
};

/* MAME digdug (rev 2). */
static const gal_romfile_t digdug_files[] = {
    {"dd1a.1", 0xa80ec984, 0x1000, 0, 0x0000},
    {"dd1a.2", 0x559f00bd, 0x1000, 0, 0x1000},
    {"dd1a.3", 0x8cbc6fe1, 0x1000, 0, 0x2000},
    {"dd1a.4", 0xd066f830, 0x1000, 0, 0x3000},
    {"dd1a.5", 0x6687933b, 0x1000, 1, 0x0000},
    {"dd1a.6", 0x843d857f, 0x1000, 1, 0x1000},
    {"dd1.7", 0xa41bce72, 0x1000, 2, 0x0000},
    {"dd1.9", 0xf14a6fe1, 0x0800, 3, 0x0000},      /* characters */
    {"dd1.10b", 0x2cf399c2, 0x1000, 4, 0x0000},    /* playfield map */
    {"dd1.11", 0x7b383983, 0x1000, 5, 0x0000},     /* playfield tiles */
    {"dd1.15", 0xe22957c8, 0x1000, 6, 0x0000},     /* sprites */
    {"dd1.14", 0x2829ec99, 0x1000, 6, 0x1000},
    {"dd1.13", 0x458499e9, 0x1000, 6, 0x2000},
    {"dd1.12", 0xc58252a0, 0x1000, 6, 0x3000},
    {"136007.113", 0x4cb9da99, 0x0020, 7, 0x0000}, /* palette */
    {"136007.111", 0x00c7c419, 0x0100, 8, 0x0000}, /* sprite colour lookup */
    {"136007.112", 0xe9b3e08e, 0x0100, 9, 0x0000}, /* playfield colour lookup */
    {"136007.110", 0x7a2815b4, 0x0100, 10, 0x0000}, /* sound waveforms */
    {"136007.109", 0x77245b66, 0x0100, 10, 0x0100},
};

/* MAME 1942 (rev. B). */
static const gal_romfile_t g1942_files[] = {
    {"srb-03.m3", 0xd9dafcc3, 0x4000, 0, 0x0000},
    {"srb-04.m4", 0xda0cf924, 0x4000, 0, 0x4000},
    {"srb-05.m5", 0xd102911c, 0x4000, 1, 0x0000}, /* banked */
    {"srb-06.m6", 0x466f8248, 0x2000, 2, 0x0000}, /* 8 KB in a 16 KB bank, the rest reads 0 as in MAME */
    {"srb-07.m7", 0x0d31038c, 0x4000, 3, 0x0000},
    {"sr-01.c11", 0xbd87f06b, 0x4000, 4, 0x0000}, /* sound CPU */
    {"sr-02.f2", 0x6ebca191, 0x2000, 5, 0x0000},  /* characters */
    {"sr-08.a1", 0x3884d9eb, 0x2000, 6, 0x0000},  /* tiles */
    {"sr-09.a2", 0x999cf6e0, 0x2000, 6, 0x2000},
    {"sr-10.a3", 0x8edb273a, 0x2000, 6, 0x4000},
    {"sr-11.a4", 0x3a2726c3, 0x2000, 6, 0x6000},
    {"sr-12.a5", 0x1bd3d8bb, 0x2000, 6, 0x8000},
    {"sr-13.a6", 0x658f02c4, 0x2000, 6, 0xa000},
    {"sr-14.l1", 0x2528bec6, 0x4000, 7, 0x0000},  /* sprites */
    {"sr-15.l2", 0xf89287aa, 0x4000, 7, 0x4000},
    {"sr-16.n1", 0x024418f8, 0x4000, 7, 0x8000},
    {"sr-17.n2", 0xe2c7e489, 0x4000, 7, 0xc000},
    {"sb-5.e8", 0x93ab8153, 0x0100, 8, 0x0000},   /* palette red */
    {"sb-6.e9", 0x8ab44f7d, 0x0100, 8, 0x0100},   /* palette green */
    {"sb-7.e10", 0xf4ade9a4, 0x0100, 8, 0x0200},  /* palette blue */
    {"sb-0.f1", 0x6047d91b, 0x0100, 9, 0x0000},   /* character colour lookup */
    {"sb-4.d6", 0x4858968d, 0x0100, 10, 0x0000},  /* tile colour lookup */
    {"sb-3.d2", 0x3b0c99af, 0x0100, 10, 0x0100},
    {"sb-2.d1", 0x8bb8b3df, 0x0100, 10, 0x0200},
    {"sb-8.k3", 0xf6fad943, 0x0100, 11, 0x0000},  /* sprite colour lookup */
};

#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

const gal_game_info_t gal_games[GAL_GAME_COUNT] = {
    [GAL_GAME_PACMAN] = {"Pac-Man", "PACMAN", "puckman", pacman_files, N(pacman_files), 6, 0x3e,
                         {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_GALAGA] = {"Galaga", "GALAGA", "galaga", galaga_files, N(galaga_files), 9, 0x1f8,
                         {0x4000, 0x1000, 0x1000, 0x1000, 0x2000, 0x20, 0x100, 0x100, 0x100}},
    [GAL_GAME_DKONG] = {"Donkey Kong", "DONKEYKONG", "dkong", dkong_files, N(dkong_files), 7, 0x7c,
                        {0x4000, 0x1000, 0x1000, 0x2000, 0x100, 0x100, 0x100}},
    [GAL_GAME_FROGGER] = {"Frogger", "FROGGER", "frogger", frogger_files, N(frogger_files), 4, 0xc,
                          {0x3000, 0x1800, 0x1000, 0x20}},
    [GAL_GAME_DIGDUG] = {"Dig Dug", "DIGDUG", "digdug", digdug_files, N(digdug_files), 11, 0x7e8,
                         {0x4000, 0x2000, 0x1000, 0x800, 0x1000, 0x1000, 0x4000, 0x20, 0x100, 0x100, 0x200}},
    [GAL_GAME_1942] = {"1942", "1942", "1942", g1942_files, N(g1942_files), 12, 0xfe0,
                       {0x8000, 0x4000, 0x4000, 0x4000, 0x4000, 0x2000, 0xc000, 0x10000, 0x300, 0x100, 0x300, 0x100}},
};
