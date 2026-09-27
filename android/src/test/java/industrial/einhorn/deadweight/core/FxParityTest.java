package industrial.einhorn.deadweight.core;

import industrial.einhorn.deadweight.generated.FxRules;
import java.io.BufferedReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * fx decision-layer parity (S555 phase 2, docs/ANDROID_PARITY_NORTHSTAR.md). Part 1 replays every vector the
 * PARENA->C build produced (tests/fx_parity_vectors.txt, from tests/gen_fx_vectors.c) through the PARENA->Java
 * build (generated/FxRules.java) and requires identical results. Part 2 drives {@link FxTimeline} through the real
 * {@link MatchModel} ROUND_START -> ROUND_RESULT path against hand-computed scenarios (the Offense > Operations >
 * Defense > Offense triangle from docs/CARD_MODE_RULES.md; card stats from tests/parity_vectors.txt), so a
 * glue-code bug in FxTimeline can't hide behind the generated functions being right. Lives in the core package
 * because MatchModel's mutators are package-private. Plain JVM, no JUnit (KARAMBIT convention).
 * Usage: FxParityTest <path/to/fx_parity_vectors.txt>
 */
public final class FxParityTest {
    private static int checks = 0, fails = 0;
    private static int b(boolean v) { return v ? 1 : 0; }

    static int eval(String fn, int[] a) {
        switch (fn) {
            case "fx_card_id": return FxRules.fxCardId(a[0], a[1], a[2]);
            case "fx_kind_of": return FxRules.fxKindOf(a[0]);
            case "fx_scenario": return FxRules.fxScenario(a[0], a[1], a[2], a[3]);
            case "fx_winner_seat": return FxRules.fxWinnerSeat(a[0], a[1], a[2], a[3]);
            case "fx_is_crit": return b(FxRules.fxIsCrit(a[0], a[1], a[2], a[3], a[4]));
            case "fx_clash_duration_ms": return FxRules.fxClashDurationMs(a[0], a[1] != 0);
            case "fx_clash_cue": return FxRules.fxClashCue(a[0], a[1] != 0, a[2]);
            case "fx_clash0": return FxRules.fxClash0();
            case "fx_clash1": return FxRules.fxClash1(a[0]);
            case "fx_hull0": return FxRules.fxHull0(a[0]);
            case "fx_hull1": return FxRules.fxHull1(a[0], a[1]);
            case "fx_armor0": return FxRules.fxArmor0(a[0], a[1]);
            case "fx_armor1": return FxRules.fxArmor1(a[0], a[1], a[2]);
            case "fx_econ0": return FxRules.fxEcon0(a[0], a[1], a[2]);
            case "fx_econ1": return FxRules.fxEcon1(a[0], a[1], a[2], a[3]);
            case "fx_stat0": return FxRules.fxStat0(a[0], a[1], a[2], a[3]);
            case "fx_stat1": return FxRules.fxStat1(a[0], a[1], a[2], a[3], a[4]);
            case "fx_settle0": return FxRules.fxSettle0(a[0], a[1], a[2], a[3], a[4]);
            case "fx_timeline_total_ms": return FxRules.fxTimelineTotalMs(a[0], a[1], a[2], a[3], a[4]);
            case "fx_has_hull": return b(FxRules.fxHasHull(a[0], a[1], a[2], a[3]));
            case "fx_has_armor": return b(FxRules.fxHasArmor(a[0], a[1], a[2], a[3]));
            case "fx_has_econ": return b(FxRules.fxHasEcon(a[0], a[1], a[2], a[3]));
            case "fx_has_stat": return b(FxRules.fxHasStat(a[0], a[1], a[2], a[3], a[4]));
            default: throw new IllegalArgumentException("unknown fx vector fn " + fn);
        }
    }

    private static void eq(String what, Object got, Object want) {
        checks++;
        if (!got.equals(want)) { fails++; System.out.println("FAIL " + what + ": got " + got + ", want " + want); }
    }

    /** One round through the real MatchModel path: ROUND_START (meters before) then ROUND_RESULT (after). */
    private static FxTimeline round(int cardYou, int cardOpp, int effYou, int effOpp, int dmgYou, int dmgOpp,
                                    int flagsYou, int flagsOpp, int vaultBeforeOpp, int vaultAfterOpp) {
        MatchModel mm = new MatchModel();
        Msg s = new Msg();
        s.round = 1; s.hullYou = 20; s.hullOpp = 20; s.armorYou = 0; s.armorOpp = 0; s.vaultYou = 3; s.vaultOpp = vaultBeforeOpp;
        mm.onRoundStart(s);
        Msg r = new Msg();
        r.round = 1; r.cardYou = cardYou; r.cardOpp = cardOpp; r.effYou = effYou; r.effOpp = effOpp;
        r.dmgYou = dmgYou; r.dmgOpp = dmgOpp; r.hullYou = 20 - dmgYou; r.hullOpp = 20 - dmgOpp;
        r.armorYou = 0; r.armorOpp = 0; r.vaultYou = 3; r.vaultOpp = vaultAfterOpp; r.flagsYou = flagsYou; r.flagsOpp = flagsOpp;
        mm.onRoundResult(r);
        return FxTimeline.of(mm.log().get(0));
    }

