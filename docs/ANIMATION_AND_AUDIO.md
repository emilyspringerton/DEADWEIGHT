# Round-resolution animation and audio (SDL2 client)

Implemented in `apps/gui/fx.[ch]` (animation) and `apps/gui/sfx.[ch]` (a small software synth; no sample files, no SDL_mixer).
Every finished round becomes one timeline of **at most 9 s (real worst case in the demo set: 5.3 s)** in the arena band of the match
screen. The hand stays playable while it runs, and the next round's timer is unaffected. The final round of a match finishes its
animation before the result screen appears. Both systems are driven by the same clock: the audio for each beat is scheduled when the
timeline starts.

## Timeline

| Phase | Time | What happens |
|---|---|---|
| Setup | 0-0.6 s | Both cards flip together (coloured edge mid-flip), a scale punch, a little screen shake; two ship silhouettes appear facing each other, accented in the card colours. |
| Clash | 1.0-2.2 s | One of the scenarios below. |
| Resources | strictly after the clash, never overlapping it | Hull, then Armor, then Energy/Credits, then status changes (0.75-0.95 s each; only the stages that changed play). Meters on screen lag the server until their stage, then tween (<= 0.7 s, eased). |
| Settle | 0.3 s | The small cards grow back into the normal reveal layout. |

## The tactical triangle as animation

| Outcome | Scenario | What you see | What you hear |
|---|---|---|---|
| Offense beats Operations | **The Blitz** | Red ship fires a five-missile volley; the Operations ship is mid-operation (radar dish, spooling ring, data lattice) and the missiles shatter it into shards; hull flash, recoil, its glow dies; "INTERRUPT". | Heavy kick + noise burst + metallic hit; a second smaller hit. |
| Defense beats Offense | **The Block** | A heavy beam meets a hex shield that expands with a shimmer; impact flare and absorption ripples; if damage came back, a thinner blue return beam and scorch on the attacker; "ABSORBED" or "REFLECTED". | Pad chord swell, reverse-cymbal rise into the absorb thump, crystalline chimes. |
| Operations beats Defense | **The Bypass** | The Defense shield deploys, then the keyword variant below undermines it without a frontal hit. | Rapid ascending square-wave arpeggio / data blips that resolve on a high note; each keyword has its own signature. |

Bypass variants (chosen by the winning card's keyword, so every Operations card looks like what it says):

| Keyword | Animation |
|---|---|
| **Lock** | Targeting brackets close in, scanning beams paint the ship, a solid LOCKED reticle snaps over the shield, which is greyed as compromised. |
| **Sabotage** | A thin data spike enters a side port, yellow circuitry lights inside the silhouette, the shield flickers and collapses from within with short-circuit sparks. |
| **Flank** | The Operations ship arcs around with a thruster trail, ends behind, and fires a burst into the unshielded aspect; the shield stays useless, facing the front. |
| **Scan** | A sensor wave washes across, weak-point wireframes highlight on the hull, "EXPOSED". |
| **Siphon** | Tethers with clamps latch on, yellow particles stream from the Blue card toward the Yellow card while the shield dims and contracts. |

**Criticals** (when the winner wins big): *Overwhelm* (Blitz with 8+ damage: the missiles punch through the silhouette, a molten hole
opens in the card, cracks on the device glass, heavy shake, sub-bass); *Perfect Deflection* (Block where the Defense power beats the
attacker's by 3+: the projectile is caught, crushed into particles and funnelled into the defender's energy pool; a rising major chord);
*Deep System Hijack* (Bypass with 8+ damage or a 5+ energy card: the shield's polarity flips, it turns yellow and constricts, crushing
the ship with its own defenses).

Other outcomes: mirror clashes (Offense vs Offense collide mid-air, Operations vs Operations jam each other, Defense vs Defense
"STALEMATE"), unopposed hits, a shield held against a pass, both players holding (+1 energy), CANCELLED (strike-through glitch),
IMMUNE, LIFELINE, COPIED, DISABLED, HANDS SWAPPED.

## Resources (after the clash)

* **Hull damage**: the card shakes (harder for bigger hits), a bold red -N pops and bounces, the hull bar drains with cracked fragments and sparks; a red vignette on a lethal/near-lethal hit. Healing floats up green.
* **Armor gain**: silver plating slides onto the card edges with a sheen sweep and "+N ARMOR". **Armor loss** is *corrosion*: the plating dissolves into sick-green vapour and the digits drip.
* **Energy / Credits**: 8-15 particles arc into the counter (yellow for energy, gold coins for credits), the counter pulses. A siphon streams from the loser's counter to the winner's. At the energy cap the counter overloads: it rings, says MAX, and the excess sparks off and fizzles.
* **Combo escalation**: each chained transfer in one round (e.g. Siphon then Scan then Sabotage) moves 20% faster.

## Persistent state

* **Burn**: charred corners on the hull panel, embers drifting up, and a crackling noise-sweep at the start of each burning turn.
* **Regen**: green sparkles. **Disabled cards (EMP)**: desaturated, CRT scanlines and a glitch bar, static-lock chains, "DISABLED"; a power-down whine when it lands, and every sound on your side is low-pass muffled while any of your cards is disabled.
* **Redline**: at 20% hull or less the screen border pulses with emergency lighting and a low-tempo heartbeat drum runs under everything.

## Audio

Timbres: Red = percussion + noise; Blue = pads, chimes, reverse-cymbal swell; Yellow = fast square arpeggios and blips. Sequencing rules: the clash
stinger is always first and loudest; resource sounds come after it has decayed (>= 0.3 s gap); every resource one-shot is <= 0.6 s; when both
players gain the same resource the two sounds are staggered 150 ms and panned, the second a **perfect fifth up** (the pitch climb); the mixer has
20 voices and steals by priority (clash > hull > armor > energy/credits > ambience). `M` mutes. With no audio device the game runs silent.

## How it is tested (and what is not)

* `tests/test_sfx.c` (in `scripts/build.sh`): every cue renders, is audible, never clips, decays inside its budget (clash <= 2.2 s, resources <= 0.6 s), the clash is at least as loud as any resource cue, damage scales volume, the pitch climb is really a 3:2 ratio (measured), EMP muffling removes the highs (measured), the voice cap and priority stealing hold, and a full sequenced resolution finishes inside 6 s.
* `dw_gui --fx-demo DIR [--no-sound]` (also run by `tests/test_gui_selftest.sh`): plays 29 scripted rounds (every scenario, keyword, critical, resource and status) headlessly, dumps arena frames (`<scenario>_NN.bmp`) and the rendered audio (`<scenario>.wav`), and fails if any timeline exceeds 9 s or its audio clips.
* **Not verified by ear or on a real display**: the sounds were checked numerically and by rendering to WAV, not listened to; the visuals were checked from dumped frames, not on a GPU/window; touch input does not apply (Windows client). The Android client does not have these animations yet.
