#pragma once

// Injected announcer voice — plays a PCM WAV (SSB64_INJECT_VOICE) mixed
// directly into the port's audio output, bypassing the N64 FGM engine.
// Used by the character-injection harness so a generated fighter gets its
// own announcer name clip in place of the replaced fighter's.

#ifdef __cplusplus
extern "C" {
#endif

// Nonzero when SSB64_INJECT_VOICE names a clip (whether or not it is
// currently audible).
int portVoiceInjectAvailable(void);

// Nonzero while a clip is actively playing (mix pass needed this frame).
int portVoiceInjectPlaying(void);

// (Re)start the clip from the beginning. Lazy-loads and caches the WAV on
// first call; logs and no-ops if the file is missing or malformed.
void portVoiceInjectPlay(void);

// Same, for an explicit WAV path — a roster of injected characters plays a
// different announcer line per fighter kind. Clips are cached per path.
void portVoiceInjectPlayPath(const char *path);

// Cut the clip (a different vanilla announcer name superseded it).
void portVoiceInjectStop(void);

// Add the clip's next sampleCount mono samples onto an interleaved stereo
// s16 buffer (both channels), saturating. Advances the play cursor.
void portVoiceInjectMix(short *stereo, int sampleCount);

// Clip length in 60 Hz tics (0 if not loadable). Lets game code stretch
// hardcoded announcer waits that assume the short vanilla clips.
int portVoiceInjectDurationTics(void);

#ifdef __cplusplus
}
#endif
