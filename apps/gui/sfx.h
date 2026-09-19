/* sfx -- DEADWEIGHT's synthesized, MIDI-style sound design for the SDL2 client. No sample files, no SDL_mixer, no soundfonts:
 * a small software synth (sine/square/saw/triangle/noise voices with ADSR, pitch glide, one-pole low-pass and pan) mixed
 * into one stereo stream. Cues are tuned so the tactical triangle is understandable by ear alone:
 *   Red (Offense)  heavy percussion + noise         Blue (Defense)  pads, crystalline chimes, reverse-cymbal swell
 *   Yellow (Operations)  quick square-wave arpeggios and data blips (each keyword has its own signature)
 * The same mixer renders to a buffer (sfx_render) so the sequencing is unit-testable and can be dumped to WAV without a
 * sound card. Not thread-safe by itself: the SDL audio callback and every sfx_* call are serialized by sfx's device lock. */
#ifndef DW_SFX_H
#define DW_SFX_H
#include <stdint.h>

#define SFX_RATE 44100
#define SFX_MAX_VOICES 20

typedef enum {
    /* clash stingers (always the loudest events) */
    SFX_BLITZ, SFX_BLITZ_CRIT, SFX_BLOCK, SFX_BLOCK_CRIT,
    SFX_BYPASS_LOCK, SFX_BYPASS_SABOTAGE, SFX_BYPASS_FLANK, SFX_BYPASS_SCAN, SFX_BYPASS_SIPHON, SFX_BYPASS_CRIT,
    SFX_MIRROR_OFFENSE, SFX_MIRROR_OPERATIONS, SFX_MIRROR_DEFENSE, SFX_UNOPPOSED, SFX_HOLD, SFX_CANCELLED, SFX_IMMUNE, SFX_LIFELINE, SFX_SWAP,
    SFX_FLIP,
    /* resources */
    SFX_HULL_HIT, SFX_HEAL, SFX_ARMOR_GAIN, SFX_ARMOR_LOSS, SFX_ENERGY_GAIN, SFX_CREDITS_GAIN, SFX_RESOURCE_LOSS, SFX_SIPHON_STREAM, SFX_OVERFLOW,
    /* statuses and ambience */
    SFX_BURN_START, SFX_BURN_TICK, SFX_EMP, SFX_HEARTBEAT, SFX_REDLINE_ALARM, SFX_UI_LOCK, SFX_UI_TICK, SFX_COUNT
} SfxCue;

/* Priorities decide which voices survive when the voice cap is hit: clash > hull > armor > energy/credits > ambience. */
enum { SFX_PRIO_AMBIENCE = 0, SFX_PRIO_ECON = 1, SFX_PRIO_ARMOR = 2, SFX_PRIO_HULL = 3, SFX_PRIO_CLASH = 4 };

typedef struct {
    float delay_ms;      /* start offset from now */
    float pan;           /* -1 (left, "you") .. +1 (right, "opponent") */
    float pitch_st;      /* semitones (a perfect fifth is 7): the pitch-climb tool */
    float intensity;     /* 0..1.5: scales volume/weight (e.g. damage size); 1 = normal */
    int muffle;          /* EMP: low-pass everything this cue plays */
} SfxOpts;

int  sfx_open(void);                        /* opens the default SDL audio device; 0 = ok, nonzero = silent (the game still runs) */
void sfx_close(void);
void sfx_set_muted(int muted);
int  sfx_is_muted(void);
int  sfx_is_open(void);
void sfx_play(SfxCue cue, SfxOpts o);       /* schedules a cue (no-op when there is no device and no offline render in progress) */
void sfx_play_simple(SfxCue cue);           /* delay 0, centre, no pitch shift, intensity 1 */
void sfx_stop_all(void);
int  sfx_active_voices(void);

/* offline rendering: begin -> sfx_play(...) -> render N seconds of stereo int16 (interleaved) -> end */
void sfx_offline_begin(void);
int  sfx_render(int16_t *out, int frames);  /* mixes `frames` stereo frames, advancing the synth; returns frames */
void sfx_offline_end(void);
int  sfx_write_wav(const char *path, const int16_t *stereo, int frames);
/* statistics over a rendered buffer: peak (0..1), rms (0..1), index of the last frame louder than `thresh` */
void sfx_stats(const int16_t *stereo, int frames, float thresh, float *peak, float *rms, int *last_loud);
#endif
