# Metaprompt engineering pass: v1 (Haiku) -> v2

Subject: `HAIKU_METAPROMPT_v1.md` (420 lines, kept verbatim). Result: `METAPROMPT_v2.md`.
Method: read v1 as the *model receiving it* would, then test each claim in it against the real rules/code
(see `PROPOSAL_REVIEW.md`), then rewrite around what an LLM designer can and cannot reliably do.

## What v1 got right (kept)

- Task-last structure, explicit iteration loop, a fill-in card template, good/bad examples, an instruction to
  reject constraint violations rather than defend them, and a "questions for the human" habit.
- Recognising that the scalar-only emitter constraint is the crux of feasibility.

## Failures in v1, in order of damage

| # | Failure | Why it matters | v2 fix |
|---|---|---|---|
| 1 | **Restates the rules from memory, with errors** (energy "to 7 max"; muddled reflection wording; `kind-beats(a-id, 1)` mixes an id with a kind; presents "hand 5, deck 15" as if baseline) | The designer inherits and amplifies the errors; Haiku's own proposal got the triangle backwards | Inject the **verbatim rules source** and a **machine-generated damage matrix** via placeholders; state the common mistakes explicitly; "if memory disagrees, memory is wrong" |
| 2 | **Wrong constraint model.** Says rules can have "no state", forbids anything persistent. Real constraint: single-expression scalar fns of *any integer the host supplies*; state lives in the host | Over-bans good designs (hull-conditional, opponent-played-card-conditional are legal), and under-warns about the real costs: id scheme, `is-legal-play <= 8`, wire, obs one-hot, art, bot retrain | Replaced by the "engine facts" block and a **Tier A/B/C blast-radius** classification the designer must apply to every card, defaulting to Tier A |
| 3 | **Self-graded rubric with a pass threshold** ("8+ checkmarks = ready") | LLMs pass their own rubrics. The checks are unverifiable by the model (win rate, pick rate) so they become confabulation | Replaced by a **dominance_check** the designer must *demonstrate with a concrete counter-state*, a required **degenerate_line**, and a **separate adversarial reviewer** call. Balance is decided by tools (gates G1-G4), never by the designer's score |
| 4 | **Asks the model to simulate** ("100-match tournament: track pick rate, win rate") | It cannot; it will emit plausible fake numbers that later get quoted as data | Hard rules 6-7: no invented metrics, no claims of having run anything. Simulation is a gate run by the harness |
| 5 | **Invented numeric targets** (40-60% win, 10-20% pick, "cost 3 -> power 7-9") | Look like requirements; are not derived from anything. The pick-rate band is even self-contradictory with 15+ cards | Dropped. Cost/power sanity is enforced by the analytic dominance sweep (G2), not by a table of vibes |
| 6 | **One role does everything** (designer + critic + simulator + spec-writer) | Same-context self-review is agreeable; errors correlate | Two-pass designer (diverge/converge) + a **separate reviewer prompt** with an adversarial mandate and a ban on softening |
| 7 | **Anchoring on the first idea.** A single good example (with an error in it), and the proposal's own cards presented as accepted | Convergent, samey output | Pass 1 requires 10-12 candidates across >= 4 named **design axes**, one mechanism per axis; ledger of rejected/parked ideas prevents rediscovery |
| 8 | **Internal contradictions** ("Never change hand size" vs "Proposed: hand of 5"; "Round 8 is final" vs exceptions; cap 6 vs "to 7") | Model resolves them arbitrarily | Single ground-truth block; anything needing an exception becomes an explicit Tier B/C flag |
| 9 | **No output contract** beyond a loose template; no lethal-order semantics | Heal-vs-lethal, simultaneous self-damage, drain order are exactly where card games break; humans must re-ask each time | `resolution_order` and `lethality` are required fields; whole spec is a **parseable block** |
| 10 | **Verbose & repetitive** (~420 lines; checklists restated 3 times; "Ready to begin?" filler) | Dilutes the constraints that matter; costs tokens every round | ~1/3 the length; sections are tagged (`<ground_truth>`, `<hard_rules>`...) so the model and the harness can address them |
| 11 | **No epistemic discipline.** Nothing tells the model what to do when a fact is missing | It guesses | Hard rule 1: write `UNKNOWN: <x>` and design so it doesn't matter |
| 12 | **No stateful loop.** "Round 2: take feedback" with no defined carrier of feedback | Second round forgets round 1 | `{{LEDGER}}` placeholder: accepted / rejected / parked with reasons, injected each round |

## Techniques applied (why they were chosen)

- **Ground-truth injection over paraphrase.** The single highest-value change: the failure mode was a wrong
  restatement of a source that exists as a file. Generate the matrix from the C build so it cannot drift.
- **Falsifiable output over graded output.** `dominance_check` demands a concrete counter-example state;
  `hypothesis` demands a claim a test could refute. This converts "sounds balanced" into something checkable.
- **Role separation.** Generation and adversarial review are different calls with different instructions;
  the reviewer is told it may not call a card balanced.
- **Cost-ordered work.** Blast-radius tiers make the model spend its ideas where they are cheap to ship first
  (Tier A), and force it to *justify* going up a tier.
- **Diversity by construction.** Named axes + "no two share axis+mechanism" beats "be creative".
- **Fail-safe defaults.** Unknowns are labelled, metrics are forbidden, no claim of testing.
- **Structure for the harness.** Tagged sections and a fixed spec block make round-over-round diffing and
  ledger-keeping mechanical.

## What I could not verify (be honest about v2's limits)

- **v2 has not been run.** I have not executed it against any model; I do not know that it produces better
  cards, only that it removes the specific, demonstrable failure causes above. First run should be treated as
  a test of the prompt itself (does the model fill every field? does the reviewer catch a seeded flaw?).
  A cheap check: feed it a deliberately bad design (Haiku's Spark) and confirm the reviewer prompt flags the
  strict dominance.
- **Gate G2 (`scripts/card_audit.py`) does not exist.** v2 refers to it as TO BUILD. It is small (enumerate
  card x energy x hull states, check strict dominance and cost/power monotonicity, dump a payoff matrix) and is
  the highest-leverage tool to add next, since it would have caught Haiku's Spark in seconds.
- **G3 needs a card-set flag** on `dw_server`/`dw_bot`/`dw_env.py`; today they are hard-wired to 9 cards.
- **Java emitter feature coverage** (`min`/`max`, nested `if` depth, integer-division semantics) was not
  re-checked; the designer is told to show integer arithmetic, but `gen_rules.sh` (G1) is the real judge.
- The damage matrix embedded in v2 is from the Python port (`training/dw_rules.py`, parity-tested against the
  C build). The harness should regenerate it from C each round rather than trusting the pasted copy.

## Recommended sequence

1. Build G2 (`card_audit.py`); run it on Haiku's 7 cards as a calibration (expect Spark to fail).
2. Run v2 designer on a Tier A goal; run the reviewer prompt on its output; keep only PASS-TO-GATES cards.
3. Add the card-set flag; run G3 at N >= 2000 on the survivors; only then touch `card_rules.prn`.
4. Human decides hand size (Tier B) separately, with data.