    private static void scenarios() {
        // Card 2 = Offense (cost 4, pwr 10) beats card 4 = Operations: BLITZ, you win; 10 dmg to the loser >= 8 -> crit.
        FxTimeline t = round(2, 4, 2, 4, 0, 10, 0, 0, 3, 3);
        eq("blitz scenario", t.scenario, FxTimeline.SC_BLITZ); eq("blitz win", t.win, 0); eq("blitz crit", t.crit, true);
        eq("blitz name", t.scenarioName(), "blitz_crit"); eq("blitz clash len", t.clash1 - t.clash0, 1900);
        eq("blitz hull stage", t.hasHull, true); eq("blitz no econ", t.hasEcon, false);
        // Card 3 = Operations/Flank (cost 1) beats card 7 = Defense: BYPASS, you win, 3 dmg, cost 1 -> no crit; cue 6 = flank.
        t = round(3, 7, 3, 7, 0, 3, 0, 0, 3, 3);
        eq("bypass scenario", t.scenario, FxTimeline.SC_BYPASS); eq("bypass kw", t.kw, 3); eq("bypass crit", t.crit, false);
        eq("bypass name", t.scenarioName(), "bypass_flank"); eq("bypass cue", t.clashCue, 6);
        // Opponent wins: card 8 = Defense beats card 1 = Offense -> BLOCK, win seat 1 (opponent), lose = you.
        t = round(1, 8, 1, 8, 0, 0, 0, 0, 3, 3);
        eq("block scenario", t.scenario, FxTimeline.SC_BLOCK); eq("block win", t.win, 1); eq("block lose", t.lose, 0);
        eq("block no hull", t.hasHull, false);
        // You pass, they play Defense: HOLD_SHIELD, opponent "wins" the reveal.
        t = round(-1, 6, -1, 6, 0, 0, 0, 0, 3, 3);
        eq("hold-shield scenario", t.scenario, FxTimeline.SC_HOLD_SHIELD); eq("hold-shield win", t.win, 1);
        // Both pass.
        t = round(-1, -1, -1, -1, 0, 0, 0, 0, 3, 3);
        eq("both-pass scenario", t.scenario, FxTimeline.SC_BOTH_PASS); eq("both-pass win", t.win, -1); eq("both-pass name", t.scenarioName(), "hold");
        // Your card cancelled (flags bit0, eff -1): the declared card is still shown; scenario CANCEL.
        t = round(2, 4, -1, 4, 0, 0, 1, 0, 3, 3);
        eq("cancel scenario", t.scenario, FxTimeline.SC_CANCEL); eq("cancel shows declared", t.cardYouId, 2);
        // Opponent vault hidden by Merkle Blindness (-128 sentinel): must NOT read as a 131-credit swing.
        t = round(-1, -1, -1, -1, 0, 0, 0, 0, 3, Protocol.HIDDEN_I8);
        eq("hidden vault no econ", t.hasEcon, false);
        // Real vault change still counts.
        t = round(-1, -1, -1, -1, 0, 0, 0, 0, 3, 5);
        eq("vault change econ", t.hasEcon, true);
        // Hands swapped (flags bit4) -> status stage.
        t = round(-1, -1, -1, -1, 0, 0, 16, 0, 3, 3);
        eq("swap status", t.hasStat, true);
        // Stage boundaries are ordered and end at totalMs, for every scenario above's shape.
        int[] seq = {t.clash0, t.clash1, t.hull0, t.hull1, t.armor0, t.armor1, t.econ0, t.econ1, t.stat0, t.stat1, t.settle0, t.totalMs};
        boolean ordered = true;
        for (int i = 1; i < seq.length; i++) if (seq[i] < seq[i - 1]) ordered = false;
        eq("stages ordered", ordered, true);
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 1) { System.err.println("usage: FxParityTest <fx_parity_vectors.txt>"); System.exit(2); }
        int vectors = 0;
        try (BufferedReader r = Files.newBufferedReader(Paths.get(args[0]))) {
            String line;
            while ((line = r.readLine()) != null) {
                int eqAt = line.indexOf(" = ");
                if (eqAt < 0) { fails++; System.out.println("FAIL malformed: " + line); continue; }
                int want = Integer.parseInt(line.substring(eqAt + 3).trim());
                String[] t = line.substring(0, eqAt).trim().split(" ");
                int[] a = new int[8];
                for (int i = 1; i < t.length && i <= 8; i++) a[i - 1] = Integer.parseInt(t[i]);
                int got = eval(t[0], a);
                checks++; vectors++;
                if (got != want) { fails++; System.out.println("FAIL " + line + "  java=" + got); }
            }
        }
        if (vectors < 5000) { fails++; System.out.println("FAIL only " + vectors + " fx vectors read"); }
        scenarios();
        System.out.println("FxParityTest: " + vectors + " vectors, " + checks + " checks, " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
