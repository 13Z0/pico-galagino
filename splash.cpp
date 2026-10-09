#include "menu.h"
#include "FrensHelpers.h"
#include <cstring>

// Called by menu.cpp. picoGalagino has no ROM browser, so this is only reached
// from the in-game settings menu path, but the symbol must exist either way.
//
// Note: putText collapses runs of whitespace into one cell, and renders '_' as
// a literal space - hence the underscores in the credit lines.
static int fgcolorSplash = DEFAULT_FGCOLOR;
static int bgcolorSplash = DEFAULT_BGCOLOR;

void splash()
{
    char s[SCREEN_COLS + 1];
    ClearScreen(bgcolorSplash);

    strcpy(s, "Pico-Galagino");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 2, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "Arcade emulator for RP2350");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 4, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "Based on Galagino");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 6, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Till Harbaum");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 7, s, CBLUE, bgcolorSplash);
    strcpy(s, "Z80 from MAME, J. Buchmueller");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 8, s, CBLUE, bgcolorSplash);

    strcpy(s, "Pico Port");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 10, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@frenskefrens");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 11, s, CBLUE, bgcolorSplash);

#if !HSTX
    strcpy(s, "DVI Support");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 13, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@shuichi_takano");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, CBLUE, bgcolorSplash);
#else
    strcpy(s, "HSTX video driver _ I2S audio___");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 13, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "__@fliperama86____@frenskefrens__");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, CBLUE, bgcolorSplash);
#endif

    strcpy(s, "(S)NES/WII controller support");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 16, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@PaintYourDragon @adafruit");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 17, s, CBLUE, bgcolorSplash);

    strcpy(s, "Pac-Man (c) 1980 Namco/Midway.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 19, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Galaga, Dig Dug (c) Namco.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 20, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Donkey Kong (c) 1981 Nintendo.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 21, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Frogger (c) 1981 Konami/Sega.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 22, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "1942 (c) 1984 Capcom.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 23, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Not affiliated with these companies.");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 24, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "https://github.com/");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 26, s, CBLUE, bgcolorSplash);
    strcpy(s, "PicoPlus-devel/pico-galagino");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 27, s, CBLUE, bgcolorSplash);
}
