/*
 * Sound, as upstream Galagino synthesises it (galagino.ino, snd_render_buffer
 * and audio_namco_waveregs_parse), at the board's sample rate instead of
 * Galagino's 24 kHz.
 *
 * Levels are Galagino's: the sum of the voices (about +/-512 at most) times
 * its MASTER_VOLUME of 64.
 */
#include "gal_internal.h"

#define MASTER_VOLUME 64

/* ---------------------------------------------------------------------------
 * Namco WSG: three voices of 32-step, 4-bit waveforms from the sound PROM.
 *
 * The chip runs at 96 kHz (3.072 MHz / 32): each step a voice adds its 20-bit
 * frequency to its accumulator and plays step (acc >> 15) & 31. Galagino at
 * 24 kHz adds the frequency once per sample and reads (cnt >> 13): four chip
 * steps per sample. Here the accumulator is kept in 1/256 chip steps, so any
 * sample rate works: acc += freq * (96000 * 256 / rate), step (acc >> 23) & 31.
 * ------------------------------------------------------------------------ */

static uint32_t wsg_acc[3];

void gal_wsg_reset(void)
{
    memset(wsg_acc, 0, sizeof(wsg_acc));
}

void GAL_HOT(gal_wsg_render)(const int8_t *waves, uint8_t wave_mask, int16_t *out, int n)
{
    const uint8_t *regs = gal.soundregs;
    const uint32_t step = (uint32_t)((96000u * 256u + gal.samplerate / 2) / gal.samplerate);

    uint32_t inc[3];
    int vol[3];
    const int8_t *wave[3];
    for (int ch = 0; ch < 3; ch++)
    {
        vol[ch] = regs[ch * 5 + 0x15];
        uint32_t freq = (ch == 0) ? regs[0x10] : 0;
        freq += regs[ch * 5 + 0x11] << 4;
        freq += regs[ch * 5 + 0x12] << 8;
        freq += regs[ch * 5 + 0x13] << 12;
        freq += regs[ch * 5 + 0x14] << 16;
        inc[ch] = freq * step;
        wave[ch] = waves + 32 * (regs[ch * 5 + 0x05] & wave_mask);
    }

    for (int i = 0; i < n; i++)
    {
        int v = 0;
        for (int ch = 0; ch < 3; ch++)
        {
            if (vol[ch])
                v += vol[ch] * wave[ch][(wsg_acc[ch] >> 23) & 0x1f];
            wsg_acc[ch] += inc[ch];
        }
        out[i] = (int16_t)(v * MASTER_VOLUME);
    }
}

/* ---------------------------------------------------------------------------
 * Sample voices: Galaga's explosion and Donkey Kong's walk, jump and stomp,
 * recorded at their own rates and stepped through at the output rate.
 * Galagino adds them unscaled (Galaga) or shifted down (Donkey Kong) to the
 * voice sum before MASTER_VOLUME.
 * ------------------------------------------------------------------------ */

void gal_voice_start(gal_voice_t *v, const int8_t *data, uint32_t len, int rate, int shift)
{
    v->data = data;
    v->len = len << 16;
    v->pos = 0;
    v->step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)gal.samplerate);
    v->shift = (uint8_t)shift;
}

void GAL_HOT(gal_voice_mix)(gal_voice_t *v, int16_t *out, int n)
{
    if (!v->data)
        return;
    for (int i = 0; i < n; i++)
    {
        if (v->pos >= v->len)
        {
            v->data = NULL;
            return;
        }
        int s = out[i] + (v->data[v->pos >> 16] >> v->shift) * MASTER_VOLUME;
        out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
        v->pos += v->step;
    }
}

/* ---------------------------------------------------------------------------
 * AY-3-8910 (Frogger: one, 1942: two), as Galagino synthesises it: tone and
 * noise, linear volume, no envelopes. The registers of chip k are
 * soundregs[16 * k ...].
 *
 * The tone counters run at clock / 8 and flip the output after `period`
 * counts. Galagino advances them by clock / 8 / 24000 per sample, rounded
 * (9 for Frogger, 8 for 1942); here the step is kept in 1/256 counts.
 * ------------------------------------------------------------------------ */

static struct
{
    int32_t cnt[2][4];
    int8_t toggle[2][3];
    uint32_t rng[2];
} ay;

void gal_ay_reset(void)
{
    memset(&ay, 0, sizeof(ay));
    for (int k = 0; k < 2; k++)
    {
        ay.rng[k] = 1;
        for (int c = 0; c < 3; c++)
            ay.toggle[k][c] = 1;
    }
}

void GAL_HOT(gal_ay_render)(int chips, int clock, int ay_vol, int16_t *out, int n)
{
    const int32_t inc = (int32_t)((((int64_t)clock / 8) * 256 + gal.samplerate / 2) / gal.samplerate);
    int32_t period[2][4];
    int volume[2][3], enable[2][3];
    for (int k = 0; k < chips; k++)
    {
        const uint8_t *r = gal.soundregs + 16 * k;
        for (int c = 0; c < 3; c++)
        {
            period[k][c] = (r[2 * c] + 256 * (r[2 * c + 1] & 15)) << 8;
            /* bit 0 tone, bit 1 noise; the mixer register enables them with a 0 */
            enable[k][c] = (((r[7] >> c) & 1) | ((r[7] >> (c + 2)) & 2)) ^ 3;
            volume[k][c] = r[8 + c] & 0x0f;
        }
        period[k][3] = (r[6] & 0x1f) << 8;
    }

    for (int i = 0; i < n; i++)
    {
        int v = 0;
        for (int k = 0; k < chips; k++)
        {
            if (period[k][3])
            {
                ay.cnt[k][3] += inc;
                while (ay.cnt[k][3] >= period[k][3])
                {
                    ay.cnt[k][3] -= period[k][3];
                    ay.rng[k] ^= (((ay.rng[k] & 1) ^ ((ay.rng[k] >> 3) & 1)) << 17);
                    ay.rng[k] >>= 1;
                }
            }
            for (int c = 0; c < 3; c++)
            {
                /* a channel sounds if period != 0, volume != 0 and it is enabled */
                if (!period[k][c] || !volume[k][c] || !enable[k][c])
                    continue;
                int bit = 1;
                if (enable[k][c] & 1)
                    bit &= ay.toggle[k][c] > 0;
                if (enable[k][c] & 2)
                    bit &= ay.rng[k] & 1;
                v += ay_vol * (bit ? 1 : -1) * volume[k][c];

                ay.cnt[k][c] += inc;
                while (ay.cnt[k][c] >= period[k][c])
                {
                    ay.cnt[k][c] -= period[k][c];
                    ay.toggle[k][c] = (int8_t)-ay.toggle[k][c];
                }
            }
        }
        out[i] = (int16_t)(v * MASTER_VOLUME);
    }
}
