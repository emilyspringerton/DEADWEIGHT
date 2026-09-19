/* Offline checks for the synthesized sound design (apps/gui/sfx.c, built without SDL): every cue renders, is audible,
 * never clips, decays to silence inside its time budget; resource one-shots stay <= 0.6 s; the voice cap holds and
 * evicts by priority; the pitch-climb and sequencing helpers behave. Writes WAVs to $DW_SFX_WAV_DIR when set (for listening). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../apps/gui/sfx.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *NAME[SFX_COUNT] = {"blitz", "blitz_crit", "block", "block_crit", "bypass_lock", "bypass_sabotage", "bypass_flank", "bypass_scan", "bypass_siphon", "bypass_crit",
    "mirror_offense", "mirror_operations", "mirror_defense", "unopposed", "hold", "cancelled", "immune", "lifeline", "swap", "flip",
    "hull_hit", "heal", "armor_gain", "armor_loss", "energy_gain", "credits_gain", "resource_loss", "siphon_stream", "overflow",
    "burn_start", "burn_tick", "emp", "heartbeat", "redline_alarm", "ui_lock", "ui_tick"};

static int16_t buf[SFX_RATE * 4 * 2];

static int render_cue(SfxCue c, SfxOpts o, float *peak, float *rms, int *last) {
    sfx_offline_begin(); sfx_play(c, o);
    int frames = SFX_RATE * 4; sfx_render(buf, frames);
    sfx_stats(buf, frames, 0.004f, peak, rms, last);
    int active = sfx_active_voices();
    sfx_offline_end();
    return active;
}

int main(void) {
    const char *wav = getenv("DW_SFX_WAV_DIR");
    for (int c = 0; c < SFX_COUNT; c++) {
        SfxOpts o = {0, 0, 0, 1.0f, 0}; float pk, rms; int last;
        int active = render_cue((SfxCue)c, o, &pk, &rms, &last);
        float secs = last < 0 ? 0 : (float)last / SFX_RATE;
        CHECK(last >= 0);                          /* audible */
        int ambience = c == SFX_HOLD || c == SFX_BURN_TICK || c == SFX_UI_TICK || c == SFX_UI_LOCK || c == SFX_REDLINE_ALARM || c == SFX_FLIP;
        CHECK(pk > (ambience ? 0.02f : 0.08f));    /* audible; ambience is deliberately quiet */
        CHECK(pk <= 0.99f);                        /* soft limiter: no digital clipping */
        CHECK(active == 0);                        /* every voice finished inside 4 s */
        if (c <= SFX_SWAP) CHECK(secs <= 2.2f);    /* clash stingers: within the animation's clash window */
        if (c >= SFX_HULL_HIT && c <= SFX_OVERFLOW) CHECK(secs <= 0.6f);   /* resource one-shots <= 0.6 s */
        if (c == SFX_BURN_TICK || c == SFX_HEARTBEAT) CHECK(secs <= 1.3f);
        if (getenv("DW_SFX_VERBOSE")) printf("%-18s peak %.2f rms %.3f length %.2fs\n", NAME[c], pk, rms, secs);
        if (wav) { char p[512]; snprintf(p, sizeof p, "%s/%s.wav", wav, NAME[c]); sfx_write_wav(p, buf, (last < 0 ? 1 : last + SFX_RATE / 20)); }
    }
    /* clash is the loudest event: its peak is at least as loud as any resource one-shot */
    { float pk_clash = 0, pk_res = 0, r, d; int l; SfxOpts o = {0, 0, 0, 1.0f, 0};
      for (int c = SFX_BLITZ; c <= SFX_SWAP; c++) { render_cue((SfxCue)c, o, &r, &d, &l); if (r > pk_clash) pk_clash = r; }
      for (int c = SFX_HULL_HIT; c <= SFX_OVERFLOW; c++) { render_cue((SfxCue)c, o, &r, &d, &l); if (r > pk_res) pk_res = r; }
      CHECK(pk_clash >= pk_res); }
    /* hull damage scales with intensity */
    { SfxOpts lo = {0, 0, 0, 0.4f, 0}, hi = {0, 0, 0, 1.4f, 0}; float pl, ph, d; int l;
      render_cue(SFX_HULL_HIT, lo, &pl, &d, &l); render_cue(SFX_HULL_HIT, hi, &ph, &d, &l); CHECK(ph > pl); }
    /* pitch climb: SFX_UI_TICK is a pure ~1480 Hz sine; +7 semitones (a perfect fifth) must raise its frequency by 2^(7/12) = 1.498 */
    { SfxOpts root = {0, 0, 0, 1.0f, 0}, fifth = {0, 0, 7, 1.0f, 0}; int zc[2];
      for (int k = 0; k < 2; k++) {
          sfx_offline_begin(); sfx_play(SFX_UI_TICK, k ? fifth : root); sfx_render(buf, SFX_RATE / 40);
          zc[k] = 0; for (int i = 1; i < SFX_RATE / 40; i++) if ((buf[2 * (i - 1)] < 0) != (buf[2 * i] < 0)) zc[k]++;
          sfx_offline_end();
      }
      double ratio = (double)zc[1] / (zc[0] ? zc[0] : 1); CHECK(ratio > 1.45 && ratio < 1.55); }
    /* EMP muffling really removes the highs: muffled cue has far less energy in the top band than the clean one */
    { SfxOpts clean = {0, 0, 0, 1.0f, 0}, muff = {0, 0, 0, 1.0f, 1}; sfx_offline_begin(); sfx_play(SFX_ARMOR_GAIN, clean); sfx_render(buf, SFX_RATE / 2);
      double hi_clean = 0, hi_muff = 0;
      for (int i = 1; i < SFX_RATE / 2; i++) { double dd = (buf[2 * i] - buf[2 * (i - 1)]) / 32768.0; hi_clean += dd * dd; }
      sfx_offline_end(); sfx_offline_begin(); sfx_play(SFX_ARMOR_GAIN, muff); sfx_render(buf, SFX_RATE / 2);
      for (int i = 1; i < SFX_RATE / 2; i++) { double dd = (buf[2 * i] - buf[2 * (i - 1)]) / 32768.0; hi_muff += dd * dd; }
      sfx_offline_end(); CHECK(hi_muff < hi_clean * 0.5); }
    /* voice cap + priority: flood with ambience, a clash must still get voices (steals from lower priority) */
    { sfx_offline_begin(); for (int i = 0; i < 30; i++) sfx_play_simple(SFX_BURN_TICK);
      CHECK(sfx_active_voices() <= SFX_MAX_VOICES);
      sfx_play_simple(SFX_BLITZ_CRIT); sfx_render(buf, SFX_RATE / 10);
      float pk; int l; sfx_stats(buf, SFX_RATE / 10, 0.004f, &pk, NULL, &l); CHECK(pk > 0.3f);   /* the clash cut through */
      sfx_offline_end(); }
    /* a full sequenced resolution (clash, gap, hull, armor, energy, credits) finishes well inside the 9 s phase and never clips */
    { sfx_offline_begin();
      SfxOpts o = {0, 0, 0, 1.0f, 0}; sfx_play(SFX_BLITZ, o);
      SfxOpts h = {2400, 0, 0, 1.0f, 0}; sfx_play(SFX_HULL_HIT, h);
      SfxOpts a1 = {3300, -0.5f, 0, 1.0f, 0}, a2 = {3450, 0.5f, 7, 1.0f, 0}; sfx_play(SFX_ARMOR_GAIN, a1); sfx_play(SFX_ARMOR_GAIN, a2);   /* both gain armor: a climbing fifth */
      SfxOpts e = {4300, 0, 0, 1.0f, 0}; sfx_play(SFX_ENERGY_GAIN, e); SfxOpts cr = {4900, 0, 0, 1.0f, 0}; sfx_play(SFX_CREDITS_GAIN, cr);
      int frames = SFX_RATE * 9; static int16_t big[SFX_RATE * 9 * 2]; sfx_render(big, frames);
      float pk, rms; int last; sfx_stats(big, frames, 0.004f, &pk, &rms, &last);
      CHECK(pk <= 0.99f); CHECK(last > 0 && last < SFX_RATE * 6); CHECK(sfx_active_voices() == 0);
      if (wav) { char p[512]; snprintf(p, sizeof p, "%s/sequence_blitz.wav", wav); sfx_write_wav(p, big, last + SFX_RATE / 10); }
      sfx_offline_end(); }
    printf("test_sfx: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
