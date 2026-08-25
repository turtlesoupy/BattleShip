// SPDX-License-Identifier: MIT
//
// Injected announcer voice — see voice_inject.h.
//
// The clip is a RIFF/WAVE file, PCM s16, mono or stereo, any sample rate
// (resampled linearly to the game's 32 kHz output on load). The pipeline's
// generate_announcer.py emits mono 32 kHz s16, which passes through as-is.

#include "voice_inject.h"
#include "port_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_RATE 32000

static short *sPcm = NULL;     /* mono s16 at OUTPUT_RATE */
static int    sLen = 0;        /* samples in sPcm */
static int    sCursor = -1;    /* -1 = idle */
static int    sLoadState = 0;  /* 0 = not tried, 1 = ok, -1 = failed */

static unsigned rd_u32le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned rd_u16le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static int load_clip(void)
{
    const char *path = getenv("SSB64_INJECT_VOICE");
    unsigned char *raw = NULL;
    long raw_len;
    FILE *f;
    unsigned pos, fmt_tag = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char *data = NULL;
    unsigned data_len = 0;

    if (sLoadState != 0) return sLoadState > 0;
    sLoadState = -1;
    if (path == NULL) return 0;

    f = fopen(path, "rb");
    if (f == NULL)
    {
        port_log("VOICE: cannot open %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    raw_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (raw_len < 44 || raw_len > 64 * 1024 * 1024 ||
        (raw = (unsigned char *)malloc((size_t)raw_len)) == NULL ||
        fread(raw, 1, (size_t)raw_len, f) != (size_t)raw_len)
    {
        port_log("VOICE: cannot read %s\n", path);
        fclose(f);
        free(raw);
        return 0;
    }
    fclose(f);

    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0)
    {
        port_log("VOICE: %s is not a RIFF/WAVE file\n", path);
        free(raw);
        return 0;
    }
    for (pos = 12; pos + 8 <= (unsigned)raw_len;)
    {
        unsigned chunk_len = rd_u32le(raw + pos + 4);
        if (chunk_len > (unsigned)raw_len - pos - 8) chunk_len = (unsigned)raw_len - pos - 8;
        if (memcmp(raw + pos, "fmt ", 4) == 0 && chunk_len >= 16)
        {
            fmt_tag  = rd_u16le(raw + pos + 8);
            channels = rd_u16le(raw + pos + 10);
            rate     = rd_u32le(raw + pos + 12);
            bits     = rd_u16le(raw + pos + 22);
        }
        else if (memcmp(raw + pos, "data", 4) == 0)
        {
            data = raw + pos + 8;
            data_len = chunk_len;
        }
        pos += 8 + chunk_len + (chunk_len & 1);
    }
    if (fmt_tag != 1 || bits != 16 || (channels != 1 && channels != 2) ||
        rate == 0 || data == NULL || data_len < 2)
    {
        port_log("VOICE: %s unsupported (want PCM s16 mono/stereo; fmt=%u ch=%u bits=%u)\n",
                 path, fmt_tag, channels, bits);
        free(raw);
        return 0;
    }

    {
        unsigned src_len = data_len / (2 * channels);
        unsigned out_len = (rate == OUTPUT_RATE)
                               ? src_len
                               : (unsigned)((unsigned long long)src_len * OUTPUT_RATE / rate);
        unsigned i;
        short *out = (short *)malloc((size_t)out_len * 2);
        if (out == NULL || out_len == 0)
        {
            free(raw);
            free(out);
            return 0;
        }
        for (i = 0; i < out_len; i++)
        {
            /* 16.16 fixed-point source position, linear interpolation */
            unsigned long long fp = (rate == OUTPUT_RATE)
                                        ? ((unsigned long long)i << 16)
                                        : ((unsigned long long)i * rate / OUTPUT_RATE * 65536ull +
                                           ((unsigned long long)i * rate % OUTPUT_RATE) * 65536ull / OUTPUT_RATE);
            unsigned s0 = (unsigned)(fp >> 16);
            unsigned frac = (unsigned)(fp & 0xFFFF);
            unsigned s1 = (s0 + 1 < src_len) ? s0 + 1 : s0;
            long a, b;
            if (s0 >= src_len) s0 = s1 = src_len - 1;
            if (channels == 1)
            {
                a = (short)rd_u16le(data + s0 * 2);
                b = (short)rd_u16le(data + s1 * 2);
            }
            else
            {
                a = ((short)rd_u16le(data + s0 * 4) + (short)rd_u16le(data + s0 * 4 + 2)) / 2;
                b = ((short)rd_u16le(data + s1 * 4) + (short)rd_u16le(data + s1 * 4 + 2)) / 2;
            }
            out[i] = (short)(a + (((b - a) * (long)frac) >> 16));
        }
        sPcm = out;
        sLen = (int)out_len;
    }
    free(raw);
    sLoadState = 1;
    port_log("VOICE: loaded %s (%d samples @ 32 kHz, %.2fs)\n", path, sLen, sLen / 32000.0);
    return 1;
}

int portVoiceInjectAvailable(void)
{
    return getenv("SSB64_INJECT_VOICE") != NULL;
}

int portVoiceInjectPlaying(void)
{
    return sCursor >= 0 && sPcm != NULL;
}

void portVoiceInjectPlay(void)
{
    if (!load_clip()) return;
    sCursor = 0;
    port_log("VOICE: play\n");
}

void portVoiceInjectStop(void)
{
    sCursor = -1;
}

int portVoiceInjectDurationTics(void)
{
    if (!load_clip()) return 0;
    return (sLen * 60 + OUTPUT_RATE - 1) / OUTPUT_RATE;
}

void portVoiceInjectMix(short *stereo, int sampleCount)
{
    int i;
    if (sCursor < 0 || sPcm == NULL) return;
    for (i = 0; i < sampleCount && sCursor < sLen; i++, sCursor++)
    {
        int v = sPcm[sCursor];
        int l = stereo[i * 2] + v;
        int r = stereo[i * 2 + 1] + v;
        stereo[i * 2]     = (short)(l < -32768 ? -32768 : (l > 32767 ? 32767 : l));
        stereo[i * 2 + 1] = (short)(r < -32768 ? -32768 : (r > 32767 ? 32767 : r));
    }
    if (sCursor >= sLen) sCursor = -1;
}
