/* Standalone regression: cc -Iport tests/voice_inject_cache.c -o /tmp/voice-cache && /tmp/voice-cache */
#include <assert.h>
#include "../port/audio/voice_inject.c"

void port_log(const char *fmt, ...) { (void)fmt; }
FILE *port_fopen_staged(const char *path, const char *mode)
{
    /* Every logical roster path returns the same valid little-endian PCM WAV. */
    static const unsigned char wave[] = {
        'R','I','F','F',40,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,1,0,0,125,0,0,0,250,0,0,2,0,16,0,
        'd','a','t','a',4,0,0,0,0xe8,3,0xd0,7
    };
    FILE *f = tmpfile();
    (void)path; (void)mode;
    assert(f != NULL);
    assert(fwrite(wave, 1, sizeof wave, f) == sizeof wave);
    rewind(f);
    return f;
}

int main(void)
{
    int i;
    char path[64];
    for (i = 0; i < 80; i++)
    {
        short mixed[4] = {0};
        snprintf(path, sizeof path, "characters/fighter%d/voice.wav", i);
        portVoiceInjectPlayPath(path);
        assert(sActive != NULL && strcmp(sActive->path, path) == 0);
        assert(portVoiceInjectPlaying());
        portVoiceInjectMix(mixed, 2);
        assert(mixed[0] == 1000 && mixed[1] == 1000);
        assert(mixed[2] == 2000 && mixed[3] == 2000);
    }
    assert(sNClips == MAX_CLIPS);
    /* An active slot remains valid while other paths churn through the cache. */
    portVoiceInjectPlayPath("characters/held/voice.wav");
    for (i = 0; i < 40; i++)
    {
        VoiceClip *candidate;
        snprintf(path, sizeof path, "characters/prefetch%d/voice.wav", i);
        candidate = clip_for_path(path);
        assert(candidate != sActive);
        assert(load_clip(candidate));
        assert(strcmp(sActive->path, "characters/held/voice.wav") == 0);
        assert(sActive->pcm[0] == 1000);
    }
    /* Revisiting an evicted fighter reloads it normally. */
    portVoiceInjectPlayPath("characters/fighter0/voice.wav");
    assert(strcmp(sActive->path, "characters/fighter0/voice.wav") == 0);
    for (i = 0; i < sNClips; i++) free(sClips[i].pcm);
    puts("PASS: announcer cache plays beyond 16 characters and preserves active audio");
    return 0;
}
