package industrial.einhorn.deadweight.core;

/** One decoded server->client message (fields valid per {@link #type}, see docs/WIRE_PROTOCOL.md). */
public final class Msg {
    public int type;
    public long sessionId, matchId, seed, nonce;
    public int flags, waiting, seat, oppKind, round, hullYou, hullOpp, energyYou, energyOpp, oppHandSize,
        deadlineMs, reason, cardYou, cardOpp, dmgYou, dmgOpp, result, code;
    public int armorYou, armorOpp, vaultYou, vaultOpp, lockMask, statusYou, statusOpp;
    public int effYou, effOpp, healYou, healOpp, rollYou, rollOpp, flagsYou, flagsOpp;
    public int[] hand = new int[4];
    public String oppName = "";
    // draft mode (S_DRAFT_OFFER / S_DRAFT_DONE)
    public int pickNo, pickTotal, deckId;
    public int[] offer = new int[2], left = new int[3], deck = new int[Protocol.DRAFT_DECK];

    @Override public String toString() { return "Msg{type=0x" + Integer.toHexString(type) + "}"; }
}
