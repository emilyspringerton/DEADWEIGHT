package industrial.einhorn.deadweight.core;

import industrial.einhorn.deadweight.generated.CardRules;
import industrial.einhorn.deadweight.generated.FxRules;

/**
 * Round-resolution animation timeline for one ROUND_RESULT -- which scenario, who won, was it a critical,
 * which clash cue, and when each stage (clash / hull / armor / econ / status / settle) starts and ends.
 * Every decision is a call into generated/FxRules.java, the PARENA->Java build of
 * PARENA/stdlib/deadweight/fx_rules.prn -- the SAME source apps/gui/fx.c (core/fx_rules.c) and
 * web/src/fx.ts (web/src/generated/FxRules.ts) call. This class is a line-for-line port of fx.ts's own
 * computeTimeline() glue, so the three clients differ only in how they draw, never in what they decide
 * (S555 phase 2, docs/ANDROID_PARITY_NORTHSTAR.md). Plain JVM: no Android types, testable in core_test.
 *
 * Honest gaps, identical to the browser client's (named in web/src/fx.ts's FxRoundInput docs):
 * the wire protocol sends no energy delta and no status on ROUND_RESULT, so energy deltas are 0 and
 * "new status" is false here -- the desktop client derives both from the NEXT ROUND_START, a lag this
 * one-shot, per-result computation does not model. Opponent meters hidden by Merkle Blindness count as
 * unchanged (C's fx.c does the same for the hidden vault sentinel).
 */
public final class FxTimeline {
    public static final int SC_NONE = 0, SC_BLITZ = 1, SC_BLOCK = 2, SC_BYPASS = 3, SC_MIRROR_OFFENSE = 4,
        SC_MIRROR_OPERATIONS = 5, SC_MIRROR_DEFENSE = 6, SC_UNOPPOSED = 7, SC_HOLD_SHIELD = 8, SC_BOTH_PASS = 9,
        SC_CANCEL = 10;
    private static final String[] SCENARIO_NAME = {"none", "blitz", "block", "bypass", "mirror_offense",
        "mirror_operations", "mirror_defense", "unopposed", "hold_shield", "hold", "cancelled"};
    private static final String[] KW_NAME = {"", "lock", "sabotage", "flank", "scan", "siphon"};

    /** win/lose: 0 = you, 1 = opponent, -1 = nobody. kindYou/kindOpp: -1 = no card resolved. */
    public final int scenario, win, lose, kw, clashCue, cardYouId, cardOppId, kindYou, kindOpp;
    public final boolean crit, hasHull, hasArmor, hasEcon, hasStat;
    /** Armor / credit changes this round; the opponent's are 0 while hidden by Merkle Blindness. */
    public final int armorDeltaYou, armorDeltaOpp, vaultDeltaYou, vaultDeltaOpp;
    /** Stage boundaries in ms from the start of the reveal, straight from FxRules. */
    public final int clash0, clash1, hull0, hull1, armor0, armor1, econ0, econ1, stat0, stat1, settle0, totalMs;

    private static int b(boolean v) { return v ? 1 : 0; }

    public static FxTimeline of(MatchModel.RoundLog r) { return new FxTimeline(r); }

    private FxTimeline(MatchModel.RoundLog r) {
        int ca = b((r.flagsYou & 1) != 0), cb = b((r.flagsOpp & 1) != 0);
        cardYouId = FxRules.fxCardId(r.effYou, r.cardYou, ca);
        cardOppId = FxRules.fxCardId(r.effOpp, r.cardOpp, cb);
        kindYou = FxRules.fxKindOf(cardYouId);
        kindOpp = FxRules.fxKindOf(cardOppId);
        scenario = FxRules.fxScenario(kindYou, kindOpp, ca, cb);
        win = FxRules.fxWinnerSeat(kindYou, kindOpp, ca, cb);
        lose = win >= 0 ? 1 - win : -1;
        boolean c = false;
        int k = 0;
        if (win >= 0 && lose >= 0) {
            int winId = win == 0 ? cardYouId : cardOppId, loseId = lose == 0 ? cardYouId : cardOppId;
            if (scenario == SC_BYPASS) k = CardRules.cardKeyword(winId);
            int dmgLose = lose == 0 ? r.dmgYou : r.dmgOpp;
            c = FxRules.fxIsCrit(scenario, dmgLose, CardRules.cardPower(winId), CardRules.cardPower(loseId), CardRules.cardCost(winId));
        }
        crit = c;
        kw = k;
        clashCue = FxRules.fxClashCue(scenario, crit, kw);
        int clashDur = FxRules.fxClashDurationMs(scenario, crit);
        boolean oppArmorHidden = r.armorBeforeOpp == Protocol.HIDDEN_U8 || r.armorAfterOpp == Protocol.HIDDEN_U8;
        boolean oppVaultHidden = r.vaultBeforeOpp == Protocol.HIDDEN_I8 || r.vaultAfterOpp == Protocol.HIDDEN_I8;
        armorDeltaYou = r.armorAfterYou - r.armorBeforeYou;
        armorDeltaOpp = oppArmorHidden ? 0 : r.armorAfterOpp - r.armorBeforeOpp;
        vaultDeltaYou = r.vaultAfterYou - r.vaultBeforeYou;
        vaultDeltaOpp = oppVaultHidden ? 0 : r.vaultAfterOpp - r.vaultBeforeOpp;
        hasHull = FxRules.fxHasHull(r.dmgYou, r.dmgOpp, r.healYou, r.healOpp);
        hasArmor = FxRules.fxHasArmor(r.armorBeforeYou, r.armorAfterYou,
            oppArmorHidden ? 0 : r.armorBeforeOpp, oppArmorHidden ? 0 : r.armorAfterOpp);
        hasEcon = FxRules.fxHasEcon(0, 0, vaultDeltaYou, vaultDeltaOpp);
        boolean disabledYou = (r.flagsOpp & 8) != 0, disabledOpp = (r.flagsYou & 8) != 0;
        boolean swapped = ((r.flagsYou | r.flagsOpp) & 16) != 0;
        hasStat = FxRules.fxHasStat(0, 0, b(disabledYou), b(disabledOpp), b(swapped));
        int h = b(hasHull), a = b(hasArmor), e = b(hasEcon), s = b(hasStat);
        clash0 = FxRules.fxClash0();
        clash1 = FxRules.fxClash1(clashDur);
        hull0 = FxRules.fxHull0(clashDur);
        hull1 = FxRules.fxHull1(clashDur, h);
        armor0 = FxRules.fxArmor0(clashDur, h);
        armor1 = FxRules.fxArmor1(clashDur, h, a);
        econ0 = FxRules.fxEcon0(clashDur, h, a);
        econ1 = FxRules.fxEcon1(clashDur, h, a, e);
        stat0 = FxRules.fxStat0(clashDur, h, a, e);
        stat1 = FxRules.fxStat1(clashDur, h, a, e, s);
        settle0 = FxRules.fxSettle0(clashDur, h, a, e, s);
        totalMs = FxRules.fxTimelineTotalMs(clashDur, h, a, e, s);
    }

    /** "bypass_sabotage_crit" etc. -- the same name apps/gui/fx.c and web/src/fx.ts log. */
    public String scenarioName() {
        String n = scenario >= 0 && scenario < SCENARIO_NAME.length ? SCENARIO_NAME[scenario] : "none";
        if (scenario == SC_BYPASS) n += "_" + (kw >= 0 && kw < KW_NAME.length ? KW_NAME[kw] : "");
        return crit ? n + "_crit" : n;
    }
}
