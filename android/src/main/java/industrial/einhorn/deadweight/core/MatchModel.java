package industrial.einhorn.deadweight.core;

import industrial.einhorn.deadweight.generated.CardRules;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Client-side view of one match (mutated only by {@link Session}; read via snapshots on the UI thread). */
public final class MatchModel {
    public static final class RoundLog {
        public final int round, cardYou, cardOpp, dmgYou, dmgOpp, hullYou, hullOpp;
        /** effYou/effOpp: the card that actually resolved (Dark Pool result, copied card; -1 = pass or cancelled). */
        public final int effYou, effOpp, healYou, healOpp, rollYou, rollOpp, flagsYou, flagsOpp;
        RoundLog(Msg m) {
            round = m.round; cardYou = m.cardYou; cardOpp = m.cardOpp; dmgYou = m.dmgYou; dmgOpp = m.dmgOpp;
            hullYou = m.hullYou; hullOpp = m.hullOpp;
            effYou = m.effYou; effOpp = m.effOpp; healYou = m.healYou; healOpp = m.healOpp;
            rollYou = m.rollYou; rollOpp = m.rollOpp; flagsYou = m.flagsYou; flagsOpp = m.flagsOpp;
        }
    }

    public long matchId, seed;
    public int seat, oppKind;
    public String oppName = "";
    public int round, hullYou = CardRules.startHull(), hullOpp = CardRules.startHull();
    public int energyYou = CardRules.startEnergy(), energyOpp = CardRules.startEnergy();
    public int armorYou, armorOpp, vaultYou = CardRules.startVault(), vaultOpp = CardRules.startVault();
    public int lockMask, statusYou, statusOpp;   // lockMask: hand slots you can't play this round
    public int[] hand = {-1, -1, -1, -1};
    public int oppHandSize;
    public int deadlineMs;
    public boolean locked;      // our play acked this round
    public int lockedSlot = -2; // -2 none, -1 pass, 0..3
    public int result = -1, endReason = -1; // set at MATCH_END
    private final List<RoundLog> log = new ArrayList<>();

    public List<RoundLog> log() { return Collections.unmodifiableList(log); }

    void onMatchFound(Msg m) { matchId = m.matchId; seed = m.seed; seat = m.seat; oppKind = m.oppKind; oppName = m.oppName; }

    void onRoundStart(Msg m) {
        round = m.round; hullYou = m.hullYou; hullOpp = m.hullOpp; energyYou = m.energyYou; energyOpp = m.energyOpp;
        hand = m.hand.clone(); oppHandSize = m.oppHandSize; deadlineMs = m.deadlineMs; locked = false; lockedSlot = -2;
        armorYou = m.armorYou; armorOpp = m.armorOpp; vaultYou = m.vaultYou; vaultOpp = m.vaultOpp;
        lockMask = m.lockMask; statusYou = m.statusYou; statusOpp = m.statusOpp;
    }

    void onRoundResult(Msg m) {
        hullYou = m.hullYou; hullOpp = m.hullOpp; armorYou = m.armorYou; armorOpp = m.armorOpp; vaultYou = m.vaultYou; vaultOpp = m.vaultOpp;
        log.add(new RoundLog(m));
    }

    void onEnd(Msg m) { result = m.result; endReason = m.reason; }

    /** UI gate: slot -1 (pass) is always legal; slots 0-3 need a card, enough energy and credits (PARENA is-legal-play),
     *  and the slot must not be locked by the opponent's Corrosion / Glacial Prison. */
    public boolean canPlaySlot(int slot) {
        if (locked) return false;
        if (slot == -1) return true;
        if (slot < 0 || slot > 3) return false;
        return hand[slot] >= 0 && ((lockMask >> slot) & 1) == 0 && CardRules.isLegalPlay(hand[slot], energyYou, vaultYou);
    }

    /** Damage preview of playing slot vs a hypothetical opponent card (-1 = pass). */
    public int previewDamage(int slot, int oppCard) {
        int id = slot < 0 ? -1 : hand[slot];
        return CardRules.damageDealt(id, oppCard);
    }

    public static String kindName(int cardId) {
        if (cardId < 0) return "PASS";
        switch (CardRules.cardKind(cardId)) { case 0: return "BURST"; case 1: return "TANK"; default: return "SHIELD"; }
    }
}
